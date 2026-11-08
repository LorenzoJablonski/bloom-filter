#ifndef BF_H
#define BF_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "common.h"

/*
 * Generic interface over every probabilistic filter and the deterministic
 * baseline. Lets demo, REPL and benchmark iterate over all structures without
 * a per-filter switch.
 */
/*
 * Concurrent insertion offered by a structure (threadbench); NULL members are
 * not supported. Lookups need nothing here: once the inserts are done (a barrier
 * separates the phases) the plain lookup only reads.
 */
typedef struct bf_conc {
    unsigned (*partitions)(const void *bf);  /* > 0: insert_partitions is available */
    void (*insert_partitions)(void *bf, const bf_key_t *key, unsigned first, unsigned last);
    void (*insert_atomic)(void *bf, const bf_key_t *key);
    size_t (*lock_domains)(const void *bf);  /* > 0: insert_locked, one lock per domain */
    void (*insert_locked)(void *bf, const bf_key_t *key, pthread_mutex_t *locks,
                          size_t nlocks, uint64_t *waits);
} bf_conc_t;

typedef struct bf_ops {
    const char *name;
    int exact;               /* 1 = no false positives (deterministic) */
    int supports_delete;
    unsigned fixed_k;        /* != 0 when the model ignores k (rDBF: 1) */
    void *(*create)(size_t m, unsigned k);
    void (*destroy)(void *bf);
    void (*insert)(void *bf, const bf_key_t *key);
    bf_member_t (*lookup)(void *bf, const bf_key_t *key);
    void (*remove)(void *bf, const bf_key_t *key); /* NULL when unsupported */
    size_t (*memory_bytes)(const void *bf);
    /* Metrics below are NULL for the exact baseline. */
    size_t (*cells)(const void *bf);                 /* effective m after rounding */
    double (*fpr_theory)(const void *bf, size_t n);  /* expected FPR after n inserts */
    double (*fill_ratio)(const void *bf);            /* fraction of non-zero cells */
    int (*params)(size_t n, double p, bf_sizing_t *out); /* sizing for target FPR p */
    /* insert that also returns the collisions of this key (probes on positions
     * already non-zero); untimed counting pass only */
    unsigned (*insert_counted)(void *bf, const bf_key_t *key);
    const bf_conc_t *conc;  /* NULL: only serial operations */
} bf_ops_t;

/* Lookup by name; returns NULL when unknown. */
const bf_ops_t *bf_ops_by_name(const char *name);

/* All registered structures, in canonical order. Sets *count. */
const bf_ops_t *bf_ops_all(size_t *count);

#endif /* BF_H */
