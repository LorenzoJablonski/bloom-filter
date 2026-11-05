#!/usr/bin/env python3
"""Run experiments through bloom_app and record them in the results database.

Every bloom_app invocation becomes one row in `runs` (code version, machine,
storage device, dataset, parameters) plus its CSV rows in filter_results or
io_results. The raw CSV is kept in results/raw/.

Each structure runs in its own process, once per sizing regime (benchmark) or
per T x neg_ratio (iobench): nothing a large structure pushes to swap can be
paid by the next one. Disk tables are built before the grid, in a separate
process that is not recorded.

Examples
  python3 scripts/run.py benchmark --types uuid cpf --reps 5 --repeat 3 --label baseline-ssd
  python3 scripts/run.py iobench --types uuid --tables 1 8 --neg 0.5 0.9 \\
      --store-dir /mnt/hd/store --label hd-T8
  python3 scripts/run.py benchmark --filters sbf baseline --label test

--repeat R runs the whole grid R times (outer loop), so repetitions of the
same configuration are spread over time instead of back to back.
"""

import argparse
import datetime as dt
import hashlib
import itertools
import json
import os
import platform
import shlex
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import resultsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "bloom_app"


# ---------------------------------------------------------------- helpers

def sh(cmd, cwd=ROOT):
    """Output of a command, or None if it is missing/fails."""
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return r.stdout.strip() if r.returncode == 0 else None


