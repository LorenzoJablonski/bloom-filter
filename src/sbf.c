#include "sbf.h"
#include "murmurhash2.h"
#include <math.h>
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

size_t sbf_memory_bytes(const sbf_t *bf)
{
    return bf ? (bf->m + 7) / 8 : 0;
}

size_t sbf_cells(const sbf_t *bf)
{
    return bf ? bf->m : 0;
}

/* classic approximation: (1 - e^{-kn/m})^k */
double sbf_fpr_theory(const sbf_t *bf, size_t n)
{
    if (!bf || bf->m == 0) return 0.0;
    return pow(1.0 - exp(-(double)bf->k * (double)n / (double)bf->m), (double)bf->k);
}

double sbf_fill_ratio(const sbf_t *bf)
{
    if (!bf || bf->m == 0) return 0.0;
    size_t ones = 0, bytes = (bf->m + 7) / 8;
    for (size_t i = 0; i < bytes; i++) ones += (size_t)__builtin_popcount(bf->bits[i]);
    return (double)ones / (double)bf->m;
}

/*
 * Sizing for a target FPR p: for each k, invert (1 - e^{-kn/m})^k = p to
 *     m = -k n / ln(1 - p^{1/k})
 * and keep the k that minimises m. Equivalent to the classic closed form
 * m = -n ln p / (ln 2)^2, k = (m/n) ln 2, but without rounding k afterwards
 * (rounding k would make the achieved FPR drift away from p).
 */
int sbf_params(size_t n, double p, bf_sizing_t *out)
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
