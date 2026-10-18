#pragma once
#include "ui_protocol.h"

namespace ui {
// Same track = same id. Text/color/speed updates keep scrolling; only a new id restarts.
inline bool sameMarqueeTrack(const Element &a, const Element &b) {
    return a.kind == Kind::Marquee && b.kind == Kind::Marquee &&
        std::strcmp(a.id, b.id) == 0;
}
// Animation state belongs to stable IDs, independent of clock refreshes or copy updates.
struct MarqueeTimeline {
    uint32_t started[MAX_ELEMENTS] = {};
    void sync(const Frame &previous, const Frame &next, uint32_t now) {
        uint32_t updated[MAX_ELEMENTS] = {};
        for (size_t i = 0; i < next.count; ++i) {
            if (next.elements[i].kind != Kind::Marquee) continue;
            updated[i] = now;
            for (size_t j = 0; j < previous.count; ++j) {
                if (sameMarqueeTrack(previous.elements[j], next.elements[i])) {
                    updated[i] = started[j];
                    break;
                }
            }
        }
        std::memcpy(started, updated, sizeof(started));
    }
};
inline int marqueeX(const Element &element, int textWidth, uint32_t started, uint32_t now) {
    // Always scroll: short messages still enter from the right so the row looks alive.
    const int span = textWidth > 0 ? textWidth : element.w;
    const uint32_t elapsed = now - started;
    const uint32_t period = static_cast<uint32_t>(element.w + span + 24);
    const uint64_t pixels = static_cast<uint64_t>(elapsed) * element.speed / 1000;
    return element.w - static_cast<int>(pixels % period);
}
inline bool hasMarquee(const Frame &frame) {
    for (size_t i = 0; i < frame.count; ++i)
        if (frame.elements[i].kind == Kind::Marquee) return true;
    return false;
}
}
