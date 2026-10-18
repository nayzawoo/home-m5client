#include "ui_protocol.h"
#include "marquee.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream fixture(argv[1]);
    assert(fixture.good());
    std::stringstream buffer;
    buffer << fixture.rdbuf();
    const std::string original = buffer.str();
    JsonDocument doc;
    ui::Frame frame;
    auto reset = [&]() { assert(!deserializeJson(doc, original)); };
    auto valid = [&]() { return ui::decode(doc.as<JsonVariantConst>(), frame); };
    reset();
    assert(valid());
    assert(frame.count == 8 && frame.brightness == 30 && frame.refreshMs == 1500);
    assert(std::strcmp(frame.elements[0].text, "10:20:30") == 0);
    assert(frame.buttons[1].action == ui::Action::AcCool);
    assert(frame.buttons[2].seconds == 36000);
    doc["version"] = 2; assert(!valid());
    reset(); doc["width"] = 240; assert(!valid());
    reset(); doc["refresh_ms"] = 0; assert(!valid());
    reset(); doc["brightness"] = 256; assert(!valid());
    reset(); doc["elements"][0]["x"] = -1; assert(!valid());
    reset(); doc["elements"][0]["color"] = "#NOTHEX"; assert(!valid());
    reset(); doc["elements"][0]["font"] = 8; assert(!valid());
    reset(); doc["elements"][0]["text"] = std::string(97, 'x'); assert(!valid());
    reset(); doc["elements"][0]["text"] = "\xE1\x80\x80"; assert(!valid());
    reset(); doc["elements"][0]["type"] = "html"; assert(!valid());
    reset(); doc["elements"][1]["w"] = 321; assert(!valid());
    reset(); doc["buttons"]["B"]["action"] = "execute"; assert(!valid());
    reset(); doc["buttons"]["C"]["seconds"] = 86401; assert(!valid());
    reset(); doc.remove("buttons"); assert(!valid());
    reset();
    auto elements = doc["elements"].as<JsonArray>();
    for (size_t i = elements.size(); i <= ui::MAX_ELEMENTS; ++i) elements.add(elements[0]);
    assert(!valid());
    reset(); doc["elements"][1]["type"] = "line";
    doc["elements"][1]["x2"] = 319; doc["elements"][1]["y2"] = 239;
    assert(valid());
    doc["elements"][1]["y2"] = 240; assert(!valid());
    reset(); assert(valid());
    assert(frame.elements[7].kind == ui::Kind::Marquee && frame.elements[7].speed == 30);
    reset(); doc["elements"][7]["speed"] = 0; assert(!valid());
    reset(); doc["elements"][7]["speed"] = 121; assert(!valid());
    reset(); doc["elements"][7]["w"] = 311; assert(!valid());
    reset(); doc["elements"][7]["h"] = 65; assert(!valid());
    reset(); doc["elements"][7]["y"] = 220; assert(!valid());
    reset(); doc["elements"][7].remove("id"); assert(!valid());
    reset(); doc["elements"][7]["id"] = "bad id"; assert(!valid());
    reset(); doc["elements"][7]["text"] = std::string(192, 'x'); assert(valid());
    doc["elements"][7]["text"] = std::string(193, 'x'); assert(!valid());
    reset(); doc["elements"].as<JsonArray>().add(doc["elements"][7]); assert(!valid());
    reset();
    for (int i = 0; i < 4; ++i) {
        doc["elements"].as<JsonArray>().add(doc["elements"][7]);
        doc["elements"][8 + i]["id"] = "info-" + std::to_string(i);
    }
    assert(!valid());
    reset(); assert(valid());
    ui::Frame empty, previous = frame, next = frame;
    ui::MarqueeTimeline timeline;
    timeline.sync(empty, previous, 100);
    assert(timeline.started[7] == 100);
    std::strcpy(next.elements[0].text, "10:20:31");
    next.elements[7].color = 0xff0000;
    timeline.sync(previous, next, 500);
    assert(timeline.started[7] == 100);
    previous = next;
    std::swap(next.elements[6], next.elements[7]);
    timeline.sync(previous, next, 1000);
    assert(timeline.started[6] == 100);
    previous = next;
    std::strcpy(next.elements[6].text, "New information");
    timeline.sync(previous, next, 1500);
    assert(timeline.started[6] == 1500);
    previous = next;
    next.elements[6].speed = 60;
    timeline.sync(previous, next, 2000);
    assert(timeline.started[6] == 2000);
    ui::Element marquee = frame.elements[7];
    // Short and long text both scroll from the right edge of the viewport.
    assert(ui::marqueeX(marquee, 200, 100, 1100) == 270);
    assert(ui::marqueeX(marquee, 300, 100, 1100) == 270);
    assert(ui::marqueeX(marquee, 500, 100, 100) == 300);
    assert(ui::marqueeX(marquee, 500, 100, 1100) == 270);
    assert(ui::marqueeX(marquee, 500, UINT32_MAX - 500, 499) == 270);
    marquee.speed = 1;
    assert(ui::marqueeX(marquee, 500, 0, 824000) == 300); // Repeat after full exit and gap.
    assert(ui::hasMarquee(frame) && !ui::hasMarquee(empty));
    std::cout << "UI protocol tests passed\n";
}