def sha256_file(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        while block := fh.read(chunk):
            h.update(block)
    return h.hexdigest()


def count_lines(path):
    with open(path, "rb") as fh:
        return sum(1 for line in fh if line.strip())


def now_iso():
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


# ---------------------------------------------------------------- context

def code_info():
    info = sh([str(BINARY), "--info"]) or ""
    protocol, cc, filters = None, None, []
    for line in info.splitlines():
        key, _, value = line.partition("=")
        if key == "protocol":
            protocol = int(value)
        elif key == "cc":
            cc = value
        elif key == "filter":
            name, exact, delete, fixed_k = value.split(",")
            filters.append((name, int(exact), int(delete), int(fixed_k)))
    if protocol is None:
        raise SystemExit("bloom_app --info failed; is the binary built?")
    status = sh(["git", "status", "--porcelain", "--untracked-files=no"])
    return {
        "protocol": protocol,
        "cc_version": cc,
        "git_commit": sh(["git", "rev-parse", "HEAD"]),
        "git_branch": sh(["git", "rev-parse", "--abbrev-ref", "HEAD"]),
        "git_dirty": None if status is None else int(bool(status)),
        "binary_sha256": sha256_file(BINARY),
    }, filters


def cgroup_memory_limit():
    try:
        for line in Path("/proc/self/cgroup").read_text().splitlines():
            if line.startswith("0::"):
                value = Path("/sys/fs/cgroup" + line[3:], "memory.max").read_text().strip()
                return None if value == "max" else int(value)
    except (OSError, ValueError):
        pass
    return None


def disk_info(source):
    """(rotational, model) of the physical disk behind a device or partition."""
    tree = sh(["lsblk", "-s", "-n", "-l", "-o", "TYPE,ROTA,MODEL", source])
    for line in (tree or "").splitlines():  # through partitions, LUKS, LVM
        parts = line.split(None, 2)
        if parts and parts[0] == "disk":
            return (int(parts[1]) if len(parts) > 1 else None,
                    parts[2].strip() if len(parts) > 2 else None)
    return None, None


def swap_info():
    """Kind, size and disk of the active swap: zram (compressed RAM), SSD and HD
    behave very differently once a structure no longer fits in memory."""
    kinds, total, models, rotas = set(), 0, set(), set()
    try:
        for line in Path("/proc/swaps").read_text().splitlines()[1:]:
            parts = line.split()
            if len(parts) >= 3:
                zram = "/zram" in parts[0]
                kinds.add("zram" if zram else "disk")
                total += int(parts[2])
                if not zram and parts[1] == "partition":  # swap files: disk unknown here
                    rota, model = disk_info(parts[0])
                    rotas.add(rota)
                    models.add(model)
    except (OSError, ValueError):
        return {"swap_kind": None, "swap_total_kb": None}
    kind = "none" if not kinds else kinds.pop() if len(kinds) == 1 else "mixed"
    return {"swap_kind": kind, "swap_total_kb": total,
            "swap_model": "+".join(sorted(m for m in models if m)) or None,
            "swap_rotational": max((r for r in rotas if r is not None), default=None)}


def vmstat_swap():
    """System-wide pages swapped in/out so far (/proc/vmstat), or None."""
    vals = {}
    try:
        for line in Path("/proc/vmstat").read_text().splitlines():
            key, _, value = line.partition(" ")
            if key in ("pswpin", "pswpout"):
                vals[key] = int(value)
    except (OSError, ValueError):
        return None
    return vals if len(vals) == 2 else None


def run_measured(cmd):
    """Run cmd and return (exit code, resource fields for the runs table)."""
    sw0 = vmstat_swap()
    proc = subprocess.Popen(cmd, cwd=ROOT)
    _, status, ru = os.wait4(proc.pid, 0)
    proc.returncode = os.waitstatus_to_exitcode(status)
    sw1 = vmstat_swap()
    res = {
        "maxrss_kb": ru.ru_maxrss,  # Linux: KiB
        "majflt": ru.ru_majflt, "minflt": ru.ru_minflt,
        "utime_s": round(ru.ru_utime, 3), "stime_s": round(ru.ru_stime, 3),
        "nvcsw": ru.ru_nvcsw, "nivcsw": ru.ru_nivcsw,
    }
    if sw0 and sw1:  # whole system: other processes count too
        res["swap_in_pages"] = sw1["pswpin"] - sw0["pswpin"]
        res["swap_out_pages"] = sw1["pswpout"] - sw0["pswpout"]
    return proc.returncode, res


def machine_info():
    cpu_model, mem_kb = None, None
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu_model = line.split(":", 1)[1].strip()
                break
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemTotal:"):
                mem_kb = int(line.split()[1])
                break
    except OSError:
        pass
    virt = sh(["systemd-detect-virt"])  # non-zero exit ("none") -> None
    return {
        "host": socket.gethostname(),
        "cpu_model": cpu_model,
        "cpu_count": os.cpu_count(),
        "mem_total_kb": mem_kb,
        "mem_limit_bytes": cgroup_memory_limit(),
        "kernel": platform.release(),
        "virt": virt or "none",
        **swap_info(),
    }


def storage_info(store_dir):
    """Filesystem and physical device behind store_dir (best effort)."""
    d = Path(store_dir)
    d.mkdir(parents=True, exist_ok=True)
    out = {"store_dir": str(d.resolve())}
    mnt = sh(["findmnt", "-n", "-o", "SOURCE,FSTYPE,OPTIONS", "--target", str(d)])
    if not mnt:
        return out
    source, fstype, opts = (mnt.split() + [None, None, None])[:3]
    source = source.split("[", 1)[0]  # btrfs: /dev/nvme0n1p3[/home]
    out.update(store_fstype=fstype, store_fsopts=opts, store_device=source)
    out["store_rotational"], out["store_model"] = disk_info(source)
    return out


_dataset_cache = {}


def dataset_info(dtype):
    d = ROOT / "data" / dtype
    files = [d / "insert.txt", d / "exist.txt", d / "inexist.txt"]
    for f in files:
        if not f.exists():
            raise SystemExit(f"missing {f}; run `make data TYPES={dtype}` first")
    stamp = tuple((f.stat().st_size, f.stat().st_mtime_ns) for f in files)
    if _dataset_cache.get(dtype, (None,))[0] != stamp:
        h = hashlib.sha256()
        for f in files:
            h.update(sha256_file(f).encode())
        meta = d / "meta.json"
        _dataset_cache[dtype] = (stamp, {
            "dataset_type": dtype,
            "dataset_sha256": h.hexdigest(),
            "dataset_meta": meta.read_text() if meta.exists() else None,
            "dataset_n_ins": count_lines(files[0]),
            "dataset_n_exist": count_lines(files[1]),
            "dataset_n_inexist": count_lines(files[2]),
        })
    return dict(_dataset_cache[dtype][1])


# ---------------------------------------------------------------- grid

def build_jobs(args, filters):
    """List of (params, argv) for one pass over the grid; one structure per job.
    filters: (name, exact, supports_delete, fixed_k) from bloom_app --info."""
    jobs = []
    names = {f[0] for f in filters}
    unknown = set(args.filters or []) - names - {"none"}
    if unknown:
        raise SystemExit(f"unknown filter(s) {sorted(unknown)}; available: {sorted(names)}")
    chosen = [f for f in filters if not args.filters or f[0] in args.filters]
    if args.experiment == "benchmark":
        sizings = ["same_m", "same_fpr"] if args.sizing == "both" else [args.sizing]
        for t in args.types:
            for name, exact, _, _ in chosen:
                for sizing in (["none"] if exact else sizings):
                    params = {"dataset_type": t, "filter": name, "n_override": args.n,
                              "p_target": args.p, "reps": args.reps, "sizing": sizing}
                    argv = ["--benchmark", "--type", t, "--filter", name, "--p", repr(args.p),
                            "--reps", str(args.reps),
                            "--sizing", "both" if exact else sizing]
                    if args.n:
                        argv += ["--n", str(args.n)]
                    jobs.append((params, argv))
    else:
        gatekeepers = [f[0] for f in chosen]
        if not args.filters or "none" in args.filters:
            gatekeepers.insert(0, "none")
        for t, tables, neg, name in itertools.product(args.types, args.tables, args.neg,
                                                      gatekeepers):
            params = {"dataset_type": t, "filter": name, "n_override": args.n, "p_target": args.p,
                      "reps": args.reps, "tables": tables, "neg_ratio": neg,
                      "queries": args.queries, "value_size": args.value_size,
                      "direct": 0 if args.cached else 1}
            argv = ["--iobench", "--type", t, "--filter", name, "--p", repr(args.p),
                    "--reps", str(args.reps), "--tables", str(tables), "--neg", repr(neg),
                    "--queries", str(args.queries), "--value-size", str(args.value_size),
                    "--store-dir", args.store_dir]
            if args.n:
                argv += ["--n", str(args.n)]
            if args.cached:
                argv.append("--cached")
            jobs.append((params, argv))
    return jobs


def build_tables(args):
    """Build the missing disk tables once, before anything is measured."""
    for t, tables in itertools.product(args.types, args.tables):
        argv = ["--iobench", "--build-store", "--type", t, "--tables", str(tables),
                "--value-size", str(args.value_size), "--store-dir", args.store_dir]
        if args.n:
            argv += ["--n", str(args.n)]
        if subprocess.run([str(BINARY)] + argv, cwd=ROOT).returncode != 0:
            raise SystemExit(f"could not build the tables of {t}, T={tables}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="experiment", required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--types", nargs="+", default=["uuid"])
    common.add_argument("--n", type=int, default=0, help="truncate Dins (0 = all)")
    common.add_argument("--p", type=float, default=0.01, help="target FPR")
    common.add_argument("--reps", type=int, default=5, help="reported reps per run (+1 warm-up)")
    common.add_argument("--repeat", type=int, default=1, help="independent runs of the whole grid")
    common.add_argument("--label", default=None, help="campaign name stored with each run")
    common.add_argument("--filters", nargs="+", metavar="NAME",
                        help="structures to run (default: all; iobench also accepts none)")
    common.add_argument("--db", default=str(ROOT / "results" / "bloom.db"))
    common.add_argument("--raw-dir", default=str(ROOT / "results" / "raw"))
    common.add_argument("--allow-dirty", action="store_true",
                        help="run even with uncommitted changes to tracked files")
    common.add_argument("--dry-run", action="store_true", help="print the commands only")

    b = sub.add_parser("benchmark", parents=[common], help="in-memory filter benchmark")
    b.add_argument("--sizing", choices=["same_m", "same_fpr", "both"], default="both")

    io = sub.add_parser("iobench", parents=[common], help="I/O gatekeeper experiment")
    io.add_argument("--tables", nargs="+", type=int, default=[1, 8])
    io.add_argument("--neg", nargs="+", type=float, default=[0.5, 0.9])
    io.add_argument("--queries", type=int, default=10000)
    io.add_argument("--value-size", type=int, default=100)
    io.add_argument("--store-dir", default=str(ROOT / "data" / "store"))
    io.add_argument("--cached", action="store_true", help="page cache instead of O_DIRECT")
    args = ap.parse_args()

    if subprocess.run(["make", "-s", "all"], cwd=ROOT).returncode != 0:
        raise SystemExit("build failed")
    code, filters = code_info()
    jobs = build_jobs(args, filters)
    total = len(jobs) * args.repeat
    if args.dry_run:
        for r in range(args.repeat):
            for _, argv in jobs:
                print(f"[{r + 1}/{args.repeat}]", shlex.join([str(BINARY)] + argv))
        return 0

    if code["git_dirty"] and not args.allow_dirty:
        raise SystemExit("tracked files have uncommitted changes; commit first or pass "
                         "--allow-dirty (those runs are marked git_dirty=1)")
    machine = machine_info()
    storage = storage_info(args.store_dir) if args.experiment == "iobench" else {}

    if args.experiment == "iobench":
        build_tables(args)

    con = resultsdb.connect(args.db)
    resultsdb.sync_filters(con, filters)
    con.commit()

    raw_dir = Path(args.raw_dir)
    raw_dir.mkdir(parents=True, exist_ok=True)
    batch = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    print(f"batch {batch}: {total} run(s), protocol {code['protocol']}, "
          f"commit {(code['git_commit'] or '?')[:8]}{' (dirty)' if code['git_dirty'] else ''}",
          file=sys.stderr)

    failures = 0
    seq = 0
    for r in range(args.repeat):
        for params, argv in jobs:
            seq += 1
            raw = raw_dir / (f"{batch}_{seq:03d}_{args.experiment}_{params['dataset_type']}_"
                             f"{params['filter']}.csv")
            cmd = [str(BINARY)] + argv + ["--out", str(raw)]
            print(f"[{seq}/{total}] {shlex.join(cmd[1:])}", file=sys.stderr)

            run = {"batch_id": batch, "label": args.label, "experiment": args.experiment,
                   "cmdline": shlex.join(cmd), "raw_csv": str(raw.relative_to(ROOT))
                   if raw.is_relative_to(ROOT) else str(raw)}
            run.update(code)
            run.update(machine)
            run.update(storage)
            run.update(dataset_info(params["dataset_type"]))
            run.update(params)

            run["started_at"] = now_iso()
            t0 = time.monotonic()
            rc, resources = run_measured(cmd)
            run.update(resources)
            run["duration_s"] = round(time.monotonic() - t0, 3)
            run["finished_at"] = now_iso()
            run["exit_code"] = rc
            run["status"] = "ok" if rc == 0 and raw.exists() else "failed"

            with con:  # one transaction per run: metadata + rows, or nothing
                run_id = resultsdb.insert_run(con, run)
                rows = 0
                if run["status"] == "ok":
                    try:
                        rows = resultsdb.ingest_csv(con, run_id, args.experiment, raw)
                    except ValueError as e:
                        print(f"  ingest failed: {e}", file=sys.stderr)
                        con.execute("UPDATE runs SET status = 'failed' WHERE run_id = ?",
                                    (run_id,))
                        run["status"] = "failed"
            if run["status"] != "ok":
                failures += 1
            print(f"  run_id={run_id} status={run['status']} rows={rows} "
                  f"({run['duration_s']} s, rss {run['maxrss_kb'] // 1024} MiB, "
                  f"{run['majflt']} major faults)", file=sys.stderr)

    con.close()
    print(f"database: {args.db}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
