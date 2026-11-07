#include "bf.h"

#include <string.h>

#include "sbf.h"
#include "cbf.h"
#include "pbf.h"
#include "rdbf.h"
#include "hybrid.h"
#include "baseline.h"

/* ---- SBF ---- */
static void *sbf_ad_create(size_t m, unsigned k) { return sbf_create(m, k); }
static void sbf_ad_destroy(void *bf) { sbf_destroy((sbf_t *)bf); }
static void sbf_ad_insert(void *bf, const bf_key_t *k) { sbf_insert((sbf_t *)bf, k); }
static unsigned sbf_ad_insert_counted(void *bf, const bf_key_t *k) { return sbf_insert_counted((sbf_t *)bf, k); }
static bf_member_t sbf_ad_lookup(void *bf, const bf_key_t *k) { return sbf_lookup((sbf_t *)bf, k); }
static size_t sbf_ad_memory(const void *bf) { return sbf_memory_bytes((const sbf_t *)bf); }
static size_t sbf_ad_cells(const void *bf) { return sbf_cells((const sbf_t *)bf); }
static double sbf_ad_fpr(const void *bf, size_t n) { return sbf_fpr_theory((const sbf_t *)bf, n); }
static double sbf_ad_fill(const void *bf) { return sbf_fill_ratio((const sbf_t *)bf); }

/* ---- CBF ---- */
static void *cbf_ad_create(size_t m, unsigned k) { return cbf_create(m, k); }
static void cbf_ad_destroy(void *bf) { cbf_destroy((cbf_t *)bf); }
static void cbf_ad_insert(void *bf, const bf_key_t *k) { cbf_insert((cbf_t *)bf, k); }
static unsigned cbf_ad_insert_counted(void *bf, const bf_key_t *k) { return cbf_insert_counted((cbf_t *)bf, k); }
static bf_member_t cbf_ad_lookup(void *bf, const bf_key_t *k) { return cbf_lookup((cbf_t *)bf, k); }
static void cbf_ad_remove(void *bf, const bf_key_t *k) { cbf_delete((cbf_t *)bf, k); }
static size_t cbf_ad_memory(const void *bf) { return cbf_memory_bytes((const cbf_t *)bf); }
static size_t cbf_ad_cells(const void *bf) { return cbf_cells((const cbf_t *)bf); }
static double cbf_ad_fpr(const void *bf, size_t n) { return cbf_fpr_theory((const cbf_t *)bf, n); }
static double cbf_ad_fill(const void *bf) { return cbf_fill_ratio((const cbf_t *)bf); }

/* ---- PBF ---- */
static void *pbf_ad_create(size_t m, unsigned k) { return pbf_create(m, k); }
static void pbf_ad_destroy(void *bf) { pbf_destroy((pbf_t *)bf); }
static void pbf_ad_insert(void *bf, const bf_key_t *k) { pbf_insert((pbf_t *)bf, k); }
static unsigned pbf_ad_insert_counted(void *bf, const bf_key_t *k) { return pbf_insert_counted((pbf_t *)bf, k); }
static bf_member_t pbf_ad_lookup(void *bf, const bf_key_t *k) { return pbf_lookup((pbf_t *)bf, k); }
static size_t pbf_ad_memory(const void *bf) { return pbf_memory_bytes((const pbf_t *)bf); }
static size_t pbf_ad_cells(const void *bf) { return pbf_cells((const pbf_t *)bf); }
static double pbf_ad_fpr(const void *bf, size_t n) { return pbf_fpr_theory((const pbf_t *)bf, n); }
static double pbf_ad_fill(const void *bf) { return pbf_fill_ratio((const pbf_t *)bf); }

/* ---- rDBF ---- */
static void *rdbf_ad_create(size_t m, unsigned k) { (void)k; return rdbf_create(m); }
static void rdbf_ad_destroy(void *bf) { rdbf_destroy((rdbf_t *)bf); }
static void rdbf_ad_insert(void *bf, const bf_key_t *k) { rdbf_insert((rdbf_t *)bf, k); }
static unsigned rdbf_ad_insert_counted(void *bf, const bf_key_t *k) { return rdbf_insert_counted((rdbf_t *)bf, k); }
static bf_member_t rdbf_ad_lookup(void *bf, const bf_key_t *k) { return rdbf_lookup((rdbf_t *)bf, k); }
static size_t rdbf_ad_memory(const void *bf) { return rdbf_memory_bytes((const rdbf_t *)bf); }
static size_t rdbf_ad_cells(const void *bf) { return rdbf_cells((const rdbf_t *)bf); }
static double rdbf_ad_fpr(const void *bf, size_t n) { return rdbf_fpr_theory((const rdbf_t *)bf, n); }
static double rdbf_ad_fill(const void *bf) { return rdbf_fill_ratio((const rdbf_t *)bf); }

