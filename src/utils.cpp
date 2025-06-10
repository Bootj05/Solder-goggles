// Copyright 2025 Bootj05
//
// Licensed under the MIT License.
#include "utils.h"

#include <stddef.h>

bool parseHexColor(const char *hex, uint32_t &value) {
    if (hex == nullptr) {
        return false;
    }
    uint32_t result = 0U;
    for (size_t i = 0U; i < 6U; ++i) {
        char c = hex[i];
        if (c == '\0') {
            return false;
        }
        result <<= 4U;
        if (c >= '0' && c <= '9') {
            result |= static_cast<uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            result |= static_cast<uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            result |= static_cast<uint32_t>(c - 'A' + 10);
        } else {
            return false;
        }
    }
    if (hex[6] != '\0') {
        return false;
    }
    value = result;
    return true;
}
