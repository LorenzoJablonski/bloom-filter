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
#include "bf.h"
#include "benchmark.h"

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

    size_t nops = 0;
    const bf_ops_t *ops = bf_ops_all(&nops);

    /* resultados por filtro */
    size_t *fn = (size_t *)calloc(nops, sizeof(size_t));
    size_t *fp = (size_t *)calloc(nops, sizeof(size_t));
    size_t *mem = (size_t *)calloc(nops, sizeof(size_t));
    double *tins = (double *)calloc(nops, sizeof(double));
    double *tlex = (double *)calloc(nops, sizeof(double));
    double *tlin = (double *)calloc(nops, sizeof(double));
    if (!fn || !fp || !mem || !tins || !tlex || !tlin) {
        free(keys); return 1;
    }

    for (size_t i = 0; i < nops; i++) {
        void *bf = ops[i].create(m, k);
        if (!bf) { fprintf(stderr, "%s_create failed\n", ops[i].name); return 1; }

        double t0 = now_ms();
        for (size_t j = 0; j < n; j++) {
            bf_key_t kk = { keys[j], strlen(keys[j]) };
            ops[i].insert(bf, &kk);
        }
        tins[i] = now_ms() - t0;

        t0 = now_ms();
        for (size_t j = 0; j < n; j++) {
            bf_key_t kk = { keys[j], strlen(keys[j]) };
            if (ops[i].lookup(bf, &kk) != BF_PRESENT) fn[i]++;
        }
        tlex[i] = now_ms() - t0;

        t0 = now_ms();
        for (size_t j = n; j < n + n_probe; j++) {
            bf_key_t kk = { keys[j], strlen(keys[j]) };
            if (ops[i].lookup(bf, &kk) == BF_PRESENT) fp[i]++;
        }
        tlin[i] = now_ms() - t0;

        mem[i] = ops[i].memory_bytes(bf);
        ops[i].destroy(bf);
    }

    /* -------- tabela -------- */
    printf("\n===== Bloom Filter Demo (automatico, sem interacao) =====\n");
    printf("params : n=%zu  p_target=%.0f%%  m=%zu bits (%.1f KiB)  k=%u\n",
           n, p_target * 100.0, m, (double)((m + 7) / 8) / 1024.0, k);
    printf("consultas: %zu existentes + %zu nunca-inseridas\n\n", n, n_probe);
    printf("%-9s %8s %8s %6s %14s %6s %8s %10s\n",
           "filter", "inserted", "lookup_ex", "FN", "lookup_inex", "FP", "FPR", "mem");
    for (size_t i = 0; i < nops; i++) {
        printf("%-9s %8zu %8zu %6zu %14zu %6zu %7.3f%% %8.1fKiB%s\n",
               ops[i].name, n, n, fn[i], n_probe, fp[i],
               100.0 * fp[i] / n_probe, (double)mem[i] / 1024.0,
               ops[i].exact ? " (exata)" : "");
    }
    printf("\nFPR teorica (sbf/cbf/pbf): %.3f%%\n", 100.0 * fpr_theory);
    for (size_t i = 0; i < nops; i++) {
        if (strcmp(ops[i].name, "rdbf") == 0) {
            /* dims not stored; recompute not needed, print generic */
        }
    }
    printf("FN = falsos negativos (deve ser 0 em todos)\n");

    printf("\ntempos (ms):");
    for (size_t i = 0; i < nops; i++) {
        printf(" %s insert=%.1f", ops[i].name, tins[i]);
    }
    printf("\n");

    int ok = 1;
    for (size_t i = 0; i < nops; i++) {
        if (fn[i] != 0) ok = 0;
    }
    /* baseline must have zero FP */
    for (size_t i = 0; i < nops; i++) {
        if (ops[i].exact && fp[i] != 0) ok = 0;
    }
    printf("resultado: %s\n\n", ok ? "OK" : "FALHOU");

    for (size_t i = 0; i < n + n_probe; i++) free(keys[i]);
    free(keys);
    free(fn); free(fp); free(mem); free(tins); free(tlex); free(tlin);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* --test : invariantes rápidas, exit code p/ make test              */
/* ------------------------------------------------------------------ */

