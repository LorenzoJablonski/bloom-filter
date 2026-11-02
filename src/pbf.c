#define _POSIX_C_SOURCE 200112L /* posix_memalign */
#include "pbf.h"
#include "murmurhash2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Partitioned Bloom Filter.
 * The filter is k independent bit vectors ("partitions") of s bits each.
 * Hash i only addresses partition i, so each insertion sets exactly one bit
 * per partition.
 *
 * Memory layout: the partitions live in one allocation, but each one starts
 * on its own cache line (PBF_ALIGN bytes). The bytes between the end of a
 * partition and the start of the next are padding (always zero, never
 * addressed):
 *
 *   | partition 0 (s bits) | pad | partition 1 (s bits) | pad | ...
 *   ^ 64-byte boundary           ^ 64-byte boundary
 *
 * Consequences: no byte or cache line is shared by two partitions, so a
 * thread that owns partition i never touches memory written by another
 * thread (no lost updates, no false sharing). Padding is < PBF_ALIGN bytes
 * per partition and is included in pbf_memory_bytes().
 */

#define PBF_ALIGN 64u

struct pbf {
    size_t m;              /* effective bits = partition_size * k (padding excluded) */
    unsigned k;
    size_t partition_size; /* bits per partition (s) */
    size_t stride;         /* bytes from one partition start to the next */
    unsigned char *bits;   /* PBF_ALIGN-aligned, k * stride bytes */
};

pbf_t *pbf_create(size_t m, unsigned k)
{
    if (k == 0) return NULL;
    pbf_t *bf = (pbf_t *)malloc(sizeof(pbf_t));
    if (!bf) return NULL;
    bf->k = k;
    bf->partition_size = m / k;
    if (bf->partition_size == 0) bf->partition_size = 1;
    bf->m = bf->partition_size * k;

    size_t part_bytes = (bf->partition_size + 7) / 8;
    bf->stride = (part_bytes + PBF_ALIGN - 1) / PBF_ALIGN * PBF_ALIGN;

    void *mem = NULL;
    if (posix_memalign(&mem, PBF_ALIGN, bf->stride * k) != 0) {
        free(bf);
        return NULL;
    }
    memset(mem, 0, bf->stride * k);
    bf->bits = (unsigned char *)mem;
    return bf;
}

void pbf_destroy(pbf_t *bf)
{
    if (!bf) return;
    free(bf->bits);
    free(bf);
}

static inline unsigned char *pbf_partition(const pbf_t *bf, unsigned i)
{
    return bf->bits + (size_t)i * bf->stride;
}

/* bit offset of key inside partition i */
static inline size_t pbf_offset(const pbf_t *bf, const bf_key_t *key, unsigned i)
{
    uint32_t h = murmurhash2(key->data, key->len, (uint32_t)(i + 1) * 131542391u);
    return (size_t)(h % (uint32_t)bf->partition_size);
}

void pbf_insert(pbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return;
    for (unsigned i = 0; i < bf->k; i++) {
        size_t off = pbf_offset(bf, key, i);
        pbf_partition(bf, i)[off / 8] |= (unsigned char)(1u << (off % 8));
    }
}

bf_member_t pbf_lookup(pbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return BF_ABSENT;
    for (unsigned i = 0; i < bf->k; i++) {
        size_t off = pbf_offset(bf, key, i);
        if (!((pbf_partition(bf, i)[off / 8] >> (off % 8)) & 1u)) {
            return BF_ABSENT;
        }
    }
    return BF_PRESENT;
}

/* allocated bytes, padding included */
size_t pbf_memory_bytes(const pbf_t *bf)
{
    return bf ? bf->stride * bf->k : 0;
}

size_t pbf_cells(const pbf_t *bf)
{
    return bf ? bf->m : 0;
}

size_t pbf_partition_stride(const pbf_t *bf)
{
    return bf ? bf->stride : 0;
}

/* k independent partitions of s bits: (1 - (1 - 1/s)^n)^k */
static double pbf_fpr_formula(size_t s, unsigned k, size_t n)
{
    double p_one = -expm1((double)n * log1p(-1.0 / (double)s));
    return pow(p_one, (double)k);
}

double pbf_fpr_theory(const pbf_t *bf, size_t n)
{
    if (!bf || bf->partition_size == 0) return 0.0;
    return pbf_fpr_formula(bf->partition_size, bf->k, n);
}

double pbf_fill_ratio(const pbf_t *bf)
{
    if (!bf || bf->m == 0) return 0.0;
    size_t ones = 0, total = bf->stride * bf->k;  /* padding is always zero */
    for (size_t i = 0; i < total; i++) ones += (size_t)__builtin_popcount(bf->bits[i]);
    return (double)ones / (double)bf->m;
}

/*
 * Sizing for a target FPR p: for each k, invert the PBF formula to get the
 * smallest partition size s with (1 - (1 - 1/s)^n)^k <= p, i.e.
 *     s = 1 / (1 - (1 - p^{1/k})^{1/n})
 * and keep the k that minimises total bits m = k * s.
 */
int pbf_params(size_t n, double p, bf_sizing_t *out)
{
    if (!out || n == 0 || !(p > 0.0 && p < 1.0)) return 0;
    size_t best_m = 0;
    unsigned best_k = 0;
    for (unsigned k = 1; k <= BF_MAX_K; k++) {
        double q = pow(p, 1.0 / (double)k);              /* per-partition hit probability */
        double inv_s = -expm1(log1p(-q) / (double)n);    /* 1 - (1-q)^{1/n} */
        size_t s = (size_t)ceil(1.0 / inv_s);
        while (s > 1 && pbf_fpr_formula(s - 1, k, n) <= p) s--;  /* undo rounding slack */
        while (pbf_fpr_formula(s, k, n) > p) s++;
        size_t m = s * k;
        if (best_k == 0 || m < best_m) { best_m = m; best_k = k; }
    }
    out->m = best_m;
    out->k = best_k;
    return 1;
}
