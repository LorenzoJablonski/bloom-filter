#!/usr/bin/env python3
"""Aggregate results from the database into tables (terminal, CSV, LaTeX).

Subcommands
  runs     list recorded runs (to pick labels/batches)
  bench    in-memory benchmark, grouped by type, N, sizing and filter
  io       I/O experiment, grouped by type, N, tables, neg_ratio and gatekeeper
  threads  concurrency experiment, grouped by type, N, filter, strategy and threads;
           speedup = serial mean / mean (same structure, campaign and machine)
  freeze   copy the database to results/frozen/ (snapshot for the document)

Grouping rules: rows are only pooled when the runs share config_key (same
protocol, dataset content and parameters) and env_key (same machine and
device). By default only the newest protocol version and clean runs
(git_dirty = 0) are used; --include-dirty and --protocol change that.

Every reported repetition of every selected run is one sample. Mean, standard
deviation and a 95% confidence interval (Student t) are computed per group.

Examples
  python3 scripts/analyze.py runs
  python3 scripts/analyze.py bench --type uuid --sizing same_fpr --latex results/tables/bench_uuid.tex
  python3 scripts/analyze.py io --label hd-T8 --csv results/tables/io.csv
  .venv/bin/python scripts/analyze.py bench --plot results/plots

--plot DIR writes one figure per group of comparable runs (PDF for LaTeX,
--format png for a quick look). It needs matplotlib (make venv); everything
else stays standard-library only.
"""

import argparse
import csv
import datetime as dt
import math
import sqlite3
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import resultsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# two-sided 95% Student t quantiles, df = 1..30; above that the normal value
_T95 = [12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
        2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
        2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042]


def t95(df):
    if df < 1:
        return float("nan")
    return _T95[df - 1] if df <= len(_T95) else 1.960


def summarize(values):
    vals = [v for v in values if v is not None]
    n = len(vals)
    if n == 0:
        return {"n": 0, "mean": None, "sd": None, "ci": None}
    mean = statistics.fmean(vals)
    sd = statistics.stdev(vals) if n > 1 else 0.0
    ci = t95(n - 1) * sd / math.sqrt(n) if n > 1 else 0.0
    return {"n": n, "mean": mean, "sd": sd, "ci": ci}


# metric: (column, header, scale, decimals)
BENCH_METRICS = {
    "bits_per_key": ("bits_per_key", "bits/key", 1, 2),
    "k": ("k", "k", 1, 0),
    "fpr": ("fpr", "FPR (%)", 100, 3),
    "fpr_theory": ("fpr_theory", "FPR teor. (%)", 100, 3),
    "insert": ("insert_ns_op", "insert (ns/op)", 1, 1),
    "lookup_exist": ("lookup_exist_ns_op", "busca pos. (ns/op)", 1, 1),
    "lookup_inexist": ("lookup_inexist_ns_op", "busca neg. (ns/op)", 1, 1),
    "hash_inexist": ("hash_lookup_inexist", "hashes/busca neg.", 1, 2),
    "fill": ("fill_ratio", "ocupação", 1, 3),
    "collisions": ("collisions", "colisões", 1, 0),
    "collision_rate": ("collision_rate", "taxa de colisões (%)", 100, 2),
    "delete": ("delete_ns_op", "deleção (ns/op)", 1, 1),
    "del_still": ("del_still_present", "deletadas ainda presentes", 1, 0),
    "fn_delete": ("fn_after_delete", "FN após deleção", 1, 0),
    "mem": ("mem_bytes", "memória (B)", 1, 0),
    "rss": ("rss_kb", "RSS (KiB)", 1, 0),
    "majflt": ("majflt", "page faults maiores", 1, 0),
    "minflt": ("minflt", "page faults menores", 1, 0),
}
BENCH_DEFAULT = ["bits_per_key", "k", "fpr", "fpr_theory", "insert", "lookup_exist",
                 "lookup_inexist", "hash_inexist"]

