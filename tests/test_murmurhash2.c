#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "murmurhash2.h"

/*
 * smhasher VerificationTest: hash keys {0}, {0,1}, ..., {0..254} of length
 * 0..255 with seed 256 - len, hash the concatenated digests with seed 0 and
 * read the first 4 bytes (little-endian). Published values (smhasher/main.cpp):
 *   MurmurHash2   (32-bit)  0x27864C1E
 *   MurmurHash64A (64-bit)  0x1F0D3804
 */
static void put_le(uint8_t *out, uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; i++) out[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t verify32(void)
{
    uint8_t key[256], hashes[4 * 256], final[4];
    for (int i = 0; i < 256; i++) {
        key[i] = (uint8_t)i;
        put_le(&hashes[i * 4], murmurhash2(key, (size_t)i, (uint32_t)(256 - i)), 4);
    }
    put_le(final, murmurhash2(hashes, sizeof(hashes), 0), 4);
    return (uint32_t)final[0] | (uint32_t)final[1] << 8 | (uint32_t)final[2] << 16 | (uint32_t)final[3] << 24;
}

static uint32_t verify64(void)
{
    uint8_t key[256], hashes[8 * 256], final[8];
    for (int i = 0; i < 256; i++) {
        key[i] = (uint8_t)i;
        put_le(&hashes[i * 8], murmurhash64a(key, (size_t)i, (uint64_t)(256 - i)), 8);
    }
    put_le(final, murmurhash64a(hashes, sizeof(hashes), 0), 8);
    return (uint32_t)final[0] | (uint32_t)final[1] << 8 | (uint32_t)final[2] << 16 | (uint32_t)final[3] << 24;
}

static void test_counter(void)
{
    uint64_t c = 0;
    murmurhash2_counter = &c;
    murmurhash2("a", 1, 0);
    murmurhash64a("a", 1, 0);
    murmurhash2_counter = NULL;
    murmurhash2("a", 1, 0);
    assert(c == 2);
}

int main(void)
{
    uint32_t v32 = verify32(), v64 = verify64();
    printf("murmurhash2  : 0x%08X (esperado 0x27864C1E)\n", v32);
    printf("murmurhash64a: 0x%08X (esperado 0x1F0D3804)\n", v64);
    assert(v32 == 0x27864C1Eu);
    assert(v64 == 0x1F0D3804u);
    test_counter();
    printf("murmurhash2: all tests passed\n");
    return 0;
}
