// Copyright 2025 Bootj05
#include <unity.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include "include/utils.h"

// Minimal stand-ins for firmware globals and helpers
constexpr size_t NUM_LEDS = 13;
struct Preset {
    uint32_t color{};
    std::vector<uint32_t> leds;
    Preset() : color(0), leds(NUM_LEDS, 0) {}
};

static std::vector<Preset> presets;
static int currentPreset = 0;
static uint8_t brightness = 255;
static uint32_t animInterval = 50;
static bool applyCalled = false;

static void applyPreset() {
    applyCalled = true;
}

static void nextPreset() {
    currentPreset = (currentPreset + 1) % presets.size();
}

static void previousPreset() {
    currentPreset = (currentPreset - 1 + presets.size()) % presets.size();
}

enum WStype_t { WStype_TEXT };

static void wsEvent(uint8_t /*num*/, WStype_t type, uint8_t *payload,
                    size_t /*len*/) {
    if (type != WStype_TEXT)
        return;
    std::string msg(reinterpret_cast<char *>(payload));

    if (msg == "next") {
        nextPreset();
    } else if (msg == "prev") {
        previousPreset();
    } else if (msg.rfind("set:", 0) == 0) {
        std::string idxStr = msg.substr(4);
        if (!idxStr.empty() &&
            idxStr.find_first_not_of("0123456789") == std::string::npos) {
            int idx = std::stoi(idxStr);
            if (idx >= 0 && idx < static_cast<int>(presets.size())) {
                currentPreset = idx;
                applyPreset();
            }
        }
    } else if (msg.rfind("bright:", 0) == 0) {
        std::string valStr = msg.substr(7);
        if (!valStr.empty() &&
            valStr.find_first_not_of("0123456789") == std::string::npos) {
            int val = std::stoi(valStr);
            if (val >= 0 && val <= 255) {
                brightness = static_cast<uint8_t>(val);
                applyPreset();
            }
        }
    } else if (msg.rfind("color:", 0) == 0) {
        std::string colorStr = msg.substr(6);
        if (colorStr.size() == 7 && colorStr[0] == '#') {
            colorStr.erase(0, 1);
            uint32_t val;
            if (parseHexColor(colorStr.c_str(), val)) {
                presets[currentPreset].color = val;
                applyPreset();
            }
        }
    } else if (msg.rfind("speed:", 0) == 0) {
        std::string valStr = msg.substr(6);
        if (!valStr.empty() &&
            valStr.find_first_not_of("0123456789") == std::string::npos) {
            int val = std::stoi(valStr);
            if (val > 0)
                animInterval = static_cast<uint32_t>(val);
        }
    } else if (msg.rfind("leds:", 0) == 0) {
        std::string data = msg.substr(5);
        std::vector<uint32_t> temp(NUM_LEDS, 0);
        size_t idx = 0;
        bool valid = true;
        while (idx < NUM_LEDS && !data.empty()) {
            size_t sep = data.find(',');
            std::string tok =
                sep == std::string::npos ? data : data.substr(0, sep);
            if (!tok.empty() && tok[0] == '#')
                tok.erase(0, 1);
            uint32_t val;
            if (parseHexColor(tok.c_str(), val)) {
                temp[idx] = val;
            } else {
                valid = false;
                break;
            }
            if (sep == std::string::npos)
                data.clear();
            else
                data.erase(0, sep + 1);
            ++idx;
        }
        if (valid && data.empty()) {
            presets[currentPreset].leds = temp;
            applyPreset();
        }
    }
}

void setUp(void) {
    presets.assign(3, Preset());
    currentPreset = 0;
    brightness = 255;
    animInterval = 50;
    applyCalled = false;
}

void test_next_message() {
    const char msg[] = "next";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL(1, currentPreset);
}

void test_set_message() {
    const char msg[] = "set:1";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL(1, currentPreset);
    TEST_ASSERT_TRUE(applyCalled);
}

void test_brightness_message() {
    const char msg[] = "bright:128";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_UINT8(128, brightness);
    TEST_ASSERT_TRUE(applyCalled);
}

void test_color_message() {
    const char msg[] = "color:#112233";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_HEX32(0x112233, presets[currentPreset].color);
    TEST_ASSERT_TRUE(applyCalled);
}

void test_speed_message() {
    const char msg[] = "speed:123";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_UINT32(123, animInterval);
}

void test_leds_message() {
    const char msg[] = "leds:#010203,#a0b0c0,#ffffff";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_HEX32(0x010203, presets[currentPreset].leds[0]);
    TEST_ASSERT_EQUAL_HEX32(0xa0b0c0, presets[currentPreset].leds[1]);
    TEST_ASSERT_EQUAL_HEX32(0xffffff, presets[currentPreset].leds[2]);
    TEST_ASSERT_TRUE(applyCalled);
}

void test_unknown_command() {
    const char msg[] = "foobar";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL(0, currentPreset);
    TEST_ASSERT_EQUAL_UINT8(255, brightness);
    TEST_ASSERT_EQUAL_UINT32(50, animInterval);
    TEST_ASSERT_FALSE(applyCalled);
}

void test_set_oob() {
    const char msg[] = "set:5";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL(0, currentPreset);
    TEST_ASSERT_FALSE(applyCalled);
}

void test_set_invalid() {
    const char msg[] = "set:abc";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL(0, currentPreset);
    TEST_ASSERT_FALSE(applyCalled);
}

void test_brightness_oob() {
    const char msg[] = "bright:300";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_UINT8(255, brightness);
    TEST_ASSERT_FALSE(applyCalled);
}

void test_brightness_invalid() {
    const char msg[] = "bright:abc";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_UINT8(255, brightness);
    TEST_ASSERT_FALSE(applyCalled);
}

void test_speed_invalid() {
    const char msg[] = "speed:0";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_UINT32(50, animInterval);
    TEST_ASSERT_FALSE(applyCalled);
}

void test_speed_nonnumeric() {
    const char msg[] = "speed:abc";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_UINT32(50, animInterval);
    TEST_ASSERT_FALSE(applyCalled);
}

void test_leds_bad_data() {
    presets[currentPreset].leds[0] = 0x123456;
    const char msg[] = "leds:#zzzzzz";
    auto buf = reinterpret_cast<uint8_t *>(const_cast<char *>(msg));
    wsEvent(0, WStype_TEXT, buf, sizeof(msg) - 1);
    TEST_ASSERT_EQUAL_HEX32(0x123456, presets[currentPreset].leds[0]);
    TEST_ASSERT_FALSE(applyCalled);
}

// Test functions are called from main() in test_utils.cpp
