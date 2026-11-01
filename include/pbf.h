#ifndef PBF_H
#define PBF_H

#include <stddef.h>
#include "common.h"

typedef struct pbf pbf_t;

pbf_t *pbf_create(size_t m, unsigned k);
void pbf_destroy(pbf_t *bf);
void pbf_insert(pbf_t *bf, const bf_key_t *key);
bf_member_t pbf_lookup(pbf_t *bf, const bf_key_t *key);

#endif /* PBF_H */