static int run_test(void)
{
    /* SBF/PBF/rDBF/baseline: todo insertado e PRESENT (FN=0) */
    size_t nops = 0;
    const bf_ops_t *ops = bf_ops_all(&nops);
    int ok = 1;
    for (size_t i = 0; i < nops; i++) {
        void *bf = ops[i].create(4096, 5);
        if (!bf) { ok = 0; continue; }
        for (int j = 0; j < 500; j++) {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "t-%d", j);
            bf_key_t kk = { buf, (size_t)len };
            ops[i].insert(bf, &kk);
        }
        int fn = 0;
        for (int j = 0; j < 500; j++) {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "t-%d", j);
            bf_key_t kk = { buf, (size_t)len };
            if (ops[i].lookup(bf, &kk) != BF_PRESENT) fn++;
        }
        /* baseline: FP=0 */
        int fp = 0;
        if (ops[i].exact) {
            for (int j = 500; j < 1000; j++) {
                char buf[32];
                int len = snprintf(buf, sizeof(buf), "t-%d", j);
                bf_key_t kk = { buf, (size_t)len };
                if (ops[i].lookup(bf, &kk) == BF_PRESENT) fp++;
            }
        }
        printf("test: %s FN=%d%s\n", ops[i].name, fn,
               ops[i].exact ? (fp ? " FP>0" : "") : "");
        if (fn != 0 || (ops[i].exact && fp != 0)) ok = 0;
        ops[i].destroy(bf);
    }

    /* CBF: delete de metade nao gera FN nos restantes */
    {
        cbf_t *cbf = cbf_create(8192, 5);
        if (!cbf) { ok = 0; }
        else {
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
            int fn = 0;
            for (int i = 250; i < 500; i++) {
                char buf[32];
                int len = snprintf(buf, sizeof(buf), "t-%d", i);
                bf_key_t kk = { buf, (size_t)len };
                if (cbf_lookup(cbf, &kk) != BF_PRESENT) fn++;
            }
            printf("test: cbf (delete) FN=%d\n", fn);
            if (fn != 0) ok = 0;
            cbf_destroy(cbf);
        }
    }

    /* sizing: each model's params(n, p) must reach p by its own formula */
    {
        const size_t ns[] = { 1000, 1000000 };
        const double ps[] = { 0.05, 0.01, 0.001 };
        for (size_t i = 0; i < nops; i++) {
            if (!ops[i].params) continue;
            int good = 1;
            for (size_t a = 0; a < 2; a++) {
                for (size_t b = 0; b < 3; b++) {
                    bf_sizing_t sz;
                    if (!ops[i].params(ns[a], ps[b], &sz)) { good = 0; continue; }
                    void *bf = ops[i].create(sz.m, sz.k);
                    if (!bf) { good = 0; continue; }
                    double f = ops[i].fpr_theory(bf, ns[a]);
                    if (f > ps[b] * (1.0 + 1e-9) || f < ps[b] * 0.9) good = 0;
                    ops[i].destroy(bf);
                }
            }
            printf("test: %s params %s\n", ops[i].name, good ? "OK" : "FALHOU");
            if (!good) ok = 0;
        }
    }

    printf("test: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* REPL interativo                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    const bf_ops_t *ops;
    void *bf;
    size_t m, k;
    size_t n_insert;
    size_t n_lookups, n_present, n_absent;
} repl_state_t;

static void repl_recreate(repl_state_t *st)
{
    if (st->bf) st->ops->destroy(st->bf);
    st->bf = st->ops->create(st->m, st->k);
    st->n_insert = 0;
    st->n_lookups = st->n_present = st->n_absent = 0;
}

static void repl_insert(repl_state_t *st, const char *key, size_t len)
{
    bf_key_t kk = { key, len };
    st->ops->insert(st->bf, &kk);
    st->n_insert++;
}

static int repl_delete(repl_state_t *st, const char *key, size_t len)
{
    if (!st->ops->remove) return 0;
    bf_key_t kk = { key, len };
    st->ops->remove(st->bf, &kk);
    return 1;
}

