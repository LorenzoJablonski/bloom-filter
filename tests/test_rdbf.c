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

/* X, Y, beta pairwise distinct primes; capacity and memory consistent */
static void test_dimensions_prime(void)
{
    const size_t ms[] = { 1, 61, 62, 1000, 100000, 958506, 100000000 };
    for (size_t a = 0; a < sizeof(ms) / sizeof(ms[0]); a++) {
        rdbf_t *bf = rdbf_create(ms[a]);
        assert(bf != NULL);
        unsigned long x = rdbf_dim_x(bf), y = rdbf_dim_y(bf);
        unsigned long beta = rdbf_beta();
        assert(is_prime(x) && is_prime(y) && is_prime(beta));
        assert(x != y && x != beta && y != beta);
        assert(rdbf_cells(bf) == (size_t)x * y * beta);
        assert(rdbf_cells(bf) >= ms[a]);
        assert(rdbf_memory_bytes(bf) == (size_t)x * y * sizeof(unsigned long));
        if (ms[a] == 100000)
            printf("rdbf: m=100000 -> %lu x %lu celulas de 64 bits, beta=%lu, %zu bits uteis\n",
                   x, y, beta, rdbf_cells(bf));
        rdbf_destroy(bf);
    }
}

/* every cell and every one of the 61 bit positions is reachable (CRT) */
static void test_full_coverage(void)
{
    rdbf_t *bf = rdbf_create(61 * 35);   /* tiny: 5 x 7 cells */
    assert(bf != NULL);
    char buf[32];
    for (int i = 0; i < 200000; i++) {
        int len = snprintf(buf, sizeof(buf), "cov-%d", i);
        bf_key_t k = { buf, (size_t)len };
        rdbf_insert(bf, &k);
    }
    assert(rdbf_fill_ratio(bf) == 1.0);   /* would be ~1/x if x == y (diagonal only) */
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
    double th = 100.0 * rdbf_fpr_theory(bf, n);
    printf("rdbf: FPR teorica  = %.3f%%\n", th);
    assert(fpr > 0.8 * th && fpr < 1.2 * th);
    rdbf_destroy(bf);
}

int main(void)
{
    test_dimensions_prime();
    test_full_coverage();
    test_no_false_negatives();
    test_false_positive_rate();
    printf("rdbf: all tests passed\n");
    return 0;
}
