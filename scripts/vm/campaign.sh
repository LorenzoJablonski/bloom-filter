#!/usr/bin/env bash
# Runs inside the VM (scripts/vm/vm.sh campaign VAR=value...): the experiments for
# each N, with the dataset of that N linked as data/<type>. REPEAT is the outer loop,
# so repetitions of one configuration are spread over the whole campaign.
set -uo pipefail

NS=${NS:-"1000000 5000000 10000000 20000000"}
TYPES=${TYPES:-uuid}
REPS=${REPS:-5}
REPEAT=${REPEAT:-3}
LABEL=${LABEL:-vm}
EXPERIMENTS=${EXPERIMENTS:-"benchmark iobench"}
FILTERS=${FILTERS:-}
TABLES=${TABLES:-"1 8"}
NEG=${NEG:-"0.5 0.9"}
THREADS=${THREADS:-"1 2 4 6"} # threadbench
EXP=${EXP:-/mnt/exp}

cd "$HOME/bloom" || exit 1
fails=0
for r in $(seq 1 "$REPEAT"); do
    for n in $NS; do
        for t in $TYPES; do
            if [ ! -f "$EXP/n$n/$t/insert.txt" ]; then
                echo "campaign: missing $EXP/n$n/$t (vm.sh data $n $t)" >&2
                fails=$((fails + 1))
                continue
            fi
            ln -sfn "$EXP/n$n/$t" "data/$t"
            for e in $EXPERIMENTS; do
                echo "=== $(date -Is) pass $r/$REPEAT N=$n $t $e"
                args=(--types "$t" --reps "$REPS" --label "$LABEL")
                [ -n "$FILTERS" ] && args+=(--filters $FILTERS)
                [ "$e" = iobench ] && args+=(--tables $TABLES --neg $NEG --store-dir "$EXP/store")
                [ "$e" = threadbench ] && args+=(--threads $THREADS)
                python3 scripts/run.py "$e" "${args[@]}" || fails=$((fails + 1))
            done
        done
    done
done
echo "=== $(date -Is) finished, $fails step(s) with failures"
[ "$fails" -eq 0 ]
