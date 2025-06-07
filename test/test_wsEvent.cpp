#include <unity.h>
#include <stdint.h>
#include <string>
#include <cstring>

#include <vector>
// Copyright 2025 Bootj05

enum WStype_t { WStype_TEXT };

enum class PresetType { STATIC };

struct Preset {
    PresetType type;
};

static std::vector<Preset> presets;
static int currentPreset = 0;
static uint8_t brightness = 255;
static uint32_t animInterval = 50;
static bool applyCalled = false;

static void applyPreset() { applyCalled = true; }

static void nextPreset() {
    currentPreset = (currentPreset + 1) % presets.size();
    applyPreset();
}

static void previousPreset() {
    currentPreset = (currentPreset - 1 + presets.size()) % presets.size();
    applyPreset();
}

static void wsEvent(uint8_t, WStype_t type, uint8_t *payload, size_t len) {
    if (type != WStype_TEXT) return;
    std::string msg(reinterpret_cast<char*>(payload), len);
    if (msg == "next") {
        nextPreset();
    } else if (msg == "prev") {
        previousPreset();
    } else if (msg.rfind("set:", 0) == 0) {
        std::string idxStr = msg.substr(4);
        bool digits = !idxStr.empty() &&
            idxStr.find_first_not_of("0123456789") == std::string::npos;
        if (digits) {
            int idx = std::stoi(idxStr);
            if (idx >= 0 && idx < static_cast<int>(presets.size())) {
                currentPreset = idx;
                applyPreset();
            }
        }
    } else if (msg.rfind("bright:", 0) == 0) {
        std::string valStr = msg.substr(7);
        bool digits = !valStr.empty() &&
            valStr.find_first_not_of("0123456789") == std::string::npos;
        if (digits) {
            int val = std::stoi(valStr);
            if (val >= 0 && val <= 255) {
                brightness = static_cast<uint8_t>(val);
                applyPreset();
            }
        }
    }
}

void setUp(void) {
    presets.clear();
    presets.push_back({PresetType::STATIC});
    presets.push_back({PresetType::STATIC});
    presets.push_back({PresetType::STATIC});
    currentPreset = 0;
    brightness = 255;
    animInterval = 50;
    applyCalled = false;
}

void tearDown(void) {}

void test_next_message() {
    const char msg[] = "next";
    wsEvent(0, WStype_TEXT,
            reinterpret_cast<uint8_t*>(const_cast<char*>(msg)), strlen(msg));
    TEST_ASSERT_EQUAL(1, currentPreset);
    TEST_ASSERT_TRUE(applyCalled);
}

void test_set_message() {
    const char msg[] = "set:1";
    wsEvent(0, WStype_TEXT,
            reinterpret_cast<uint8_t*>(const_cast<char*>(msg)), strlen(msg));
    TEST_ASSERT_EQUAL(1, currentPreset);
    TEST_ASSERT_TRUE(applyCalled);
}

void test_bright_message() {
    const char msg[] = "bright:128";
    wsEvent(0, WStype_TEXT,
            reinterpret_cast<uint8_t*>(const_cast<char*>(msg)), strlen(msg));
    TEST_ASSERT_EQUAL_UINT8(128, brightness);
    TEST_ASSERT_TRUE(applyCalled);
}

