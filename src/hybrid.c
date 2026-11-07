#define _POSIX_C_SOURCE 200112L /* posix_memalign */
#include "hybrid.h"
#include "murmurhash2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Hybrid Bloom filter.
 *
 *   key --MurmurHash64A (1 call)--> h (64 bits)
 *        shard = high 32 bits of h, mapped to [0, S)      (key partitioning)
 *        h1    = low 32 bits of h
 *        h2    = multiplicative remix of h (no extra hash call)
 *        g_i   = (h1 + i*h2) mod C,  i = 0..k-1          (double hashing)
 *        cell  = (x, y) = (g_i mod p1, g_i div p1) in the shard's p1 x p2
 *                prime matrix                             (rDBF)
 *
 * Row-major storage makes the linear cell index y*p1 + x equal to g_i, so the
 * matrix coordinates never have to be materialised.
 *
 * Every key touches exactly one shard, so a per-shard lock (or ownership) is
 * enough for concurrent writers. Each shard starts on its own 64-byte cache
 * line; padding (< 64 bytes per shard) is included in the reported memory.
 *
 * counting = 1 replaces bits with 4-bit saturating counters (CBF semantics:
 * a saturated counter is never decremented, so deletes never cause false
 * negatives).
 */

#define HYBRID_ALIGN 64u
#define HYBRID_SEED 0x2545F4914F6CDD1DULL
#define HYBRID_COUNTER_MAX 15u

struct hybrid {
    size_t shards;        /* S */
    unsigned long p1, p2; /* shard matrix dimensions (primes) */
    size_t cells;         /* C = p1 * p2, cells per shard */
    unsigned k;
    int counting;
    size_t stride;        /* bytes per shard, multiple of HYBRID_ALIGN */
    unsigned char *mem;
};

/* ---- shard geometry ---- */

static int is_prime(unsigned long n)
{
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (unsigned long d = 3; d <= n / d; d += 2) {
        if (n % d == 0) return 0;
    }
    return 1;
}

static unsigned long prev_prime(unsigned long n)
{
    while (n >= 2 && !is_prime(n)) n--;
    return n;
}

/* primes p1 <= p2 with the largest product <= limit (ties: squarer matrix) */
static int shard_dims(size_t limit, unsigned long *p1, unsigned long *p2)
{
    unsigned long best1 = 0, best2 = 0;
    for (unsigned long a = 2; a * a <= limit; a++) {
        if (!is_prime(a)) continue;
        unsigned long b = prev_prime((unsigned long)(limit / a));
        if (b < a) continue;
        if (a * b >= best1 * best2) { best1 = a; best2 = b; }
    }
    if (best1 == 0) return 0;
    *p1 = best1;
    *p2 = best2;
    return 1;
}

static size_t shard_stride(size_t cells, int counting)
{
    size_t bytes = counting ? (cells + 1) / 2 : (cells + 7) / 8;
    return (bytes + HYBRID_ALIGN - 1) / HYBRID_ALIGN * HYBRID_ALIGN;
}

hybrid_t *hybrid_create(size_t m, unsigned k, size_t shard_max, int counting)
{
    if (k == 0 || m == 0) return NULL;
    hybrid_t *bf = (hybrid_t *)malloc(sizeof(hybrid_t));
    if (!bf) return NULL;
    if (!shard_dims(shard_max, &bf->p1, &bf->p2)) { free(bf); return NULL; }
    bf->cells = (size_t)bf->p1 * (size_t)bf->p2;
    bf->shards = (m + bf->cells - 1) / bf->cells;
    if (bf->shards > UINT32_MAX) { free(bf); return NULL; }
    bf->k = k;
    bf->counting = counting ? 1 : 0;
    bf->stride = shard_stride(bf->cells, bf->counting);

    void *mem = NULL;
    if (posix_memalign(&mem, HYBRID_ALIGN, bf->shards * bf->stride) != 0) {
        free(bf);
        return NULL;
    }
    memset(mem, 0, bf->shards * bf->stride);
    bf->mem = (unsigned char *)mem;
    return bf;
}

void hybrid_destroy(hybrid_t *bf)
{
    if (!bf) return;
    free(bf->mem);
    free(bf);
}

/* ---- addressing ---- */

