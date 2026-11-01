#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "common.h"
#include "sbf.h"
#include "cbf.h"
#include "pbf.h"
#include "rdbf.h"
#include "baseline.h"

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static char *dup_range(const char *start, size_t len)
{
    char *s = (char *)malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static char *read_line(char *buf, size_t size)
{
    if (!fgets(buf, (int)size, stdin)) return NULL;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return buf;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ------------------------------------------------------------------ */
/* demo automático — execução não-interativa, imprime tabela          */
/* ------------------------------------------------------------------ */

static int demo(void)
{
    const size_t n = 5000;          /* inseridas */
    const size_t n_probe = 5000;    /* consultadas e nunca inseridas */
    const double p_target = 0.01;   /* 1% */

    /* parâmetros ótimos de Bloom: m = -n ln(p) / (ln2)^2 ; k = (m/n) ln2 */
    size_t m = (size_t)ceil(-((double)n * log(p_target)) / (log(2.0) * log(2.0)));
    unsigned k = (unsigned)((double)m / (double)n * log(2.0) + 0.5);
    double fpr_theory = pow(1.0 - exp(-(double)k * (double)n / (double)m), (double)k);

    /* chaves: key-00000000 .. key-00004999 (inseridas)          */
    /*          key-00005000 .. key-00009999 (nunca inseridas)   */
    char **keys = (char **)malloc((n + n_probe) * sizeof(char *));
    if (!keys) return 1;
    for (size_t i = 0; i < n + n_probe; i++) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "key-%08zu", i);
        keys[i] = dup_range(buf, (size_t)len);
    }

    /* -------- SBF -------- */
    sbf_t *bf = sbf_create(m, k);
    if (!bf) { fprintf(stderr, "sbf_create failed\n"); return 1; }

    double t0 = now_ms();
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        sbf_insert(bf, &kk);
    }
    double t_insert = now_ms() - t0;

    size_t fn_sbf = 0;
    t0 = now_ms();
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (sbf_lookup(bf, &kk) != BF_PRESENT) fn_sbf++;
    }
    double t_lookup_exist = now_ms() - t0;

    size_t fp_sbf = 0;
    t0 = now_ms();
    for (size_t i = n; i < n + n_probe; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (sbf_lookup(bf, &kk) == BF_PRESENT) fp_sbf++;
    }
    double t_lookup_inexist = now_ms() - t0;

    double mem_sbf = (double)((m + 7) / 8);

    /* -------- CBF (4-bit packed counters) -------- */
    cbf_t *cbf = cbf_create(m, k);
    if (!cbf) { fprintf(stderr, "cbf_create failed\n"); return 1; }

    t0 = now_ms();
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        cbf_insert(cbf, &kk);
    }
    double t_insert_cbf = now_ms() - t0;

    size_t fn_cbf = 0;
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (cbf_lookup(cbf, &kk) != BF_PRESENT) fn_cbf++;
    }
    size_t fp_cbf = 0;
    for (size_t i = n; i < n + n_probe; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (cbf_lookup(cbf, &kk) == BF_PRESENT) fp_cbf++;
    }
    double mem_cbf = (double)((m + 1) / 2); /* 4 bits per counter */

    /* -------- PBF (k partições disjuntas) -------- */
    pbf_t *pbf = pbf_create(m, k);
    if (!pbf) { fprintf(stderr, "pbf_create failed\n"); return 1; }

    t0 = now_ms();
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        pbf_insert(pbf, &kk);
    }
    double t_insert_pbf = now_ms() - t0;

    size_t fn_pbf = 0;
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (pbf_lookup(pbf, &kk) != BF_PRESENT) fn_pbf++;
    }
    size_t fp_pbf = 0;
    for (size_t i = n; i < n + n_probe; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (pbf_lookup(pbf, &kk) == BF_PRESENT) fp_pbf++;
    }
    double mem_pbf = (double)((m + 7) / 8);

    /* -------- rDBF (matriz 2D de dimensões primas) -------- */
    rdbf_t *rdbf = rdbf_create(m);
    if (!rdbf) { fprintf(stderr, "rdbf_create failed\n"); return 1; }

    t0 = now_ms();
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        rdbf_insert(rdbf, &kk);
    }
    double t_insert_rdbf = now_ms() - t0;

    size_t fn_rdbf = 0;
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (rdbf_lookup(rdbf, &kk) != BF_PRESENT) fn_rdbf++;
    }
    size_t fp_rdbf = 0;
    for (size_t i = n; i < n + n_probe; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (rdbf_lookup(rdbf, &kk) == BF_PRESENT) fp_rdbf++;
    }
    double mem_rdbf = (double)rdbf_memory_bytes(rdbf);

    /* -------- baseline (uthash, determinística) -------- */
    baseline_t *bl = baseline_create();
    if (!bl) { fprintf(stderr, "baseline_create failed\n"); return 1; }

    t0 = now_ms();
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        baseline_insert(bl, &kk);
    }
    double t_insert_bl = now_ms() - t0;

    size_t fn_bl = 0;
    for (size_t i = 0; i < n; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (baseline_lookup(bl, &kk) != BF_PRESENT) fn_bl++;
    }
    size_t fp_bl = 0;
    for (size_t i = n; i < n + n_probe; i++) {
        bf_key_t kk = { keys[i], strlen(keys[i]) };
        if (baseline_lookup(bl, &kk) == BF_PRESENT) fp_bl++;
    }
    /* estimativa: entrada uthash ~ 48 bytes (handle + ponteiros) */
    double mem_bl = (double)n * 48.0;

    /* -------- tabela -------- */
    printf("\n===== Bloom Filter Demo (automatico, sem interacao) =====\n");
    printf("params : n=%zu  p_target=%.0f%%  m=%zu bits (%.1f KiB)  k=%u\n",
           n, p_target * 100.0, m, mem_sbf / 1024.0, k);
    printf("consultas: %zu existentes + %zu nunca-inseridas\n\n", n, n_probe);
    printf("%-9s %8s %8s %6s %14s %6s %8s %10s\n",
           "filter", "inserted", "lookup_ex", "FN", "lookup_inex", "FP", "FPR", "mem");
    printf("%-9s %8zu %8zu %6zu %14zu %6zu %7.3f%% %8.1fKiB\n",
           "sbf", n, n, fn_sbf, n_probe, fp_sbf, 100.0 * fp_sbf / n_probe, mem_sbf / 1024.0);
    printf("%-9s %8zu %8zu %6zu %14zu %6zu %7.3f%% %8.1fKiB\n",
           "cbf", n, n, fn_cbf, n_probe, fp_cbf, 100.0 * fp_cbf / n_probe, mem_cbf / 1024.0);
    printf("%-9s %8zu %8zu %6zu %14zu %6zu %7.3f%% %8.1fKiB\n",
           "pbf", n, n, fn_pbf, n_probe, fp_pbf, 100.0 * fp_pbf / n_probe, mem_pbf / 1024.0);
    printf("%-9s %8zu %8zu %6zu %14zu %6zu %7.3f%% %8.1fKiB\n",
           "rdbf", n, n, fn_rdbf, n_probe, fp_rdbf, 100.0 * fp_rdbf / n_probe, mem_rdbf / 1024.0);
    printf("%-9s %8zu %8zu %6zu %14zu %6zu %7.3f%% %8.1fKiB (est)\n",
           "baseline", n, n, fn_bl, n_probe, fp_bl, 100.0 * fp_bl / n_probe, mem_bl / 1024.0);
    printf("\nFPR teorica (sbf/cbf/pbf): %.3f%%   |   sbf obs: %.3f%%   cbf obs: %.3f%%   pbf obs: %.3f%%\n",
           100.0 * fpr_theory, 100.0 * fp_sbf / n_probe, 100.0 * fp_cbf / n_probe,
           100.0 * fp_pbf / n_probe);
    printf("rdbf: matriz %lu x %lu (primos), 1 hash por chave, 1 bit por chave -> FPR obs: %.3f%%\n",
           rdbf_dim_x(rdbf), rdbf_dim_y(rdbf), 100.0 * fp_rdbf / n_probe);
    printf("FN = falsos negativos (deve ser 0 em todos)\n");
    printf("mem sbf/pbf: m bits. mem cbf: 4-bit packed counters (4x sbf). mem rdbf: matriz de unsigned long. mem baseline: estimativa\n");

    printf("\ntempos (ms): sbf insert=%.1f lookup_exist=%.1f lookup_inexist=%.1f | cbf insert=%.1f | pbf insert=%.1f | rdbf insert=%.1f | baseline insert=%.1f\n",
           t_insert, t_lookup_exist, t_lookup_inexist, t_insert_cbf, t_insert_pbf,
           t_insert_rdbf, t_insert_bl);

    int ok = (fn_sbf == 0) && (fn_cbf == 0) && (fn_pbf == 0) && (fn_rdbf == 0) &&
             (fn_bl == 0) && (fp_bl == 0);
    printf("resultado: %s\n\n", ok ? "OK" : "FALHOU");

    for (size_t i = 0; i < n + n_probe; i++) free(keys[i]);
    free(keys);
    sbf_destroy(bf);
    cbf_destroy(cbf);
    pbf_destroy(pbf);
    rdbf_destroy(rdbf);
    baseline_destroy(bl);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* --test : invariantes rápidas, exit code p/ make test              */
