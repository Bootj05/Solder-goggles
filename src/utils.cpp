#include "utils.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

bool parseHexColor(const char *hex, uint32_t &value) {
    if (!hex || strlen(hex) != 6)
        return false;
    for (size_t i = 0; i < 6; ++i) {
        if (!isxdigit(static_cast<unsigned char>(hex[i])))
            return false;
    }
    value = strtoul(hex, nullptr, 16);
    return true;
}