typedef struct {
    unsigned char *shard;
    size_t g;     /* current position in [0, C) */
    size_t step;  /* h2 mod C, never 0 */
} probe_t;

static inline probe_t hybrid_probe(const hybrid_t *bf, const bf_key_t *key)
{
    uint64_t h = murmurhash64a(key->data, key->len, HYBRID_SEED);
    uint32_t hi = (uint32_t)(h >> 32);
    uint32_t h1 = (uint32_t)h;
    uint32_t h2 = (uint32_t)((h * 0x9E3779B97F4A7C15ULL) >> 32);

    probe_t p;
    size_t shard = (size_t)(((uint64_t)hi * (uint64_t)bf->shards) >> 32);
    p.shard = bf->mem + shard * bf->stride;
    p.g = (size_t)(h1 % bf->cells);
    p.step = (size_t)(h2 % bf->cells);
    if (p.step == 0) p.step = 1;
    return p;
}

static inline void probe_next(const hybrid_t *bf, probe_t *p)
{
    p->g += p->step;
    if (p->g >= bf->cells) p->g -= bf->cells;
}

static inline unsigned cnt_get(const unsigned char *s, size_t i)
{
    unsigned char b = s[i / 2];
    return (i % 2 == 0) ? (unsigned)(b & 0x0Fu) : (unsigned)(b >> 4);
}

static inline void cnt_set(unsigned char *s, size_t i, unsigned v)
{
    unsigned char *b = &s[i / 2];
    if (i % 2 == 0) *b = (unsigned char)((*b & 0xF0u) | (v & 0x0Fu));
    else *b = (unsigned char)((*b & 0x0Fu) | ((v & 0x0Fu) << 4));
}

/* ---- operations ---- */

void hybrid_insert(hybrid_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return;
    probe_t p = hybrid_probe(bf, key);
    for (unsigned i = 0; i < bf->k; i++, probe_next(bf, &p)) {
        if (bf->counting) {
            unsigned v = cnt_get(p.shard, p.g);
            if (v < HYBRID_COUNTER_MAX) cnt_set(p.shard, p.g, v + 1);
        } else {
            p.shard[p.g / 8] |= (unsigned char)(1u << (p.g % 8));
        }
    }
}

bf_member_t hybrid_lookup(hybrid_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return BF_ABSENT;
    probe_t p = hybrid_probe(bf, key);
    for (unsigned i = 0; i < bf->k; i++, probe_next(bf, &p)) {
        int set = bf->counting ? cnt_get(p.shard, p.g) != 0
                               : (p.shard[p.g / 8] >> (p.g % 8)) & 1u;
        if (!set) return BF_ABSENT;
    }
    return BF_PRESENT;
}

int hybrid_delete(hybrid_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data || !bf->counting) return 0;
    probe_t p = hybrid_probe(bf, key);
    for (unsigned i = 0; i < bf->k; i++, probe_next(bf, &p)) {
        unsigned v = cnt_get(p.shard, p.g);
        /* saturated counters stay locked: decrementing could drop a real entry */
        if (v > 0 && v < HYBRID_COUNTER_MAX) cnt_set(p.shard, p.g, v - 1);
    }
    return 1;
}

/* ---- metrics ---- */

size_t hybrid_memory_bytes(const hybrid_t *bf)
{
    return bf ? bf->shards * bf->stride : 0;
}

size_t hybrid_cells(const hybrid_t *bf)
{
    return bf ? bf->shards * bf->cells : 0;
}

size_t hybrid_num_shards(const hybrid_t *bf)
{
    return bf ? bf->shards : 0;
}

size_t hybrid_shard_stride(const hybrid_t *bf)
{
    return bf ? bf->stride : 0;
}

void hybrid_shard_dims(const hybrid_t *bf, unsigned long *p1, unsigned long *p2)
{
    if (p1) *p1 = bf ? bf->p1 : 0;
    if (p2) *p2 = bf ? bf->p2 : 0;
}

/*
 * Blocked Bloom filter FPR (Putze, Sanders & Singler, 2007): the number of
 * keys j in a shard is ~ Poisson(lambda = n/S); a shard holding j keys has
 * FPR (1 - (1 - 1/C)^{jk})^k. Average over j.
 */
