#define _GNU_SOURCE /* O_DIRECT, pread, posix_memalign */
#include "store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "murmurhash2.h"

static const char STORE_MAGIC[8] = { 'B', 'F', 'S', 'S', 'T', '0', '1', '\0' };

typedef struct {
    char magic[8];
    uint64_t records;
    uint64_t blocks;        /* data blocks */
    uint64_t value_size;
    uint64_t fingerprint;
    uint64_t index_offset;  /* bytes, multiple of STORE_BLOCK */
    uint64_t index_bytes;   /* payload bytes of the index region */
} store_header_t;

struct store {
    int fd;
    store_header_t hdr;
    unsigned char *index_raw;  /* index region as read from disk */
    size_t index_alloc;
    const unsigned char **first_key; /* per block: pointer into index_raw */
    uint16_t *first_len;
    unsigned char *block;      /* STORE_BLOCK-aligned read buffer */
    store_stats_t stats;
};

/* ---- helpers ---- */

static int key_cmp(const void *a, size_t alen, const void *b, size_t blen)
{
    size_t n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);
    if (c != 0) return c;
    return (alen > blen) - (alen < blen);
}

static int key_cmp_qsort(const void *pa, const void *pb)
{
    const bf_key_t *a = *(const bf_key_t *const *)pa;
    const bf_key_t *b = *(const bf_key_t *const *)pb;
    return key_cmp(a->data, a->len, b->data, b->len);
}

static size_t round_block(size_t x)
{
    return (x + STORE_BLOCK - 1) / STORE_BLOCK * STORE_BLOCK;
}