/* ------------------------------------------------------------------ */

static int run_test(void)
{
    /* SBF: todo insertado é PRESENT (FN=0) */
    sbf_t *bf = sbf_create(4096, 5);
    if (!bf) return 1;
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        sbf_insert(bf, &kk);
    }
    int fn = 0;
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        if (sbf_lookup(bf, &kk) != BF_PRESENT) fn++;
    }

    /* baseline: FN=0 e FP=0 */
    baseline_t *bl = baseline_create();
    if (!bl) { sbf_destroy(bf); return 1; }
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        baseline_insert(bl, &kk);
    }
    int fn_bl = 0, fp_bl = 0;
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        if (baseline_lookup(bl, &kk) != BF_PRESENT) fn_bl++;
    }
    for (int i = 500; i < 1000; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        if (baseline_lookup(bl, &kk) == BF_PRESENT) fp_bl++;
    }

    /* CBF: FN=0 apos insert e apos delete de metade */
    cbf_t *cbf = cbf_create(8192, 5);
    if (!cbf) { sbf_destroy(bf); baseline_destroy(bl); return 1; }
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        cbf_insert(cbf, &kk);
    }
    for (int i = 0; i < 250; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        cbf_delete(cbf, &kk);
    }
    int fn_cbf = 0;
    for (int i = 250; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        if (cbf_lookup(cbf, &kk) != BF_PRESENT) fn_cbf++;
    }

    /* PBF: FN=0 */
    pbf_t *pbf = pbf_create(4096, 5);
    if (!pbf) { sbf_destroy(bf); cbf_destroy(cbf); baseline_destroy(bl); return 1; }
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        pbf_insert(pbf, &kk);
    }
    int fn_pbf = 0;
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        if (pbf_lookup(pbf, &kk) != BF_PRESENT) fn_pbf++;
    }

    /* rDBF: FN=0 */
    rdbf_t *rdbf = rdbf_create(16384);
    if (!rdbf) { sbf_destroy(bf); cbf_destroy(cbf); pbf_destroy(pbf); baseline_destroy(bl); return 1; }
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        rdbf_insert(rdbf, &kk);
    }
    int fn_rdbf = 0;
    for (int i = 0; i < 500; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "t-%d", i);
        bf_key_t kk = { buf, (size_t)len };
        if (rdbf_lookup(rdbf, &kk) != BF_PRESENT) fn_rdbf++;
    }

    int ok = (fn == 0) && (fn_bl == 0) && (fp_bl == 0) && (fn_cbf == 0) &&
             (fn_pbf == 0) && (fn_rdbf == 0);
    printf("test: sbf FN=%d | cbf FN=%d | pbf FN=%d | rdbf FN=%d | baseline FN=%d FP=%d -> %s\n",
           fn, fn_cbf, fn_pbf, fn_rdbf, fn_bl, fp_bl, ok ? "PASS" : "FAIL");

    sbf_destroy(bf);
    cbf_destroy(cbf);
    pbf_destroy(pbf);
    rdbf_destroy(rdbf);
    baseline_destroy(bl);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* REPL interativo                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    sbf_t *bf;
    cbf_t *cbf;
    pbf_t *pbf;
    rdbf_t *rdbf;
    baseline_t *bl;
    int use_baseline;
    int use_cbf;
    int use_pbf;
    int use_rdbf;
    size_t m, k;
    size_t n_insert;
    size_t n_lookups, n_present, n_absent;
} repl_state_t;

