#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sbf.h"
#include "common.h"

static void test_no_false_negatives(void)
{
    sbf_t *bf = sbf_create(1024, 3);
    assert(bf != NULL);

    const char *keys[] = {"a", "bb", "ccc", "dddd", "eeeee", "ffffff"};
    const size_t n = sizeof(keys) / sizeof(keys[0]);

    for (size_t i = 0; i < n; i++) {
        bf_key_t k = { keys[i], strlen(keys[i]) };
        sbf_insert(bf, &k);
    }

    /* all inserted must be present (no false negative) */
    for (size_t i = 0; i < n; i++) {
        bf_key_t k = { keys[i], strlen(keys[i]) };
        assert(sbf_lookup(bf, &k) == BF_PRESENT);
    }

    sbf_destroy(bf);
}

static void test_fp_rate_small(void)
{
    /* small example: expect some FPs possible, but ensure sanity */
    sbf_t *bf = sbf_create(128, 2);
    assert(bf != NULL);

    const char *keys[] = {"item1", "item2", "item3", "item4"};
    const size_t n = sizeof(keys) / sizeof(keys[0]);

    for (size_t i = 0; i < n; i++) {
        bf_key_t k = { keys[i], strlen(keys[i]) };
        sbf_insert(bf, &k);
    }

    /* inserted are present */
    for (size_t i = 0; i < n; i++) {
        bf_key_t k = { keys[i], strlen(keys[i]) };
        assert(sbf_lookup(bf, &k) == BF_PRESENT);
    }

    /* some definitely not inserted */
    const char *bad[] = {"zzz", "qqq", "xxx", "yyy"};
    const size_t bn = sizeof(bad) / sizeof(bad[0]);
    for (size_t i = 0; i < bn; i++) {
        bf_key_t k = { bad[i], strlen(bad[i]) };
        /* may be FP, but never FN — do not assert ABSENT */
        (void)sbf_lookup(bf, &k);
    }

    sbf_destroy(bf);
}

int main(void)
{
    test_no_false_negatives();
    test_fp_rate_small();
    printf("sbf: all tests passed\n");
    return 0;
}
