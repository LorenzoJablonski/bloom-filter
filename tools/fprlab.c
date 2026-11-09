/*
 * fprlab: why does the hybrid's measured FPR sit above its formula?
 *
 * Rebuilds the hybrid filter outside the library and changes one piece at a
 * time, with the same shards S, cells per shard C and k:
 *   A  as implemented (must match the library's false positives exactly)
 *   B  k independent positions per key (k extra hash calls) instead of double hashing
 *   C  double hashing with the step from an independent hash call
 *   D  double hashing with a prime C (same total bits)
 *   E  enhanced double hashing (Kirsch & Mitzenmacher): g_i = h1 + i*h2 + (i^3 - i)/6
 * and, for each, three estimates that separate the causes:
 *   poisson  the library's formula (shard loads ~ Poisson)
 *   loads    the same formula with the real load of each queried shard
 *   fill     mean of f^k over the queried shards, f = real fill of the shard
 *            (exact if a query's k positions behaved as independent draws)
 *
 *   make all && cc -std=c99 -O2 -Iinclude tools/fprlab.c build/hybrid.o \
 *      build/murmurhash2.o build/keyset.o build/baseline.o -lm -o build/fprlab
 *   build/fprlab uuid 512 [DIR]  (also 4096; DIR/<type>/{insert,inexist}.txt, default data)
 */
#define _POSIX_C_SOURCE 200112L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hybrid.h"
#include "keyset.h"
#include "murmurhash2.h"

#define SEED 0x2545F4914F6CDD1DULL /* the library's HYBRID_SEED */

static int is_prime(unsigned long n)
{
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (unsigned long d = 3; d <= n / d; d += 2)
        if (n % d == 0) return 0;
    return 1;
}

/* the library's shard geometry: primes p1 <= p2 with the largest product <= limit */
static size_t shard_cells(size_t limit)
{
    unsigned long best = 0;
    for (unsigned long a = 2; a * a <= limit; a++) {
        if (!is_prime(a)) continue;
        unsigned long b = limit / a;
        while (b >= 2 && !is_prime(b)) b--;
        if (b >= a && a * b > best) best = a * b;
    }
    return best;
}

static double formula_j(double j, size_t C, unsigned k)
{
    return pow(-expm1(j * (double)k * log1p(-1.0 / (double)C)), (double)k);
}

static double formula_poisson(size_t S, size_t C, unsigned k, size_t n)
{
    double lambda = (double)n / (double)S, fpr = 0.0;
    double span = 12.0 * sqrt(lambda) + 12.0;
    double lo = floor(lambda - span), hi = ceil(lambda + span);
    if (lo < 0) lo = 0;
    for (double j = lo; j <= hi; j += 1.0)
        fpr += exp(j * log(lambda) - lambda - lgamma(j + 1.0)) * formula_j(j, C, k);
    return fpr;
}

typedef struct {
    size_t S, C;
    unsigned k;
    char mode;          /* 'A'..'E' */
    unsigned char *bits;
    uint32_t *load;     /* keys per shard */
} lab_t;

static size_t positions(const lab_t *f, const bf_key_t *key, size_t *g)
{
    uint64_t h = murmurhash64a(key->data, key->len, SEED);
    uint32_t hi = (uint32_t)(h >> 32), lo = (uint32_t)h;
    size_t shard = (size_t)(((uint64_t)hi * (uint64_t)f->S) >> 32);
    size_t C = f->C;
    size_t h1 = lo % C, h2;
    if (f->mode == 'C')
        h2 = (size_t)(murmurhash64a(key->data, key->len, SEED ^ 0x9E3779B97F4A7C15ULL) % C);
    else
        h2 = (size_t)((uint32_t)((h * 0x9E3779B97F4A7C15ULL) >> 32) % C);
    if (h2 == 0) h2 = 1;
    for (unsigned i = 0; i < f->k; i++) {
        if (f->mode == 'B') {
            g[i] = (size_t)(murmurhash64a(key->data, key->len, SEED + 1 + i) % C);
        } else if (f->mode == 'E') {
            unsigned long long t = (unsigned long long)i * i * i - i; /* i^3 - i, divisible by 6 */
            g[i] = (size_t)((h1 + (unsigned long long)i * h2 + t / 6) % C);
        } else {
            g[i] = (h1 + (size_t)i * h2) % C; /* A, C, D */
        }
    }
    return shard;
}

static int bit_get(const lab_t *f, size_t shard, size_t g)
{
    size_t b = shard * f->C + g;
    return (f->bits[b / 8] >> (b % 8)) & 1;
}