static void repl_recreate(repl_state_t *st)
{
    if (st->use_baseline) {
        if (st->bl) baseline_destroy(st->bl);
        st->bl = baseline_create();
    } else if (st->use_cbf) {
        if (st->cbf) cbf_destroy(st->cbf);
        st->cbf = cbf_create(st->m, st->k);
    } else if (st->use_pbf) {
        if (st->pbf) pbf_destroy(st->pbf);
        st->pbf = pbf_create(st->m, st->k);
    } else if (st->use_rdbf) {
        if (st->rdbf) rdbf_destroy(st->rdbf);
        st->rdbf = rdbf_create(st->m);
    } else {
        if (st->bf) sbf_destroy(st->bf);
        st->bf = sbf_create(st->m, st->k);
    }
    st->n_insert = 0;
    st->n_lookups = st->n_present = st->n_absent = 0;
}

static void repl_insert(repl_state_t *st, const char *key, size_t len)
{
    bf_key_t kk = { key, len };
    if (st->use_baseline) baseline_insert(st->bl, &kk);
    else if (st->use_cbf) cbf_insert(st->cbf, &kk);
    else if (st->use_pbf) pbf_insert(st->pbf, &kk);
    else if (st->use_rdbf) rdbf_insert(st->rdbf, &kk);
    else sbf_insert(st->bf, &kk);
    st->n_insert++;
}

