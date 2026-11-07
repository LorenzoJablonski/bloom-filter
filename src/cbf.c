#include "cbf.h"
#include "murmurhash2.h"
#include <math.h>
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

size_t cbf_memory_bytes(const cbf_t *bf)
{
    return bf ? (bf->m + 1) / 2 : 0;
}

size_t cbf_cells(const cbf_t *bf)
{
    return bf ? bf->m : 0;
}

/* same as SBF (counter saturation is negligible at 4 bits for typical loads) */
double cbf_fpr_theory(const cbf_t *bf, size_t n)
{
    if (!bf || bf->m == 0) return 0.0;
    return pow(1.0 - exp(-(double)bf->k * (double)n / (double)bf->m), (double)bf->k);
}

double cbf_fill_ratio(const cbf_t *bf)
{
    if (!bf || bf->m == 0) return 0.0;
    size_t nz = 0;
    for (size_t i = 0; i < bf->m; i++) nz += cbf_get(bf, i) != 0;
    return (double)nz / (double)bf->m;
}

size_t cbf_saturated(const cbf_t *bf)
{
    if (!bf) return 0;
    size_t s = 0;
    for (size_t i = 0; i < bf->m; i++) s += cbf_get(bf, i) == CBF_COUNTER_MAX;
    return s;
}

/* Same FPR as the SBF with the same (m, k); m here counts 4-bit counters. */
/*
 * Sizing for a target FPR p: for each k, invert (1 - e^{-kn/m})^k = p to
 *     m = -k n / ln(1 - p^{1/k})
 * and keep the k that minimises m. Equivalent to the classic closed form
 * m = -n ln p / (ln 2)^2, k = (m/n) ln 2, but without rounding k afterwards
 * (rounding k would make the achieved FPR drift away from p).
 */
int cbf_params(size_t n, double p, bf_sizing_t *out)
{
    if (!out || n == 0 || !(p > 0.0 && p < 1.0)) return 0;
    size_t best_m = 0;
    unsigned best_k = 0;
    for (unsigned k = 1; k <= BF_MAX_K; k++) {
        double q = pow(p, 1.0 / (double)k);
        size_t m = (size_t)ceil(-(double)k * (double)n / log1p(-q));
        if (best_k == 0 || m < best_m) { best_m = m; best_k = k; }
    }
    out->m = best_m;
    out->k = best_k;
    return 1;
}

/* ---- collision counting (untimed pass; kept apart from the measured insert) ---- */

unsigned cbf_insert_counted(cbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data || bf->m == 0) return 0;
    unsigned col = 0;
    for (unsigned i = 0; i < bf->k; i++) {
        size_t idx = cbf_hash_index(key, i, bf->m);
        unsigned v = cbf_get(bf, idx);
        col += v > 0;
        if (v < CBF_COUNTER_MAX) cbf_set(bf, idx, v + 1);
    }
    return col;
}