static void run(const char *label, lab_t *f, const keyset_t *ins, const keyset_t *inex,
                size_t lib_fp)
{
    size_t total = f->S * f->C;
    f->bits = (unsigned char *)calloc((total + 7) / 8, 1);
    f->load = (uint32_t *)calloc(f->S, sizeof(uint32_t));
    uint32_t *fill = (uint32_t *)calloc(f->S, sizeof(uint32_t));
    size_t g[64];
    for (size_t i = 0; i < ins->count; i++) {
        size_t s = positions(f, &ins->keys[i], g);
        f->load[s]++;
        for (unsigned j = 0; j < f->k; j++) {
            size_t b = s * f->C + g[j];
            f->bits[b / 8] |= (unsigned char)(1u << (b % 8));
        }
    }
    for (size_t s = 0; s < f->S; s++)
        for (size_t c = 0; c < f->C; c++) fill[s] += (uint32_t)bit_get(f, s, c);

    size_t fp = 0, distinct_short = 0;
    double est_loads = 0.0, est_fill = 0.0;
    for (size_t i = 0; i < inex->count; i++) {
        size_t s = positions(f, &inex->keys[i], g);
        int all = 1;
        for (unsigned j = 0; j < f->k && all; j++) all = bit_get(f, s, g[j]);
        fp += (size_t)all;
        est_loads += formula_j((double)f->load[s], f->C, f->k);
        est_fill += pow((double)fill[s] / (double)f->C, (double)f->k);
        /* how often a query's k positions are not all distinct */
        for (unsigned a = 1; a < f->k; a++) {
            int dup = 0;
            for (unsigned b = 0; b < a; b++) dup |= g[a] == g[b];
            if (dup) { distinct_short++; break; }
        }
    }
    double nq = (double)inex->count;
    double meas = fp / nq, pois = formula_poisson(f->S, f->C, f->k, ins->count);
    printf("%-30s C=%4zu S=%7zu k=%u  medida %.4f%%  poisson %.4f%%  cargas %.4f%%  ocupacao %.4f%%"
           "  medida/poisson %+.2f%%  consultas com posicoes repetidas %.3f%%",
           label, f->C, f->S, f->k, 100 * meas, 100 * pois, 100 * est_loads / nq,
           100 * est_fill / nq, 100 * (meas / pois - 1), 100 * distinct_short / nq);
    if (lib_fp != (size_t)-1) printf("  [biblioteca: %zu FP, aqui %zu: %s]", lib_fp, fp, lib_fp == fp ? "igual" : "DIFERENTE");
    printf("\n");
    free(f->bits);
    free(f->load);
    free(fill);
}

int main(int argc, char **argv)
{
    const char *type = argc > 1 ? argv[1] : "uuid";
    size_t shard_max = argc > 2 ? (size_t)atol(argv[2]) : 512;
    const char *dir = argc > 3 ? argv[3] : "data"; /* e.g. data/vm/n20000000/data */
    char path[512];
    keyset_t ins, inex;
    snprintf(path, sizeof(path), "%s/%s/insert.txt", dir, type);
    if (!keyset_load(path, &ins)) { fprintf(stderr, "cannot load %s\n", path); return 1; }
    snprintf(path, sizeof(path), "%s/%s/inexist.txt", dir, type);
    if (!keyset_load(path, &inex)) { fprintf(stderr, "cannot load %s\n", path); return 1; }

    bf_sizing_t sz;
    if (!hybrid_params(ins.count, 0.01, shard_max, &sz)) return 1;
    size_t C = shard_cells(shard_max);
    size_t S = (sz.m + C - 1) / C;

    /* the library's filter, for the sanity check of variant A */
    hybrid_t *lib = hybrid_create(sz.m, sz.k, shard_max, 0);
    for (size_t i = 0; i < ins.count; i++) hybrid_insert(lib, &ins.keys[i]);
    size_t lib_fp = 0;
    for (size_t i = 0; i < inex.count; i++) lib_fp += hybrid_lookup(lib, &inex.keys[i]) == BF_PRESENT;
    hybrid_destroy(lib);

    printf("%s, n=%zu, consultas negativas=%zu, bloco de %zu bits\n", type, ins.count, inex.count, shard_max);
    lab_t f = { S, C, sz.k, 'A', NULL, NULL };
    run("A atual (hash duplo)", &f, &ins, &inex, lib_fp);
    f.mode = 'B'; run("B k hashes independentes", &f, &ins, &inex, (size_t)-1);
    f.mode = 'C'; run("C passo de outro hash", &f, &ins, &inex, (size_t)-1);
    size_t Cp = C;
    while (!is_prime(Cp)) Cp--;
    lab_t fp = { (S * C + Cp - 1) / Cp, Cp, sz.k, 'D', NULL, NULL };
    run("D C primo, hash duplo", &fp, &ins, &inex, (size_t)-1);
    f.mode = 'E'; run("E hash duplo melhorado", &f, &ins, &inex, (size_t)-1);
    keyset_free(&ins);
    keyset_free(&inex);
    return 0;
}