static int pread_full(int fd, void *buf, size_t len, off_t off)
{
    size_t done = 0;
    while (done < len) {
        ssize_t r = pread(fd, (char *)buf + done, len - done, off + (off_t)done);
        if (r < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (r == 0) return 0;
        done += (size_t)r;
    }
    return 1;
}

static void *aligned_zalloc(size_t len)
{
    void *p = NULL;
    if (posix_memalign(&p, STORE_BLOCK, len ? len : STORE_BLOCK) != 0) return NULL;
    memset(p, 0, len ? len : STORE_BLOCK);
    return p;
}

uint64_t store_fingerprint_init(size_t n)
{
    return 0x9E3779B97F4A7C15ULL * (uint64_t)n;
}

uint64_t store_fingerprint_add(uint64_t sum, const bf_key_t *keys, size_t n)
{
    for (size_t i = 0; i < n; i++) sum += murmurhash64a(keys[i].data, keys[i].len, 0x51ED270BULL);
    return sum;
}

uint64_t store_fingerprint(const bf_key_t *keys, size_t n)
{
    return store_fingerprint_add(store_fingerprint_init(n), keys, n);
}

/* ---- build ---- */

int store_build(const char *path, const bf_key_t *keys, size_t n, size_t value_size,
                uint64_t fingerprint)
{
    if (!path || (!keys && n > 0)) return 0;

    const bf_key_t **sorted = (const bf_key_t **)malloc((n ? n : 1) * sizeof(*sorted));
    unsigned char *block = (unsigned char *)malloc(STORE_BLOCK);
    unsigned char *value = (unsigned char *)malloc(value_size ? value_size : 1);
    size_t idx_cap = 4096, idx_len = 0;
    unsigned char *idx = (unsigned char *)malloc(idx_cap);
    char tmp[1024];
    FILE *f = NULL;
    int ok = 0;
    if (!sorted || !block || !value || !idx) goto out;

    for (size_t i = 0; i < n; i++) sorted[i] = &keys[i];
    qsort(sorted, n, sizeof(*sorted), key_cmp_qsort);
    for (size_t i = 0; i < value_size; i++) value[i] = (unsigned char)('a' + i % 26);

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) goto out;

    /* placeholder header block */
    memset(block, 0, STORE_BLOCK);
    if (fwrite(block, 1, STORE_BLOCK, f) != STORE_BLOCK) goto out;

    uint64_t blocks = 0;
    size_t off = 0;
    memset(block, 0, STORE_BLOCK);
    for (size_t i = 0; i < n; i++) {
        const bf_key_t *k = sorted[i];
        if (i > 0 && key_cmp(sorted[i - 1]->data, sorted[i - 1]->len, k->data, k->len) == 0) {
            fprintf(stderr, "store: duplicate key in input\n");
            goto out;
        }
        size_t rec = 2 + k->len + value_size;
        if (k->len == 0 || k->len > 0xFFFF || rec > STORE_BLOCK - 2) {
            fprintf(stderr, "store: record of %zu bytes does not fit a block\n", rec);
            goto out;
        }
        if (off + rec > STORE_BLOCK) {             /* flush full block */
            if (fwrite(block, 1, STORE_BLOCK, f) != STORE_BLOCK) goto out;
            blocks++;
            off = 0;
            memset(block, 0, STORE_BLOCK);
        }
        if (off == 0) {                              /* first key of a block -> index */
            if (idx_len + 2 + k->len > idx_cap) {
                while (idx_len + 2 + k->len > idx_cap) idx_cap *= 2;
                unsigned char *t = (unsigned char *)realloc(idx, idx_cap);
                if (!t) goto out;
                idx = t;
            }
            uint16_t l16 = (uint16_t)k->len;
            memcpy(idx + idx_len, &l16, 2);
            memcpy(idx + idx_len + 2, k->data, k->len);
            idx_len += 2 + k->len;
        }
        uint16_t l16 = (uint16_t)k->len;
        memcpy(block + off, &l16, 2);
        memcpy(block + off + 2, k->data, k->len);
        memcpy(block + off + 2 + k->len, value, value_size);
        off += rec;
    }
    if (off > 0) {
        if (fwrite(block, 1, STORE_BLOCK, f) != STORE_BLOCK) goto out;
        blocks++;
    }

    /* sparse index region, padded to a whole block */
    size_t idx_region = round_block(idx_len);
    if (idx_len && fwrite(idx, 1, idx_len, f) != idx_len) goto out;
    memset(block, 0, STORE_BLOCK);
    for (size_t pad = idx_region - idx_len; pad > 0; ) {
        size_t w = pad < STORE_BLOCK ? pad : STORE_BLOCK;
        if (fwrite(block, 1, w, f) != w) goto out;
        pad -= w;
    }

    store_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, STORE_MAGIC, sizeof(STORE_MAGIC));
    hdr.records = n;
    hdr.blocks = blocks;
    hdr.value_size = value_size;
    hdr.fingerprint = fingerprint;
    hdr.index_offset = (uint64_t)STORE_BLOCK * (1 + blocks);
    hdr.index_bytes = idx_len;
    if (fseek(f, 0, SEEK_SET) != 0) goto out;
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1) goto out;
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) goto out;
    if (fclose(f) != 0) { f = NULL; goto out; }
    f = NULL;
    if (rename(tmp, path) != 0) goto out;
    ok = 1;

out:
    if (f) fclose(f);
    if (!ok) remove(tmp);
    free(sorted);
    free(block);
    free(value);
    free(idx);
    return ok;
}

int store_matches(const char *path, size_t n, size_t value_size, uint64_t fingerprint)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    store_header_t hdr;
    int ok = fread(&hdr, sizeof(hdr), 1, f) == 1 &&
             memcmp(hdr.magic, STORE_MAGIC, sizeof(STORE_MAGIC)) == 0 &&
             hdr.records == n && hdr.value_size == value_size &&
             hdr.fingerprint == fingerprint;
    fclose(f);
    return ok;
}

/* ---- open / close ---- */

