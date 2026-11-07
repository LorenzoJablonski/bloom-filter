#ifndef CBF_H
#define CBF_H

#include <stddef.h>
#include "common.h"

typedef struct cbf cbf_t;

cbf_t *cbf_create(size_t m, unsigned k);
void cbf_destroy(cbf_t *bf);
void cbf_insert(cbf_t *bf, const bf_key_t *key);
/* Same writes as cbf_insert, returning how many probed positions were already
 * non-zero (collisions). For the untimed counting pass only: cbf_insert stays the
 * measured path. */
unsigned cbf_insert_counted(cbf_t *bf, const bf_key_t *key);
void cbf_delete(cbf_t *bf, const bf_key_t *key);
bf_member_t cbf_lookup(cbf_t *bf, const bf_key_t *key);
size_t cbf_memory_bytes(const cbf_t *bf);
size_t cbf_cells(const cbf_t *bf); /* effective m */
double cbf_fpr_theory(const cbf_t *bf, size_t n); /* expected FPR after n inserts */
double cbf_fill_ratio(const cbf_t *bf); /* fraction of non-zero cells */
int cbf_params(size_t n, double p, bf_sizing_t *out); /* sizing for target FPR p */
size_t cbf_saturated(const cbf_t *bf); /* counters stuck at max */

#endif /* CBF_H */
