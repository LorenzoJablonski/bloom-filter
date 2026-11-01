#include "cbf.h"
#include "murmurhash2.h"
#include <stdlib.h>

/*
 * Counting Bloom Filter with 4-bit packed counters.
 * Each byte holds two counters (low nibble = even index, high nibble = odd).
 * Memory = 4 * m bits (4x the standard SBF bit vector), matching the TCC.
 * Counters saturate at 15; a saturated counter is never decremented on delete
 * (avoids false negatives, at the cost of a "ghost" record).
 */

#define CBF_COUNTER_MAX 15u

struct cbf {
    size_t m;             /* number of counters (positions) */
    unsigned k;
    unsigned char *cnt;   /* packed 4-bit counters */
};

cbf_t *cbf_create(size_t m, unsigned k)
{
    cbf_t *bf = (cbf_t *)malloc(sizeof(cbf_t));
    if (!bf) return NULL;
    bf->m = m;
    bf->k = k;
    bf->cnt = (unsigned char *)calloc((m + 1) / 2, sizeof(unsigned char));
    if (!bf->cnt) {
        free(bf);
        return NULL;
    }
    return bf;
}

void cbf_destroy(cbf_t *bf)
{
    if (!bf) return;
    free(bf->cnt);
    free(bf);
}

static inline unsigned cbf_get(const cbf_t *bf, size_t idx)
{
    unsigned char byte = bf->cnt[idx / 2];
    return (idx % 2 == 0) ? (unsigned)(byte & 0x0Fu) : (unsigned)(byte >> 4);
}

static inline void cbf_set(cbf_t *bf, size_t idx, unsigned value)
{
    unsigned char *byte = &bf->cnt[idx / 2];
    if (idx % 2 == 0) {
        *byte = (unsigned char)((*byte & 0xF0u) | (value & 0x0Fu));
    } else {
        *byte = (unsigned char)((*byte & 0x0Fu) | ((value & 0x0Fu) << 4));
    }
}

static inline size_t cbf_hash_index(const bf_key_t *key, unsigned k_iter, size_t m)
{
    uint32_t h = murmurhash2(key->data, key->len, (uint32_t)(k_iter + 1) * 131542391u);
    return (size_t)(h % (uint32_t)m);
}

void cbf_insert(cbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data || bf->m == 0) return;
    for (unsigned i = 0; i < bf->k; i++) {
        size_t idx = cbf_hash_index(key, i, bf->m);
        unsigned v = cbf_get(bf, idx);
        if (v < CBF_COUNTER_MAX) {
            cbf_set(bf, idx, v + 1);
        }
    }
}

void cbf_delete(cbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data || bf->m == 0) return;
    for (unsigned i = 0; i < bf->k; i++) {
        size_t idx = cbf_hash_index(key, i, bf->m);
        unsigned v = cbf_get(bf, idx);
        /* saturated counters stay locked: decrementing could drop a real entry */
        if (v > 0 && v < CBF_COUNTER_MAX) {
            cbf_set(bf, idx, v - 1);
        }
    }
}

bf_member_t cbf_lookup(cbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data || bf->m == 0) return BF_ABSENT;
    for (unsigned i = 0; i < bf->k; i++) {
        size_t idx = cbf_hash_index(key, i, bf->m);
        if (cbf_get(bf, idx) == 0) {
            return BF_ABSENT;
        }
    }
    return BF_PRESENT;
}
