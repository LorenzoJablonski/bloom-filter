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
make benchmark    # via scripts/run.py -> results/bloom.db, one process per structure x regime: REPS=5 P=0.01 REPEAT=1 LABEL= [ALLOW_DIRTY=1]
make iobench      # I/O gatekeeper experiment, same flags + TABLES="1 8" NEG="0.5 0.9" QUERIES=10000 STORE_DIR=data/store [CACHED=1]
python3 scripts/run.py threadbench --filters pbf hybrid_512 hybrid_c_512 --threads 1 2 4 6   # concurrency (experiment 3)
python3 scripts/analyze.py runs|bench|io|threads|freeze   # tables (terminal/--csv/--latex) from results/bloom.db
make venv         # .venv with matplotlib (requirements.txt; uses uv when available)
make plots        # figures in results/plots: FORMAT="pdf png" PLOT_ARGS="--label ..." DB=
make test         # quick invariants via the generic interface (--test)
make check        # C unit tests in tests/
make clean        # removes build/ and bloom_app
scripts/vm/vm.sh  # controlled environment for official runs: create|push-code|data|campaign|official|pull (see its header)
scripts/vm/vm6.sh # the same VM with 6 vCPUs, for threadbench (only one of the two may run at a time)
```

Compiler: GCC with `-std=c99 -Wall -Wextra -O3`. Links pthreads (`-lpthread`).

## Directory Layout
- `include/` — C headers (one per filter + common types)
- `src/` — C sources (murmurhash2.c, each filter, baseline.c, main.c)
- `scripts/` — Python: dataset generation, partitioning, analysis
- `tests/` — C test files per filter
- `tools/` — standalone investigation programs, not part of bloom_app (fprlab.c: why the hybrid's FPR exceeds its formula; build line in its header)
- `data/` and `results/` — generated artifacts (not source)

## Branch Strategy
Git Flow: `main` (stable) → `develop` (integration) → `feature/*` (per component).

Merge order matters — murmurhash is prerequisite for all filters:
1. `feature/murmurhash` — DONE (commit 4628533, merged to develop)
2. `feature/datasets` — DONE (scripts + protocol partition, merged to develop)
3. `feature/sbf`, `feature/baseline` — DONE
4. `feature/cbf`, `feature/pbf`, `feature/rdbf` — DONE (rDBF follows robustBF Eq. 1: X x Y 64-bit cells, beta=61, X != Y primes; no delete: 1 bit/key, no counters)
5. `feature/benchmark` — DONE; `feature/threads` — DONE (experiment 3, concurrency)
   `feature/rusage`, `feature/isolation`, `feature/vm` (memory/paging metrics, one process per structure, VM for the official runs) — DONE
6. `feature/hybrid` — key-sharded prime-matrix filter, 1 MurmurHash64A call, double hashing inside the shard;
   variants hybrid_512 / hybrid_4096 (bits) and hybrid_c_512 / hybrid_c_4096 (4-bit counters, delete)
7. `feature/io` — DONE (store + iobench); `feature/analysis` — DONE (SQLite results, tables); `feature/plots` — DONE (`analyze.py --plot`); `feature/docs`
8. `feature/results` — DONE: official VM campaign frozen in results/campaigns/vm-nvme/
9. `feature/delete-collisions`, `feature/threads`, `feature/hybrid-fpr` — DONE (protocol 5,
   schema 6; campaigns in results/campaigns/vm-nvme-extra/ and vm6-nvme/, fprlab/)
10. `text/*` — chapter base content in docs/texto/ (methods, results, conclusion)
Development closed on 2026-11-09; only writing remains. `main` still holds the initial
commit: bring `develop` into it at delivery, when the author asks.

## Key Conventions
- MurmurHash2 family only: MurmurHash2 (32-bit, seed varies per invocation) for SBF/CBF/PBF/rDBF; MurmurHash64A (64-bit, one call) for the hybrid. Both checked against smhasher verification values in tests/test_murmurhash2.c
- MurmurHash2 is implemented from scratch (appleby-style), validated against the published reference; seed operates via `murmurhash2(key, len, seed)`
- Bitwise operations for filter manipulation (no abstractions)
- Threads only in experiment 3 (src/threadbench.c); experiments 1 and 2 stay single-threaded. Concurrent inserts are offered through `bf_ops_t.conc` (`bf_conc_t`): PBF by partition range (no lock: partitions never share a cache line) or atomic OR; bit hybrids atomic OR; counting hybrids under a mutex, one global or one per shard (trylock first, failures counted as lock waits). These functions are appended after the serial code, which must keep compiling to the same instructions (objdump). Lookups run after a barrier and take no lock. Every strategy must answer exactly like the serial insert (`make test` checks it); workers are pinned one per physical core first
- Every structure is exposed through `bf_ops_t` (include/bf.h, src/bf.c); demo, REPL and benchmark iterate the registry — register new filters there
- Baseline owns copies of its keys; filters never keep key pointers
- Each filter has `<model>_params(n, p)` (sizing for target FPR by its own formula), registered in `bf_ops_t.params`
- I/O experiment (src/iobench.c, src/store.c): SSTable-like sorted tables on disk, one 4 KB block read per probe, O_DIRECT by default; tables are built once in STORE_DIR (--build-store) and reused (fingerprint in header). Each repetition draws its own query sample, the same for every gatekeeper: repeating one sample would keep its pages in RAM and hide paging. On btrfs with compression, O_DIRECT silently falls back to the page cache — iobench sets NOCOMP on its table dir and warns when proc read_bytes << reads*4096
- Datasets are streamed in batches through a fixed 1 MiB buffer (include/keyset.h: dataset_open, keystream_next), and only the operations are timed: a measured process holds the structure under test, not the dataset. Whole-file loading (keyset_load) is only for the unmeasured table build. Generate one dataset per N (`make data N=...`); `--n` keeps keys in RAM and is for quick tests only
- One structure per process: run.py launches bloom_app with `--filter NAME` per structure x sizing regime (benchmark) or per structure x T x neg (iobench, `none` is the control), so paging is attributed to the structure that caused it. Disk tables are built first by `--iobench --build-store`, unmeasured; a measured run fails if they are missing
- Benchmark: rep 0 is an unreported warm-up; hash-call and collision counts come from an untimed pass (`murmurhash2_counter`, `bf_ops_t.insert_counted`), never enable or call them in timed code. Each filter keeps `<model>_insert_counted` apart from `<model>_insert` so the measured insert compiles to the same code (checked by objdump and an A/B timing run). Structures with delete (CBF, hybrid_c_*, baseline) then delete Dexist after the lookups and the fault count; state metrics (memory, fill, FPR) are read before that
- Results: scripts/run.py records each bloom_app run in results/bloom.db (runs + filter_results/io_results, schema in scripts/resultsdb.py) and keeps the raw CSV in results/raw/. Both are ignored by git; a finished campaign is frozen (`analyze.py freeze --out`) into results/campaigns/<name>/ with its raw CSVs, logs, tables and plots, which is versioned (see its README). analyze.py opens databases read-only (`resultsdb.connect_readonly`: no migration), so querying a frozen database never changes it; only run.py writes and migrates. Runs are pooled only with equal config_key (protocol, dataset hash, parameters; not the structure or its sizing regime, which are result columns) and env_key (machine, device, swap). Bump `BF_PROTOCOL_VERSION` (include/common.h) when a change breaks comparability; update resultsdb column lists when CSV columns change. Python scripts use the standard library only; matplotlib is optional and confined to scripts/plots.py (imported only with --plot)
- Commit messages in English
