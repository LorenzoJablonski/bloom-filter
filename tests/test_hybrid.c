#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hybrid.h"
#include "common.h"

static int is_prime(unsigned long n)
{
    if (n < 2) return 0;
    for (unsigned long d = 2; d * d <= n; d++) if (n % d == 0) return 0;
    return 1;
}

/* shard geometry: prime dims, product <= bound, cache-line aligned stride */
static void test_geometry(void)
{
    const size_t bounds[] = { 64, 512, 4096, 10000 };
    for (size_t b = 0; b < sizeof(bounds) / sizeof(bounds[0]); b++) {
        for (int counting = 0; counting <= 1; counting++) {
            hybrid_t *bf = hybrid_create(100000, 7, bounds[b], counting);
            assert(bf != NULL);
            unsigned long p1, p2;
            hybrid_shard_dims(bf, &p1, &p2);
            assert(is_prime(p1) && is_prime(p2) && p1 <= p2);
            assert(p1 * p2 <= bounds[b]);
            size_t stride = hybrid_shard_stride(bf);
            size_t need = counting ? (p1 * p2 + 1) / 2 : (p1 * p2 + 7) / 8;
            assert(stride % 64 == 0 && stride >= need && stride - need < 64);
            assert(hybrid_cells(bf) >= 100000);
            assert(hybrid_memory_bytes(bf) == hybrid_num_shards(bf) * stride);
            hybrid_destroy(bf);
        }
    }
    unsigned long p1, p2;
    hybrid_t *bf = hybrid_create(1000, 3, 512, 0);
    hybrid_shard_dims(bf, &p1, &p2);
    printf("hybrid: shard 512  -> %lu x %lu = %lu celulas, stride %zu B\n",
           p1, p2, p1 * p2, hybrid_shard_stride(bf));
    hybrid_destroy(bf);
    bf = hybrid_create(1000, 3, 4096, 0);
    hybrid_shard_dims(bf, &p1, &p2);
    printf("hybrid: shard 4096 -> %lu x %lu = %lu celulas, stride %zu B\n",
           p1, p2, p1 * p2, hybrid_shard_stride(bf));
    hybrid_destroy(bf);
}

static void test_no_false_negatives(void)
{
    char buf[32];
    for (int counting = 0; counting <= 1; counting++) {
        for (unsigned k = 1; k <= 12; k += 5) {
            hybrid_t *bf = hybrid_create(80000, k, 512, counting);
            assert(bf != NULL);
            for (int i = 0; i < 8000; i++) {
                int len = snprintf(buf, sizeof(buf), "key-%d", i);
                bf_key_t kk = { buf, (size_t)len };
                hybrid_insert(bf, &kk);
            }
            for (int i = 0; i < 8000; i++) {
                int len = snprintf(buf, sizeof(buf), "key-%d", i);
                bf_key_t kk = { buf, (size_t)len };
                assert(hybrid_lookup(bf, &kk) == BF_PRESENT);
            }
            hybrid_destroy(bf);
        }
    }
}

/* params reach p by the blocked formula, and one shard less does not */
static void test_params(void)
{
    const size_t ns[] = { 1000, 100000, 10000000 };
    const double ps[] = { 0.1, 0.01, 0.001 };
    const size_t bounds[] = { 512, 4096 };
    for (size_t a = 0; a < 3; a++)
        for (size_t b = 0; b < 3; b++)
            for (size_t c = 0; c < 2; c++) {
                bf_sizing_t sz;
                assert(hybrid_params(ns[a], ps[b], bounds[c], &sz));
                hybrid_t *bf = hybrid_create(sz.m, sz.k, bounds[c], 0);
                assert(bf != NULL);
                assert(hybrid_fpr_theory(bf, ns[a]) <= ps[b]);
                size_t cells = hybrid_cells(bf) / hybrid_num_shards(bf);
                size_t shards = hybrid_num_shards(bf);
                hybrid_destroy(bf);
                if (shards > 1) {
                    bf = hybrid_create(sz.m - cells, sz.k, bounds[c], 0);
                    assert(hybrid_fpr_theory(bf, ns[a]) > ps[b]);
                    hybrid_destroy(bf);
                }
            }
}

/* measured FPR agrees with the blocked formula */
static void test_fpr_matches_theory(void)
{
    const size_t n = 50000, probes = 200000;
    const size_t bounds[] = { 512, 4096 };
    char buf[32];
    for (size_t c = 0; c < 2; c++) {
        bf_sizing_t sz;
        assert(hybrid_params(n, 0.01, bounds[c], &sz));
        hybrid_t *bf = hybrid_create(sz.m, sz.k, bounds[c], 0);
        for (size_t i = 0; i < n; i++) {
            int len = snprintf(buf, sizeof(buf), "in-%zu", i);
            bf_key_t kk = { buf, (size_t)len };
            hybrid_insert(bf, &kk);
        }
        size_t fp = 0;
        for (size_t i = 0; i < probes; i++) {
            int len = snprintf(buf, sizeof(buf), "out-%zu", i);
            bf_key_t kk = { buf, (size_t)len };
            fp += hybrid_lookup(bf, &kk) == BF_PRESENT;
        }
        double fpr = (double)fp / (double)probes, th = hybrid_fpr_theory(bf, n);
        printf("hybrid_%zu: k=%u bits/chave=%.2f FPR medida=%.3f%% teorica=%.3f%%\n",
               bounds[c], sz.k, (double)hybrid_cells(bf) / (double)n, 100 * fpr, 100 * th);
        assert(fpr > 0.8 * th && fpr < 1.2 * th);
        hybrid_destroy(bf);
    }
}

/* counting variant: deleting half the keys never hides the other half */
static void test_delete(void)
{
    char buf[32];
    const int n = 20000;
    hybrid_t *bf = hybrid_create(200000, 7, 512, 1);
    for (int i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "d-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        hybrid_insert(bf, &kk);
    }
    for (int i = 0; i < n / 2; i++) {
        int len = snprintf(buf, sizeof(buf), "d-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        assert(hybrid_delete(bf, &kk) == 1);
    }
    size_t still = 0;
    for (int i = 0; i < n; i++) {
        int len = snprintf(buf, sizeof(buf), "d-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        int present = hybrid_lookup(bf, &kk) == BF_PRESENT;
        if (i >= n / 2) assert(present);   /* no false negatives */
        else still += present;             /* deleted: only FP-like residue */
    }
    assert(still < (size_t)(n / 2) / 20);
    printf("hybrid_c: apos deletar %d chaves, %zu ainda respondem presente; saturados=%zu\n",
           n / 2, still, hybrid_saturated(bf));
    hybrid_destroy(bf);

    /* bits variant refuses deletion */
    bf = hybrid_create(1000, 3, 512, 0);
    bf_key_t kk = { "x", 1 };
    hybrid_insert(bf, &kk);
    assert(hybrid_delete(bf, &kk) == 0);
    assert(hybrid_lookup(bf, &kk) == BF_PRESENT);
    hybrid_destroy(bf);
}

int main(void)
{
    test_geometry();
    test_no_false_negatives();
    test_params();
    test_fpr_matches_theory();
    test_delete();
    printf("hybrid: all tests passed\n");
    return 0;
}
