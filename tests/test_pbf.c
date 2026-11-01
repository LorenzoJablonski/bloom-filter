#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pbf.h"
#include "common.h"

static void test_no_false_negatives(void)
{
    pbf_t *bf = pbf_create(8192, 3);
    assert(bf != NULL);

    const size_t n = 1000;
    char buf[32];
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "key-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        pbf_insert(bf, &k);
    }
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "key-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        assert(pbf_lookup(bf, &k) == BF_PRESENT);
    }

    pbf_destroy(bf);
}

static void test_false_positive_rate(void)
{
    /* m/n = 10 bits per key, k = 3 -> theoretical FPR ~ 1% */
    const size_t m = 100000;
    const unsigned k = 3;
    pbf_t *bf = pbf_create(m, k);
    assert(bf != NULL);

    const size_t n = 10000;
    char buf[32];
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "in-%zu", i);
        bf_key_t kk = { buf, (size_t)len };
        pbf_insert(bf, &kk);
    }
    size_t fp = 0;
    const size_t probes = 10000;
    for (size_t i = 0; i < probes; i++) {
        int len = snprintf(buf, sizeof(buf), "out-%zu", i);
        bf_key_t kk = { buf, (size_t)len };
        if (pbf_lookup(bf, &kk) == BF_PRESENT) fp++;
    }
    double fpr = 100.0 * (double)fp / (double)probes;
    printf("pbf: FPR observada = %.3f%% (%zu/%zu)\n", fpr, fp, probes);
    assert(fpr < 5.0);

    pbf_destroy(bf);
}

static void test_degenerate(void)
{
    /* m < k: partition size clamped to 1 bit, must not crash */
    pbf_t *bf = pbf_create(2, 4);
    assert(bf != NULL);
    const char *key = "x";
    bf_key_t k = { key, strlen(key) };
    pbf_insert(bf, &k);
    assert(pbf_lookup(bf, &k) == BF_PRESENT);
    pbf_destroy(bf);
}

int main(void)
{
    test_no_false_negatives();
    test_false_positive_rate();
    test_degenerate();
    printf("pbf: all tests passed\n");
    return 0;
}
