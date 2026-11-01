#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cbf.h"
#include "common.h"

static void test_no_false_negatives(void)
{
    cbf_t *bf = cbf_create(8192, 3);
    assert(bf != NULL);

    const size_t n = 1000;
    char buf[32];
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "key-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        cbf_insert(bf, &k);
    }
    /* all inserted must be present */
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "key-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        assert(cbf_lookup(bf, &k) == BF_PRESENT);
    }

    cbf_destroy(bf);
}

static void test_delete(void)
{
    cbf_t *bf = cbf_create(16384, 3);
    assert(bf != NULL);

    const size_t n = 500;
    char buf[32];
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "del-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        cbf_insert(bf, &k);
    }
    /* delete the first half */
    for (size_t i = 0; i < n / 2; i++) {
        int len = snprintf(buf, sizeof(buf), "del-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        cbf_delete(bf, &k);
    }
    /* remaining half must still be present (no false negative) */
    for (size_t i = n / 2; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "del-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        assert(cbf_lookup(bf, &k) == BF_PRESENT);
    }
    /* deleted keys should be absent (a few may be FP, so count and allow) */
    size_t still_present = 0;
    for (size_t i = 0; i < n / 2; i++) {
        int len = snprintf(buf, sizeof(buf), "del-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        if (cbf_lookup(bf, &k) == BF_PRESENT) still_present++;
    }
    printf("cbf: after deleting %zu keys, %zu still reported present (collisions)\n",
           n / 2, still_present);
    /* sparse filter: deletions should almost always clear the counters */
    assert(still_present < n / 10);

    cbf_destroy(bf);
}

static void test_saturation(void)
{
    cbf_t *bf = cbf_create(64, 1);
    assert(bf != NULL);

    const char *key = "same-key";
    bf_key_t k = { key, strlen(key) };

    /* insert well past the 4-bit max; must not wrap around */
    for (int i = 0; i < 40; i++) {
        cbf_insert(bf, &k);
    }
    assert(cbf_lookup(bf, &k) == BF_PRESENT);

    /* deleting a saturated entry must not zero it (no false negative) */
    cbf_delete(bf, &k);
    assert(cbf_lookup(bf, &k) == BF_PRESENT);

    cbf_destroy(bf);
}

int main(void)
{
    test_no_false_negatives();
    test_delete();
    test_saturation();
    printf("cbf: all tests passed\n");
    return 0;
}
