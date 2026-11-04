#ifndef KEYSET_H
#define KEYSET_H

#include <stddef.h>
#include "baseline.h"
#include "common.h"

/*
 * Dataset access. Experiments stream the keys in batches through a small fixed
 * buffer, so the RAM of a process holds the structure under test and not the
 * dataset: if something is paged, it is the structure. In a real system keys
 * arrive in batches too; they are not all held in memory.
 */

#define KEYSTREAM_BUF (1u << 20) /* 1 MiB, about 27 thousand UUIDs */

typedef struct {
    int fd;
    char *buf;
    size_t cap;
    size_t have, used;  /* buf[used..have) is a partial line kept for the next read */
    size_t file_pos;    /* bytes read from the file */
    size_t advised;     /* file bytes already dropped from the page cache */
    int eof;
    int error;
    size_t limit;       /* maximum keys delivered per pass; 0 = all */
    size_t delivered;
    baseline_t *only;   /* when set, deliver only members of this set (not owned) */
    bf_key_t *keys;     /* current batch; valid until the next keystream_next */
    size_t keys_cap;
    size_t count;
} keystream_t;

int keystream_open(keystream_t *ks, const char *path, size_t buf_size, size_t limit);
void keystream_close(keystream_t *ks);
void keystream_rewind(keystream_t *ks);
/* Next batch in ks->keys; returns how many keys it has, 0 at the end (or on ks->error). */
size_t keystream_next(keystream_t *ks);
/* Keys in one full pass (rewinds before and after). */
size_t keystream_count(keystream_t *ks);

/* Keys of one file held in memory: only for building tables, never while measuring. */
typedef struct {
    char *arena;      /* whole file contents; keys point into it */
    bf_key_t *keys;
    size_t count;
} keyset_t;

int keyset_load(const char *path, keyset_t *ks);
void keyset_free(keyset_t *ks);

/* Protocol 4.3.2 partitions of one dataset type, as streams. */
typedef struct {
    keystream_t ins;   /* Dins     data/<type>/insert.txt  */
    keystream_t ex;    /* Dexist   data/<type>/exist.txt   */
    keystream_t inex;  /* Dinexist data/<type>/inexist.txt */
    size_t n, n_exist, n_inexist;
    baseline_t *restrict_set; /* first n keys of Dins, only with n_override */
} dataset_t;

/*
 * Open data/<type>/{insert,exist,inexist}.txt and count their keys. When
 * 0 < n_override < |Dins|, only the first n_override keys of Dins are used and
 * Dexist is restricted to them, which keeps those keys in a hash table in RAM:
 * fine for quick tests, not for measuring paging (generate a dataset per N with
 * `make data N=...` instead). Returns 1 on success; messages are prefixed by `who`.
 */
int dataset_open(const char *type, size_t n_override, const char *who, dataset_t *ds);
void dataset_close(dataset_t *ds);

#endif /* KEYSET_H */
