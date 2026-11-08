#ifndef THREADBENCH_H
#define THREADBENCH_H

#include <stddef.h>

/*
 * Experiment 3: concurrent insertion and lookup with T threads, for one
 * structure and one strategy per process (run.py drives the grid).
 *
 * Strategies:
 *   serial       no worker threads: the plain insert/lookup (T must be 1)
 *   partition    T threads own disjoint ranges of partitions (PBF); every thread
 *                sees every key and writes only its partitions; no lock
 *   atomic       T threads split the keys; atomic OR (PBF, bit hybrids)
 *   lock_global  T threads split the keys; one mutex for the whole filter
 *   lock_shard   T threads split the keys; one mutex per shard (counting hybrids)
 * Lookups always split the keys and take no lock: they run after the inserts.
 */
typedef struct {
    const char *type;      /* dataset type */
    const char *filter;    /* structure name */
    const char *strategy;  /* see above */
    unsigned threads;      /* T */
    double p_target;       /* sizing: the structure's own params(n, p) (same_fpr) */
    unsigned reps;         /* reported repetitions (+1 warm-up) */
    const char *out_path;  /* CSV, NULL = stdout */
} threadbench_cfg_t;

int threadbench_run(const threadbench_cfg_t *cfg);

/* Strategies a structure supports, as a '|'-separated list (for --info). */
void threadbench_strategies(const char *filter, char *out, size_t cap);

#endif /* THREADBENCH_H */
