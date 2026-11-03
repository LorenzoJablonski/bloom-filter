#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const void *data;
    size_t len;
} bf_key_t;

typedef struct {
    size_t m;           /* number of bits (bitmask size) */
    unsigned k;         /* number of hash functions */
    uint32_t base_seed; /* base seed for MurmurHash2 */
} bf_params_t;

/*
 * Result of a per-model sizing function: parameters that make the model reach
 * a target FPR p for n keys (according to that model's own FPR formula).
 */
typedef struct {
    size_t m;   /* cells (bits or counters) to request from <model>_create */
    unsigned k; /* hash positions per key (1 for rDBF) */
} bf_sizing_t;

#define BF_MAX_K 32u /* upper bound of the k search in the sizing functions */

/*
 * Experimental protocol version. Bump it whenever a change affects whether
 * results are comparable with earlier runs (sizing formulas, hashing, warm-up,
 * timing method, CSV columns, store layout...). scripts/run.py records it per
 * run and the analysis only groups runs with the same version.
 */
#define BF_PROTOCOL_VERSION 2 /* 2: rss_kb/majflt/minflt CSV columns */

typedef enum {
    BF_ABSENT  = 0,
    BF_PRESENT = 1
} bf_member_t;

#endif /* COMMON_H */