IO_METRICS = {
    "reads": ("reads_per_query", "leituras/consulta", 1, 3),
    "wasted": ("wasted_reads", "leituras inúteis", 1, 0),
    "mean_us": ("mean_us", "latência média (µs)", 1, 1),
    "p50_us": ("p50_us", "p50 (µs)", 1, 1),
    "p99_us": ("p99_us", "p99 (µs)", 1, 1),
    "total_ms": ("total_ms", "tempo total (ms)", 1, 1),
    "gk_mem": ("gk_mem_bytes", "memória filtros (B)", 1, 0),
    "k": ("k", "k", 1, 0),
    "errors": ("errors", "erros", 1, 0),
    "rss": ("rss_kb", "RSS (KiB)", 1, 0),
    "majflt": ("majflt", "page faults maiores", 1, 0),
    "minflt": ("minflt", "page faults menores", 1, 0),
}
IO_DEFAULT = ["reads", "wasted", "mean_us", "p50_us", "p99_us", "gk_mem"]

THREAD_METRICS = {
    "insert": ("insert_ns_op", "insert (ns/op)", 1, 1),
    "lookup_exist": ("lookup_exist_ns_op", "busca pos. (ns/op)", 1, 1),
    "lookup_inexist": ("lookup_inexist_ns_op", "busca neg. (ns/op)", 1, 1),
    "insert_mops": ("insert_mops", "insert (Mops/s)", 1, 1),
    "lookup_mops": ("lookup_inexist_mops", "busca neg. (Mops/s)", 1, 1),
    "speedup_insert": ("speedup_insert", "aceleração insert", 1, 2),
    "speedup_lookup": ("speedup_lookup", "aceleração busca neg.", 1, 2),
    "lock_waits": ("lock_waits", "esperas por trava", 1, 0),
    "lock_bytes": ("lock_bytes", "memória das travas (B)", 1, 0),
    "fp": ("fp", "falsos positivos", 1, 0),
    "fn": ("fn", "falsos negativos", 1, 0),
    "mem": ("mem_bytes", "memória (B)", 1, 0),
}
THREAD_DEFAULT = ["insert", "speedup_insert", "lookup_inexist", "speedup_lookup", "lock_waits"]
# derived from the aggregates, not read from rows: mean of the serial run of the same
# structure (same type, N, config and environment) divided by the mean of this one
THREAD_DERIVED = {"speedup_insert": "insert_ns_op", "speedup_lookup": "lookup_inexist_ns_op"}
STRATEGY_ORDER = {s: i for i, s in enumerate(["serial", "partition", "atomic", "lock_global",
                                               "lock_shard"])}

FILTER_ORDER = {name: i for i, name in enumerate(resultsdb.FILTER_INFO)}
FILTER_ORDER["none"] = -1  # iobench: no gatekeeper first


# ---------------------------------------------------------------- selection

def where_clause(args, extra):
    conds, params = [], []
    if not args.include_dirty:
        conds.append("git_dirty = 0")
    if args.protocol is not None:
        conds.append("protocol = ?")
        params.append(args.protocol)
    for col, val in [("label", args.label), ("batch_id", args.batch),
                     ("type", args.type), ("host", args.host)]:
        if val:
            conds.append(f"{col} IN ({', '.join('?' * len(val))})")
            params.extend(val)
    if args.commit:
        conds.append("git_commit LIKE ?")
        params.append(args.commit + "%")
    for col, val in extra:
        if val:
            conds.append(f"{col} IN ({', '.join('?' * len(val))})")
            params.extend(val)
    return (" WHERE " + " AND ".join(conds)) if conds else "", params


def newest_protocol(con, view):
    row = con.execute(f"SELECT MAX(protocol) FROM {view}").fetchone()
    return row[0]


def aggregate(con, view, group_cols, metrics, args, extra=()):
    if args.protocol is None and not args.all_protocols:
        args.protocol = newest_protocol(con, view)
    where, params = where_clause(args, extra)
    rows = con.execute(f"SELECT * FROM {view}{where}", params).fetchall()
    groups = {}
    for r in rows:
        key = tuple(r[c] for c in group_cols) + (r["config_key"], r["env_key"])
        g = groups.setdefault(key, {"rows": [], "runs": set()})
        g["rows"].append(r)
        g["runs"].add(r["run_id"])
    out = []
    for key, g in groups.items():
        rec = dict(zip(group_cols, key[:len(group_cols)]))
        rec["config_key"], rec["env_key"] = key[-2], key[-1]
        rec["runs"] = len(g["runs"])
        rec["samples"] = len(g["rows"])
        for m in metrics:
            col = m[0]
            # a column the database predates (older schema) reads as missing
            rec[col] = summarize([row[col] if col in row.keys() else None for row in g["rows"]])
        out.append(rec)
    return out, len(rows)


