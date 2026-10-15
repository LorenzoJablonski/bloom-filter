#include "sbf.h"
#include "murmurhash2.h"
#include <stdlib.h>

struct sbf {
    size_t m;
    unsigned k;
    unsigned char *bits; /* bitmask: use bytes for simplicity */
};

sbf_t *sbf_create(size_t m, unsigned k)
{
    sbf_t *bf = (sbf_t *)malloc(sizeof(sbf_t));
    if (!bf) return NULL;
    bf->m = m;
    bf->k = k;
    /* allocate bits rounded to bytes */
    bf->bits = (unsigned char *)calloc((m + 7) / 8, sizeof(unsigned char));
    if (!bf->bits) {
        free(bf);
        return NULL;
    }
    return bf;
}

void sbf_destroy(sbf_t *bf)
{
    if (!bf) return;
    free(bf->bits);
    free(bf);
}

static inline size_t sbf_hash_index(const bf_key_t *key, unsigned k_iter, size_t m)
{
    /* generate k independent positions using MurmurHash2 with varying seed */
    uint32_t h = murmurhash2(key->data, key->len, (uint32_t)(k_iter + 1) * 131542391u);
    /* reduce modulo m */
    return (size_t)(h % (uint32_t)m);
}

void sbf_insert(sbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data || bf->m == 0) return;
    for (unsigned i = 0; i < bf->k; i++) {
        size_t idx = sbf_hash_index(key, i, bf->m);
        bf->bits[idx / 8] |= (unsigned char)(1u << (idx % 8));
    }
}

bf_member_t sbf_lookup(sbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data || bf->m == 0) return BF_ABSENT;
    for (unsigned i = 0; i < bf->k; i++) {
        size_t idx = sbf_hash_index(key, i, bf->m);
        unsigned char bit = (unsigned char)(1u << (idx % 8));
        if ((bf->bits[idx / 8] & bit) == 0) {
            return BF_ABSENT;
        }
    }
    return BF_PRESENT;
}
