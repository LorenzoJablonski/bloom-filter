#include "murmurhash2.h"

uint64_t *murmurhash2_counter = NULL;

uint32_t murmurhash2(const void *key, size_t len, uint32_t seed)
{
    const uint32_t m = 0x5bd1e995;
    const int r = 24;
    const uint8_t *data = (const uint8_t *)key;
    const uint8_t *end = data + (len & ~((size_t)3));

    uint32_t h = seed ^ (uint32_t)len;

    if (murmurhash2_counter) (*murmurhash2_counter)++;

    while (data != end) {
        uint32_t k = ((uint32_t)data[0]) |
                     ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) |
                     ((uint32_t)data[3] << 24);

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
    }

    switch (len & 3) {
    case 3:
        h ^= (uint32_t)data[2] << 16;
        __attribute__((fallthrough));
    case 2:
        h ^= (uint32_t)data[1] << 8;
        __attribute__((fallthrough));
    case 1:
        h ^= (uint32_t)data[0];
        h *= m;
        break;
    }

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
}
