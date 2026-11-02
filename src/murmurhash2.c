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

uint64_t murmurhash64a(const void *key, size_t len, uint64_t seed)
{
    const uint64_t m = 0xc6a4a7935bd1e995ULL;
    const int r = 47;
    const uint8_t *data = (const uint8_t *)key;
    const uint8_t *end = data + (len & ~((size_t)7));

    uint64_t h = seed ^ ((uint64_t)len * m);

    if (murmurhash2_counter) (*murmurhash2_counter)++;

    while (data != end) {
        /* little-endian load, independent of host alignment/endianness */
        uint64_t k = ((uint64_t)data[0])       | ((uint64_t)data[1] << 8)  |
                     ((uint64_t)data[2] << 16) | ((uint64_t)data[3] << 24) |
                     ((uint64_t)data[4] << 32) | ((uint64_t)data[5] << 40) |
                     ((uint64_t)data[6] << 48) | ((uint64_t)data[7] << 56);

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;

        data += 8;
    }

    switch (len & 7) {
    case 7:
        h ^= (uint64_t)data[6] << 48;
        __attribute__((fallthrough));
    case 6:
        h ^= (uint64_t)data[5] << 40;
        __attribute__((fallthrough));
    case 5:
        h ^= (uint64_t)data[4] << 32;
        __attribute__((fallthrough));
    case 4:
        h ^= (uint64_t)data[3] << 24;
        __attribute__((fallthrough));
    case 3:
        h ^= (uint64_t)data[2] << 16;
        __attribute__((fallthrough));
    case 2:
        h ^= (uint64_t)data[1] << 8;
        __attribute__((fallthrough));
    case 1:
        h ^= (uint64_t)data[0];
        h *= m;
        break;
    }

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}
