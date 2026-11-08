#ifndef HYBRID_H
#define HYBRID_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "common.h"

/*
 * Hybrid Bloom filter: one MurmurHash64A call per operation (rDBF idea),
 * key-partitioned shards (each key touches a single shard), each shard a
 * prime-dimensioned p1 x p2 matrix (rDBF), optionally with 4-bit counters
 * (CBF) to support deletion.
 */
typedef struct hybrid hybrid_t;

/*
 * m          requested cells (rounded up to a whole number of shards)
 * k          positions per key, all inside the key's shard
 * shard_max  upper bound for cells per shard; dims are the primes p1 <= p2
 *            with the largest p1*p2 <= shard_max (e.g. 512 -> 7 x 73 = 511)
 * counting   0 = bits (no delete), 1 = 4-bit saturating counters (delete)
 */
hybrid_t *hybrid_create(size_t m, unsigned k, size_t shard_max, int counting);
void hybrid_destroy(hybrid_t *bf);
void hybrid_insert(hybrid_t *bf, const bf_key_t *key);
/* Same writes as hybrid_insert, returning how many probed positions were already
 * non-zero (collisions). For the untimed counting pass only: hybrid_insert stays the
 * measured path. */
unsigned hybrid_insert_counted(hybrid_t *bf, const bf_key_t *key);

/*
 * Concurrent insertion (threadbench), apart from the measured serial insert.
 * Threads split the keys; each key writes inside one shard.
 * - hybrid_insert_atomic: bit variants only, atomic OR, no lock.
 * - hybrid_insert_locked: takes locks[shard % nlocks] around the key's writes
 *   (nlocks = 1: one global lock; nlocks = hybrid_shard_count: one per shard).
 *   It tries the lock first; when it is taken, *waits is incremented before
 *   blocking, which counts lock contention.
 */
size_t hybrid_shard_count(const hybrid_t *bf);
void hybrid_insert_atomic(hybrid_t *bf, const bf_key_t *key);
void hybrid_insert_locked(hybrid_t *bf, const bf_key_t *key, pthread_mutex_t *locks,
                          size_t nlocks, uint64_t *waits);
bf_member_t hybrid_lookup(hybrid_t *bf, const bf_key_t *key);
int hybrid_delete(hybrid_t *bf, const bf_key_t *key); /* 0 when not counting */

size_t hybrid_memory_bytes(const hybrid_t *bf);
size_t hybrid_cells(const hybrid_t *bf);              /* shards * p1 * p2 */
double hybrid_fpr_theory(const hybrid_t *bf, size_t n);
double hybrid_fill_ratio(const hybrid_t *bf);
size_t hybrid_saturated(const hybrid_t *bf);          /* counters stuck at max (counting only) */

size_t hybrid_num_shards(const hybrid_t *bf);
size_t hybrid_shard_stride(const hybrid_t *bf);       /* bytes per shard (multiple of 64) */
void hybrid_shard_dims(const hybrid_t *bf, unsigned long *p1, unsigned long *p2);

/* sizing for target FPR p with the given shard bound */
int hybrid_params(size_t n, double p, size_t shard_max, bf_sizing_t *out);

#endif /* HYBRID_H */
