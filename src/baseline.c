#include "baseline.h"

#include <stdlib.h>
#include <string.h>
#include "uthash.h"

typedef struct {
    bf_key_t key;
    UT_hash_handle hh;
} baseline_entry_t;

struct baseline {
    baseline_entry_t *entries;
};

baseline_t *baseline_create(void)
{
    baseline_t *bl = (baseline_t *)malloc(sizeof(baseline_t));
    if (!bl) return NULL;
    bl->entries = NULL;
    return bl;
}

void baseline_destroy(baseline_t *bl)
{
    if (!bl) return;
    baseline_entry_t *cur = NULL, *tmp = NULL;
    HASH_ITER(hh, bl->entries, cur, tmp) {
        HASH_DEL(bl->entries, cur);
        free(cur);
    }
    free(bl);
}

void baseline_insert(baseline_t *bl, const bf_key_t *key)
{
    if (!bl || !key || !key->data) return;
    /* avoid duplicates */
    baseline_entry_t *found = NULL;
    HASH_FIND(hh, bl->entries, key->data, key->len, found);
    if (found) return;
    baseline_entry_t *e = (baseline_entry_t *)malloc(sizeof(baseline_entry_t));
    if (!e) return;
    e->key.data = key->data;
    e->key.len = key->len;
    HASH_ADD_KEYPTR(hh, bl->entries, e->key.data, e->key.len, e);
}

bf_member_t baseline_lookup(baseline_t *bl, const bf_key_t *key)
{
    if (!bl || !key || !key->data) return BF_ABSENT;
    baseline_entry_t *found = NULL;
    HASH_FIND(hh, bl->entries, key->data, key->len, found);
    return found ? BF_PRESENT : BF_ABSENT;
}
