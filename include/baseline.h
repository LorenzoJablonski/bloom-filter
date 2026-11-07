#ifndef BASELINE_H
#define BASELINE_H

#include "common.h"

typedef struct baseline baseline_t;

baseline_t *baseline_create(void);
void baseline_destroy(baseline_t *bl);
void baseline_insert(baseline_t *bl, const bf_key_t *key);
bf_member_t baseline_lookup(baseline_t *bl, const bf_key_t *key);
/* Removes the key if present (exact: no effect on other keys). */
void baseline_remove(baseline_t *bl, const bf_key_t *key);
size_t baseline_count(const baseline_t *bl);
size_t baseline_memory_bytes(const baseline_t *bl);

#endif /* BASELINE_H */
