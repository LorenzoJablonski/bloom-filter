#define _POSIX_C_SOURCE 200809L
#include "benchmark.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "baseline.h"
#include "murmurhash2.h"

/*
 * Benchmark runner.
 *
 * Output is "long format": one CSV row per (filter, repetition), so the
 * analysis scripts can compute median / dispersion / confidence intervals.
 * Repetition 0 is a warm-up (page faults, caches, CPU frequency) and is not
 * written. Repetitions are interleaved across filters (rep -> filter) so that
 * slow drifts of the machine affect every filter equally.
 *
 * Hash-call counts come from a separate, untimed pass with the MurmurHash2
 * counter enabled; timed passes run with the counter disabled.
 */

typedef struct {
    char *arena;      /* whole file contents; keys point into it */
    bf_key_t *keys;
    size_t count;
} keyset_t;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int keyset_load(const char *path, keyset_t *ks)
{
    memset(ks, 0, sizeof(*ks));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return 0; }
    rewind(f);

    ks->arena = (char *)malloc((size_t)size + 1);
    if (!ks->arena) { fclose(f); return 0; }
    size_t got = fread(ks->arena, 1, (size_t)size, f);
    fclose(f);
    ks->arena[got] = '\n';

    size_t lines = 0;
    for (size_t i = 0; i <= got; i++) lines += ks->arena[i] == '\n';
    ks->keys = (bf_key_t *)malloc((lines + 1) * sizeof(bf_key_t));
    if (!ks->keys) { free(ks->arena); return 0; }

    char *p = ks->arena, *end = ks->arena + got;
    while (p < end) {
        char *nl = memchr(p, '\n', (size_t)(end - p) + 1);
        size_t len = (size_t)(nl - p);
        if (len > 0 && p[len - 1] == '\r') len--;
        if (len > 0) {
            ks->keys[ks->count].data = p;
            ks->keys[ks->count].len = len;
            ks->count++;
        }
        p = nl + 1;
    }
    return 1;
}

static void keyset_free(keyset_t *ks)
{
    free(ks->keys);
    free(ks->arena);
    memset(ks, 0, sizeof(*ks));
}

/* Keep only keys that are members of the first n inserted keys. */
static void keyset_restrict(keyset_t *ks, const bf_key_t *inserted, size_t n)
{
    baseline_t *bl = baseline_create();
    if (!bl) return;
    for (size_t i = 0; i < n; i++) baseline_insert(bl, &inserted[i]);
    size_t w = 0;
    for (size_t i = 0; i < ks->count; i++) {
        if (baseline_lookup(bl, &ks->keys[i]) == BF_PRESENT) ks->keys[w++] = ks->keys[i];
    }
    ks->count = w;
    baseline_destroy(bl);
}

typedef struct {
    double insert, exist, inexist; /* average MurmurHash2 calls per operation */
} hash_avg_t;

static hash_avg_t count_hashes(const bf_ops_t *ops, size_t m, unsigned k,
                               const keyset_t *ins, size_t n,
                               const keyset_t *ex, const keyset_t *inex)
{
    hash_avg_t r = { 0.0, 0.0, 0.0 };
    void *bf = ops->create(m, k);
    if (!bf) return r;
    uint64_t c = 0;
    murmurhash2_counter = &c;

    for (size_t j = 0; j < n; j++) ops->insert(bf, &ins->keys[j]);
    r.insert = n ? (double)c / (double)n : 0.0;

    c = 0;
    for (size_t j = 0; j < ex->count; j++) ops->lookup(bf, &ex->keys[j]);
    r.exist = ex->count ? (double)c / (double)ex->count : 0.0;

    c = 0;
    for (size_t j = 0; j < inex->count; j++) ops->lookup(bf, &inex->keys[j]);
    r.inexist = inex->count ? (double)c / (double)inex->count : 0.0;

    murmurhash2_counter = NULL;
    ops->destroy(bf);
    return r;
}

