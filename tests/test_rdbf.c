#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rdbf.h"
#include "common.h"

static int is_prime(unsigned long n)
{
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (unsigned long d = 3; d <= n / d; d += 2) {
        if (n % d == 0) return 0;
    }
    return 1;
}

static void test_dimensions_prime(void)
{
    rdbf_t *bf = rdbf_create(100000);
    assert(bf != NULL);
    unsigned long p1 = rdbf_dim_x(bf), p2 = rdbf_dim_y(bf);
    printf("rdbf: dims = %lu x %lu\n", p1, p2);
    assert(is_prime(p1));
    assert(is_prime(p2));
    assert((size_t)p1 * (size_t)p2 >= 100000);
    assert(rdbf_memory_bytes(bf) * 8 >= (size_t)p1 * (size_t)p2);
    rdbf_destroy(bf);
}

static void test_no_false_negatives(void)
{
    rdbf_t *bf = rdbf_create(200000);
    assert(bf != NULL);

    const size_t n = 1000;
    char buf[32];
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "key-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        rdbf_insert(bf, &k);
    }
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "key-%zu", i);
        bf_key_t k = { buf, (size_t)len };
        assert(rdbf_lookup(bf, &k) == BF_PRESENT);
    }
    rdbf_destroy(bf);
}

static void test_false_positive_rate(void)
{
    /* one bit per key: FPR ~ n / m */
    const size_t m = 100000;
    rdbf_t *bf = rdbf_create(m);
    assert(bf != NULL);

    const size_t n = 10000;
    char buf[32];
    for (size_t i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "in-%zu", i);
        bf_key_t kk = { buf, (size_t)len };
        rdbf_insert(bf, &kk);
    }
    size_t fp = 0;
    const size_t probes = 10000;
    for (size_t i = 0; i < probes; i++) {
        int len = snprintf(buf, sizeof(buf), "out-%zu", i);
        bf_key_t kk = { buf, (size_t)len };
        if (rdbf_lookup(bf, &kk) == BF_PRESENT) fp++;
    }
    double fpr = 100.0 * (double)fp / (double)probes;
    printf("rdbf: FPR observada = %.3f%% (%zu/%zu)\n", fpr, fp, probes);
    assert(fpr < 20.0);
    rdbf_destroy(bf);
}

int main(void)
{
    test_dimensions_prime();
    test_no_false_negatives();
    test_false_positive_rate();
    printf("rdbf: all tests passed\n");
    return 0;
}