static int repl_delete(repl_state_t *st, const char *key, size_t len)
{
    if (!st->use_cbf) return 0;
    bf_key_t kk = { key, len };
    cbf_delete(st->cbf, &kk);
    return 1;
}

static bf_member_t repl_lookup(repl_state_t *st, const char *key, size_t len)
{
    bf_key_t kk = { key, len };
    bf_member_t r;
    if (st->use_baseline) r = baseline_lookup(st->bl, &kk);
    else if (st->use_cbf) r = cbf_lookup(st->cbf, &kk);
    else if (st->use_pbf) r = pbf_lookup(st->pbf, &kk);
    else if (st->use_rdbf) r = rdbf_lookup(st->rdbf, &kk);
    else r = sbf_lookup(st->bf, &kk);
    st->n_lookups++;
    if (r == BF_PRESENT) st->n_present++;
    else st->n_absent++;
    return r;
}

static int repl_load(repl_state_t *st, const char *path, int insert_mode)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("erro: nao abriu %s\n", path);
        return 0;
    }
    char buf[4096];
    size_t count = 0, present = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) len--;
        if (len == 0) continue;
        if (insert_mode) repl_insert(st, buf, len);
        else if (repl_lookup(st, buf, len) == BF_PRESENT) present++;
        count++;
    }
    fclose(f);
    if (insert_mode) printf("inseridas %zu chaves de %s\n", count, path);
    else printf("consultadas %zu chaves de %s -> presentes=%zu ausentes=%zu\n",
                count, path, present, count - present);
    return (int)count;
}

static void repl_stats(repl_state_t *st)
{
    const char *name = st->use_baseline ? "baseline (uthash, exata)"
                     : st->use_cbf      ? "cbf (contadores 4-bit)"
                     : st->use_pbf      ? "pbf (particionado)"
                     : st->use_rdbf     ? "rdbf (matriz 2D de primos)"
                                        : "sbf";
    printf("estrutura : %s\n", name);
    if (st->use_cbf)
        printf("m=%zu contadores (%.1f KiB, 4-bit packed)  k=%zu\n",
               st->m, (double)((st->m + 1) / 2) / 1024.0, st->k);
    else if (st->use_rdbf)
        printf("matriz %lu x %lu (primos, %zu bits, %.1f KiB)\n",
               rdbf_dim_x(st->rdbf), rdbf_dim_y(st->rdbf),
               (size_t)rdbf_dim_x(st->rdbf) * (size_t)rdbf_dim_y(st->rdbf),
               (double)rdbf_memory_bytes(st->rdbf) / 1024.0);
    else if (!st->use_baseline)
        printf("m=%zu bits (%.1f KiB)  k=%zu\n", st->m, (double)((st->m + 7) / 8) / 1024.0, st->k);
    printf("inseridas : %zu\n", st->n_insert);
    printf("lookups   : %zu (presentes=%zu ausentes=%zu)\n",
           st->n_lookups, st->n_present, st->n_absent);
    if (st->use_baseline)
        printf("FPR       : 0.000%% (estrutura deterministica)\n");
    else if (st->n_lookups > 0)
        printf("nota      : 'presentes' inclui falsos positivos; FPR precisa de consultas a chaves nao-inseridas (ex: data/<tipo>/inexist.txt)\n");
}

static void repl_help(void)
{
    printf(
        "comandos:\n"
        "  insert <chave>      insere chave (espacos permitidos)\n"
        "  delete <chave>      remove chave (apenas cbf)\n"
        "  search <chave>      consulta -> PRESENT/ABSENT\n"
        "  load <arquivo>      insere todas as linhas do arquivo\n"
        "  query <arquivo>     consulta todas as linhas do arquivo\n"
        "  stats               estatisticas correntes\n"
        "  clear               zera a estrutura\n"
        "  help                esta ajuda\n"
        "  quit                sai\n");
}

