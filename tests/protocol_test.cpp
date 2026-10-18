#include "ui_protocol.h"
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
    assert(frame.count == 7 && frame.brightness == 30 && frame.refreshMs == 1500);
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
    std::cout << "UI protocol tests passed\n";
}
