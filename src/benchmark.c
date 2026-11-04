#define _POSIX_C_SOURCE 200809L
#include "benchmark.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "keyset.h"
#include "murmurhash2.h"
#include "procstat.h"

/*
 * Benchmark runner.
 *
 * Output is "long format": one CSV row per (filter, repetition), so the
 * analysis scripts can compute median / dispersion / confidence intervals.
 * Repetition 0 is a warm-up (page faults, caches, CPU frequency) and is not
 * written.
 *
 * Experiments run one structure per process (--filter, driven by run.py): if a
 * big structure shared a process with others, what it pushed to swap would be
 * paid by whoever ran next. Keys are streamed in batches (keyset.h) and only
 * the operations are timed, so the file reads do not count and the dataset
 * does not compete with the structure for RAM.
 *
 * Hash-call counts come from a separate, untimed pass with the MurmurHash2
 * counter enabled; timed passes run with the counter disabled.
 *
 * rss_kb is the process resident set right after the inserts (dataset
 * included); majflt/minflt are the page faults of create + insert + lookups.
 * Major faults there mean the structure did not fit in RAM and was paged.
 */

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

typedef struct {
    double insert, exist, inexist; /* average MurmurHash2 calls per operation */
} hash_avg_t;

/* Insert one pass of the stream; only the operations are timed, not the file reads. */
static double timed_insert(const bf_ops_t *op, void *bf, keystream_t *ks)
{
    double total = 0.0;
    size_t c;
    keystream_rewind(ks);
    while ((c = keystream_next(ks)) > 0) {
        double t0 = now_ms();
        for (size_t j = 0; j < c; j++) op->insert(bf, &ks->keys[j]);
        total += now_ms() - t0;
    }
    return total;
}

static double timed_lookup(const bf_ops_t *op, void *bf, keystream_t *ks, size_t *present)
{
    double total = 0.0;
    size_t c, hits = 0;
    keystream_rewind(ks);
    while ((c = keystream_next(ks)) > 0) {
        double t0 = now_ms();
        for (size_t j = 0; j < c; j++) hits += op->lookup(bf, &ks->keys[j]) == BF_PRESENT;
        total += now_ms() - t0;
    }
    *present = hits;
    return total;
}

static hash_avg_t count_hashes(const bf_ops_t *ops, size_t m, unsigned k, dataset_t *ds)
{
    hash_avg_t r = { 0.0, 0.0, 0.0 };
    void *bf = ops->create(m, k);
    if (!bf) return r;
    uint64_t c = 0;
    murmurhash2_counter = &c;

    size_t present;
    (void)timed_insert(ops, bf, &ds->ins);
    r.insert = ds->n ? (double)c / (double)ds->n : 0.0;

    c = 0;
    (void)timed_lookup(ops, bf, &ds->ex, &present);
    r.exist = ds->n_exist ? (double)c / (double)ds->n_exist : 0.0;

    c = 0;
    (void)timed_lookup(ops, bf, &ds->inex, &present);
    r.inexist = ds->n_inexist ? (double)c / (double)ds->n_inexist : 0.0;

    murmurhash2_counter = NULL;
    ops->destroy(bf);
    return r;
}

typedef struct {
    const bf_ops_t *ops;
    const char *sizing; /* "same_m", "same_fpr" or "none" */
    size_t m;
    unsigned k;
    hash_avg_t hashes;
} run_t;