# ---------------------------------------------------------------- output

def fmt(stat, scale, dec, with_ci=True, latex=False):
    if stat["mean"] is None:
        return "-"
    mean = stat["mean"] * scale
    s = f"{mean:.{dec}f}"
    if with_ci and stat["ci"]:
        ci = stat["ci"] * scale
        if round(ci, dec) > 0:
            s += (r" $\pm$ " if latex else " ± ") + f"{ci:.{dec}f}"
    return s


def emit(records, group_cols, group_headers, metrics, args):
    # the same visible group coming from different config/env keys: show them apart
    seen = {}
    for r in records:
        seen.setdefault(tuple(r[c] for c in group_cols), set()).add((r["config_key"], r["env_key"]))
    multi = any(len(v) > 1 for v in seen.values())
    headers = list(group_headers) + [m[1] for m in metrics] + ["n"]
    if multi:
        headers += ["config", "env"]
    table = []
    for r in records:
        line = [str(r[c]) if r[c] is not None else "-" for c in group_cols]
        line += [fmt(r[m[0]], m[2], m[3], not args.no_ci) for m in metrics]
        line.append(f"{r['samples']} ({r['runs']} runs)")
        if multi:
            line += [r["config_key"][:8], r["env_key"][:8]]
        table.append(line)

    widths = [max(len(h), *(len(row[i]) for row in table)) for i, h in enumerate(headers)]
    print("  ".join(h.ljust(w) for h, w in zip(headers, widths)))
    print("  ".join("-" * w for w in widths))
    for row in table:
        print("  ".join(c.ljust(w) for c, w in zip(row, widths)))
    if multi:
        print("\naviso: há grupos iguais vindos de config/env diferentes (outro dataset, "
              "parâmetro ou máquina); eles NÃO foram juntados (colunas config/env). "
              "Use --label/--batch/--host/--commit para escolher.",
              file=sys.stderr)

    if args.csv:
        Path(args.csv).parent.mkdir(parents=True, exist_ok=True)
        with open(args.csv, "w", newline="") as fh:
            w = csv.writer(fh)
            head = list(group_cols)
            for m in metrics:
                head += [f"{m[0]}_mean", f"{m[0]}_sd", f"{m[0]}_ci95"]
            head += ["samples", "runs", "config_key", "env_key"]
            w.writerow(head)
            for r in records:
                line = [r[c] for c in group_cols]
                for m in metrics:
                    st = r[m[0]]
                    line += [st["mean"], st["sd"], st["ci"]]
                line += [r["samples"], r["runs"], r["config_key"], r["env_key"]]
                w.writerow(line)
        print(f"csv: {args.csv}", file=sys.stderr)

    if args.latex:
        Path(args.latex).parent.mkdir(parents=True, exist_ok=True)
        cols = len(group_cols) + len(metrics)
        lines = [r"\begin{tabular}{" + "l" * len(group_cols) + "r" * len(metrics) + "}",
                 r"\toprule",
                 " & ".join(latex_escape(h) for h in list(group_headers) +
                            [m[1] for m in metrics]) + r" \\",
                 r"\midrule"]
        for r in records:
            cells = [latex_escape(str(r[c])) for c in group_cols]
            cells += [fmt(r[m[0]], m[2], m[3], not args.no_ci, latex=True) for m in metrics]
            lines.append(" & ".join(cells) + r" \\")
        lines += [r"\bottomrule", r"\end{tabular}"]
        assert cols > 0
        Path(args.latex).write_text("\n".join(lines) + "\n")
        print(f"latex: {args.latex} (requer \\usepackage{{booktabs}})", file=sys.stderr)


def latex_escape(s):
    return (s.replace("\\", r"\textbackslash{}").replace("_", r"\_").replace("%", r"\%")
            .replace("&", r"\&").replace("#", r"\#"))


def pick_metrics(table, names, default):
    names = names or default
    unknown = [n for n in names if n not in table]
    if unknown:
        raise SystemExit(f"unknown metric(s) {unknown}; available: {', '.join(table)}")
    return [table[n] for n in names]


