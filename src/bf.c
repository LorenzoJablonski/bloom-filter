#include "bf.h"

#include <string.h>

#include "sbf.h"
#include "cbf.h"
#include "pbf.h"
#include "rdbf.h"
#include "baseline.h"

/* ---- SBF ---- */
static void *sbf_ad_create(size_t m, unsigned k) { return sbf_create(m, k); }
static void sbf_ad_destroy(void *bf) { sbf_destroy((sbf_t *)bf); }
static void sbf_ad_insert(void *bf, const bf_key_t *k) { sbf_insert((sbf_t *)bf, k); }
static bf_member_t sbf_ad_lookup(void *bf, const bf_key_t *k) { return sbf_lookup((sbf_t *)bf, k); }
static size_t sbf_ad_memory(const void *bf) { return sbf_memory_bytes((const sbf_t *)bf); }
static size_t sbf_ad_cells(const void *bf) { return sbf_cells((const sbf_t *)bf); }
static double sbf_ad_fpr(const void *bf, size_t n) { return sbf_fpr_theory((const sbf_t *)bf, n); }
static double sbf_ad_fill(const void *bf) { return sbf_fill_ratio((const sbf_t *)bf); }

/* ---- CBF ---- */
static void *cbf_ad_create(size_t m, unsigned k) { return cbf_create(m, k); }
static void cbf_ad_destroy(void *bf) { cbf_destroy((cbf_t *)bf); }
static void cbf_ad_insert(void *bf, const bf_key_t *k) { cbf_insert((cbf_t *)bf, k); }
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
static bf_member_t pbf_ad_lookup(void *bf, const bf_key_t *k) { return pbf_lookup((pbf_t *)bf, k); }
static size_t pbf_ad_memory(const void *bf) { return pbf_memory_bytes((const pbf_t *)bf); }
static size_t pbf_ad_cells(const void *bf) { return pbf_cells((const pbf_t *)bf); }
static double pbf_ad_fpr(const void *bf, size_t n) { return pbf_fpr_theory((const pbf_t *)bf, n); }
static double pbf_ad_fill(const void *bf) { return pbf_fill_ratio((const pbf_t *)bf); }

/* ---- rDBF ---- */
static void *rdbf_ad_create(size_t m, unsigned k) { (void)k; return rdbf_create(m); }
static void rdbf_ad_destroy(void *bf) { rdbf_destroy((rdbf_t *)bf); }
static void rdbf_ad_insert(void *bf, const bf_key_t *k) { rdbf_insert((rdbf_t *)bf, k); }
static bf_member_t rdbf_ad_lookup(void *bf, const bf_key_t *k) { return rdbf_lookup((rdbf_t *)bf, k); }
static size_t rdbf_ad_memory(const void *bf) { return rdbf_memory_bytes((const rdbf_t *)bf); }
static size_t rdbf_ad_cells(const void *bf) { return rdbf_cells((const rdbf_t *)bf); }
static double rdbf_ad_fpr(const void *bf, size_t n) { return rdbf_fpr_theory((const rdbf_t *)bf, n); }
static double rdbf_ad_fill(const void *bf) { return rdbf_fill_ratio((const rdbf_t *)bf); }

/* ---- baseline ---- */
static void *baseline_ad_create(size_t m, unsigned k) { (void)m; (void)k; return baseline_create(); }
static void baseline_ad_destroy(void *bf) { baseline_destroy((baseline_t *)bf); }
static void baseline_ad_insert(void *bf, const bf_key_t *k) { baseline_insert((baseline_t *)bf, k); }
static bf_member_t baseline_ad_lookup(void *bf, const bf_key_t *k) { return baseline_lookup((baseline_t *)bf, k); }
static size_t baseline_ad_memory(const void *bf) { return baseline_memory_bytes((const baseline_t *)bf); }

static const bf_ops_t ops_table[] = {
    { "sbf",      0, 0, 0, sbf_ad_create,      sbf_ad_destroy,      sbf_ad_insert,      sbf_ad_lookup,      NULL,            sbf_ad_memory, sbf_ad_cells, sbf_ad_fpr, sbf_ad_fill, sbf_params },
    { "cbf",      0, 1, 0, cbf_ad_create,      cbf_ad_destroy,      cbf_ad_insert,      cbf_ad_lookup,      cbf_ad_remove,   cbf_ad_memory, cbf_ad_cells, cbf_ad_fpr, cbf_ad_fill, cbf_params },
    { "pbf",      0, 0, 0, pbf_ad_create,      pbf_ad_destroy,      pbf_ad_insert,      pbf_ad_lookup,      NULL,            pbf_ad_memory, pbf_ad_cells, pbf_ad_fpr, pbf_ad_fill, pbf_params },
    { "rdbf",     0, 0, 1, rdbf_ad_create,     rdbf_ad_destroy,     rdbf_ad_insert,     rdbf_ad_lookup,     NULL,            rdbf_ad_memory, rdbf_ad_cells, rdbf_ad_fpr, rdbf_ad_fill, rdbf_params },
    { "baseline", 1, 0, 0, baseline_ad_create, baseline_ad_destroy, baseline_ad_insert, baseline_ad_lookup, NULL,            baseline_ad_memory, NULL, NULL, NULL, NULL },
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
