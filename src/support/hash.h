#pragma once

#include "bits.h"

#include <stdint.h>

static inline uint32_t hash_init(void) {
    return 0x811c9dc5;
}

static inline uint32_t hash_uint8(uint32_t h, uint8_t x) {
    return (h ^ x) * 0x01000193;
}

static inline uint32_t hash_uint16(uint32_t h, uint16_t x) {
    return hash_uint8(hash_uint8(h, x >> 8), x);
}

static inline uint32_t hash_uint32(uint32_t h, uint32_t x) {
    return hash_uint16(hash_uint16(h, x >> 16), x);
}

static inline uint32_t hash_uint64(uint32_t h, uint64_t x) {
    return hash_uint32(hash_uint32(h, x >> 32), x);
}
