#define _GNU_SOURCE /* sched_getaffinity, pthread_setaffinity_np, CPU_* */
#include "threadbench.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bf.h"
#include "keyset.h"
#include "procstat.h"

/*
 * Experiment 3: concurrency (see threadbench.h for the strategies).
 *
 * The main thread streams the keys in batches, as the other experiments do, and
 * does not count the file reads. For each batch it releases T worker threads
 * through a barrier, waits for them on a second barrier, and adds the time
 * between the two: that is the parallel work on the batch. Batches are larger
 * here (8 MiB, ~225 thousand UUIDs) so that the two barrier crossings per batch
 * stay a small share of the time even with many threads.
 *
 * Workers are pinned, worker i to the i-th CPU the process may use, so the
 * scheduler does not move them between cores during a measurement.
 *
 * Correctness: the final state of a bit or saturating-counter filter does not
 * depend on the order of the inserts, so every strategy must give exactly the
 * same false positives as the serial insert; the false negatives must be 0.
 */

#define TB_BUF (8u << 20)

typedef enum { ST_SERIAL, ST_PARTITION, ST_ATOMIC, ST_LOCK_GLOBAL, ST_LOCK_SHARD } strategy_t;
typedef enum { PH_INSERT, PH_LOOKUP, PH_QUIT } phase_t;

static const char *const STRATEGY_NAMES[] = { "serial", "partition", "atomic", "lock_global",
                                              "lock_shard" };

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int supports(const bf_ops_t *op, strategy_t s)
{
    const bf_conc_t *c = op->conc;
    switch (s) {
    case ST_SERIAL: return !op->exact;
    case ST_PARTITION: return c && c->partitions && c->insert_partitions;
    case ST_ATOMIC: return c && c->insert_atomic;
    case ST_LOCK_GLOBAL:
    case ST_LOCK_SHARD: return c && c->insert_locked && c->lock_domains;
    }
    return 0;
}

void threadbench_strategies(const char *filter, char *out, size_t cap)
{
    const bf_ops_t *op = bf_ops_by_name(filter);
    size_t used = 0;
    if (cap) out[0] = '\0';
    if (!op) return;
    for (int s = ST_SERIAL; s <= ST_LOCK_SHARD; s++) {
        if (!supports(op, (strategy_t)s)) continue;
        int w = snprintf(out + used, cap > used ? cap - used : 0, "%s%s", used ? "|" : "",
                         STRATEGY_NAMES[s]);
        if (w > 0) used += (size_t)w;
    }
}

/* ---------------------------------------------------------------- worker pool */

typedef struct pool pool_t;

typedef struct {
    pool_t *pool;
    unsigned id;
    int cpu;          /* -1: not pinned */
    size_t hits;      /* lookups answered "present" in the current phase */
    uint64_t waits;   /* lock contention: trylock found the lock taken */
    pthread_t thread;
} worker_t;

struct pool {
    pthread_barrier_t start, done;
    phase_t phase;
    const bf_ops_t *op;
    void *bf;
    strategy_t strategy;
    pthread_mutex_t *locks;
    size_t nlocks;
    unsigned parts;   /* partitions, for ST_PARTITION */
    unsigned threads;
    const bf_key_t *keys;
    size_t count;
};

