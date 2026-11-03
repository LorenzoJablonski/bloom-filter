#define _POSIX_C_SOURCE 200809L
#include "iobench.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#endif
#include <time.h>

#include "bf.h"
#include "keyset.h"
#include "store.h"
#include "procstat.h"

/*
 * I/O experiment: filters as gatekeepers in front of on-disk tables.
 *
 * Dins is split into T contiguous chunks (insertion batches), each written to
 * its own sorted table (SSTable-like, see store.h) with its own filter, as in
 * an LSM-tree. A query probes the tables from newest to oldest; for each
 * table the gatekeeper is asked first and the disk is read only on "maybe".
 *
 *   gatekeeper "none"     every probe reads a block (control)
 *   probabilistic filters skip the read on "absent"; FPs cost a wasted read
 *   "baseline"            exact set, never wastes a read, costs RAM
 *
 * Tables are built once and reused while the key set is unchanged (writes
 * are the only thing that wears an SSD; queries only read).
 *
 * Output: long format CSV, one row per (gatekeeper, repetition). Repetition
 * 0 is a warm-up and is not written. Gatekeepers run one after the other
 * (filters of one gatekeeper are freed before the next is built) so that a
 * RAM-limited machine never holds every structure at once.
 *
 * rss_kb/majflt/minflt: process resident set after the query loop and the
 * page faults during it. O_DIRECT and pread do not fault, so major faults
 * there are the gatekeeper (or the index) being paged in from swap.
 */

typedef struct {
    store_t *st;
    void *bf;          /* NULL for gatekeeper "none" */
} table_t;

static uint64_t rng_state = 0x853C49E6748FEA9BULL;

static uint64_t rng_next(void)
{
    uint64_t x = rng_state;      /* xorshift64* */
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* bytes the process caused to be fetched from the storage layer, or -1 */
static long long proc_read_bytes(void)
{
    FILE *f = fopen("/proc/self/io", "r");
    if (!f) return -1;
    char line[128];
    long long v = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "read_bytes:", 11) == 0) { v = atoll(line + 11); break; }
    }
    fclose(f);
    return v;
}

static int mkdir_p(const char *path)
{
    char buf[1024];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) return 0;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) return 0;
            buf[i] = c;
        }
    }
    return 1;
}

/*
 * btrfs with transparent compression (Fedora's default, compress=zstd)
 * silently serves O_DIRECT reads of compressed files through the page cache,
 * so no read would reach the device. New files inherit the directory's
 * NOCOMP flag; set it best-effort (no-op on ext4/xfs, which do not compress).
 */
