#ifndef STORE_H
#define STORE_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"

/*
 * On-disk sorted table (SSTable-like), the "secondary storage" behind the
 * gatekeeper filters in the I/O experiment.
 *
 * File layout (all regions aligned to STORE_BLOCK bytes):
 *   block 0            header
 *   blocks 1..B        data blocks: records sorted by key, packed as
 *                      [u16 key_len][key][value_size bytes], key_len 0 = end
 *   after data         sparse index: first key of every data block
 *
 * The sparse index lives in RAM after store_open. store_get does a binary
 * search in it and then reads exactly ONE data block with pread — the unit
 * of I/O that a gatekeeper filter tries to avoid.
 */

#define STORE_BLOCK 4096u

typedef struct store store_t;

typedef struct {
    uint64_t reads;  /* data blocks read (pread calls) */
    uint64_t bytes;  /* bytes requested from the file */
} store_stats_t;

/*
 * Write a table with keys[0..n) (any order, no duplicates) to `path`.
 * `fingerprint` identifies the key set; it is stored in the header so an
 * existing file can be reused (see store_matches). Returns 1 on success.
 */
int store_build(const char *path, const bf_key_t *keys, size_t n, size_t value_size,
                uint64_t fingerprint);

/* 1 when `path` exists and was built from the same key set and value size */
int store_matches(const char *path, size_t n, size_t value_size, uint64_t fingerprint);

/*
 * direct = 1 opens with O_DIRECT (bypasses the OS page cache: every get hits
 * the device); 0 uses normal buffered reads. Returns NULL on failure (e.g.
 * filesystems without O_DIRECT support, such as tmpfs).
 */
store_t *store_open(const char *path, int direct);
void store_close(store_t *st);

/* 1 = key present in the table, 0 = absent, -1 = I/O error */
int store_get(store_t *st, const bf_key_t *key);

store_stats_t store_stats(const store_t *st);
void store_reset_stats(store_t *st);
size_t store_index_bytes(const store_t *st); /* RAM used by the sparse index */
size_t store_records(const store_t *st);
size_t store_blocks(const store_t *st);

/* Order-independent fingerprint of a key set (MurmurHash64A based). */
uint64_t store_fingerprint(const bf_key_t *keys, size_t n);
/* The same value computed over batches: init, then add every key once (any order). */
uint64_t store_fingerprint_init(size_t n);
uint64_t store_fingerprint_add(uint64_t sum, const bf_key_t *keys, size_t n);

#endif /* STORE_H */
