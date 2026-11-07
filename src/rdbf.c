#include "rdbf.h"
#include "murmurhash2.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>

/*
 * r-Dimensional Bloom Filter, 2D variant, following robustBF (Patgiri, 2021),
 * Equation (1):
 *
 *     h = H(K);  i = h % X;  j = h % Y;  rho = h % beta;
 *     B[i][j] <- B[i][j] OR (1 << rho)
 *
 * B is an X x Y matrix of unsigned long cells (64 bits each); beta = 61, the
 * largest prime that fits a 64-bit cell, so 3 bits per cell are never used.
 * One MurmurHash2 call per operation; each key sets exactly one bit.
 *
 * X, Y and beta must be pairwise distinct primes. Then, by the Chinese
 * Remainder Theorem, (h % X, h % Y, h % beta) is uniform over all X*Y*beta
 * combinations (equivalent to h % (X*Y*beta)). If X == Y, i == j for every
 * key and only the diagonal would ever be used.
 *
 * Limit: h has 32 bits, so the address space X*Y*beta should stay well below
 * 2^32 (~4.3e9 bits); close to it, some bits become slightly more likely.
 *
 * Deletion is intentionally not supported. robustBF deletes with XOR, but
 * without per-cell counters, clearing a bit shared by two keys introduces
 * false negatives.
 */

#define RDBF_CELL_BITS ((unsigned)(sizeof(unsigned long) * CHAR_BIT))
#define RDBF_BETA 61u
#define RDBF_SEED 0x9E3779B1u

struct rdbf {
    unsigned long x;      /* prime, rows */
    unsigned long y;      /* prime, columns, y != x */
    size_t ncells;        /* x * y unsigned long cells */
    unsigned long *cells;
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

/* smallest prime >= n that is not in {avoid1, avoid2} */
static unsigned long next_prime_except(unsigned long n, unsigned long avoid1, unsigned long avoid2)
{
    if (n < 2) n = 2;
    while (!is_prime(n) || n == avoid1 || n == avoid2) n++;
    return n;
}

/* m = usable bits requested; each cell provides RDBF_BETA usable bits */
rdbf_t *rdbf_create(size_t m)
{
    if (m == 0) return NULL;
    rdbf_t *bf = (rdbf_t *)malloc(sizeof(rdbf_t));
    if (!bf) return NULL;

    /*
     * Cells needed, then the pair of distinct primes with the smallest product
     * X * Y >= want: scan X over primes up to ~sqrt(want) (Y >= X), Y the
     * smallest admissible prime for that X. Keeps rounding overhead small so
     * the "same m" comparison is fair.
     */
    size_t want = (m + RDBF_BETA - 1) / RDBF_BETA;
    unsigned long side = (unsigned long)ceil(sqrt((double)want));
    unsigned long best_x = 0, best_y = 0;
    size_t best = 0;
    for (unsigned long x = next_prime_except(side / 2 + 1, RDBF_BETA, 0); x <= side + 1;
         x = next_prime_except(x + 1, RDBF_BETA, 0)) {
        unsigned long y = next_prime_except((unsigned long)((want + x - 1) / x), RDBF_BETA, x);
        size_t prod = (size_t)x * (size_t)y;
        if (best == 0 || prod < best) { best = prod; best_x = x; best_y = y; }
    }
    bf->x = best_x;
    bf->y = best_y;

    bf->ncells = (size_t)bf->x * (size_t)bf->y;
    bf->cells = (unsigned long *)calloc(bf->ncells, sizeof(unsigned long));
    if (!bf->cells) {
        free(bf);
        return NULL;
    }
    return bf;
}

void rdbf_destroy(rdbf_t *bf)
{
    if (!bf) return;
    free(bf->cells);
    free(bf);
}

/* Equation (1): cell (i, j) and bit rho, all from the same hash value */
static inline unsigned long *rdbf_cell(const rdbf_t *bf, const bf_key_t *key, unsigned *rho)
{
    uint32_t h = murmurhash2(key->data, key->len, RDBF_SEED);
    unsigned long i = (unsigned long)h % bf->x;
    unsigned long j = (unsigned long)h % bf->y;
    *rho = (unsigned)(h % RDBF_BETA);
    return &bf->cells[(size_t)i * (size_t)bf->y + (size_t)j];
}

void rdbf_insert(rdbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return;
    unsigned rho;
    unsigned long *c = rdbf_cell(bf, key, &rho);
    *c |= 1UL << rho;
}

bf_member_t rdbf_lookup(rdbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return BF_ABSENT;
    unsigned rho;
    const unsigned long *c = rdbf_cell(bf, key, &rho);
    return ((*c >> rho) & 1UL) ? BF_PRESENT : BF_ABSENT;
}

unsigned long rdbf_dim_x(const rdbf_t *bf)
{
    return bf ? bf->x : 0;
}

unsigned long rdbf_dim_y(const rdbf_t *bf)
{
    return bf ? bf->y : 0;
}

unsigned rdbf_beta(void)
{
    return RDBF_BETA;
}

/* allocated bytes: X*Y cells of 64 bits (3 of them unused per cell) */
size_t rdbf_memory_bytes(const rdbf_t *bf)
{
    return bf ? bf->ncells * sizeof(unsigned long) : 0;
}

/* usable bits: X * Y * beta */
size_t rdbf_cells(const rdbf_t *bf)
{
    return bf ? bf->ncells * RDBF_BETA : 0;
}

/* one bit per key over X*Y*beta usable bits: 1 - (1 - 1/(X Y beta))^n */
double rdbf_fpr_theory(const rdbf_t *bf, size_t n)
{
    if (!bf || bf->ncells == 0) return 0.0;
    return -expm1((double)n * log1p(-1.0 / (double)rdbf_cells(bf)));
}

double rdbf_fill_ratio(const rdbf_t *bf)
{
    if (!bf || bf->ncells == 0) return 0.0;
    size_t ones = 0;
    for (size_t i = 0; i < bf->ncells; i++) ones += (size_t)__builtin_popcountl(bf->cells[i]);
    return (double)ones / (double)rdbf_cells(bf);
}

/*
 * Sizing for a target FPR p. k is fixed at 1 (one bit per key), so invert
 * 1 - (1 - 1/m)^n = p  ->  m = 1 / (1 - (1 - p)^{1/n})  (~ n/p usable bits).
 * rdbf_create then picks primes X != Y with X*Y*beta >= m.
 */
int rdbf_params(size_t n, double p, bf_sizing_t *out)
{
    if (!out || n == 0 || !(p > 0.0 && p < 1.0)) return 0;
    out->m = (size_t)ceil(1.0 / -expm1(log1p(-p) / (double)n));
    out->k = 1;
    return 1;
}

/* ---- collision counting (untimed pass; kept apart from the measured insert) ---- */

unsigned rdbf_insert_counted(rdbf_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return 0;
    unsigned rho;
    unsigned long *c = rdbf_cell(bf, key, &rho);
    unsigned col = (unsigned)((*c >> rho) & 1UL);
    *c |= 1UL << rho;
    return col;
}
