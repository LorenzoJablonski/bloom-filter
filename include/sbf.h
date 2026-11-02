#ifndef SBF_H
#define SBF_H

#include <stddef.h>
#include <stdbool.h>
#include "common.h"

typedef struct sbf sbf_t;

sbf_t *sbf_create(size_t m, unsigned k);
void sbf_destroy(sbf_t *bf);
void sbf_insert(sbf_t *bf, const bf_key_t *key);
bf_member_t sbf_lookup(sbf_t *bf, const bf_key_t *key);
size_t sbf_memory_bytes(const sbf_t *bf);
size_t sbf_cells(const sbf_t *bf); /* effective m */
double sbf_fpr_theory(const sbf_t *bf, size_t n); /* expected FPR after n inserts */
double sbf_fill_ratio(const sbf_t *bf); /* fraction of non-zero cells */
int sbf_params(size_t n, double p, bf_sizing_t *out); /* sizing for target FPR p */

#endif /* SBF_H */
