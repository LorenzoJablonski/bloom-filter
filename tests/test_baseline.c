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

/* keys inserted from a reused buffer must survive the buffer being overwritten */
static void test_key_ownership(void)
{
    baseline_t *bl = baseline_create();
    assert(bl != NULL);

    char buf[32];
    for (int i = 0; i < 1000; i++) {
        int len = snprintf(buf, sizeof(buf), "own-%d", i);
        bf_key_t k = { buf, (size_t)len };
        baseline_insert(bl, &k);
    }
    memset(buf, 'X', sizeof(buf));
    assert(baseline_count(bl) == 1000);

    for (int i = 0; i < 1000; i++) {
        int len = snprintf(buf, sizeof(buf), "own-%d", i);
        bf_key_t k = { buf, (size_t)len };
        assert(baseline_lookup(bl, &k) == BF_PRESENT);
    }
    for (int i = 1000; i < 2000; i++) {
        int len = snprintf(buf, sizeof(buf), "own-%d", i);
        bf_key_t k = { buf, (size_t)len };
        assert(baseline_lookup(bl, &k) == BF_ABSENT);
    }

    /* duplicates are not stored twice */
    int len = snprintf(buf, sizeof(buf), "own-%d", 7);
    bf_key_t dup = { buf, (size_t)len };
    baseline_insert(bl, &dup);
    assert(baseline_count(bl) == 1000);

    baseline_destroy(bl);
}

/* remove drops only the given key, updates the footprint and ignores absent keys */
static void test_remove(void)
{
    baseline_t *bl = baseline_create();
    assert(bl != NULL);
    char buf[32];
    for (int i = 0; i < 100; i++) {
        int len = snprintf(buf, sizeof(buf), "rm-%d", i);
        bf_key_t k = { buf, (size_t)len };
        baseline_insert(bl, &k);
    }
    size_t before = baseline_memory_bytes(bl);
    for (int i = 0; i < 50; i++) {
        int len = snprintf(buf, sizeof(buf), "rm-%d", i);
        bf_key_t k = { buf, (size_t)len };
        baseline_remove(bl, &k);
        baseline_remove(bl, &k); /* second time: absent, no effect */
    }
    assert(baseline_count(bl) == 50);
    assert(baseline_memory_bytes(bl) < before);
    for (int i = 0; i < 100; i++) {
        int len = snprintf(buf, sizeof(buf), "rm-%d", i);
        bf_key_t k = { buf, (size_t)len };
        assert(baseline_lookup(bl, &k) == (i < 50 ? BF_ABSENT : BF_PRESENT));
    }
    baseline_destroy(bl);
}

int main(void)
{
    test_basic();
    test_key_ownership();
    test_remove();
    printf("baseline: all tests passed\n");
    return 0;
}
