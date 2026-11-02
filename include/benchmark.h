#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stddef.h>
#include "common.h"
#include "bf.h"

/*
 * Run benchmarks for every registered filter on the dataset type `type`
 * (uuid/cpf/cnpj/url). Results are written as CSV to `out_path` (or stdout
 * when NULL), one row per repetition (plus an unreported warm-up run).
 * Returns 0 on success.
 */
/* sizing regimes (bit flags) */
#define SIZING_SAME_M   1u  /* all filters get the SBF's (m, k) for p */
#define SIZING_SAME_FPR 2u  /* each filter sized by its own <model>_params */
#define SIZING_BOTH     (SIZING_SAME_M | SIZING_SAME_FPR)

int benchmark_run(const char *type, size_t n_override, double p_target,
                  unsigned reps, unsigned sizing, const char *out_path);

#endif /* BENCHMARK_H */
