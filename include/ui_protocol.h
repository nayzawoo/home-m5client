#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>

namespace ui {
constexpr size_t MAX_BODY = 16384;
constexpr size_t MAX_ELEMENTS = 32;
constexpr size_t MAX_LABEL_TEXT = 96;
constexpr size_t MAX_TEXT = 192;
constexpr size_t MAX_MARQUEES = 4;
constexpr int MAX_MARQUEE_HEIGHT = 64;
enum class Kind { Label, Rect, Line, Marquee };
enum class Action { None, Refresh, AcCool, Sleep };
struct Element {
    Kind kind = Kind::Label;
    int x = 0, y = 0, w = 0, h = 0, x2 = 0, y2 = 0;
    int font = 2, size = 1, speed = 30;
    char id[33] = {};
    uint32_t color = 0xffffff;
    char text[MAX_TEXT + 1] = {};
};
struct Button {
    Action action = Action::None;
    uint32_t seconds = 0;
};
struct Frame {
    uint32_t background = 0;
    uint32_t refreshMs = 1500;
    uint8_t brightness = 20;
    size_t count = 0;
    Element elements[MAX_ELEMENTS];
    Button buttons[3];
};
inline bool integer(JsonVariantConst value, int minimum, int maximum) {
    return value.is<int>() && value.as<int>() >= minimum && value.as<int>() <= maximum;
}
inline bool color(JsonVariantConst value, uint32_t &result) {
    if (!value.is<const char *>()) return false;
    const char *str = value.as<const char *>();
    if (std::strlen(str) != 7 || str[0] != '#') return false;
    result = 0;
    for (int i = 1; i < 7; ++i) {
        char c = str[i];
        int digit = c >= '0' && c <= '9' ? c - '0' :
                    c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) return false;
        result = (result << 4) | digit;
    }
    return true;
}
// Call into a candidate frame; only publish it after validation succeeds.
inline bool decode(JsonVariantConst root, Frame &out) {
    if (!root.is<JsonObjectConst>() || !integer(root["version"], 1, 1) ||
        !integer(root["width"], 320, 320) || !integer(root["height"], 240, 240) ||
        !integer(root["refresh_ms"], 1000, 60000) ||
        !integer(root["brightness"], 1, 255) || !color(root["background"], out.background)) return false;
    out.refreshMs = root["refresh_ms"].as<uint32_t>();
    out.brightness = root["brightness"].as<uint8_t>();
    if (!root["elements"].is<JsonArrayConst>()) return false;
    JsonArrayConst elements = root["elements"].as<JsonArrayConst>();
    if (elements.size() > MAX_ELEMENTS) return false;
    out.count = 0;
    size_t marqueeCount = 0;
    for (JsonVariantConst item : elements) {
        if (!item.is<JsonObjectConst>() || !item["type"].is<const char *>() ||
            !integer(item["x"], 0, 319) || !integer(item["y"], 0, 239)) return false;
        Element e;
        e.x = item["x"].as<int>(); e.y = item["y"].as<int>();
        if (!color(item["color"], e.color)) return false;
        const char *type = item["type"].as<const char *>();
        if (std::strcmp(type, "label") == 0 || std::strcmp(type, "marquee") == 0) {
            e.kind = std::strcmp(type, "marquee") == 0 ? Kind::Marquee : Kind::Label;
            if (!item["text"].is<const char *>() || !integer(item["size"], 1, 3)) return false;
            const char *text = item["text"].as<const char *>();
            if (std::strlen(text) > (e.kind == Kind::Marquee ? MAX_TEXT : MAX_LABEL_TEXT)) return false;
            for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
                if (*p < 32 || *p > 126) return false;
            e.font = item["font"].as<int>();
            if (!item["font"].is<int>() || (e.font != 1 && e.font != 2 && e.font != 4 && e.font != 7)) return false;
            e.size = item["size"].as<int>();
            std::strcpy(e.text, text);
            if (e.kind == Kind::Marquee) {
                if (++marqueeCount > MAX_MARQUEES || !item["id"].is<const char *>() ||
                    !integer(item["w"], 1, 320 - e.x) ||
                    !integer(item["h"], 1, MAX_MARQUEE_HEIGHT) ||
                    item["h"].as<int>() > 240 - e.y || !integer(item["speed"], 1, 120)) return false;
                const char *id = item["id"].as<const char *>();
                size_t length = std::strlen(id);
                if (length == 0 || length > 32) return false;
                for (size_t j = 0; j < length; ++j) {
                    char c = id[j];
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
                }
                for (size_t j = 0; j < out.count; ++j)
                    if (out.elements[j].kind == Kind::Marquee &&
                        std::strcmp(out.elements[j].id, id) == 0) return false;
                std::strcpy(e.id, id);
                e.w = item["w"].as<int>(); e.h = item["h"].as<int>();
                e.speed = item["speed"].as<int>();
            }
        } else if (std::strcmp(type, "rect") == 0) {
            e.kind = Kind::Rect;
            if (!integer(item["w"], 1, 320 - e.x) || !integer(item["h"], 1, 240 - e.y)) return false;
            e.w = item["w"].as<int>(); e.h = item["h"].as<int>();
        } else if (std::strcmp(type, "line") == 0) {
            e.kind = Kind::Line;
            if (!integer(item["x2"], 0, 319) || !integer(item["y2"], 0, 239)) return false;
            e.x2 = item["x2"].as<int>(); e.y2 = item["y2"].as<int>();
        } else return false;
        out.elements[out.count++] = e;
    }
    if (!root["buttons"].is<JsonObjectConst>()) return false;
    const char *names[] = {"A", "B", "C"};
    for (int i = 0; i < 3; ++i) {
        out.buttons[i] = Button{};
        JsonVariantConst button = root["buttons"][names[i]];
        if (!button.is<JsonObjectConst>() || !button["action"].is<const char *>()) return false;
        const char *action = button["action"].as<const char *>();
        if (std::strcmp(action, "refresh") == 0) out.buttons[i].action = Action::Refresh;
        else if (std::strcmp(action, "ac_cool") == 0) out.buttons[i].action = Action::AcCool;
        else if (std::strcmp(action, "sleep") == 0) {
            if (!integer(button["seconds"], 1, 86400)) return false;
            out.buttons[i].action = Action::Sleep;
            out.buttons[i].seconds = button["seconds"].as<uint32_t>();
        } else if (std::strcmp(action, "none") != 0) return false;
    }
    return true;
}
}
