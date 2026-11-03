#include "keyset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baseline.h"

int keyset_load(const char *path, keyset_t *ks)
{
    memset(ks, 0, sizeof(*ks));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return 0; }
    rewind(f);

    ks->arena = (char *)malloc((size_t)size + 1);
    if (!ks->arena) { fclose(f); return 0; }
    size_t got = fread(ks->arena, 1, (size_t)size, f);
    fclose(f);
    ks->arena[got] = '\n';

    size_t lines = 0;
    for (size_t i = 0; i <= got; i++) lines += ks->arena[i] == '\n';
    ks->keys = (bf_key_t *)malloc((lines + 1) * sizeof(bf_key_t));
    if (!ks->keys) { free(ks->arena); return 0; }

    char *p = ks->arena, *end = ks->arena + got;
    while (p < end) {
        char *nl = memchr(p, '\n', (size_t)(end - p) + 1);
        size_t len = (size_t)(nl - p);
        if (len > 0 && p[len - 1] == '\r') len--;
        if (len > 0) {
            ks->keys[ks->count].data = p;
            ks->keys[ks->count].len = len;
            ks->count++;
        }
        p = nl + 1;
    }
    return 1;
}

void keyset_free(keyset_t *ks)
{
    free(ks->keys);
    free(ks->arena);
    memset(ks, 0, sizeof(*ks));
}

/* Keep only keys that are members of the first n inserted keys. */
void keyset_restrict(keyset_t *ks, const bf_key_t *inserted, size_t n)
{
    baseline_t *bl = baseline_create();
    if (!bl) return;
    for (size_t i = 0; i < n; i++) baseline_insert(bl, &inserted[i]);
    size_t w = 0;
    for (size_t i = 0; i < ks->count; i++) {
        if (baseline_lookup(bl, &ks->keys[i]) == BF_PRESENT) ks->keys[w++] = ks->keys[i];
    }
    ks->count = w;
    baseline_destroy(bl);
}

int dataset_load(const char *type, size_t n_override, const char *who, dataset_t *ds)
{
    char path[512];
    memset(ds, 0, sizeof(*ds));

    snprintf(path, sizeof(path), "data/%s/insert.txt", type);
    if (!keyset_load(path, &ds->ins)) goto fail;
    snprintf(path, sizeof(path), "data/%s/exist.txt", type);
    if (!keyset_load(path, &ds->ex)) goto fail;
    snprintf(path, sizeof(path), "data/%s/inexist.txt", type);
    if (!keyset_load(path, &ds->inex)) goto fail;

    ds->n = ds->ins.count;
    if (n_override > 0 && n_override < ds->n) {
        ds->n = n_override;
        /* Dexist must be a subset of what was actually inserted (protocol 4.3.2) */
        keyset_restrict(&ds->ex, ds->ins.keys, ds->n);
        fprintf(stderr, "%s: n truncated to %zu, Dexist restricted to %zu keys\n",
                who, ds->n, ds->ex.count);
    }
    if (ds->n == 0) {
        fprintf(stderr, "%s: empty insert set\n", who);
        dataset_free(ds);
        return 0;
    }
    return 1;

fail:
    fprintf(stderr, "%s: cannot open %s (run make data)\n", who, path);
    dataset_free(ds);
    return 0;
}

void dataset_free(dataset_t *ds)
{
    keyset_free(&ds->ins);
    keyset_free(&ds->ex);
    keyset_free(&ds->inex);
    ds->n = 0;
}
