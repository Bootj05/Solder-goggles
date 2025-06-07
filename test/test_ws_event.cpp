// Copyright 2025 Bootj05
#include <unity.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>

// Minimal stand-ins for firmware globals and helpers
struct Preset {};

static std::vector<Preset> presets;
static int currentPreset = 0;
static uint8_t brightness = 255;
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
    }
}

void setUp(void) {
    presets.assign(3, Preset());
    currentPreset = 0;
    brightness = 255;
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

// Test functions are called from main() in test_utils.cpp
