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

/* every partition starts on its own 64-byte cache line; padding is counted */
static void test_alignment(void)
{
    const size_t ms[] = { 7, 100, 19171, 100000, 958506 };
    const unsigned ks[] = { 1, 3, 7, 10 };
    for (size_t a = 0; a < sizeof(ms) / sizeof(ms[0]); a++) {
        for (size_t b = 0; b < sizeof(ks) / sizeof(ks[0]); b++) {
            pbf_t *bf = pbf_create(ms[a], ks[b]);
            assert(bf != NULL);
            size_t stride = pbf_partition_stride(bf);
            size_t part_bytes = (pbf_cells(bf) / ks[b] + 7) / 8;
            assert(stride % 64 == 0);
            assert(stride >= part_bytes && stride - part_bytes < 64);
            assert(pbf_memory_bytes(bf) == stride * ks[b]);
            pbf_destroy(bf);
        }
    }
}

/* pbf_params reaches the target and is minimal (one bit less per partition misses it) */
static void test_params(void)
{
    const size_t ns[] = { 1000, 100000, 10000000 };
    const double ps[] = { 0.1, 0.01, 0.001 };
    for (size_t a = 0; a < 3; a++) {
        for (size_t b = 0; b < 3; b++) {
            bf_sizing_t sz;
            assert(pbf_params(ns[a], ps[b], &sz));
            pbf_t *bf = pbf_create(sz.m, sz.k);
            assert(bf != NULL);
            assert(pbf_fpr_theory(bf, ns[a]) <= ps[b]);
            pbf_destroy(bf);
            bf = pbf_create(sz.m - sz.k, sz.k);
            assert(pbf_fpr_theory(bf, ns[a]) > ps[b]);
            pbf_destroy(bf);
        }
    }
}

int main(void)
{
    test_alignment();
    test_params();
    test_no_false_negatives();
    test_false_positive_rate();
    test_degenerate();
    printf("pbf: all tests passed\n");
    return 0;
}
