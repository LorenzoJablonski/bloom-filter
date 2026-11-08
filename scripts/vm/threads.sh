#!/usr/bin/env bash
# Runs inside the 6-vCPU VM (PLAN=threads.sh scripts/vm/vm6.sh official): the
# concurrency campaign. N = 1M (the PBF, 1.2 MB, fits in the caches) and 20M
# (24 MB, beyond the 18 MB L3), so scaling is seen both bound by the CPU and by
# memory. The bridge step repeats a slice of the official campaign, single
# threaded, so the results of the two VMs can be compared: if they match, the
# 2-vCPU campaigns stand and only the thread count differs between the VMs.
set -uo pipefail

EXP=${EXP:-/mnt/exp}
NS="1000000 20000000"

missing=0
for n in $NS; do
    [ -f "$EXP/n$n/uuid/insert.txt" ] || { echo "threads: missing dataset uuid N=$n" >&2; missing=$((missing + 1)); }
done
[ "$(nproc)" -ge 6 ] || { echo "threads: $(nproc) CPUs; this plan expects 6 (vm6.sh)" >&2; missing=$((missing + 1)); }
[ "$missing" -eq 0 ] || { echo "threads: nothing was run" >&2; exit 1; }

fails=0
step() {
    echo "##### $(date -Is) $*"
    env "$@" bash "$HOME/campaign.sh" || fails=$((fails + 1))
}

step LABEL=threads TYPES=uuid NS="$NS" EXPERIMENTS=threadbench THREADS="1 2 4 6" \
     FILTERS="sbf pbf hybrid_512 hybrid_4096 hybrid_c_512 hybrid_c_4096" REPS=5 REPEAT=3
step LABEL=ponte TYPES=uuid NS="$NS" EXPERIMENTS=benchmark \
     FILTERS="sbf pbf hybrid_512 hybrid_c_512" REPS=5 REPEAT=3

echo "##### $(date -Is) threads finished, $fails step(s) with failures"
[ "$fails" -eq 0 ]
