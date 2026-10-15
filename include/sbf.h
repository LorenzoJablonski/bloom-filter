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

#endif /* SBF_H */
