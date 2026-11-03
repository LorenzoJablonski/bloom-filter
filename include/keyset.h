#ifndef KEYSET_H
#define KEYSET_H

#include <stddef.h>
#include "common.h"

/* Keys of one dataset file, loaded into a single arena (one key per line). */
typedef struct {
    char *arena;      /* whole file contents; keys point into it */
    bf_key_t *keys;
    size_t count;
} keyset_t;

int keyset_load(const char *path, keyset_t *ks);
void keyset_free(keyset_t *ks);

/* Keep only keys that are members of inserted[0..n). */
void keyset_restrict(keyset_t *ks, const bf_key_t *inserted, size_t n);

/* Protocol 4.3.2 partitions of one dataset type. */
typedef struct {
    keyset_t ins;    /* Dins     data/<type>/insert.txt  */
    keyset_t ex;     /* Dexist   data/<type>/exist.txt   */
    keyset_t inex;   /* Dinexist data/<type>/inexist.txt */
    size_t n;        /* keys of Dins actually used */
} dataset_t;

/*
 * Load data/<type>/{insert,exist,inexist}.txt. When 0 < n_override < |Dins|,
 * only the first n_override keys of Dins are used and Dexist is restricted to
 * them. Returns 1 on success (messages go to stderr prefixed by `who`).
 */
int dataset_load(const char *type, size_t n_override, const char *who, dataset_t *ds);
void dataset_free(dataset_t *ds);

#endif /* KEYSET_H */