static void *worker_main(void *arg)
{
    worker_t *w = (worker_t *)arg;
    pool_t *p = w->pool;
    if (w->cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(w->cpu, &set);
        pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    }
    for (;;) {
        pthread_barrier_wait(&p->start);
        if (p->phase == PH_QUIT) break;
        size_t lo = p->count * w->id / p->threads;
        size_t hi = p->count * (w->id + 1) / p->threads;
        const bf_conc_t *c = p->op->conc;
        if (p->phase == PH_INSERT) {
            switch (p->strategy) {
            case ST_PARTITION: {
                /* every key, only this thread's partitions */
                unsigned a = p->parts * w->id / p->threads;
                unsigned b = p->parts * (w->id + 1) / p->threads;
                for (size_t j = 0; j < p->count; j++) c->insert_partitions(p->bf, &p->keys[j], a, b);
                break;
            }
            case ST_ATOMIC:
                for (size_t j = lo; j < hi; j++) c->insert_atomic(p->bf, &p->keys[j]);
                break;
            case ST_LOCK_GLOBAL:
            case ST_LOCK_SHARD:
                for (size_t j = lo; j < hi; j++)
                    c->insert_locked(p->bf, &p->keys[j], p->locks, p->nlocks, &w->waits);
                break;
            case ST_SERIAL:
                break;
            }
        } else {
            size_t h = 0;
            for (size_t j = lo; j < hi; j++) h += p->op->lookup(p->bf, &p->keys[j]) == BF_PRESENT;
            w->hits += h;
        }
        pthread_barrier_wait(&p->done);
    }
    return NULL;
}

/* One pass of the stream through the workers; returns the parallel time (ms). */
static double pool_pass(pool_t *p, worker_t *ws, phase_t phase, keystream_t *ks, size_t *hits)
{
    double total = 0.0;
    size_t c;
    for (unsigned i = 0; i < p->threads; i++) ws[i].hits = 0;
    keystream_rewind(ks);
    while ((c = keystream_next(ks)) > 0) {
        p->phase = phase;
        p->keys = ks->keys;
        p->count = c;
        double t0 = now_ms();
        pthread_barrier_wait(&p->start);
        pthread_barrier_wait(&p->done);
        total += now_ms() - t0;
    }
    if (hits) {
        size_t h = 0;
        for (unsigned i = 0; i < p->threads; i++) h += ws[i].hits;
        *hits = h;
    }
    return total;
}

static double serial_pass(const bf_ops_t *op, void *bf, phase_t phase, keystream_t *ks, size_t *hits)
{
    double total = 0.0;
    size_t c, h = 0;
    keystream_rewind(ks);
    while ((c = keystream_next(ks)) > 0) {
        double t0 = now_ms();
        if (phase == PH_INSERT)
            for (size_t j = 0; j < c; j++) op->insert(bf, &ks->keys[j]);
        else
            for (size_t j = 0; j < c; j++) h += op->lookup(bf, &ks->keys[j]) == BF_PRESENT;
        total += now_ms() - t0;
    }
    if (hits) *hits = h;
    return total;
}

/* First CPU listed in this CPU's thread_siblings_list (its physical core), or itself. */
static int core_leader(int cpu)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
    FILE *fh = fopen(path, "r");
    int first = cpu;
    if (fh) {
        if (fscanf(fh, "%d", &first) != 1) first = cpu;
        fclose(fh);
    }
    return first;
}

/*
 * CPUs this process may run on, in the order the workers get them: one per
 * physical core first, then the second hardware thread of each core. Without
 * this, 6 workers on a 6-core SMT machine would sit on CPUs 0-5, which are
 * 3 cores (0/1, 2/3, 4/5 are siblings).
 */
static int allowed_cpus(int *cpus, int cap)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) return 0;
    int n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int c = 0; c < CPU_SETSIZE && n < cap; c++) {
            if (!CPU_ISSET(c, &set)) continue;
            int leader = core_leader(c);
            /* pass 0: the core's first allowed CPU; pass 1: the others */
            int first_allowed = leader;
            while (first_allowed < c && !CPU_ISSET(first_allowed, &set)) first_allowed++;
            if ((pass == 0) == (first_allowed == c)) cpus[n++] = c;
        }
    return n;
}

static int reopen(keystream_t *ks, const char *type, const char *file)
{
    char path[512];
    snprintf(path, sizeof(path), "data/%s/%s", type, file);
    keystream_close(ks);
    return keystream_open(ks, path, TB_BUF, 0);
}

/* ---------------------------------------------------------------- run */

