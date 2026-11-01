#ifndef RDBF_H
#define RDBF_H

#include <stddef.h>
#include "common.h"

typedef struct rdbf rdbf_t;

/* m is the requested number of bits; dimensions are chosen as primes. */
rdbf_t *rdbf_create(size_t m);
void rdbf_destroy(rdbf_t *bf);
void rdbf_insert(rdbf_t *bf, const bf_key_t *key);
bf_member_t rdbf_lookup(rdbf_t *bf, const bf_key_t *key);

unsigned long rdbf_dim_x(const rdbf_t *bf);
unsigned long rdbf_dim_y(const rdbf_t *bf);
size_t rdbf_memory_bytes(const rdbf_t *bf);
size_t rdbf_cells(const rdbf_t *bf); /* effective m */
double rdbf_fpr_theory(const rdbf_t *bf, size_t n); /* expected FPR after n inserts */
double rdbf_fill_ratio(const rdbf_t *bf); /* fraction of non-zero cells */

#endif /* RDBF_H */
