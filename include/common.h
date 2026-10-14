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

typedef enum {
    BF_ABSENT  = 0,
    BF_PRESENT = 1
} bf_member_t;

#endif /* COMMON_H */