static double hybrid_fpr_formula(size_t shards, size_t cells, unsigned k, size_t n)
{
    double lambda = (double)n / (double)shards;
    double span = 12.0 * sqrt(lambda) + 12.0;
    double lo = floor(lambda - span), hi = ceil(lambda + span);
    if (lo < 0.0) lo = 0.0;
    double log_keep = log1p(-1.0 / (double)cells);   /* ln(1 - 1/C) */
    double fpr = 0.0;
    for (double j = lo; j <= hi; j += 1.0) {
        double pmf = (lambda > 0.0)
            ? exp(j * log(lambda) - lambda - lgamma(j + 1.0))
            : (j == 0.0 ? 1.0 : 0.0);
        double one = -expm1(j * (double)k * log_keep); /* 1 - (1-1/C)^{jk} */
        fpr += pmf * pow(one, (double)k);
    }
    return fpr;
}

double hybrid_fpr_theory(const hybrid_t *bf, size_t n)
{
    if (!bf) return 0.0;
    return hybrid_fpr_formula(bf->shards, bf->cells, bf->k, n);
}

double hybrid_fill_ratio(const hybrid_t *bf)
{
    if (!bf) return 0.0;
    size_t nz = 0;
    for (size_t s = 0; s < bf->shards; s++) {
        const unsigned char *sh = bf->mem + s * bf->stride;
        if (bf->counting) {
            for (size_t i = 0; i < bf->cells; i++) nz += cnt_get(sh, i) != 0;
        } else {
            for (size_t i = 0; i < bf->stride; i++) nz += (size_t)__builtin_popcount(sh[i]);
        }
    }
    return (double)nz / (double)(bf->shards * bf->cells);
}

size_t hybrid_saturated(const hybrid_t *bf)
{
    if (!bf || !bf->counting) return 0;
    size_t sat = 0;
    for (size_t s = 0; s < bf->shards; s++) {
        const unsigned char *sh = bf->mem + s * bf->stride;
        for (size_t i = 0; i < bf->cells; i++) sat += cnt_get(sh, i) == HYBRID_COUNTER_MAX;
    }
    return sat;
}

/*
 * Sizing: cells per shard C is fixed by shard_max. For each k, find the
 * smallest shard count S with FPR(S) <= p (FPR decreases with S: exponential
 * then binary search) and keep the k minimising m = S * C.
 */
int hybrid_params(size_t n, double p, size_t shard_max, bf_sizing_t *out)
{
    unsigned long p1, p2;
    if (!out || n == 0 || !(p > 0.0 && p < 1.0) || !shard_dims(shard_max, &p1, &p2)) return 0;
    size_t cells = (size_t)p1 * (size_t)p2;

    size_t best_m = 0;
    unsigned best_k = 0;
    for (unsigned k = 1; k <= BF_MAX_K; k++) {
        size_t hi = 1;
        int found = 0;
        for (int it = 0; it < 40; it++, hi *= 2) {
            if (hybrid_fpr_formula(hi, cells, k, n) <= p) { found = 1; break; }
        }
        if (!found) continue;
        size_t lo = hi / 2;            /* FPR(lo) > p, or lo == 0 */
        while (hi - lo > 1) {
            size_t mid = lo + (hi - lo) / 2;
            if (hybrid_fpr_formula(mid, cells, k, n) <= p) hi = mid;
            else lo = mid;
        }
        size_t m = hi * cells;
        if (best_k == 0 || m < best_m) { best_m = m; best_k = k; }
    }
    if (best_k == 0) return 0;
    out->m = best_m;
    out->k = best_k;
    return 1;
}

/* ---- collision counting (untimed pass; kept apart from the measured insert) ---- */

unsigned hybrid_insert_counted(hybrid_t *bf, const bf_key_t *key)
{
    if (!bf || !key || !key->data) return 0;
    unsigned col = 0;
    probe_t p = hybrid_probe(bf, key);
    for (unsigned i = 0; i < bf->k; i++, probe_next(bf, &p)) {
        if (bf->counting) {
            unsigned v = cnt_get(p.shard, p.g);
            col += v > 0;
            if (v < HYBRID_COUNTER_MAX) cnt_set(p.shard, p.g, v + 1);
        } else {
            unsigned char bit = (unsigned char)(1u << (p.g % 8));
            col += (p.shard[p.g / 8] & bit) != 0;
            p.shard[p.g / 8] |= bit;
        }
    }
    return col;
}
