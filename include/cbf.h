#ifndef CBF_H
#define CBF_H

#include <stddef.h>
#include "common.h"

typedef struct cbf cbf_t;

cbf_t *cbf_create(size_t m, unsigned k);
void cbf_destroy(cbf_t *bf);
void cbf_insert(cbf_t *bf, const bf_key_t *key);
void cbf_delete(cbf_t *bf, const bf_key_t *key);
bf_member_t cbf_lookup(cbf_t *bf, const bf_key_t *key);

#endif /* CBF_H */
