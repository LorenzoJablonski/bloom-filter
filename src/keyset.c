#define _POSIX_C_SOURCE 200809L
#include "keyset.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


/* ---- streaming ---- */

int keystream_open(keystream_t *ks, const char *path, size_t buf_size, size_t limit)
{
    memset(ks, 0, sizeof(*ks));
    ks->fd = open(path, O_RDONLY);
    if (ks->fd < 0) return 0;
    ks->cap = buf_size ? buf_size : KEYSTREAM_BUF;
    ks->limit = limit;
    ks->buf = (char *)malloc(ks->cap);
    if (!ks->buf) { close(ks->fd); ks->fd = -1; return 0; }
    return 1;
}

void keystream_close(keystream_t *ks)
{
    if (ks->buf) close(ks->fd);
    free(ks->buf);
    free(ks->keys);
    memset(ks, 0, sizeof(*ks));
    ks->fd = -1;
}

void keystream_rewind(keystream_t *ks)
{
    if (lseek(ks->fd, 0, SEEK_SET) != 0) ks->error = 1;
    ks->have = ks->used = ks->file_pos = ks->advised = ks->delivered = ks->count = 0;
    ks->eof = 0;
}

/* Keep the partial line and fill the rest of the buffer from the file. */
static int refill(keystream_t *ks)
{
    size_t tail = ks->have - ks->used;
    if (tail && ks->used) memmove(ks->buf, ks->buf + ks->used, tail);
    ks->have = tail;
    ks->used = 0;
    while (!ks->eof && ks->have < ks->cap) {
        ssize_t got = read(ks->fd, ks->buf + ks->have, ks->cap - ks->have);
        if (got < 0) return 0;
        if (got == 0) ks->eof = 1;
        ks->have += (size_t)got;
        ks->file_pos += (size_t)got;
    }
    /* What was read must not compete with the structure under test for RAM. */
    size_t start = ks->advised & ~(size_t)4095;
    (void)posix_fadvise(ks->fd, (off_t)start, (off_t)(ks->file_pos - start), POSIX_FADV_DONTNEED);
    ks->advised = ks->file_pos;
    return 1;
}

static int push_key(keystream_t *ks, char *p, size_t len)
{
    if (len > 0 && p[len - 1] == '\r') len--;
    if (len == 0) return 1;
    if (ks->count == ks->keys_cap) {
        size_t cap = ks->keys_cap ? ks->keys_cap * 2 : 4096;
        bf_key_t *grown = (bf_key_t *)realloc(ks->keys, cap * sizeof(*grown));
        if (!grown) return 0;
        ks->keys = grown;
        ks->keys_cap = cap;
    }
    bf_key_t key = { p, len };
    if (ks->only && baseline_lookup(ks->only, &key) != BF_PRESENT) return 1;
    ks->keys[ks->count++] = key;
    return 1;
}

size_t keystream_next(keystream_t *ks)
{
    ks->count = 0;
    while (!ks->error && !ks->count) {
        size_t room = ks->limit ? ks->limit - ks->delivered : (size_t)-1;
        if (room == 0) return 0;
        if (!refill(ks)) { ks->error = 1; break; }
        if (ks->have == 0) return 0;

        char *p = ks->buf, *end = ks->buf + ks->have;
        while (p < end && ks->count < room) {
            char *nl = (char *)memchr(p, '\n', (size_t)(end - p));
            if (!nl && !ks->eof) break;                 /* partial line: next read */
            size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
            if (!push_key(ks, p, len)) { ks->error = 1; break; }
            p = nl ? nl + 1 : end;
        }
        ks->used = (size_t)(p - ks->buf);
        if (ks->used == 0 && !ks->eof && ks->have == ks->cap) {
            fprintf(stderr, "keystream: line longer than the %zu-byte buffer\n", ks->cap);
            ks->error = 1;
        }
        if (ks->eof && ks->used == ks->have && !ks->count) return 0;
    }
    ks->delivered += ks->count;
    return ks->error ? 0 : ks->count;
}

size_t keystream_count(keystream_t *ks)
{
    size_t total = 0, c;
    keystream_rewind(ks);
    while ((c = keystream_next(ks)) > 0) total += c;
    keystream_rewind(ks);
    return total;
}

/* ---- whole file in memory ---- */

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

/* ---- protocol partitions ---- */

int dataset_open(const char *type, size_t n_override, const char *who, dataset_t *ds)
{
    char path[512];
    memset(ds, 0, sizeof(*ds));
    ds->ins.fd = ds->ex.fd = ds->inex.fd = -1;

    snprintf(path, sizeof(path), "data/%s/insert.txt", type);
    if (!keystream_open(&ds->ins, path, KEYSTREAM_BUF, 0)) goto missing;
    snprintf(path, sizeof(path), "data/%s/exist.txt", type);
    if (!keystream_open(&ds->ex, path, KEYSTREAM_BUF, 0)) goto missing;
    snprintf(path, sizeof(path), "data/%s/inexist.txt", type);
    if (!keystream_open(&ds->inex, path, KEYSTREAM_BUF, 0)) goto missing;

    ds->n = keystream_count(&ds->ins);
    if (n_override > 0 && n_override < ds->n) {
        ds->n = n_override;
        ds->ins.limit = n_override;
        /* Dexist must be a subset of what was actually inserted (protocol 4.3.2) */
        ds->restrict_set = baseline_create();
        if (!ds->restrict_set) { dataset_close(ds); return 0; }
        size_t c;
        while ((c = keystream_next(&ds->ins)) > 0)
            for (size_t i = 0; i < c; i++) baseline_insert(ds->restrict_set, &ds->ins.keys[i]);
        keystream_rewind(&ds->ins);
        ds->ex.only = ds->restrict_set;
    }
    ds->n_exist = keystream_count(&ds->ex);
    ds->n_inexist = keystream_count(&ds->inex);
    if (n_override > 0 && ds->restrict_set)
        fprintf(stderr, "%s: n truncated to %zu, Dexist restricted to %zu keys "
                "(kept in RAM: use a dataset of this size to measure paging)\n",
                who, ds->n, ds->n_exist);
    if (ds->ins.error || ds->ex.error || ds->inex.error) {
        fprintf(stderr, "%s: error reading the dataset\n", who);
        dataset_close(ds);
        return 0;
    }
    if (ds->n == 0) {
        fprintf(stderr, "%s: empty insert set\n", who);
        dataset_close(ds);
        return 0;
    }
    return 1;

missing:
    fprintf(stderr, "%s: cannot open %s (run make data)\n", who, path);
    dataset_close(ds);
    return 0;
}

void dataset_close(dataset_t *ds)
{
    if (ds->ins.buf) keystream_close(&ds->ins);
    if (ds->ex.buf) keystream_close(&ds->ex);
    if (ds->inex.buf) keystream_close(&ds->inex);
    if (ds->restrict_set) baseline_destroy(ds->restrict_set);
    ds->restrict_set = NULL;
    ds->n = ds->n_exist = ds->n_inexist = 0;
}