int threadbench_run(const threadbench_cfg_t *cfg)
{
    const bf_ops_t *op = bf_ops_by_name(cfg->filter ? cfg->filter : "");
    if (!op) {
        fprintf(stderr, "threadbench: unknown filter %s\n", cfg->filter ? cfg->filter : "(none)");
        return 2;
    }
    int strategy = -1;
    for (int s = ST_SERIAL; s <= ST_LOCK_SHARD; s++)
        if (cfg->strategy && strcmp(cfg->strategy, STRATEGY_NAMES[s]) == 0) strategy = s;
    if (strategy < 0 || !supports(op, (strategy_t)strategy)) {
        char list[128];
        threadbench_strategies(op->name, list, sizeof(list));
        fprintf(stderr, "threadbench: %s does not support strategy %s (supported: %s)\n", op->name,
                cfg->strategy ? cfg->strategy : "(none)", list[0] ? list : "none");
        return 2;
    }
    unsigned threads = cfg->threads ? cfg->threads : 1;
    if (strategy == ST_SERIAL && threads != 1) {
        fprintf(stderr, "threadbench: serial runs with --threads 1\n");
        return 2;
    }

    dataset_t ds;
    if (!dataset_open(cfg->type, 0, "threadbench", &ds)) return 1;
    if (!reopen(&ds.ins, cfg->type, "insert.txt") || !reopen(&ds.ex, cfg->type, "exist.txt") ||
        !reopen(&ds.inex, cfg->type, "inexist.txt")) {
        fprintf(stderr, "threadbench: cannot reopen the dataset of %s\n", cfg->type);
        dataset_close(&ds);
        return 1;
    }
    size_t n = ds.n;

    bf_sizing_t own;
    if (!op->params(n, cfg->p_target, &own)) {
        fprintf(stderr, "threadbench: invalid p=%g\n", cfg->p_target);
        dataset_close(&ds);
        return 1;
    }

    int cpus[CPU_SETSIZE];
    int ncpu = allowed_cpus(cpus, CPU_SETSIZE);
    if (strategy != ST_SERIAL && ncpu > 0 && (int)threads > ncpu)
        fprintf(stderr, "threadbench: %u threads on %d CPUs (threads will share CPUs)\n", threads, ncpu);
    if (strategy != ST_SERIAL && ncpu > 0) {
        fprintf(stderr, "threadbench: workers on CPUs");
        for (unsigned i = 0; i < threads; i++) fprintf(stderr, " %d", cpus[i % (unsigned)ncpu]);
        fprintf(stderr, "\n");
    }

    FILE *out = stdout;
    if (cfg->out_path && cfg->out_path[0]) {
        out = fopen(cfg->out_path, "w");
        if (!out) {
            fprintf(stderr, "threadbench: cannot open %s\n", cfg->out_path);
            dataset_close(&ds);
            return 1;
        }
    }
    fprintf(out, "type,filter,strategy,threads,rep,n,n_exist,n_inexist,p_target,m,k,mem_bytes,"
                 "lock_bytes,insert_ms,lookup_exist_ms,lookup_inexist_ms,fn,fp,fpr,lock_waits,"
                 "rss_kb,majflt,minflt,cpus\n");

    int rc = 0;
    unsigned reps = cfg->reps ? cfg->reps : 1;
    for (unsigned rep = 0; rep <= reps && rc == 0; rep++) { /* rep 0 = warm-up */
        proc_faults_t f0 = proc_faults();
        void *bf = op->create(own.m, own.k);
        if (!bf) { fprintf(stderr, "threadbench: %s create failed\n", op->name); rc = 1; break; }

        pool_t pool;
        memset(&pool, 0, sizeof(pool));
        pool.op = op;
        pool.bf = bf;
        pool.strategy = (strategy_t)strategy;
        pool.threads = threads;
        if (strategy == ST_PARTITION) {
            pool.parts = op->conc->partitions(bf);
            if (threads > pool.parts) {
                fprintf(stderr, "threadbench: %u threads but only %u partitions\n", threads, pool.parts);
                op->destroy(bf);
                rc = 2;
                break;
            }
        }
        if (strategy == ST_LOCK_GLOBAL || strategy == ST_LOCK_SHARD) {
            pool.nlocks = strategy == ST_LOCK_GLOBAL ? 1 : op->conc->lock_domains(bf);
            pool.locks = (pthread_mutex_t *)malloc(pool.nlocks * sizeof(pthread_mutex_t));
            if (!pool.locks) { op->destroy(bf); rc = 1; break; }
            for (size_t i = 0; i < pool.nlocks; i++) pthread_mutex_init(&pool.locks[i], NULL);
        }

        worker_t *ws = NULL;
        if (strategy != ST_SERIAL) {
            ws = (worker_t *)calloc(threads, sizeof(worker_t));
            if (!ws || pthread_barrier_init(&pool.start, NULL, threads + 1) != 0 ||
                pthread_barrier_init(&pool.done, NULL, threads + 1) != 0) {
                fprintf(stderr, "threadbench: cannot set up %u threads\n", threads);
                free(ws);
                op->destroy(bf);
                rc = 1;
                break;
            }
            for (unsigned i = 0; i < threads; i++) {
                ws[i].pool = &pool;
                ws[i].id = i;
                ws[i].cpu = ncpu > 0 ? cpus[i % (unsigned)ncpu] : -1;
                if (pthread_create(&ws[i].thread, NULL, worker_main, &ws[i]) != 0) {
                    fprintf(stderr, "threadbench: pthread_create failed (%s)\n", strerror(errno));
                    exit(1); /* barriers already count on all of them */
                }
            }
        }

        size_t present = 0, fp = 0;
        double t_ins, t_ex, t_inex;
        if (strategy == ST_SERIAL) {
            t_ins = serial_pass(op, bf, PH_INSERT, &ds.ins, NULL);
        } else {
            t_ins = pool_pass(&pool, ws, PH_INSERT, &ds.ins, NULL);
        }
        long rss = proc_rss_kb();
        if (strategy == ST_SERIAL) {
            t_ex = serial_pass(op, bf, PH_LOOKUP, &ds.ex, &present);
            t_inex = serial_pass(op, bf, PH_LOOKUP, &ds.inex, &fp);
        } else {
            t_ex = pool_pass(&pool, ws, PH_LOOKUP, &ds.ex, &present);
            t_inex = pool_pass(&pool, ws, PH_LOOKUP, &ds.inex, &fp);
        }
        proc_faults_t f1 = proc_faults();

        uint64_t waits = 0;
        if (ws) {
            pool.phase = PH_QUIT;
            pthread_barrier_wait(&pool.start);
            for (unsigned i = 0; i < threads; i++) {
                pthread_join(ws[i].thread, NULL);
                waits += ws[i].waits;
            }
            pthread_barrier_destroy(&pool.start);
            pthread_barrier_destroy(&pool.done);
            free(ws);
        }

        if (rep > 0) {
            size_t mem = op->memory_bytes(bf);
            size_t lock_bytes = pool.nlocks * sizeof(pthread_mutex_t);
            fprintf(out, "%s,%s,%s,%u,%u,%zu,%zu,%zu,%g,%zu,%u,%zu,%zu,%.4f,%.4f,%.4f,%zu,%zu,%.6f,"
                         "%llu,%ld,%ld,%ld,%d\n",
                    cfg->type, op->name, STRATEGY_NAMES[strategy], threads, rep, n, ds.n_exist,
                    ds.n_inexist, cfg->p_target, own.m, op->fixed_k ? op->fixed_k : own.k, mem,
                    lock_bytes, t_ins, t_ex, t_inex, ds.n_exist - present, fp,
                    ds.n_inexist ? (double)fp / (double)ds.n_inexist : 0.0,
                    (unsigned long long)waits, rss, f1.majflt - f0.majflt, f1.minflt - f0.minflt, ncpu);
        }
        if (pool.locks) {
            for (size_t i = 0; i < pool.nlocks; i++) pthread_mutex_destroy(&pool.locks[i]);
            free(pool.locks);
        }
        op->destroy(bf);
    }

    if (out != stdout) fclose(out);
    dataset_close(&ds);
    return rc;
}