# ---------------------------------------------------------------- commands

def cmd_runs(con, args):
    rows = con.execute(
        "SELECT run_id, batch_id, label, experiment, dataset_type, filter, status, protocol, "
        "substr(git_commit, 1, 8) AS commit_, git_dirty, host, store_model, swap_kind, "
        "config_key, env_key, duration_s, maxrss_kb / 1024, majflt "
        f"FROM runs{' WHERE label IN (' + ', '.join('?' * len(args.label)) + ')' if args.label else ''} "
        "ORDER BY run_id DESC LIMIT ?",
        (*(args.label or []), args.limit)).fetchall()
    headers = ["id", "batch", "label", "exp", "type", "filter", "status", "proto", "commit", "dirty",
               "host", "device", "swap", "config", "env", "s", "rss MiB", "majflt"]
    table = [[("-" if v is None else str(v)) for v in r] for r in rows]
    for line in table:
        line[13], line[14] = line[13][:8], line[14][:8]
    widths = [max([len(h)] + [len(t[i]) for t in table]) for i, h in enumerate(headers)]
    print("  ".join(h.ljust(w) for h, w in zip(headers, widths)))
    for t in table:
        print("  ".join(c.ljust(w) for c, w in zip(t, widths)))


def cmd_bench(con, args):
    metrics = pick_metrics(BENCH_METRICS, args.metrics, BENCH_DEFAULT)
    group = ["type", "n", "sizing", "filter"]
    recs, nrows = aggregate(con, "v_bench", group, metrics, args,
                            extra=[("sizing", args.sizing), ("filter", args.filter)])
    if not recs:
        raise SystemExit("nenhuma linha selecionada (use `analyze.py runs`; runs sujos "
                         "precisam de --include-dirty)")
    recs.sort(key=lambda r: (r["type"], r["n"], r["sizing"] != "none", r["sizing"],
                             FILTER_ORDER.get(r["filter"], 99)))
    print(f"protocolo {args.protocol}, {nrows} amostras\n", file=sys.stderr)
    emit(recs, group, ["tipo", "N", "regime", "filtro"], metrics, args)
    plot(args, "bench", recs, metrics)


def cmd_io(con, args):
    metrics = pick_metrics(IO_METRICS, args.metrics, IO_DEFAULT)
    group = ["type", "n", "store_model", "direct", "tables", "neg_ratio", "gatekeeper"]
    recs, nrows = aggregate(con, "v_io", group, metrics, args,
                            extra=[("gatekeeper", args.filter),
                                   ("tables", args.tables), ("neg_ratio", args.neg)])
    if not recs:
        raise SystemExit("nenhuma linha selecionada (use `analyze.py runs`; runs sujos "
                         "precisam de --include-dirty)")
    recs.sort(key=lambda r: (r["type"], r["n"], str(r["store_model"]), r["direct"], r["tables"],
                             r["neg_ratio"], FILTER_ORDER.get(r["gatekeeper"], 99)))
    print(f"protocolo {args.protocol}, {nrows} amostras\n", file=sys.stderr)
    emit(recs, group, ["tipo", "N", "disco", "direct", "T", "neg", "gatekeeper"], metrics, args)
    plot(args, "io", recs, metrics)


