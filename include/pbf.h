#ifndef PBF_H
#define PBF_H

#include <stddef.h>
#include "common.h"

typedef struct pbf pbf_t;

pbf_t *pbf_create(size_t m, unsigned k);
void pbf_destroy(pbf_t *bf);
void pbf_insert(pbf_t *bf, const bf_key_t *key);
bf_member_t pbf_lookup(pbf_t *bf, const bf_key_t *key);
size_t pbf_memory_bytes(const pbf_t *bf);
size_t pbf_cells(const pbf_t *bf); /* effective m */
double pbf_fpr_theory(const pbf_t *bf, size_t n); /* expected FPR after n inserts */
double pbf_fill_ratio(const pbf_t *bf); /* fraction of non-zero cells */
size_t pbf_partition_stride(const pbf_t *bf); /* bytes between partition starts (multiple of 64) */
int pbf_params(size_t n, double p, bf_sizing_t *out); /* sizing for target FPR p */

#endif /* PBF_H */
