#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baseline.h"
#include "keyset.h"

#define PATH "build/test_keyset.tmp"
#define N 5000

static void write_file(const char *contents)
{
    FILE *f = fopen(PATH, "wb");
    assert(f);
    fputs(contents, f);
    fclose(f);
}

static int key_is(const bf_key_t *k, const char *s)
{
    return k->len == strlen(s) && memcmp(k->data, s, k->len) == 0;
}

/* Every buffer size must see the same keys: batches split lines anywhere. */
static void same_keys_as_load(size_t buf_size)
{
    keyset_t all;
    assert(keyset_load(PATH, &all));
    keystream_t ks;
    assert(keystream_open(&ks, PATH, buf_size, 0));
    for (int pass = 0; pass < 2; pass++) {           /* second pass: rewind */
        size_t seen = 0, c;
        while ((c = keystream_next(&ks)) > 0) {
            for (size_t i = 0; i < c; i++, seen++) {
                assert(seen < all.count);
                assert(ks.keys[i].len == all.keys[seen].len);
                assert(memcmp(ks.keys[i].data, all.keys[seen].data, ks.keys[i].len) == 0);
            }
        }
        assert(!ks.error);
        assert(seen == all.count);
        keystream_rewind(&ks);
    }
    assert(keystream_count(&ks) == all.count);
    keystream_close(&ks);
    keyset_free(&all);
}

int main(void)
{
    /* line endings: CRLF, blank lines, no newline at the end */
    write_file("alpha\r\n\nbeta\n\n\r\ngamma");
    keystream_t ks;
    assert(keystream_open(&ks, PATH, 8, 0));
    const char *want[] = { "alpha", "beta", "gamma" };
    size_t seen = 0, c;
    while ((c = keystream_next(&ks)) > 0)
        for (size_t i = 0; i < c; i++) assert(key_is(&ks.keys[i], want[seen++]));
    assert(seen == 3 && !ks.error);
    keystream_close(&ks);

    /* a line that does not fit the buffer is an error, not a truncated key */
    write_file("short\nmuch-longer-than-the-buffer\n");
    assert(keystream_open(&ks, PATH, 8, 0));
    while (keystream_next(&ks) > 0) { }
    assert(ks.error);
    keystream_close(&ks);

    /* large file across several buffer sizes */
    FILE *f = fopen(PATH, "wb");
    assert(f);
    for (int i = 0; i < N; i++) fprintf(f, "key-%06d\n", i);
    fclose(f);
    same_keys_as_load(16);
    same_keys_as_load(4096);
    same_keys_as_load(KEYSTREAM_BUF);

    /* limit: stops mid-batch, and again after a rewind */
    assert(keystream_open(&ks, PATH, 4096, 1234));
    assert(keystream_count(&ks) == 1234);
    assert(keystream_count(&ks) == 1234);
    keystream_close(&ks);

    /* restriction to a set */
    baseline_t *only = baseline_create();
    assert(only);
    bf_key_t k = { "key-000042", 10 };
    baseline_insert(only, &k);
    assert(keystream_open(&ks, PATH, 4096, 0));
    ks.only = only;
    assert(keystream_next(&ks) == 1);
    assert(key_is(&ks.keys[0], "key-000042"));
    assert(keystream_next(&ks) == 0 && !ks.error);
    keystream_close(&ks);
    baseline_destroy(only);

    remove(PATH);
    printf("keyset: all tests passed\n");
    return 0;
}
