#include <unity.h>
// Copyright 2025 Bootj05
#include "include/utils.h"

// Declarations from test_ws_event.cpp
void test_next_message();
void test_set_message();
void test_brightness_message();
void test_color_message();
void test_speed_message();
void test_leds_message();
void test_unknown_command();
void test_set_oob();
void test_set_invalid();
void test_brightness_oob();
void test_brightness_invalid();
void test_speed_invalid();
void test_speed_nonnumeric();
void test_leds_bad_data();

void test_valid_color() {
    uint32_t val;
    TEST_ASSERT_TRUE(parseHexColor("ff00ff", val));
    TEST_ASSERT_EQUAL_HEX32(0xff00ff, val);
}

void test_uppercase_color() {
    uint32_t val;
    TEST_ASSERT_TRUE(parseHexColor("FF00FF", val));
    TEST_ASSERT_EQUAL_HEX32(0xFF00FF, val);
}

void test_invalid_length() {
    uint32_t val;
    TEST_ASSERT_FALSE(parseHexColor("fff", val));
}

void test_invalid_chars() {
    uint32_t val;
    TEST_ASSERT_FALSE(parseHexColor("gg0000", val));
}

void test_invalid_chars_upper() {
    uint32_t val;
    TEST_ASSERT_FALSE(parseHexColor("FF00FG", val));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_color);
    RUN_TEST(test_uppercase_color);
    RUN_TEST(test_invalid_length);
    RUN_TEST(test_invalid_chars);
    RUN_TEST(test_invalid_chars_upper);
    RUN_TEST(test_next_message);
    RUN_TEST(test_set_message);
    RUN_TEST(test_brightness_message);
    RUN_TEST(test_color_message);
    RUN_TEST(test_speed_message);
    RUN_TEST(test_leds_message);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_set_oob);
    RUN_TEST(test_set_invalid);
    RUN_TEST(test_brightness_oob);
    RUN_TEST(test_brightness_invalid);
    RUN_TEST(test_speed_invalid);
    RUN_TEST(test_speed_nonnumeric);
    RUN_TEST(test_leds_bad_data);
    return UNITY_END();
}

