#pragma once
#include "ui_protocol.h"

namespace ui {
inline bool sameMarquee(const Element &a, const Element &b) {
    return a.kind == Kind::Marquee && b.kind == Kind::Marquee &&
        std::strcmp(a.id, b.id) == 0 && std::strcmp(a.text, b.text) == 0 &&
        a.w == b.w && a.h == b.h && a.font == b.font && a.size == b.size && a.speed == b.speed;
}
// Animation state belongs to stable IDs, independent of element order or clock updates.
struct MarqueeTimeline {
    uint32_t started[MAX_ELEMENTS] = {};
    void sync(const Frame &previous, const Frame &next, uint32_t now) {
        uint32_t updated[MAX_ELEMENTS] = {};
        for (size_t i = 0; i < next.count; ++i) {
            if (next.elements[i].kind != Kind::Marquee) continue;
            updated[i] = now;
            for (size_t j = 0; j < previous.count; ++j) {
                if (sameMarquee(previous.elements[j], next.elements[i])) {
                    updated[i] = started[j];
                    break;
                }
            }
        }
        std::memcpy(started, updated, sizeof(started));
    }
};
inline int marqueeX(const Element &element, int textWidth, uint32_t started, uint32_t now) {
    if (textWidth <= element.w) return (element.w - textWidth) / 2;
    const uint32_t elapsed = now - started;
    const uint32_t period = static_cast<uint32_t>(element.w + textWidth + 24);
    const uint64_t pixels = static_cast<uint64_t>(elapsed) * element.speed / 1000;
    return element.w - static_cast<int>(pixels % period);
}
inline bool hasMarquee(const Frame &frame) {
    for (size_t i = 0; i < frame.count; ++i)
        if (frame.elements[i].kind == Kind::Marquee) return true;
    return false;
}
}