/* ---- hybrid: shard bound (cells) x bits/counters ---- */
static void hybrid_ad_destroy(void *bf) { hybrid_destroy((hybrid_t *)bf); }
static void hybrid_ad_insert(void *bf, const bf_key_t *k) { hybrid_insert((hybrid_t *)bf, k); }
static unsigned hybrid_ad_insert_counted(void *bf, const bf_key_t *k) { return hybrid_insert_counted((hybrid_t *)bf, k); }
static bf_member_t hybrid_ad_lookup(void *bf, const bf_key_t *k) { return hybrid_lookup((hybrid_t *)bf, k); }
static void hybrid_ad_remove(void *bf, const bf_key_t *k) { hybrid_delete((hybrid_t *)bf, k); }
static size_t hybrid_ad_memory(const void *bf) { return hybrid_memory_bytes((const hybrid_t *)bf); }
static size_t hybrid_ad_cells(const void *bf) { return hybrid_cells((const hybrid_t *)bf); }
static double hybrid_ad_fpr(const void *bf, size_t n) { return hybrid_fpr_theory((const hybrid_t *)bf, n); }
static double hybrid_ad_fill(const void *bf) { return hybrid_fill_ratio((const hybrid_t *)bf); }

#define HYBRID_VARIANT(tag, shard_max, counting)                                        \
    static void *hybrid_##tag##_create(size_t m, unsigned k)                            \
    { return hybrid_create(m, k, (shard_max), (counting)); }                            \
    static int hybrid_##tag##_params(size_t n, double p, bf_sizing_t *out)              \
    { return hybrid_params(n, p, (shard_max), out); }

HYBRID_VARIANT(b512, 512, 0)    /* 1 cache line of bits (7 x 73)            */
HYBRID_VARIANT(b4096, 4096, 0)  /* 8 cache lines of bits (61 x 67)          */
HYBRID_VARIANT(c512, 512, 1)    /* counters: 4 cache lines per shard        */
HYBRID_VARIANT(c4096, 4096, 1)  /* counters: 32 cache lines per shard       */

#define HYBRID_OPS(name, tag, del, rm)                                                  \
    { name, 0, del, 0, hybrid_##tag##_create, hybrid_ad_destroy, hybrid_ad_insert,     \
      hybrid_ad_lookup, rm, hybrid_ad_memory, hybrid_ad_cells, hybrid_ad_fpr,           \
      hybrid_ad_fill, hybrid_##tag##_params, hybrid_ad_insert_counted }

/* ---- baseline ---- */
static void *baseline_ad_create(size_t m, unsigned k) { (void)m; (void)k; return baseline_create(); }
static void baseline_ad_destroy(void *bf) { baseline_destroy((baseline_t *)bf); }
static void baseline_ad_insert(void *bf, const bf_key_t *k) { baseline_insert((baseline_t *)bf, k); }
static bf_member_t baseline_ad_lookup(void *bf, const bf_key_t *k) { return baseline_lookup((baseline_t *)bf, k); }
static void baseline_ad_remove(void *bf, const bf_key_t *k) { baseline_remove((baseline_t *)bf, k); }
static size_t baseline_ad_memory(const void *bf) { return baseline_memory_bytes((const baseline_t *)bf); }

static const bf_ops_t ops_table[] = {
    { "sbf",      0, 0, 0, sbf_ad_create,      sbf_ad_destroy,      sbf_ad_insert,      sbf_ad_lookup,      NULL,            sbf_ad_memory, sbf_ad_cells, sbf_ad_fpr, sbf_ad_fill, sbf_params, sbf_ad_insert_counted },
    { "cbf",      0, 1, 0, cbf_ad_create,      cbf_ad_destroy,      cbf_ad_insert,      cbf_ad_lookup,      cbf_ad_remove,   cbf_ad_memory, cbf_ad_cells, cbf_ad_fpr, cbf_ad_fill, cbf_params, cbf_ad_insert_counted },
    { "pbf",      0, 0, 0, pbf_ad_create,      pbf_ad_destroy,      pbf_ad_insert,      pbf_ad_lookup,      NULL,            pbf_ad_memory, pbf_ad_cells, pbf_ad_fpr, pbf_ad_fill, pbf_params, pbf_ad_insert_counted },
    { "rdbf",     0, 0, 1, rdbf_ad_create,     rdbf_ad_destroy,     rdbf_ad_insert,     rdbf_ad_lookup,     NULL,            rdbf_ad_memory, rdbf_ad_cells, rdbf_ad_fpr, rdbf_ad_fill, rdbf_params, rdbf_ad_insert_counted },
    HYBRID_OPS("hybrid_512",    b512,  0, NULL),
    HYBRID_OPS("hybrid_4096",   b4096, 0, NULL),
    HYBRID_OPS("hybrid_c_512",  c512,  1, hybrid_ad_remove),
    HYBRID_OPS("hybrid_c_4096", c4096, 1, hybrid_ad_remove),
    { "baseline", 1, 1, 0, baseline_ad_create, baseline_ad_destroy, baseline_ad_insert, baseline_ad_lookup, baseline_ad_remove, baseline_ad_memory, NULL, NULL, NULL, NULL, NULL },
};

#define OPS_COUNT (sizeof(ops_table) / sizeof(ops_table[0]))

const bf_ops_t *bf_ops_by_name(const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < OPS_COUNT; i++) {
        if (strcmp(ops_table[i].name, name) == 0) return &ops_table[i];
    }
    return NULL;
}

const bf_ops_t *bf_ops_all(size_t *count)
{
    if (count) *count = OPS_COUNT;
    return ops_table;
}
