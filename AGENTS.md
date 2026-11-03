# Bloom Filter TCC

Status, priorities, working agreements and decision log: **docs/PROGRESS.md** (read it first).
Never commit, merge or push before the author reviews and approves the diff.

## Project
- TCC: "Filtros de Bloom para Otimização de I/O em Big Data" — UCS
- 5 Bloom Filter variants (SBF, CBF, PBF, rDBF, Hybrid) + deterministic baseline (uthash)
- All filters implemented in C (C99), Python for dataset generation and analysis

## Build
```bash
make all          # builds bloom_app (all filters + baseline + main)
make sbf          # individual module object (also: cbf, pbf, rdbf, hybrid, baseline)
make data         # generates datasets: N=2000 TYPES="uuid cpf cnpj url" SEED=42
make benchmark    # via scripts/run.py -> results/bloom.db: REPS=5 P=0.01 REPEAT=1 LABEL= [ALLOW_DIRTY=1]
make iobench      # I/O gatekeeper experiment, same flags + TABLES="1 8" NEG="0.5 0.9" QUERIES=10000 STORE_DIR=data/store [CACHED=1]
python3 scripts/analyze.py runs|bench|io|freeze   # tables (terminal/--csv/--latex) from results/bloom.db
make venv         # .venv with matplotlib (requirements.txt; uses uv when available)
make plots        # figures in results/plots: FORMAT="pdf png" PLOT_ARGS="--label ..." DB=
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
4. `feature/cbf`, `feature/pbf`, `feature/rdbf` — DONE (rDBF follows robustBF Eq. 1: X x Y 64-bit cells, beta=61, X != Y primes; no delete: 1 bit/key, no counters)
5. `feature/benchmark` — DONE; `feature/threads` — later (after scale runs)
6. `feature/hybrid` — key-sharded prime-matrix filter, 1 MurmurHash64A call, double hashing inside the shard;
   variants hybrid_512 / hybrid_4096 (bits) and hybrid_c_512 / hybrid_c_4096 (4-bit counters, delete)
7. `feature/io` — DONE (store + iobench); `feature/analysis` — DONE (SQLite results, tables); `feature/plots` — figures (`analyze.py --plot`); `feature/docs`

## Key Conventions
- MurmurHash2 family only: MurmurHash2 (32-bit, seed varies per invocation) for SBF/CBF/PBF/rDBF; MurmurHash64A (64-bit, one call) for the hybrid. Both checked against smhasher verification values in tests/test_murmurhash2.c
- MurmurHash2 is implemented from scratch (appleby-style), validated against the published reference; seed operates via `murmurhash2(key, len, seed)`
- Bitwise operations for filter manipulation (no abstractions)
- pthreads with mutexes for mutable variants (CBF, hybrid)
- Every structure is exposed through `bf_ops_t` (include/bf.h, src/bf.c); demo, REPL and benchmark iterate the registry — register new filters there
- Baseline owns copies of its keys; filters never keep key pointers
- Each filter has `<model>_params(n, p)` (sizing for target FPR by its own formula), registered in `bf_ops_t.params`
- I/O experiment (src/iobench.c, src/store.c): SSTable-like sorted tables on disk, one 4 KB block read per probe, O_DIRECT by default; tables are built once in STORE_DIR and reused (fingerprint in header). On btrfs with compression, O_DIRECT silently falls back to the page cache — iobench sets NOCOMP on its table dir and warns when proc read_bytes << reads*4096
- Dataset loading is shared via include/keyset.h (dataset_load)
- Benchmark: rep 0 is an unreported warm-up; hash-call counts come from an untimed pass (`murmurhash2_counter`), never enable it in timed code
- Results: scripts/run.py records each bloom_app run in results/bloom.db (runs + filter_results/io_results, schema in scripts/resultsdb.py) and keeps the raw CSV in results/raw/. Runs are pooled only with equal config_key (protocol, dataset hash, parameters) and env_key (machine, device). Bump `BF_PROTOCOL_VERSION` (include/common.h) when a change breaks comparability; update resultsdb column lists when CSV columns change. Python scripts use the standard library only; matplotlib is optional and confined to scripts/plots.py (imported only with --plot)
- Commit messages in English
