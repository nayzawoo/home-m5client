#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <IRremoteESP8266.h>
#include <ir_Daikin.h>
#include "ui_protocol.h"
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

namespace {
TFT_eSprite canvas(&M5.Lcd);
ui::Frame frame, candidate;
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
    BoundedBody() { text.reserve(ui::MAX_BODY); }
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
void render() {
    canvas.fillSprite(rgb(frame.background));
    for (size_t i = 0; i < frame.count; ++i) {
        const auto &e = frame.elements[i];
        if (e.kind == ui::Kind::Label) {
            canvas.setTextColor(rgb(e.color));
            canvas.setTextSize(e.size);
            canvas.drawString(e.text, e.x, e.y, e.font);
        } else if (e.kind == ui::Kind::Rect) canvas.fillRect(e.x, e.y, e.w, e.h, rgb(e.color));
        else canvas.drawLine(e.x, e.y, e.x2, e.y2, rgb(e.color));
    }
    canvas.pushSprite(0, 0);
    M5.Lcd.setBrightness(frame.brightness);
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
    if (error || !ui::decode(document.as<JsonVariantConst>(), candidate)) {
        status("Invalid UI document"); return false;
    }
    frame = candidate;
    hasFrame = true;
    render();
    return true;
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
    M5.Power.setWakeupButton(BUTTON_B_PIN);
    canvas.setColorDepth(8);
    if (!canvas.createSprite(320, 240)) { status("Display allocation failed"); return; }
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
    delay(10);
}
