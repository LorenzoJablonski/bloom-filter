#!/usr/bin/env bash
# Runs inside the VM (scripts/vm/vm.sh official): the official campaign, one
# campaign.sh call per plan line, cheapest first, so a stop still leaves the most
# results. The plan and its reasons are in docs/PROGRESS.md ("Campanha oficial").
# Checks every dataset and table first: a gap must not surface hours into the run.
set -uo pipefail

LABEL=${LABEL:-oficial}
EXP=${EXP:-/mnt/exp}
FILTERS_ONLY="sbf cbf pbf rdbf hybrid_512 hybrid_4096 hybrid_c_512 hybrid_c_4096"
OTHER_TYPES="cpf cnpj url"
SMALL="1000000 5000000"
LARGE="10000000 20000000"
VALUE_SIZE=${VALUE_SIZE:-100}

missing=0
need_data() {
    [ -f "$EXP/n$1/$2/insert.txt" ] || { echo "official: missing dataset $2 N=$1" >&2; missing=$((missing + 1)); }
}
need_tables() {
    local t
    for t in 1 8; do
        [ -e "$EXP/store/${2}_n${1}_T${t}_v$VALUE_SIZE" ] ||
            { echo "official: missing table $2 N=$1 T=$t" >&2; missing=$((missing + 1)); }
    done
}
for n in $SMALL $LARGE; do
    need_data "$n" uuid
    need_tables "$n" uuid
done
for t in $OTHER_TYPES; do
    for n in $SMALL; do
        need_data "$n" "$t"
        need_tables "$n" "$t"
    done
    for n in $LARGE; do need_data "$n" "$t"; done
done
[ "$missing" -eq 0 ] || { echo "official: $missing item(s) missing, nothing was run" >&2; exit 1; }

fails=0
step() {
    echo "##### $(date -Is) $*"
    env LABEL="$LABEL" "$@" bash "$HOME/campaign.sh" || fails=$((fails + 1))
}

# `none` is the no-filter control of the I/O experiment; the benchmark ignores it
step TYPES=uuid NS="$SMALL $LARGE" FILTERS="none $FILTERS_ONLY" REPS=5 REPEAT=3
step TYPES=uuid NS="$SMALL" FILTERS=baseline REPS=5 REPEAT=3
step TYPES="$OTHER_TYPES" NS="$SMALL" REPS=5 REPEAT=3
step TYPES="$OTHER_TYPES" NS="$LARGE" FILTERS="$FILTERS_ONLY" EXPERIMENTS=benchmark REPS=2 REPEAT=1
# by far the longest: a baseline that does not fit in RAM
step TYPES=uuid NS="$LARGE" FILTERS=baseline REPS=3 REPEAT=1

echo "##### $(date -Is) official finished, $fails step(s) with failures"
[ "$fails" -eq 0 ]