int benchmark_run(const char *type, size_t n_override, double p_target,
                  unsigned reps, unsigned sizing, const char *filter, const char *out_path)
{
    if (filter && !bf_ops_by_name(filter)) {
        fprintf(stderr, "benchmark: unknown filter %s\n", filter);
        return 2;
    }
    dataset_t ds;
    if (!dataset_open(type, n_override, "benchmark", &ds)) return 1;
    size_t n = ds.n;
    if (reps == 0) reps = 1;

    FILE *out = stdout;
    if (out_path && out_path[0]) {
        out = fopen(out_path, "w");
        if (!out) { fprintf(stderr, "benchmark: cannot open %s\n", out_path); return 1; }
    }

    size_t nops = 0;
    const bf_ops_t *ops = bf_ops_all(&nops);

    /*
     * Build the run plan. Two sizing regimes:
     *   same_m   every filter receives the (m, k) the SBF needs to reach p
     *            (same number of cells; CBF still pays 4 bits per cell)
     *   same_fpr every filter is sized by its own <model>_params(n, p)
     * The exact baseline has no parameters and runs once (sizing "none").
     */
    bf_sizing_t ref;
    const bf_ops_t *sbf_ops = bf_ops_by_name("sbf");
    if (!sbf_ops || !sbf_ops->params(n, p_target, &ref)) {
        fprintf(stderr, "benchmark: invalid p=%g\n", p_target);
        return 1;
    }

    size_t nruns = 0;
    run_t *runs = (run_t *)calloc(2 * nops, sizeof(run_t));
    if (!runs) return 1;
    for (size_t i = 0; i < nops; i++) {
        if (filter && strcmp(ops[i].name, filter) != 0) continue;
        if (ops[i].exact) {
            runs[nruns++] = (run_t){ &ops[i], "none", 0, 0, { 0, 0, 0 } };
            continue;
        }
        if (sizing & SIZING_SAME_M)
            runs[nruns++] = (run_t){ &ops[i], "same_m", ref.m, ref.k, { 0, 0, 0 } };
        if (sizing & SIZING_SAME_FPR) {
            bf_sizing_t own;
            if (ops[i].params(n, p_target, &own))
                runs[nruns++] = (run_t){ &ops[i], "same_fpr", own.m, own.k, { 0, 0, 0 } };
        }
    }
    for (size_t r = 0; r < nruns; r++) {
        if (!runs[r].ops->exact)
            runs[r].hashes = count_hashes(runs[r].ops, runs[r].m, runs[r].k, &ds);
    }

    fprintf(out, "type,filter,sizing,rep,n,n_exist,n_inexist,p_target,m,cells,k,mem_bytes,bits_per_key,"
                 "insert_ms,lookup_exist_ms,lookup_inexist_ms,"
                 "hash_insert,hash_lookup_exist,hash_lookup_inexist,"
                 "fn,fp,fpr,fpr_theory,fill_ratio,rss_kb,majflt,minflt\n");

    for (unsigned rep = 0; rep <= reps; rep++) {   /* rep 0 = warm-up */
        for (size_t r = 0; r < nruns; r++) {
            const bf_ops_t *op = runs[r].ops;
            proc_faults_t f0 = proc_faults();
            void *bf = op->create(runs[r].m, runs[r].k);
            if (!bf) { fprintf(stderr, "benchmark: %s create failed\n", op->name); continue; }

            double t_ins = timed_insert(op, bf, &ds.ins);
            long rss = proc_rss_kb();

            size_t present;
            double t_ex = timed_lookup(op, bf, &ds.ex, &present);
            size_t fn = ds.n_exist - present;

            size_t fp;
            double t_inex = timed_lookup(op, bf, &ds.inex, &fp);
            proc_faults_t f1 = proc_faults();

            if (rep > 0) {
                size_t mem = op->memory_bytes(bf);
                double fpr = ds.n_inexist ? (double)fp / (double)ds.n_inexist : 0.0;
                fprintf(out, "%s,%s,%s,%u,%zu,%zu,%zu,%g,", type, op->name, runs[r].sizing, rep,
                        n, ds.n_exist, ds.n_inexist, p_target);
                if (op->exact) fprintf(out, ",,,");
                else fprintf(out, "%zu,%zu,%u,", runs[r].m, op->cells(bf),
                             op->fixed_k ? op->fixed_k : runs[r].k);
                fprintf(out, "%zu,%.4f,%.4f,%.4f,%.4f,", mem, 8.0 * (double)mem / (double)n,
                        t_ins, t_ex, t_inex);
                if (op->exact) fprintf(out, ",,,");
                else fprintf(out, "%.4f,%.4f,%.4f,", runs[r].hashes.insert,
                             runs[r].hashes.exist, runs[r].hashes.inexist);
                fprintf(out, "%zu,%zu,%.6f,", fn, fp, fpr);
                if (op->exact) fprintf(out, ",,");
                else fprintf(out, "%.6f,%.6f,", op->fpr_theory(bf, n), op->fill_ratio(bf));
                fprintf(out, "%ld,%ld,%ld\n", rss, f1.majflt - f0.majflt, f1.minflt - f0.minflt);
            }
            op->destroy(bf);
        }
    }

    free(runs);
    if (out != stdout) fclose(out);
    dataset_close(&ds);
    return 0;
}
