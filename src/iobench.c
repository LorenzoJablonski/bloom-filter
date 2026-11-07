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
 * Tables are built once, by a separate unmeasured step (--build-store, which
 * needs all the keys in RAM to sort them), and reused while the key set is
 * unchanged (writes are the only thing that wears an SSD; queries only read).
 * A measured run holds no dataset in RAM: filters are built from a key stream
 * and only the sampled query keys are kept.
 *
 * Output: long format CSV, one row per (gatekeeper, repetition). Repetition
 * 0 is a warm-up and is not written. Experiments run one gatekeeper per process
 * (--filter, driven by run.py) so that one structure's paging cannot be paid by
 * the next one.
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

/* Table index of key number j of Dins, for chunk bounds lo[0..T]. */
static unsigned table_of(const size_t *lo, unsigned t, size_t j)
{
    while (j >= lo[t + 1]) t++;
    return t;
}

/* Build the tables whose fingerprint does not match. Needs all the keys in RAM. */
static int build_tables(const iobench_cfg_t *cfg, const char *dir, const size_t *lo, unsigned T,
                        const int *ready, const uint64_t *fp)
{
    keyset_t all;
    char path[1100];
    snprintf(path, sizeof(path), "data/%s/insert.txt", cfg->type);
    if (!keyset_load(path, &all)) { fprintf(stderr, "iobench: cannot read %s\n", path); return 0; }
    int ok = 1;
    for (unsigned t = 0; t < T && ok; t++) {
        if (ready[t]) continue;
        snprintf(path, sizeof(path), "%s/table_%u.sst", dir, t);
        fprintf(stderr, "iobench: building %s (%zu records)\n", path, lo[t + 1] - lo[t]);
        ok = store_build(path, all.keys + lo[t], lo[t + 1] - lo[t], cfg->value_size, fp[t]);
        if (!ok) fprintf(stderr, "iobench: failed to build %s\n", path);
    }
    keyset_free(&all);
    return ok;
}

typedef struct {
    size_t index; /* position of the key in its file */
    size_t slot;  /* position in the query sequence */
} pick_t;

static int cmp_pick(const void *a, const void *b)
{
    size_t x = ((const pick_t *)a)->index, y = ((const pick_t *)b)->index;
    return (x > y) - (x < y);
}

/* Copy the picked keys of a file into qk, in one pass over the stream. */
static int copy_picks(keystream_t *ks, pick_t *picks, size_t count, bf_key_t *qk)
{
    qsort(picks, count, sizeof(*picks), cmp_pick);
    size_t p = 0, base = 0, c;
    keystream_rewind(ks);
    while (p < count && (c = keystream_next(ks)) > 0) {
        for (; p < count && picks[p].index < base + c; p++) {
            const bf_key_t *key = &ks->keys[picks[p].index - base];
            void *copy = malloc(key->len);
            if (!copy) return 0;
            memcpy(copy, key->data, key->len);
            qk[picks[p].slot].data = copy;
            qk[picks[p].slot].len = key->len;
        }
        base += c;
    }
    return p == count;
}