int benchmark_run(const char *type, size_t n_override, double p_target,
                  unsigned reps, const char *out_path)
{
    char path[512];
    keyset_t ins, ex, inex;

    snprintf(path, sizeof(path), "data/%s/insert.txt", type);
    if (!keyset_load(path, &ins)) { fprintf(stderr, "benchmark: cannot open %s\n", path); return 1; }
    snprintf(path, sizeof(path), "data/%s/exist.txt", type);
    if (!keyset_load(path, &ex)) { fprintf(stderr, "benchmark: cannot open %s\n", path); keyset_free(&ins); return 1; }
    snprintf(path, sizeof(path), "data/%s/inexist.txt", type);
    if (!keyset_load(path, &inex)) { fprintf(stderr, "benchmark: cannot open %s\n", path); keyset_free(&ins); keyset_free(&ex); return 1; }

    size_t n = ins.count;
    if (n_override > 0 && n_override < n) {
        n = n_override;
        /* Dexist must be a subset of what was actually inserted (protocol 4.3.2) */
        keyset_restrict(&ex, ins.keys, n);
        fprintf(stderr, "benchmark: n truncated to %zu, Dexist restricted to %zu keys\n", n, ex.count);
    }
    if (n == 0) { fprintf(stderr, "benchmark: empty insert set\n"); return 1; }
    if (reps == 0) reps = 1;

    /* optimal parameters: m = -n ln p / (ln 2)^2, k = (m/n) ln 2 */
    size_t m = (size_t)ceil(-(double)n * log(p_target) / (log(2.0) * log(2.0)));
    if (m < 64) m = 64;
    unsigned k = (unsigned)((double)m / (double)n * log(2.0) + 0.5);
    if (k < 1) k = 1;

    FILE *out = stdout;
    if (out_path && out_path[0]) {
        out = fopen(out_path, "w");
        if (!out) { fprintf(stderr, "benchmark: cannot open %s\n", out_path); return 1; }
    }

    size_t nops = 0;
    const bf_ops_t *ops = bf_ops_all(&nops);

    hash_avg_t *hashes = (hash_avg_t *)calloc(nops, sizeof(hash_avg_t));
    if (!hashes) return 1;
    for (size_t i = 0; i < nops; i++) {
        if (!ops[i].exact) hashes[i] = count_hashes(&ops[i], m, k, &ins, n, &ex, &inex);
    }

    fprintf(out, "type,filter,rep,n,n_exist,n_inexist,p_target,m,cells,k,mem_bytes,bits_per_key,"
                 "insert_ms,lookup_exist_ms,lookup_inexist_ms,"
                 "hash_insert,hash_lookup_exist,hash_lookup_inexist,"
                 "fn,fp,fpr,fpr_theory,fill_ratio\n");

    for (unsigned rep = 0; rep <= reps; rep++) {   /* rep 0 = warm-up */
        for (size_t i = 0; i < nops; i++) {
            void *bf = ops[i].create(m, k);
            if (!bf) { fprintf(stderr, "benchmark: %s create failed\n", ops[i].name); continue; }

            double t0 = now_ms();
            for (size_t j = 0; j < n; j++) ops[i].insert(bf, &ins.keys[j]);
            double t_ins = now_ms() - t0;

            size_t fn = 0;
            t0 = now_ms();
            for (size_t j = 0; j < ex.count; j++) fn += ops[i].lookup(bf, &ex.keys[j]) != BF_PRESENT;
            double t_ex = now_ms() - t0;

            size_t fp = 0;
            t0 = now_ms();
            for (size_t j = 0; j < inex.count; j++) fp += ops[i].lookup(bf, &inex.keys[j]) == BF_PRESENT;
            double t_inex = now_ms() - t0;

            if (rep > 0) {
                size_t mem = ops[i].memory_bytes(bf);
                double fpr = inex.count ? (double)fp / (double)inex.count : 0.0;
                fprintf(out, "%s,%s,%u,%zu,%zu,%zu,%g,%zu,", type, ops[i].name, rep,
                        n, ex.count, inex.count, p_target, m);
                if (ops[i].cells) fprintf(out, "%zu,%u,", ops[i].cells(bf), k);
                else fprintf(out, ",,");
                fprintf(out, "%zu,%.4f,%.4f,%.4f,%.4f,", mem, 8.0 * (double)mem / (double)n,
                        t_ins, t_ex, t_inex);
                if (ops[i].exact) fprintf(out, ",,,");
                else fprintf(out, "%.4f,%.4f,%.4f,", hashes[i].insert, hashes[i].exist, hashes[i].inexist);
                fprintf(out, "%zu,%zu,%.6f,", fn, fp, fpr);
                if (ops[i].fpr_theory) fprintf(out, "%.6f,%.6f\n", ops[i].fpr_theory(bf, n), ops[i].fill_ratio(bf));
                else fprintf(out, ",\n");
            }
            ops[i].destroy(bf);
        }
    }

    if (out != stdout) fclose(out);
    free(hashes);
    keyset_free(&ins);
    keyset_free(&ex);
    keyset_free(&inex);
    return 0;
}
