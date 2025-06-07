#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include <ctype.h>

inline bool parseHexColor(const String &input, CRGB &out) {
    if (input.length() != 7 || input[0] != '#')
        return false;
    String hex = input.substring(1);
    for (size_t i = 0; i < hex.length(); ++i) {
        if (!isxdigit(static_cast<unsigned char>(hex[i])))
            return false;
    }
    long val = strtol(hex.c_str(), nullptr, 16);
    out = CRGB((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
    return true;
}
