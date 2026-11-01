#include "pbf.h"
#include "murmurhash2.h"
#include <math.h>
#include <stdlib.h>

/*
 * Partitioned Bloom Filter.
 * The address space is split into k disjoint partitions of m/k bits each.
 * Hash i is restricted to partition i, so each insertion touches exactly one
 * bit per partition. This bounds cross-partition collisions and improves
 * cache locality during lookups.
 */

struct pbf {
    size_t m;              /* requested bits (partition_size * k used) */
    unsigned k;
    size_t partition_size; /* bits per partition */
    unsigned char *bits;
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
    bf->bits = (unsigned char *)calloc((bf->m + 7) / 8, sizeof(unsigned char));
    if (!bf->bits) {
        free(bf);
        return NULL;
    }
    return bf;
}

void pbf_destroy(pbf_t *bf)
{
    if (!bf) return;
    free(bf->bits);
    free(bf);
}

static inline size_t pbf_position(const pbf_t *bf, const bf_key_t *key, unsigned i)
{
    uint32_t h = murmurhash2(key->data, key->len, (uint32_t)(i + 1) * 131542391u);
    size_t offset = (size_t)i * bf->partition_size;
    return offset + (size_t)(h % (uint32_t)bf->partition_size);
}

static inline int pbf_test(const pbf_t *bf, size_t pos)
{
    return (bf->bits[pos / 8] >> (pos % 8)) & 1u;
}

static inline void pbf_set(pbf_t *bf, size_t pos)
{
    bf->bits[pos / 8] |= (unsigned char)(1u << (pos % 8));
}

void pbf_insert(pbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return;
    for (unsigned i = 0; i < bf->k; i++) {
        pbf_set(bf, pbf_position(bf, key, i));
    }
}

bf_member_t pbf_lookup(pbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return BF_ABSENT;
    for (unsigned i = 0; i < bf->k; i++) {
        if (!pbf_test(bf, pbf_position(bf, key, i))) {
            return BF_ABSENT;
        }
    }
    return BF_PRESENT;
}

size_t pbf_memory_bytes(const pbf_t *bf)
{
    return bf ? (bf->m + 7) / 8 : 0;
}

size_t pbf_cells(const pbf_t *bf)
{
    return bf ? bf->m : 0;
}

/* k independent partitions of s = m/k bits: (1 - (1 - 1/s)^n)^k */
double pbf_fpr_theory(const pbf_t *bf, size_t n)
{
    if (!bf || bf->partition_size == 0) return 0.0;
    double s = (double)bf->partition_size;
    double p_one = 1.0 - pow(1.0 - 1.0 / s, (double)n);
    return pow(p_one, (double)bf->k);
}

double pbf_fill_ratio(const pbf_t *bf)
{
    if (!bf || bf->m == 0) return 0.0;
    size_t ones = 0, bytes = (bf->m + 7) / 8;
    for (size_t i = 0; i < bytes; i++) ones += (size_t)__builtin_popcount(bf->bits[i]);
    return (double)ones / (double)bf->m;
}