def cmd_threads(con, args):
    metrics = pick_metrics(THREAD_METRICS, args.metrics, THREAD_DEFAULT)
    measured = [m for m in metrics if m[0] not in THREAD_DERIVED]
    for base in {THREAD_DERIVED[m[0]] for m in metrics if m[0] in THREAD_DERIVED}:
        if base not in [m[0] for m in measured]:
            measured.append((base, base, 1, 1))  # needed for the ratio
    group = ["type", "n", "filter", "strategy", "threads"]
    try:
        recs, nrows = aggregate(con, "v_thread", group, measured, args,
                                extra=[("filter", args.filter), ("strategy", args.strategy),
                                       ("threads", args.threads)])
    except sqlite3.OperationalError:
        raise SystemExit("este banco não tem o experimento de concorrência (schema anterior ao 6)")
    if not recs:
        raise SystemExit("nenhuma linha selecionada (use `analyze.py runs`; runs sujos "
                         "precisam de --include-dirty)")
    serial = {(r["type"], r["n"], r["filter"], r["config_key"], r["env_key"]): r
              for r in recs if r["strategy"] == "serial"}
    for r in recs:
        ref = serial.get((r["type"], r["n"], r["filter"], r["config_key"], r["env_key"]))
        for name, base in THREAD_DERIVED.items():
            mean = None
            if ref and ref[base]["mean"] and r[base]["mean"]:
                mean = ref[base]["mean"] / r[base]["mean"]
            r[name] = {"n": r[base]["n"], "mean": mean, "sd": None, "ci": None}
    recs.sort(key=lambda r: (r["type"], r["n"], FILTER_ORDER.get(r["filter"], 99),
                             STRATEGY_ORDER.get(r["strategy"], 99), r["threads"]))
    print(f"protocolo {args.protocol}, {nrows} amostras; aceleração = média serial / média "
          f"(sem IC)\n", file=sys.stderr)
    emit(recs, group, ["tipo", "N", "filtro", "estratégia", "T"], metrics, args)
    plot(args, "threads", recs, metrics)


def plot(args, kind, recs, metrics):
    if not args.plot:
        return
    import plots  # matplotlib only when figures are requested
    for path in getattr(plots, kind)(recs, metrics, args.plot, args.format):
        print(f"figura: {path}", file=sys.stderr)


def cmd_freeze(con, args):
    commit = con.execute("SELECT substr(git_commit, 1, 8) FROM runs "
                         "ORDER BY run_id DESC LIMIT 1").fetchone()
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    dest = Path(args.out or ROOT / "results" / "frozen" /
                f"bloom_{stamp}_{commit[0] if commit else 'empty'}.db")
    dest.parent.mkdir(parents=True, exist_ok=True)
    target = sqlite3.connect(dest)
    con.backup(target)
    target.close()
    print(f"snapshot: {dest}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default=str(ROOT / "results" / "bloom.db"))
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("runs", help="list recorded runs")
    r.add_argument("--limit", type=int, default=50)
    r.add_argument("--label", nargs="+", help="only these campaign labels")

    sel = argparse.ArgumentParser(add_help=False)
    sel.add_argument("--label", nargs="+")
    sel.add_argument("--batch", nargs="+")
    sel.add_argument("--type", nargs="+")
    sel.add_argument("--host", nargs="+")
    sel.add_argument("--commit", help="git commit prefix")
    sel.add_argument("--filter", nargs="+", help="filter/gatekeeper names")
    sel.add_argument("--protocol", type=int, help="default: newest in the database")
    sel.add_argument("--all-protocols", action="store_true")
    sel.add_argument("--include-dirty", action="store_true")
    sel.add_argument("--metrics", nargs="+")
    sel.add_argument("--no-ci", action="store_true", help="only means, no ± CI")
    sel.add_argument("--csv", help="write mean/sd/ci95 per group")
    sel.add_argument("--latex", help="write a booktabs tabular")
    sel.add_argument("--plot", metavar="DIR", help="write figures to DIR (needs matplotlib)")
    sel.add_argument("--format", nargs="+", default=["pdf"], choices=["pdf", "png", "svg"],
                     help="figure formats (default: pdf)")

    b = sub.add_parser("bench", parents=[sel], help="benchmark table")
    b.add_argument("--sizing", nargs="+", choices=["same_m", "same_fpr", "none"])
    io = sub.add_parser("io", parents=[sel], help="I/O table")
    io.add_argument("--tables", nargs="+", type=int)
    io.add_argument("--neg", nargs="+", type=float)
    th = sub.add_parser("threads", parents=[sel], help="concurrency table (threadbench)")
    th.add_argument("--strategy", nargs="+")
    th.add_argument("--threads", nargs="+", type=int)

    f = sub.add_parser("freeze", help="snapshot the database")
    f.add_argument("--out")
    args = ap.parse_args()

    if not Path(args.db).exists():
        raise SystemExit(f"{args.db} não existe; rode scripts/run.py primeiro")
    con = resultsdb.connect_readonly(args.db)  # analysis never writes, not even a migration
    {"runs": cmd_runs, "bench": cmd_bench, "io": cmd_io, "threads": cmd_threads,
     "freeze": cmd_freeze}[args.cmd](con, args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
