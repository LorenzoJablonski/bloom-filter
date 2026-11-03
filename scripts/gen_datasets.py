#!/usr/bin/env python3
"""Generate synthetic key pools (UUID, CPF, CNPJ, URL) for Bloom Filter experiments.

Output: data/<type>/pool.txt  (one key per line, newline-terminated)

Usage: python3 scripts/gen_datasets.py --type <uuid|cpf|cnpj|url> --count N [--outdir data]
"""

import argparse
import json
import random
import sys
import uuid as uuidlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def gen_cpf(rng: random.Random) -> str:
    """Generate a structurally valid CPF (11 digits) with correct check digits."""
    digits = [rng.randint(0, 9) for _ in range(9)]
    for n in (9, 10):
        weights = range(n + 1, 1, -1)  # 10..2 for 9 digits, 11..2 for 10
        total = sum(d * w for d, w in zip(digits, weights))
        rest = (total * 10) % 11
        digits.append(0 if rest == 10 else rest)
    return "".join(str(d) for d in digits)


def gen_cnpj(rng: random.Random) -> str:
    """Generate a structurally valid CNPJ (14 digits) with correct check digits."""
    weights1 = [5, 4, 3, 2, 9, 8, 7, 6, 5, 4, 3, 2]
    weights2 = [6] + weights1
    digits = [rng.randint(0, 9) for _ in range(12)]
    for weights in (weights1, weights2):
        total = sum(d * w for d, w in zip(digits, weights))
        rest = 11 - (total % 11)
        digits.append(0 if rest >= 10 else rest)
    return "".join(str(d) for d in digits)


WORDS = [
    "api", "app", "auth", "blog", "cdn", "checkout", "docs", "files", "gateway",
    "login", "metrics", "orders", "payments", "search", "shop", "status", "store",
    "user", "webhook", "www",
]
TLDS = ["com", "net", "org", "io", "br", "com.br", "net.br", "dev", "co"]
DOMAINS = ["acme", "globex", "initech", "umbrella", "stark", "wayne", "wonka",
           "boring", "hooli", "piedpiper", "cyberdyne", "tyrell"]


def gen_url(rng: random.Random) -> str:
    """Generate a realistic URL sharing common prefixes (protocol + host)."""
    scheme = rng.choice(["http", "https"])
    domain = rng.choice(DOMAINS)
    tld = rng.choice(TLDS)
    path_count = rng.randint(0, 3)
    parts = [rng.choice(WORDS) for _ in range(path_count)]
    path = "/" + "/".join(parts) if parts else ""
    query = ""
    if rng.random() < 0.3:
        query = "?" + rng.choice(["id", "page", "ref", "token", "q"]) + "=" + str(rng.randint(0, 99999))
    return f"{scheme}://{domain}.{tld}{path}{query}"


GENERATORS = {
    "uuid": lambda rng: str(uuidlib.uuid4()),
    "cpf": gen_cpf,
    "cnpj": gen_cnpj,
    "url": gen_url,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--type", choices=sorted(GENERATORS), required=True)
    ap.add_argument("--count", type=int, required=True)
    ap.add_argument("--outdir", default="data")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    outdir = (ROOT / args.outdir) / args.type
    outdir.mkdir(parents=True, exist_ok=True)

    gen = GENERATORS[args.type]
    seen = set()
    outfile = outdir / "pool.txt"

    with outfile.open("w") as fh:
        written = 0
        while written < args.count:
            key = gen(rng)
            if key in seen:
                continue
            seen.add(key)
            fh.write(key + "\n")
            written += 1

    # provenance for scripts/run.py (partition.py adds its own fields)
    meta = {"type": args.type, "pool_count": written, "pool_seed": args.seed}
    (outdir / "meta.json").write_text(json.dumps(meta, indent=2) + "\n")

    print(f"wrote {written} unique {args.type} keys -> {outfile}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
