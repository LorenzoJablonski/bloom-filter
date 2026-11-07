#include "baseline.h"

#include <stdlib.h>
#include <string.h>
#include "uthash.h"

/*
 * Deterministic baseline: exact HashSet via uthash.
 * Each entry owns a private copy of the key (flexible array member), so the
 * caller's buffer may be reused or freed right after insert.
 */
typedef struct {
    UT_hash_handle hh;
    size_t len;
    char key[];
} baseline_entry_t;

struct baseline {
    baseline_entry_t *entries;
    size_t entry_bytes; /* sum of sizeof(entry) + key length for every entry */
};

baseline_t *baseline_create(void)
{
    baseline_t *bl = (baseline_t *)malloc(sizeof(baseline_t));
    if (!bl) return NULL;
    bl->entries = NULL;
    bl->entry_bytes = 0;
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
    baseline_entry_t *found = NULL;
    HASH_FIND(hh, bl->entries, key->data, key->len, found);
    if (found) return;

    size_t bytes = sizeof(baseline_entry_t) + key->len;
    baseline_entry_t *e = (baseline_entry_t *)malloc(bytes);
    if (!e) return;
    e->len = key->len;
    memcpy(e->key, key->data, key->len);
    HASH_ADD_KEYPTR(hh, bl->entries, e->key, e->len, e);
    bl->entry_bytes += bytes;
}

bf_member_t baseline_lookup(baseline_t *bl, const bf_key_t *key)
{
    if (!bl || !key || !key->data) return BF_ABSENT;
    baseline_entry_t *found = NULL;
    HASH_FIND(hh, bl->entries, key->data, key->len, found);
    return found ? BF_PRESENT : BF_ABSENT;
}

void baseline_remove(baseline_t *bl, const bf_key_t *key)
{
    if (!bl || !key || !key->data) return;
    baseline_entry_t *found = NULL;
    HASH_FIND(hh, bl->entries, key->data, key->len, found);
    if (!found) return;
    HASH_DEL(bl->entries, found);
    bl->entry_bytes -= sizeof(baseline_entry_t) + found->len;
    free(found);
}

size_t baseline_count(const baseline_t *bl)
{
    return (bl && bl->entries) ? (size_t)HASH_COUNT(bl->entries) : 0;
}

/*
 * Structural footprint: entries (handle + length + key bytes) plus the uthash
 * bucket array and table header. malloc bookkeeping is NOT included; use the
 * process RSS for an allocator-inclusive figure.
 */
size_t baseline_memory_bytes(const baseline_t *bl)
{
    if (!bl) return 0;
    size_t total = sizeof(baseline_t) + bl->entry_bytes;
    if (bl->entries) {
        const UT_hash_table *tbl = bl->entries->hh.tbl;
        total += tbl->num_buckets * sizeof(UT_hash_bucket) + sizeof(UT_hash_table);
    }
    return total;
}
