#!/usr/bin/env bash
# Runs inside the VM (PLAN=extra.sh scripts/vm/vm.sh official): the measurements
# added after the official campaign (protocol 5). Collisions come from an untimed,
# deterministic pass, so one repetition is enough for every structure, type and N;
# delete timing needs repetitions and runs only on UUID (the baseline only where it
# fits in RAM: deleting needs N extra lookups, hours once it pages).
set -uo pipefail

EXP=${EXP:-/mnt/exp}
ALL_N="1000000 5000000 10000000 20000000"
FILTERS_ONLY="sbf cbf pbf rdbf hybrid_512 hybrid_4096 hybrid_c_512 hybrid_c_4096"

missing=0
for t in uuid cpf cnpj url; do
    for n in $ALL_N; do
        [ -f "$EXP/n$n/$t/insert.txt" ] || { echo "extra: missing dataset $t N=$n" >&2; missing=$((missing + 1)); }
    done
done
[ "$missing" -eq 0 ] || { echo "extra: $missing dataset(s) missing, nothing was run" >&2; exit 1; }

fails=0
step() {
    echo "##### $(date -Is) $*"
    env "$@" bash "$HOME/campaign.sh" || fails=$((fails + 1))
}

step LABEL=colisoes TYPES="uuid cpf cnpj url" NS="$ALL_N" FILTERS="$FILTERS_ONLY" EXPERIMENTS=benchmark REPS=1 REPEAT=1
step LABEL=delecao TYPES=uuid NS="$ALL_N" FILTERS="cbf hybrid_c_512 hybrid_c_4096" EXPERIMENTS=benchmark REPS=5 REPEAT=3
step LABEL=delecao TYPES=uuid NS="1000000 5000000" FILTERS=baseline EXPERIMENTS=benchmark REPS=5 REPEAT=3

echo "##### $(date -Is) extra finished, $fails step(s) with failures"
[ "$fails" -eq 0 ]