static void disable_compression(const char *dir)
{
#if defined(__linux__) && defined(FS_IOC_GETFLAGS) && defined(FS_NOCOMP_FL)
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return;
    int flags = 0;
    if (ioctl(fd, FS_IOC_GETFLAGS, &flags) == 0 && !(flags & FS_NOCOMP_FL)) {
        flags |= FS_NOCOMP_FL;
        (void)ioctl(fd, FS_IOC_SETFLAGS, &flags);
    }
    close(fd);
#else
    (void)dir;
#endif
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double percentile(const double *sorted, size_t n, double q)
{
    if (n == 0) return 0.0;
    size_t i = (size_t)(q * (double)(n - 1) + 0.5);
    return sorted[i];
}

int iobench_run(const iobench_cfg_t *cfg)
{
    dataset_t ds;
    if (!dataset_load(cfg->type, cfg->n_override, "iobench", &ds)) return 1;
    size_t n = ds.n;
    unsigned T = cfg->tables ? cfg->tables : 1;
    if (T > n) T = (unsigned)n;
    if (ds.ex.count == 0 || ds.inex.count == 0) {
        fprintf(stderr, "iobench: Dexist and Dinexist must be non-empty\n");
        dataset_free(&ds);
        return 1;
    }

    /* ---- tables on disk (built once, reused) ---- */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/%s_n%zu_T%u_v%zu", cfg->store_dir, cfg->type, n, T, cfg->value_size);
    if (!mkdir_p(dir)) { fprintf(stderr, "iobench: cannot create %s\n", dir); dataset_free(&ds); return 1; }

    disable_compression(dir);

    table_t *tb = (table_t *)calloc(T, sizeof(table_t));
    size_t *lo = (size_t *)calloc(T + 1, sizeof(size_t));
    if (!tb || !lo) { dataset_free(&ds); return 1; }
    for (unsigned t = 0; t <= T; t++) lo[t] = (size_t)((unsigned long long)n * t / T);

    int fail = 0;
    size_t index_mem = 0, blocks = 0;
    for (unsigned t = 0; t < T && !fail; t++) {
        char path[1100];
        snprintf(path, sizeof(path), "%s/table_%u.sst", dir, t);
        const bf_key_t *keys = ds.ins.keys + lo[t];
        size_t cnt = lo[t + 1] - lo[t];
        uint64_t fp = store_fingerprint(keys, cnt);
        if (!store_matches(path, cnt, cfg->value_size, fp)) {
            fprintf(stderr, "iobench: building %s (%zu records)\n", path, cnt);
            if (!store_build(path, keys, cnt, cfg->value_size, fp)) {
                fprintf(stderr, "iobench: failed to build %s\n", path);
                fail = 1;
                break;
            }
        }
        tb[t].st = store_open(path, cfg->direct);
        if (!tb[t].st) {
            fprintf(stderr, "iobench: cannot open %s%s\n", path,
                    cfg->direct ? " with O_DIRECT (filesystem without support? try --cached)" : "");
            fail = 1;
            break;
        }
        index_mem += store_index_bytes(tb[t].st);
        blocks += store_blocks(tb[t].st);
    }

    /* ---- query workload: fixed sequence shared by every gatekeeper ---- */
    size_t Q = cfg->queries;
    size_t nneg = (size_t)((double)Q * cfg->neg_ratio + 0.5);
    if (nneg > Q) nneg = Q;
    const bf_key_t **qk = (const bf_key_t **)malloc((Q ? Q : 1) * sizeof(*qk));
    unsigned char *truth = (unsigned char *)malloc(Q ? Q : 1);
    double *lat = (double *)malloc((Q ? Q : 1) * sizeof(double));
    if (!qk || !truth || !lat) fail = 1;
    if (!fail) {
        rng_state = 0x853C49E6748FEA9BULL;
        for (size_t i = 0; i < Q; i++) {
            if (i < nneg) { qk[i] = &ds.inex.keys[rng_next() % ds.inex.count]; truth[i] = 0; }
            else          { qk[i] = &ds.ex.keys[rng_next() % ds.ex.count];     truth[i] = 1; }
        }
        for (size_t i = Q; i > 1; i--) {         /* Fisher-Yates */
            size_t j = (size_t)(rng_next() % i);
            const bf_key_t *tk = qk[i - 1]; qk[i - 1] = qk[j]; qk[j] = tk;
            unsigned char tt = truth[i - 1]; truth[i - 1] = truth[j]; truth[j] = tt;
        }
    }

    FILE *out = stdout;
    if (!fail && cfg->out_path && cfg->out_path[0]) {
        out = fopen(cfg->out_path, "w");
        if (!out) { fprintf(stderr, "iobench: cannot open %s\n", cfg->out_path); fail = 1; out = stdout; }
    }

    if (!fail) {
        fprintf(out, "type,gatekeeper,rep,n,tables,neg_ratio,queries,direct,value_size,blocks,"
                     "gk_mem_bytes,index_mem_bytes,k,disk_reads,wasted_reads,reads_per_query,"
                     "proc_read_bytes,total_ms,mean_us,p50_us,p99_us,errors,rss_kb,majflt,minflt\n");

        size_t nops = 0;
        const bf_ops_t *ops = bf_ops_all(&nops);
        int warned = 0;
        for (size_t g = 0; g <= nops && !fail; g++) {   /* g == 0: no gatekeeper */
            const bf_ops_t *op = g ? &ops[g - 1] : NULL;
            const char *name = op ? op->name : "none";
            size_t gk_mem = 0;
            unsigned k_used = 0;

            for (unsigned t = 0; t < T && op; t++) {
                size_t cnt = lo[t + 1] - lo[t];
                bf_sizing_t sz = { 1, 1 };
                if (op->params && !op->params(cnt, cfg->p_target, &sz)) { fail = 1; break; }
                tb[t].bf = op->create(sz.m, sz.k);
                if (!tb[t].bf) { fail = 1; break; }
                for (size_t j = lo[t]; j < lo[t + 1]; j++) op->insert(tb[t].bf, &ds.ins.keys[j]);
                gk_mem += op->memory_bytes(tb[t].bf);
                if (op->params) k_used = op->fixed_k ? op->fixed_k : sz.k;
            }
            if (fail) { fprintf(stderr, "iobench: cannot build gatekeeper %s\n", name); break; }

            for (unsigned rep = 0; rep <= cfg->reps; rep++) {   /* rep 0 = warm-up */
                for (unsigned t = 0; t < T; t++) store_reset_stats(tb[t].st);
                size_t errors = 0, wasted = 0;
                long long rb0 = proc_read_bytes();
                proc_faults_t f0 = proc_faults();
                double t_all = now_ns();

                for (size_t i = 0; i < Q; i++) {
                    double t0 = now_ns();
                    int found = 0;
                    for (unsigned t = T; t-- > 0 && !found; ) {          /* newest first */
                        if (op && op->lookup(tb[t].bf, qk[i]) == BF_ABSENT) continue;
                        int r = store_get(tb[t].st, qk[i]);
                        if (r < 0) { errors++; break; }
                        if (r == 1) found = 1;
                        else wasted++;
                    }
                    lat[i] = now_ns() - t0;
                    if (found != truth[i]) errors++;
                }

                double total_ms = (now_ns() - t_all) / 1e6;
                long long rb1 = proc_read_bytes();
                proc_faults_t f1 = proc_faults();
                if (rep == 0) continue;

                uint64_t reads = 0;
                for (unsigned t = 0; t < T; t++) reads += store_stats(tb[t].st).reads;
                double mean = 0.0;
                for (size_t i = 0; i < Q; i++) mean += lat[i];
                mean = Q ? mean / (double)Q : 0.0;
                qsort(lat, Q, sizeof(double), cmp_double);

                fprintf(out, "%s,%s,%u,%zu,%u,%g,%zu,%d,%zu,%zu,%zu,%zu,",
                        cfg->type, name, rep, n, T, cfg->neg_ratio, Q, cfg->direct ? 1 : 0,
                        cfg->value_size, blocks, gk_mem, index_mem);
                if (k_used) fprintf(out, "%u,", k_used); else fprintf(out, ",");
                fprintf(out, "%llu,%zu,%.4f,", (unsigned long long)reads, wasted,
                        Q ? (double)reads / (double)Q : 0.0);
                if (rb0 >= 0 && rb1 >= 0) fprintf(out, "%lld,", rb1 - rb0); else fprintf(out, ",");
                fprintf(out, "%.3f,%.3f,%.3f,%.3f,%zu,", total_ms, mean / 1e3,
                        percentile(lat, Q, 0.50) / 1e3, percentile(lat, Q, 0.99) / 1e3, errors);
                fprintf(out, "%ld,%ld,%ld\n", proc_rss_kb(), f1.majflt - f0.majflt,
                        f1.minflt - f0.minflt);
                fflush(out);
                if (cfg->direct && rb0 >= 0 && rb1 >= 0 && reads > 0 && !warned &&
                    (unsigned long long)(rb1 - rb0) < reads * (uint64_t)STORE_BLOCK / 2) {
                    fprintf(stderr, "iobench: AVISO: %llu leituras O_DIRECT mas so %lld bytes chegaram ao "
                            "dispositivo; as leituras estao vindo do page cache (btrfs com compressao? "
                            "apague %s e rode de novo)\n", (unsigned long long)reads, rb1 - rb0, dir);
                    warned = 1;
                }
                if (errors) fprintf(stderr, "iobench: %s returned %zu wrong answers\n", name, errors);
            }

            for (unsigned t = 0; t < T && op; t++) {
                if (tb[t].bf) op->destroy(tb[t].bf);
                tb[t].bf = NULL;
            }
        }
    }

    if (out != stdout) fclose(out);
    for (unsigned t = 0; t < T; t++) store_close(tb[t].st);
    free(tb);
    free(lo);
    free(qk);
    free(truth);
    free(lat);
    dataset_free(&ds);
    return fail;
}
