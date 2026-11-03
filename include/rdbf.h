#ifndef RDBF_H
#define RDBF_H

#include <stddef.h>
#include "common.h"

typedef struct rdbf rdbf_t;

/*
 * robustBF-style 2D filter: X x Y matrix of 64-bit cells, beta = 61 usable
 * bits per cell (Patgiri, 2021, Eq. 1). m is the requested number of usable
 * bits; X != Y are primes (both != 61) with X * Y * 61 >= m.
 */
rdbf_t *rdbf_create(size_t m);
void rdbf_destroy(rdbf_t *bf);
void rdbf_insert(rdbf_t *bf, const bf_key_t *key);
bf_member_t rdbf_lookup(rdbf_t *bf, const bf_key_t *key);

unsigned long rdbf_dim_x(const rdbf_t *bf);
unsigned long rdbf_dim_y(const rdbf_t *bf);
unsigned rdbf_beta(void);                       /* usable bits per cell (61) */
size_t rdbf_memory_bytes(const rdbf_t *bf);
size_t rdbf_cells(const rdbf_t *bf); /* usable bits: X * Y * beta */
double rdbf_fpr_theory(const rdbf_t *bf, size_t n); /* expected FPR after n inserts */
double rdbf_fill_ratio(const rdbf_t *bf); /* fraction of non-zero cells */
int rdbf_params(size_t n, double p, bf_sizing_t *out); /* sizing for target FPR p */

#endif /* RDBF_H */
