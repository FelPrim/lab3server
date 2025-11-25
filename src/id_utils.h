#pragma once
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>

static inline uint32_t generate_id(void) {
    static bool was_called = false;
    if (!was_called) {
        srand((unsigned int)time(NULL));
        was_called = true;
    }
    return (uint32_t)(rand() % 308915776);  // 26^6 = 308915776
}

static inline void id_to_string(uint32_t id, char str[6]) {
    for (int i = 0; i < 6; ++i) {
        str[i] = (char)(id % 26 + 'A');
        id /= 26;
    }
}

static inline uint32_t string_to_id(const char* str) {
    uint32_t id = 0;
    for (int i = 0; i < 6; ++i) {
        id *= 26;
        id += (uint32_t)(str[5 - i] - 'A');
    }
    return id;
}