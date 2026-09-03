# Bloom Filter TCC

## Project
- TCC: "Filtros de Bloom para Otimização de I/O em Big Data" — UCS
- 5 Bloom Filter variants (SBF, CBF, PBF, rDBF, Hybrid) + deterministic baseline (uthash)
- All filters implemented in C (C99), Python for dataset generation and analysis

## Build
```bash
make all          # builds bloom_app (all filters + baseline + main)
make sbf          # individual filter target (also: cbf, pbf, rdbf, hybrid, baseline)
make benchmark    # runs benchmark suite (requires bloom_app built)
make test         # runs tests
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
3. `feature/sbf`, `feature/baseline`
4. `feature/cbf`, `feature/pbf`, `feature/rdbf`
5. `feature/benchmark`, `feature/threads`
6. `feature/hybrid` (depends on all above)
7. `feature/analysis`, `feature/docs`

## Key Conventions
- MurmurHash2 (32-bit) is the sole hash function — seed varies per invocation
- MurmurHash2 is implemented from scratch (appleby-style), validated against the published reference; seed operates via `murmurhash2(key, len, seed)`
- Bitwise operations for filter manipulation (no abstractions)
- pthreads with mutexes for mutable variants (CBF, hybrid)
- Commit messages in English
