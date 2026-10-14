#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "baseline.h"
#include "common.h"

static void test_basic(void)
{
    baseline_t *bl = baseline_create();
    assert(bl != NULL);

    const char *keys[] = {"alpha", "beta", "gamma", "delta"};
    const size_t n = sizeof(keys) / sizeof(keys[0]);

    for (size_t i = 0; i < n; i++) {
        bf_key_t k = { keys[i], strlen(keys[i]) };
        baseline_insert(bl, &k);
    }

    /* all inserted must be present */
    for (size_t i = 0; i < n; i++) {
        bf_key_t k = { keys[i], strlen(keys[i]) };
        assert(baseline_lookup(bl, &k) == BF_PRESENT);
    }

    /* none inserted must be absent */
    const char *bad[] = {"omega", "zeta", "eta"};
    const size_t bn = sizeof(bad) / sizeof(bad[0]);
    for (size_t i = 0; i < bn; i++) {
        bf_key_t k = { bad[i], strlen(bad[i]) };
        assert(baseline_lookup(bl, &k) == BF_ABSENT);
    }

    baseline_destroy(bl);
}

int main(void)
{
    test_basic();
    printf("baseline: all tests passed\n");
    return 0;
}