static int repl(const char *filter)
{
    repl_state_t st = {0};
    st.m = 1 << 20; /* 1 Mibit default (ou 1M contadores no cbf) */
    st.k = 7;
    st.use_baseline = (strcmp(filter, "baseline") == 0);
    st.use_cbf = (strcmp(filter, "cbf") == 0);
    st.use_pbf = (strcmp(filter, "pbf") == 0);
    st.use_rdbf = (strcmp(filter, "rdbf") == 0);

    int created;
    if (st.use_baseline) { st.bl = baseline_create(); created = (st.bl != NULL); }
    else if (st.use_cbf) { st.cbf = cbf_create(st.m, st.k); created = (st.cbf != NULL); }
    else if (st.use_pbf) { st.pbf = pbf_create(st.m, st.k); created = (st.pbf != NULL); }
    else if (st.use_rdbf) { st.rdbf = rdbf_create(st.m); created = (st.rdbf != NULL); }
    else { st.bf = sbf_create(st.m, st.k); created = (st.bf != NULL); }
    if (!created) {
        fprintf(stderr, "falha ao criar estrutura\n");
        return 1;
    }

    const char *fname = st.use_baseline ? "baseline"
                      : st.use_cbf      ? "cbf"
                      : st.use_pbf      ? "pbf"
                      : st.use_rdbf     ? "rdbf"
                                        : "sbf";
    printf("===== Bloom REPL (%s) =====\n", fname);
    if (st.use_cbf)
        printf("m=%zu contadores (4-bit packed)  k=%zu\n", st.m, st.k);
    else if (st.use_rdbf)
        printf("matriz %lu x %lu (primos)\n", rdbf_dim_x(st.rdbf), rdbf_dim_y(st.rdbf));
    else if (!st.use_baseline)
        printf("m=%zu bits  k=%zu\n", st.m, st.k);
    repl_help();

    char line[8192];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!read_line(line, sizeof(line))) break; /* EOF */
        if (line[0] == '\0') continue;

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0 ||
            strcmp(line, "q") == 0) break;
        if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) { repl_help(); continue; }
        if (strcmp(line, "stats") == 0) { repl_stats(&st); continue; }
        if (strcmp(line, "clear") == 0) { repl_recreate(&st); printf("estrutura zerada\n"); continue; }

        const char *rest;
        if (strncmp(line, "insert ", 7) == 0) {
            rest = line + 7;
            if (*rest) repl_insert(&st, rest, strlen(rest));
            else printf("uso: insert <chave>\n");
            continue;
        }
        if (strncmp(line, "delete ", 7) == 0) {
            rest = line + 7;
            if (!*rest) { printf("uso: delete <chave>\n"); continue; }
            if (repl_delete(&st, rest, strlen(rest))) printf("removida\n");
            else printf("delete nao suportado nesta estrutura (use --filter cbf)\n");
            continue;
        }
        if (strncmp(line, "search ", 7) == 0) {
            rest = line + 7;
            if (!*rest) { printf("uso: search <chave>\n"); continue; }
            bf_member_t r = repl_lookup(&st, rest, strlen(rest));
            printf("%s\n", r == BF_PRESENT ? "PRESENT" : "ABSENT");
            continue;
        }
        if (strncmp(line, "load ", 5) == 0) {
            rest = line + 5;
            if (*rest) repl_load(&st, rest, 1);
            else printf("uso: load <arquivo>\n");
            continue;
        }
        if (strncmp(line, "query ", 6) == 0) {
            rest = line + 6;
            if (!*rest) { printf("uso: query <arquivo>\n"); continue; }
            repl_load(&st, rest, 0);
            continue;
        }
        printf("comando desconhecido: %s (help lista comandos)\n", line);
    }

    if (st.use_baseline) baseline_destroy(st.bl);
    else if (st.use_cbf) cbf_destroy(st.cbf);
    else if (st.use_pbf) pbf_destroy(st.pbf);
    else if (st.use_rdbf) rdbf_destroy(st.rdbf);
    else sbf_destroy(st.bf);
    return 0;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *mode = "demo";
    const char *filter = "sbf";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--repl") == 0) mode = "repl";
        else if (strcmp(argv[i], "--test") == 0) mode = "test";
        else if (strcmp(argv[i], "--demo") == 0) mode = "demo";
        else if (strcmp(argv[i], "--benchmark") == 0) mode = "benchmark";
        else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) {
            printf("uso: bloom_app [--demo|--test|--repl [--filter sbf|cbf|pbf|rdbf|baseline]|--benchmark]\n");
            return 0;
        }
    }

    if (strcmp(mode, "repl") == 0) return repl(filter);
    if (strcmp(mode, "test") == 0) return run_test();
    if (strcmp(mode, "benchmark") == 0) {
        printf("benchmark: nao implementado ainda (feature/benchmark)\n");
        return 0;
    }
    return demo();
}