store_t *store_open(const char *path, int direct)
{
    store_t *st = (store_t *)calloc(1, sizeof(store_t));
    if (!st) return NULL;
    st->fd = open(path, O_RDONLY | (direct ? O_DIRECT : 0));
    if (st->fd < 0) { free(st); return NULL; }

    st->block = (unsigned char *)aligned_zalloc(STORE_BLOCK);
    if (!st->block || !pread_full(st->fd, st->block, STORE_BLOCK, 0)) goto fail;
    memcpy(&st->hdr, st->block, sizeof(st->hdr));
    if (memcmp(st->hdr.magic, STORE_MAGIC, sizeof(STORE_MAGIC)) != 0) goto fail;

    st->index_alloc = round_block((size_t)st->hdr.index_bytes);
    st->index_raw = (unsigned char *)aligned_zalloc(st->index_alloc);
    if (!st->index_raw) goto fail;
    if (st->index_alloc &&
        !pread_full(st->fd, st->index_raw, st->index_alloc, (off_t)st->hdr.index_offset)) goto fail;

    size_t nb = (size_t)st->hdr.blocks;
    st->first_key = (const unsigned char **)malloc((nb ? nb : 1) * sizeof(*st->first_key));
    st->first_len = (uint16_t *)malloc((nb ? nb : 1) * sizeof(*st->first_len));
    if (!st->first_key || !st->first_len) goto fail;
    size_t off = 0;
    for (size_t b = 0; b < nb; b++) {
        if (off + 2 > st->hdr.index_bytes) goto fail;
        uint16_t l16;
        memcpy(&l16, st->index_raw + off, 2);
        st->first_len[b] = l16;
        st->first_key[b] = st->index_raw + off + 2;
        off += 2 + l16;
    }
    return st;

fail:
    store_close(st);
    return NULL;
}

void store_close(store_t *st)
{
    if (!st) return;
    if (st->fd >= 0) close(st->fd);
    free(st->block);
    free(st->index_raw);
    free(st->first_key);
    free(st->first_len);
    free(st);
}

/* ---- lookup ---- */

int store_get(store_t *st, const bf_key_t *key)
{
    if (!st || !key || st->hdr.blocks == 0) return 0;

    /* last block whose first key <= key (block 0 when key is smaller than all) */
    size_t lo = 0, hi = (size_t)st->hdr.blocks;
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (key_cmp(st->first_key[mid], st->first_len[mid], key->data, key->len) <= 0) lo = mid;
        else hi = mid;
    }

    off_t pos = (off_t)STORE_BLOCK * (off_t)(1 + lo);
    st->stats.reads++;
    st->stats.bytes += STORE_BLOCK;
    if (!pread_full(st->fd, st->block, STORE_BLOCK, pos)) return -1;

    size_t off = 0;
    size_t vs = (size_t)st->hdr.value_size;
    while (off + 2 <= STORE_BLOCK) {
        uint16_t l16;
        memcpy(&l16, st->block + off, 2);
        if (l16 == 0 || off + 2 + l16 > STORE_BLOCK) break;
        int c = key_cmp(st->block + off + 2, l16, key->data, key->len);
        if (c == 0) return 1;
        if (c > 0) break;                 /* records are sorted */
        off += 2 + l16 + vs;
    }
    return 0;
}

/* ---- stats ---- */

store_stats_t store_stats(const store_t *st)
{
    store_stats_t z = { 0, 0 };
    return st ? st->stats : z;
}

void store_reset_stats(store_t *st)
{
    if (st) { st->stats.reads = 0; st->stats.bytes = 0; }
}

size_t store_index_bytes(const store_t *st)
{
    if (!st) return 0;
    size_t nb = (size_t)st->hdr.blocks;
    return st->index_alloc + nb * (sizeof(*st->first_key) + sizeof(*st->first_len));
}

size_t store_records(const store_t *st)
{
    return st ? (size_t)st->hdr.records : 0;
}

size_t store_blocks(const store_t *st)
{
    return st ? (size_t)st->hdr.blocks : 0;
}
