# Bloom Filter TCC

## Project
- TCC: "Filtros de Bloom para Otimização de I/O em Big Data" — UCS
- 5 Bloom Filter variants (SBF, CBF, PBF, rDBF, Hybrid) + deterministic baseline (uthash)
- All filters implemented in C (C99), Python for dataset generation and analysis

## Build
```bash
make all          # builds bloom_app (all filters + baseline + main)
make sbf          # individual module object (also: cbf, pbf, rdbf, baseline)
make data         # generates datasets: N=2000 TYPES="uuid cpf cnpj url" SEED=42
make benchmark    # results/benchmark_<type>.csv, long format: REPS=5 P=0.01
make test         # quick invariants via the generic interface (--test)
make check        # C unit tests in tests/
make clean        # removes build/ and bloom_app
```

Compiler: GCC with `-std=c99 -Wall -Wextra -O3`. Links pthreads (`-lpthread`).

## Directory Layout
- `include/` — C headers (one per filter + common types)
- `src/` — C sources (murmurhash2.c, each filter, baseline.c, main.c)
- `scripts/` — Python: dataset generation, partitioning, analysis
- `tests/` — C test files per filter
- `data/` and `results/` — generated artifacts (not source)

## Branch Strategy
Git Flow: `main` (stable) → `develop` (integration) → `feature/*` (per component).

Merge order matters — murmurhash is prerequisite for all filters:
1. `feature/murmurhash` — DONE (commit 4628533, merged to develop)
2. `feature/datasets` — DONE (scripts + protocol partition, merged to develop)
3. `feature/sbf`, `feature/baseline` — DONE
4. `feature/cbf`, `feature/pbf`, `feature/rdbf` — DONE (rDBF has no delete: 1 bit/key, no counters)
5. `feature/benchmark`, `feature/threads`
6. `feature/hybrid` (depends on all above)
7. `feature/analysis`, `feature/docs`

## Key Conventions
- MurmurHash2 (32-bit) is the sole hash function — seed varies per invocation
- MurmurHash2 is implemented from scratch (appleby-style), validated against the published reference; seed operates via `murmurhash2(key, len, seed)`
- Bitwise operations for filter manipulation (no abstractions)
- pthreads with mutexes for mutable variants (CBF, hybrid)
- Every structure is exposed through `bf_ops_t` (include/bf.h, src/bf.c); demo, REPL and benchmark iterate the registry — register new filters there
- Baseline owns copies of its keys; filters never keep key pointers
- Benchmark: rep 0 is an unreported warm-up; hash-call counts come from an untimed pass (`murmurhash2_counter`), never enable it in timed code
- Commit messages in English
