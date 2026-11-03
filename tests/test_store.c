#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store.h"

#define N 20000

static char keybuf[N][24];
static bf_key_t keys[N];

static void make_keys(void)
{
    for (int i = 0; i < N; i++) {
        /* shuffled insertion order: build must sort */
        int v = (int)(((unsigned)i * 7919u) % N);
        int len = snprintf(keybuf[i], sizeof(keybuf[i]), "k%07d", v);
        keys[i].data = keybuf[i];
        keys[i].len = (size_t)len;
    }
}

static void check(const char *path, int direct)
{
    store_t *st = store_open(path, direct);
    if (!st) {
        printf("store: O_DIRECT indisponivel neste filesystem, pulando modo direct\n");
        assert(direct);
        return;
    }
    assert(store_records(st) == N);
    char buf[24];
    for (int i = 0; i < N; i++) {
        int len = snprintf(buf, sizeof(buf), "k%07d", i);
        bf_key_t k = { buf, (size_t)len };
        assert(store_get(st, &k) == 1);
    }
    for (int i = N; i < 2 * N; i++) {
        int len = snprintf(buf, sizeof(buf), "k%07d", i);
        bf_key_t k = { buf, (size_t)len };
        assert(store_get(st, &k) == 0);
    }
    const char *edge[] = { "", "a", "k", "k0000000x", "z" };
    for (size_t i = 0; i < sizeof(edge) / sizeof(edge[0]); i++) {
        bf_key_t k = { edge[i], strlen(edge[i]) };
        assert(store_get(st, &k) == 0);
    }
    store_stats_t s = store_stats(st);
    assert(s.reads == 2 * N + sizeof(edge) / sizeof(edge[0]));  /* exactly one read per get */
    assert(s.bytes == s.reads * STORE_BLOCK);
    printf("store(%s): %zu registros em %zu blocos, indice %zu B, %llu leituras OK\n",
           direct ? "direct" : "cached", store_records(st), store_blocks(st),
           store_index_bytes(st), (unsigned long long)s.reads);
    store_close(st);
}

int main(void)
{
    const char *path = "build/test_store.sst";
    make_keys();
    uint64_t fp = store_fingerprint(keys, N);
    assert(store_build(path, keys, N, 100, fp));
    assert(store_matches(path, N, 100, fp));
    assert(!store_matches(path, N, 101, fp));
    assert(!store_matches(path, N, 100, fp + 1));

    check(path, 0);
    check(path, 1);

    /* duplicates are rejected */
    bf_key_t dup[2] = { { "x", 1 }, { "x", 1 } };
    assert(!store_build("build/test_store_dup.sst", dup, 2, 10, 0));

    remove(path);
    printf("store: all tests passed\n");
    return 0;
}
