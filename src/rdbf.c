#include "rdbf.h"
#include "murmurhash2.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>

/*
 * r-Dimensional Bloom Filter (2D variant).
 * The filter is a prime-sized matrix (p1 x p2) of bits stored in unsigned long
 * words. A single MurmurHash2 call yields all coordinates via successive
 * modulo operations; each key touches exactly one bit.
 *
 * Deletion is intentionally not supported: without per-cell counters, clearing
 * a bit shared by two keys would introduce false negatives. This matches the
 * published rDBF/robustBF design (no counting variant exists).
 */

#define RDBF_UL_BITS ((unsigned)(sizeof(unsigned long) * CHAR_BIT))
#define RDBF_SEED 0x9E3779B1u

struct rdbf {
    unsigned long p1;   /* prime, x axis */
    unsigned long p2;   /* prime, y axis */
    size_t nbits;       /* p1 * p2 */
    size_t nwords;
    unsigned long *words;
};

static int is_prime(unsigned long n)
{
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (unsigned long d = 3; d <= n / d; d += 2) {
        if (n % d == 0) return 0;
    }
    return 1;
}

static unsigned long next_prime(unsigned long n)
{
    if (n <= 2) return 2;
    if (n % 2 == 0) n++;
    while (!is_prime(n)) n += 2;
    return n;
}

rdbf_t *rdbf_create(size_t m)
{
    if (m == 0) return NULL;
    rdbf_t *bf = (rdbf_t *)malloc(sizeof(rdbf_t));
    if (!bf) return NULL;

    unsigned long side = (unsigned long)sqrt((double)m);
    if (side < 2) side = 2;
    bf->p1 = next_prime(side);
    unsigned long rows = (unsigned long)((m + bf->p1 - 1) / bf->p1);
    bf->p2 = next_prime(rows);

    bf->nbits = (size_t)bf->p1 * (size_t)bf->p2;
    bf->nwords = (bf->nbits + RDBF_UL_BITS - 1) / RDBF_UL_BITS;
    bf->words = (unsigned long *)calloc(bf->nwords, sizeof(unsigned long));
    if (!bf->words) {
        free(bf);
        return NULL;
    }
    return bf;
}

void rdbf_destroy(rdbf_t *bf)
{
    if (!bf) return;
    free(bf->words);
    free(bf);
}

static inline size_t rdbf_bit_index(const rdbf_t *bf, const bf_key_t *key)
{
    uint32_t h = murmurhash2(key->data, key->len, RDBF_SEED);
    unsigned long x = (unsigned long)h % bf->p1;
    unsigned long y = ((unsigned long)h / bf->p1) % bf->p2;
    return (size_t)y * (size_t)bf->p1 + (size_t)x;
}

static inline void rdbf_set(rdbf_t *bf, size_t bit)
{
    bf->words[bit / RDBF_UL_BITS] |= (1UL << (bit % RDBF_UL_BITS));
}

static inline int rdbf_test(const rdbf_t *bf, size_t bit)
{
    return (bf->words[bit / RDBF_UL_BITS] >> (bit % RDBF_UL_BITS)) & 1UL;
}

void rdbf_insert(rdbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return;
    rdbf_set(bf, rdbf_bit_index(bf, key));
}

bf_member_t rdbf_lookup(rdbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return BF_ABSENT;
    return rdbf_test(bf, rdbf_bit_index(bf, key)) ? BF_PRESENT : BF_ABSENT;
}

unsigned long rdbf_dim_x(const rdbf_t *bf)
{
    return bf ? bf->p1 : 0;
}

unsigned long rdbf_dim_y(const rdbf_t *bf)
{
    return bf ? bf->p2 : 0;
}

size_t rdbf_memory_bytes(const rdbf_t *bf)
{
    return bf ? bf->nwords * sizeof(unsigned long) : 0;
}

size_t rdbf_cells(const rdbf_t *bf)
{
    return bf ? bf->nbits : 0;
}

/* one bit per key over p1*p2 cells: 1 - (1 - 1/(p1 p2))^n */
double rdbf_fpr_theory(const rdbf_t *bf, size_t n)
{
    if (!bf || bf->nbits == 0) return 0.0;
    return 1.0 - pow(1.0 - 1.0 / (double)bf->nbits, (double)n);
}

double rdbf_fill_ratio(const rdbf_t *bf)
{
    if (!bf || bf->nbits == 0) return 0.0;
    size_t ones = 0;
    for (size_t i = 0; i < bf->nwords; i++) ones += (size_t)__builtin_popcountl(bf->words[i]);
    return (double)ones / (double)bf->nbits;
}
