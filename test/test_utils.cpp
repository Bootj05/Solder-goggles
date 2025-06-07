#include <unity.h>
// Copyright 2025 Bootj05
#include "include/utils.h"

void test_valid_color() {
    uint32_t val;
    TEST_ASSERT_TRUE(parseHexColor("ff00ff", val));
    TEST_ASSERT_EQUAL_HEX32(0xff00ff, val);
}

void test_invalid_length() {
    uint32_t val;
    TEST_ASSERT_FALSE(parseHexColor("fff", val));
}

void test_invalid_chars() {
    uint32_t val;
    TEST_ASSERT_FALSE(parseHexColor("gg0000", val));
}

// wsEvent tests from test_wsEvent.cpp
void test_next_message();
void test_set_message();
void test_bright_message();

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_color);
    RUN_TEST(test_invalid_length);
    RUN_TEST(test_invalid_chars);
    RUN_TEST(test_next_message);
    RUN_TEST(test_set_message);
    RUN_TEST(test_bright_message);
    return UNITY_END();
}

