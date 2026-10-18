#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <IRremoteESP8266.h>
#include <ir_Daikin.h>
#include "ui_protocol.h"
#include "marquee.h"
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

namespace {
// Marquee scratch only (~20 KiB). No full-screen sprite — that ~76 KiB heap
// allocation forced a free/recreate around every HTTPS fetch and flashed the LCD.
TFT_eSprite marqueeCanvas(&M5.Lcd);
ui::Frame frame, candidate;
ui::MarqueeTimeline timeline;
bool marqueeReady = false, animating = false;
uint8_t appliedBrightness = 0;
uint32_t lastRender = 0;
IRDaikin64 ac(device_config::IR_SEND_PIN);
uint32_t nextFetch = 0, lastReconnect = 0;
bool hasFrame = false, configured = false;

uint16_t rgb(uint32_t value) {
    return M5.Lcd.color565(value >> 16, (value >> 8) & 255, value & 255);
}
void status(const char *message) {
    // Keep the last good frame visible during outages; never log URLs or payloads.
    Serial.println(message);
    if (hasFrame) return;
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextFont(2);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.drawString(message, 10, 100, 2);
}
class BoundedBody : public Stream {
public:
    String text;
    bool overflow = false;
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t length) override {
        if (length > ui::MAX_BODY - text.length()) { overflow = true; return 0; }
        if (!text.concat(reinterpret_cast<const char *>(data), length)) return 0;
        return length;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
bool ensureMarquee(const ui::Frame &source) {
    if (!ui::hasMarquee(source)) return true;
    if (marqueeReady) return true;
    marqueeCanvas.setColorDepth(8);
    marqueeReady = marqueeCanvas.createSprite(320, ui::MAX_MARQUEE_HEIGHT) != nullptr;
    if (!marqueeReady) status("Marquee allocation failed");
    return marqueeReady;
}
void applyBrightness() {
    if (frame.brightness == appliedBrightness) return;
    appliedBrightness = frame.brightness;
    M5.Lcd.setBrightness(frame.brightness);
}
void paintMarquees(uint32_t now) {
    animating = false;
    if (!marqueeReady) return;
    const uint16_t bg = rgb(frame.background);
    for (size_t i = 0; i < frame.count; ++i) {
        const auto &e = frame.elements[i];
        if (e.kind != ui::Kind::Marquee) continue;
        marqueeCanvas.fillSprite(bg);
        marqueeCanvas.setTextColor(rgb(e.color));
        marqueeCanvas.setTextSize(e.size);
        int textWidth = marqueeCanvas.textWidth(e.text, e.font);
        animating = true;
        int x = ui::marqueeX(e, textWidth, timeline.started[i], now);
        marqueeCanvas.drawString(e.text, x, 0, e.font);
        uint16_t row[320];
        for (int y = 0; y < e.h; ++y) {
            for (int column = 0; column < e.w; ++column)
                row[column] = marqueeCanvas.readPixel(column, y);
            M5.Lcd.pushImage(e.x, e.y + y, e.w, 1, row);
        }
    }
    lastRender = now;
}
// Content refresh: labels/rects/background only. Marquee keeps one continuous scroll loop.
void renderContent(uint32_t now, bool clearBackground) {
    const uint16_t bg = rgb(frame.background);
    if (clearBackground) M5.Lcd.fillScreen(bg);
    for (size_t i = 0; i < frame.count; ++i) {
        const auto &e = frame.elements[i];
        if (e.kind == ui::Kind::Marquee) continue;
        if (e.kind == ui::Kind::Label) {
            // Opaque text erases previous glyphs (clock digits) without a screen wipe.
            M5.Lcd.setTextColor(rgb(e.color), bg);
            M5.Lcd.setTextSize(e.size);
            M5.Lcd.drawString(e.text, e.x, e.y, e.font);
        } else if (e.kind == ui::Kind::Rect) {
            M5.Lcd.fillRect(e.x, e.y, e.w, e.h, rgb(e.color));
        } else {
            M5.Lcd.drawLine(e.x, e.y, e.x2, e.y2, rgb(e.color));
        }
    }
    animating = ui::hasMarquee(frame);
    applyBrightness();
    // After a full wipe, seed the strip once; afterward only paintMarquees advances it.
    if (clearBackground && animating) paintMarquees(now);
}
bool acceptFrame(uint32_t now) {
    if (!ensureMarquee(candidate)) return false;
    const bool firstPaint = !hasFrame;
    const bool lostMarquee = ui::hasMarquee(frame) && !ui::hasMarquee(candidate);
    timeline.sync(frame, candidate, now);
    frame = candidate;
    hasFrame = true;
    renderContent(now, firstPaint || lostMarquee);
    return true;
}
bool fetch() {
    WiFiClient plain;
    WiFiClientSecure secure;
    HTTPClient http;
    String url(device_config::UI_URL);
    bool https = url.startsWith("https://");
    if (https) {
        secure.setInsecure();
    } else if (!url.startsWith("http://") || !device_config::ALLOW_HTTP) {
        status("HTTPS required"); return false;
    }
    http.setConnectTimeout(3000);
    http.setTimeout(3000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    WiFiClient &transport = https ? static_cast<WiFiClient &>(secure) : plain;
    if (!http.begin(transport, url)) return false;
    http.addHeader("Accept", "application/json");
    http.addHeader("X-UI-Capabilities", "marquee");
    http.addHeader("Authorization", String("Bearer ") + device_config::API_TOKEN);
    int code = http.GET();
    if (code != HTTP_CODE_OK || http.getSize() > static_cast<int>(ui::MAX_BODY)) {
        http.end(); status("UI request failed"); return false;
    }
    BoundedBody body;
    int received = http.writeToStream(&body);
    http.end();
    if (received < 0 || body.overflow) { status("UI response rejected"); return false; }
    JsonDocument document;
    auto error = deserializeJson(document, body.text, DeserializationOption::NestingLimit(8));
    body.text = String();
    if (error || !ui::decode(document.as<JsonVariantConst>(), candidate)) {
        status("Invalid UI document"); return false;
    }
    document = JsonDocument();
    return acceptFrame(millis());
}
void press(const ui::Button &button) {
    switch (button.action) {
    case ui::Action::Refresh: nextFetch = millis(); break;
    case ui::Action::Sleep:
        M5.Power.deepSleep(static_cast<uint64_t>(button.seconds) * 1000000ULL);
        break;
    case ui::Action::AcCool:
        if (!device_config::ENABLE_IR) { status("IR disabled in local config"); break; }
        // Explicit power-toggle command, sent once per physical button press.
        ac.setPowerToggle(true);
        ac.setMode(kDaikin64Cool);
        ac.setTemp(24);
        ac.setFan(kDaikin64FanHigh);
        ac.setSwingVertical(true);
        ac.setTurbo(true);
        ac.setOffTimeEnabled(false);
        ac.setOnTimeEnabled(false);
        ac.send();
        break;
    case ui::Action::None: break;
    }
}
}
void setup() {
    M5.begin(true, false, true, false);
    M5.Speaker.end();
    M5.Lcd.setRotation(1);
    M5.Lcd.setBrightness(20);
    appliedBrightness = 20;
    M5.Power.setWakeupButton(BUTTON_B_PIN);
    configured = device_config::WIFI_SSID[0] && device_config::UI_URL[0] && device_config::API_TOKEN[0];
    if (!configured) { status("Configure config.local.h"); return; }
    if (device_config::ENABLE_IR) ac.begin();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(device_config::WIFI_SSID, device_config::WIFI_PASSWORD);
    status("Connecting...");
}
void loop() {
    M5.update();
    if (!configured) { delay(10); return; }
    if (hasFrame) {
        if (M5.BtnA.wasPressed()) press(frame.buttons[0]);
        if (M5.BtnB.wasPressed()) press(frame.buttons[1]);
        if (M5.BtnC.wasPressed()) press(frame.buttons[2]);
    }
    uint32_t tick = millis();
    if (WiFi.status() != WL_CONNECTED) {
        if (tick - lastReconnect >= 15000) { lastReconnect = tick; WiFi.reconnect(); }
    } else if (static_cast<int32_t>(tick - nextFetch) >= 0) {
        bool ok = fetch();
        nextFetch = millis() + (ok ? frame.refreshMs : 5000);
    }
    // Single continuous marquee loop; content refresh never restarts or restacks it.
    if (hasFrame && animating && tick - lastRender >= 33) paintMarquees(tick);
    delay(10);
}
