#!/usr/bin/env python3
"""Partition a generated key pool per the TCC experimental protocol (Sec 4.3.2).

Partitions (mutually disjoint):
  Dins      -> data/<type>/insert.txt    pool to load into the filters (build phase)
  Dcons     -> split into:
      Dexist   -> data/<type>/exist.txt    sample drawn FROM Dins (expect 100% hits)
      Dinexist -> data/<type>/inexist.txt  valid keys SAME domain, never in Dins

Dinexist keys are freshly generated (same generator as the pool) so they are
structurally valid but guaranteed absent from Dins.

Usage:
  python3 scripts/partition.py --type <uuid|cpf|cnpj|url> [--n N] [--exist RATIO] [--inexist COUNT] [--seed S]
Defaults: n = all pool keys, exist = 10% of Dins, inexist = n (matching insertion volume)
"""

import argparse
import random
import sys
from pathlib import Path

from gen_datasets import GENERATORS

ROOT = Path(__file__).resolve().parent.parent


def load_pool(path: Path) -> list[str]:
    with path.open() as fh:
        return [line.rstrip("\n") for line in fh if line.strip()]


def write_keys(path: Path, keys) -> None:
    with path.open("w") as fh:
        for k in keys:
            fh.write(k + "\n")


def _verify(dins, dexist, dinexist) -> None:
    """Enforce the Sec 4.3.2 protocol invariants before writing to disk."""
    ins = set(dins)
    ex = set(dexist)
    inex = set(dinexist)

    if not ex:
        raise ValueError("Dexist is empty (increase --exist)")
    if not ex <= ins:
        raise ValueError("Dexist is not a subset of Dins")
    if not ins.isdisjoint(inex):
        raise ValueError("Dinexist overlaps Dins")
    if len(ins) != len(dins) or len(inex) != len(dinexist):
        raise ValueError("duplicate keys found within a partition")
    if ex & inex:
        raise ValueError("Dexist overlaps Dinexist")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--type", choices=sorted(GENERATORS), required=True)
    ap.add_argument("--n", type=int, default=None, help="Dins size (default: all pool keys)")
    ap.add_argument("--exist", type=float, default=0.10, help="Dexist as ratio of Dins (0..1)")
    ap.add_argument("--inexist", type=int, default=None, help="Dinexist size (default: equals Dins)")
    ap.add_argument("--outdir", default="data")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    datdir = (ROOT / args.outdir) / args.type
    pool = load_pool(datdir / "pool.txt")

    n = args.n if args.n is not None else len(pool)
    if n > len(pool):
        sys.exit(f"error: --n {n} exceeds pool size {len(pool)}")
    if not 0 < args.exist <= 1:
        sys.exit("error: --exist must be in (0, 1]")

    dins = rng.sample(pool, n)
    insert_set = set(dins)
    dexist = rng.sample(dins, max(1, int(round(n * args.exist))))

    inext = args.inexist if args.inexist is not None else n
    gen = GENERATORS[args.type]
    dinexist = set()
    while len(dinexist) < inext:
        key = gen(rng)
        if key not in insert_set:
            dinexist.add(key)

    _verify(dins, dexist, dinexist)

    write_keys(datdir / "insert.txt", dins)
    write_keys(datdir / "exist.txt", dexist)
    write_keys(datdir / "inexist.txt", dinexist)

    print(f"type={args.type} pool={len(pool)}")
    print(f"  Dins={len(dins)} ({datdir / 'insert.txt'})")
    print(f"  Dexist={len(dexist)} ({datdir / 'exist.txt'})")
    print(f"  Dinexist={len(dinexist)} ({datdir / 'inexist.txt'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