static bf_member_t repl_lookup(repl_state_t *st, const char *key, size_t len)
{
    bf_key_t kk = { key, len };
    bf_member_t r = st->ops->lookup(st->bf, &kk);
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
    printf("estrutura : %s%s\n", st->ops->name, st->ops->exact ? " (exata)" : "");
    printf("memoria   : %.1f KiB\n", (double)st->ops->memory_bytes(st->bf) / 1024.0);
    printf("inseridas : %zu\n", st->n_insert);
    if (st->ops->cells) {
        printf("celulas   : %zu (m efetivo)  k=%zu\n", st->ops->cells(st->bf), st->k);
        printf("ocupacao  : %.3f%%\n", 100.0 * st->ops->fill_ratio(st->bf));
        printf("FPR teor. : %.4f%%\n", 100.0 * st->ops->fpr_theory(st->bf, st->n_insert));
    }
    printf("lookups   : %zu (presentes=%zu ausentes=%zu)\n",
           st->n_lookups, st->n_present, st->n_absent);
    if (st->ops->exact)
        printf("FPR       : 0.000%% (estrutura deterministica)\n");
    else if (st->n_lookups > 0)
        printf("nota      : 'presentes' inclui falsos positivos; FPR precisa de consultas a chaves nao-inseridas (ex: data/<tipo>/inexist.txt)\n");
}

static void repl_help(void)
{
    printf(
        "comandos:\n"
        "  insert <chave>      insere chave (espacos permitidos)\n"
        "  delete <chave>      remove chave (apenas se suportado)\n"
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
    const bf_ops_t *ops = bf_ops_by_name(filter);
    if (!ops) ops = bf_ops_by_name("sbf");

    repl_state_t st = {0};
    st.ops = ops;
    st.m = 1 << 20;
    st.k = 7;
    st.bf = ops->create(st.m, st.k);
    if (!st.bf) {
        fprintf(stderr, "falha ao criar estrutura\n");
        return 1;
    }

    printf("===== Bloom REPL (%s) =====\n", ops->name);
    printf("memoria (estimada): %.1f KiB\n", (double)ops->memory_bytes(st.bf) / 1024.0);
    repl_help();

    char line[8192];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!read_line(line, sizeof(line))) break;
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
            else printf("delete nao suportado nesta estrutura\n");
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

    if (st.bf) st.ops->destroy(st.bf);
    return 0;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *mode = "demo";
    const char *filter = "sbf";
    const char *type = "uuid";
    size_t n_override = 0;
    double p_target = 0.01;
    unsigned reps = 5;
    unsigned sizing = SIZING_BOTH;
    const char *out_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--repl") == 0) mode = "repl";
        else if (strcmp(argv[i], "--test") == 0) mode = "test";
        else if (strcmp(argv[i], "--demo") == 0) mode = "demo";
        else if (strcmp(argv[i], "--benchmark") == 0) mode = "benchmark";
        else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
        else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) type = argv[++i];
        else if (strcmp(argv[i], "--n") == 0 && i + 1 < argc) n_override = (size_t)atol(argv[++i]);
        else if (strcmp(argv[i], "--p") == 0 && i + 1 < argc) p_target = atof(argv[++i]);
        else if (strcmp(argv[i], "--reps") == 0 && i + 1 < argc) reps = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "--sizing") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "same_m") == 0) sizing = SIZING_SAME_M;
            else if (strcmp(v, "same_fpr") == 0) sizing = SIZING_SAME_FPR;
            else if (strcmp(v, "both") == 0) sizing = SIZING_BOTH;
            else { fprintf(stderr, "--sizing: use same_m, same_fpr ou both\n"); return 2; }
        }
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) {
            printf("uso: bloom_app [--demo|--test|--repl [--filter NAME]|--benchmark [--type T] [--n N] [--p P] [--reps R] [--sizing same_m|same_fpr|both] [--out FILE]]\n");
            printf("filtros: sbf cbf pbf rdbf baseline\n");
            printf("tipos  : uuid cpf cnpj url\n");
            return 0;
        }
    }

    if (strcmp(mode, "repl") == 0) return repl(filter);
    if (strcmp(mode, "test") == 0) return run_test();
    if (strcmp(mode, "benchmark") == 0) {
        return benchmark_run(type, n_override, p_target, reps, sizing, out_path);
    }
    return demo();
}
