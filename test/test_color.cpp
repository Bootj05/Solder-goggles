#include <Arduino.h>
#include <unity.h>
#include <FastLED.h>
#include "../src/utils.h"

void test_parse_valid() {
    CRGB c;
    TEST_ASSERT_TRUE(parseHexColor("#1A2B3C", c));
    TEST_ASSERT_EQUAL_UINT8(0x1A, c.r);
    TEST_ASSERT_EQUAL_UINT8(0x2B, c.g);
    TEST_ASSERT_EQUAL_UINT8(0x3C, c.b);
}

void test_parse_invalid_format() {
    CRGB c;
    TEST_ASSERT_FALSE(parseHexColor("123456", c));
    TEST_ASSERT_FALSE(parseHexColor("#12345", c));
}

void test_parse_invalid_chars() {
    CRGB c;
    TEST_ASSERT_FALSE(parseHexColor("#ZZZZZZ", c));
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_valid);
    RUN_TEST(test_parse_invalid_format);
    RUN_TEST(test_parse_invalid_chars);
    UNITY_END();
}

void loop() {}