int iobench_run(const iobench_cfg_t *cfg)
{
    if (cfg->filter && strcmp(cfg->filter, "none") != 0 && !bf_ops_by_name(cfg->filter)) {
        fprintf(stderr, "iobench: unknown gatekeeper %s\n", cfg->filter);
        return 2;
    }
    dataset_t ds;
    if (!dataset_open(cfg->type, cfg->n_override, "iobench", &ds)) return 1;
    size_t n = ds.n;
    unsigned T = cfg->tables ? cfg->tables : 1;
    if (T > n) T = (unsigned)n;
    if (ds.n_exist == 0 || ds.n_inexist == 0) {
        fprintf(stderr, "iobench: Dexist and Dinexist must be non-empty\n");
        dataset_close(&ds);
        return 1;
    }

    /* ---- tables on disk (built once, reused) ---- */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/%s_n%zu_T%u_v%zu", cfg->store_dir, cfg->type, n, T, cfg->value_size);
    if (!mkdir_p(dir)) { fprintf(stderr, "iobench: cannot create %s\n", dir); dataset_close(&ds); return 1; }

    disable_compression(dir);

    table_t *tb = (table_t *)calloc(T, sizeof(table_t));
    size_t *lo = (size_t *)calloc(T + 1, sizeof(size_t));
    uint64_t *fps = (uint64_t *)calloc(T, sizeof(uint64_t));
    int *ready = (int *)calloc(T, sizeof(int));
    if (!tb || !lo || !fps || !ready) { dataset_close(&ds); return 1; }
    for (unsigned t = 0; t <= T; t++) lo[t] = (size_t)((unsigned long long)n * t / T);

    /* fingerprints of the chunks, to check the tables on disk */
    int fail = 0;
    {
        for (unsigned t = 0; t < T; t++) fps[t] = store_fingerprint_init(lo[t + 1] - lo[t]);
        size_t c, j = 0;
        unsigned t = 0;
        keystream_rewind(&ds.ins);
        while ((c = keystream_next(&ds.ins)) > 0) {
            for (size_t i = 0; i < c; i++, j++) {
                t = table_of(lo, t, j);
                fps[t] = store_fingerprint_add(fps[t], &ds.ins.keys[i], 1);
            }
        }
        int missing = 0;
        for (unsigned u = 0; u < T; u++) {
            char path[1100];
            snprintf(path, sizeof(path), "%s/table_%u.sst", dir, u);
            ready[u] = store_matches(path, lo[u + 1] - lo[u], cfg->value_size, fps[u]);
            missing += !ready[u];
        }
        if (cfg->build_only) {
            int ok = !missing || build_tables(cfg, dir, lo, T, ready, fps);
            free(tb); free(lo); free(fps); free(ready);
            dataset_close(&ds);
            return ok ? 0 : 1;
        }
        if (missing) {
            fprintf(stderr, "iobench: %d table(s) missing or stale in %s; build them first "
                    "with --build-store\n", missing, dir);
            fail = 1;
        }
    }

    size_t index_mem = 0, blocks = 0;
    for (unsigned t = 0; t < T && !fail; t++) {
        char path[1100];
        snprintf(path, sizeof(path), "%s/table_%u.sst", dir, t);
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
    bf_key_t *qk = (bf_key_t *)calloc(Q ? Q : 1, sizeof(*qk));
    unsigned char *truth = (unsigned char *)malloc(Q ? Q : 1);
    double *lat = (double *)malloc((Q ? Q : 1) * sizeof(double));
    pick_t *picks = (pick_t *)malloc((Q ? Q : 1) * sizeof(*picks));
    if (!qk || !truth || !lat || !picks) fail = 1;
    if (!fail) {
        rng_state = 0x853C49E6748FEA9BULL;
        for (size_t i = 0; i < Q; i++) {
            picks[i].slot = i;
            if (i < nneg) { picks[i].index = rng_next() % ds.n_inexist; truth[i] = 0; }
            else          { picks[i].index = rng_next() % ds.n_exist;   truth[i] = 1; }
        }
        if (!copy_picks(&ds.inex, picks, nneg, qk) ||
            !copy_picks(&ds.ex, picks + nneg, Q - nneg, qk)) {
            fprintf(stderr, "iobench: cannot sample the query keys\n");
            fail = 1;
        }
        for (size_t i = Q; i > 1 && !fail; i--) { /* Fisher-Yates */
            size_t j = (size_t)(rng_next() % i);
            bf_key_t tk = qk[i - 1]; qk[i - 1] = qk[j]; qk[j] = tk;
            unsigned char tt = truth[i - 1]; truth[i - 1] = truth[j]; truth[j] = tt;
        }
    }
    free(picks);

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
            if (cfg->filter && strcmp(cfg->filter, name) != 0) continue;
            size_t gk_mem = 0;
            unsigned k_used = 0;

            for (unsigned t = 0; t < T && op; t++) {
                size_t cnt = lo[t + 1] - lo[t];
                bf_sizing_t sz = { 1, 1 };
                if (op->params && !op->params(cnt, cfg->p_target, &sz)) { fail = 1; break; }
                tb[t].bf = op->create(sz.m, sz.k);
                if (!tb[t].bf) { fail = 1; break; }
                if (op->params) k_used = op->fixed_k ? op->fixed_k : sz.k;
            }
            if (fail) { fprintf(stderr, "iobench: cannot build gatekeeper %s\n", name); break; }
            if (op) {
                size_t c, j = 0;
                unsigned t = 0;
                keystream_rewind(&ds.ins);
                while ((c = keystream_next(&ds.ins)) > 0) {
                    for (size_t i = 0; i < c; i++, j++) {
                        t = table_of(lo, t, j);
                        op->insert(tb[t].bf, &ds.ins.keys[i]);
                    }
                }
                for (t = 0; t < T; t++) gk_mem += op->memory_bytes(tb[t].bf);
            }

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
                        if (op && op->lookup(tb[t].bf, &qk[i]) == BF_ABSENT) continue;
                        int r = store_get(tb[t].st, &qk[i]);
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
    free(fps);
    free(ready);
    if (qk) for (size_t i = 0; i < Q; i++) free((void *)qk[i].data);
    free(qk);
    free(truth);
    free(lat);
    dataset_close(&ds);
    return fail;
}
