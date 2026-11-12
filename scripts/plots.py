"""Figures for analyze.py (--plot DIR). Needs matplotlib (requirements.txt).

Kept apart from analyze.py so that the tables keep working with the standard
library only; this module is imported only when --plot is given.

Every figure is drawn from the aggregated records analyze.py already built
(mean and 95% CI per group), so tables and figures always show the same
numbers. Runs from different machines/devices (env_key) never share a figure;
runs with different parameters share one only when they do not overlap.
"""

import math
import re
from pathlib import Path

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover - depends on the environment
    raise SystemExit("--plot precisa do matplotlib: make venv (ou "
                     "pip install -r requirements.txt) e rode com .venv/bin/python") from exc

# categorical slots in fixed order (dataviz reference palette, light mode)
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300",
          "#6250d6", "#e34948"]
INK = "#0b0b0b"
INK_2 = "#52514e"
GRID = "#e4e3df"

STYLE = {
    "font.size": 9,
    "axes.titlesize": 9,
    "axes.labelsize": 9,
    "axes.edgecolor": INK_2,
    "axes.labelcolor": INK,
    "axes.linewidth": 0.6,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "axes.axisbelow": True,
    "grid.color": GRID,
    "grid.linewidth": 0.6,
    "xtick.color": INK_2,
    "ytick.color": INK_2,
    "legend.frameon": False,
    "savefig.bbox": "tight",
    "pdf.fonttype": 42,  # embed TrueType: text stays selectable in the PDF
}

# a metric gets a log axis when its positive values span more than this factor
LOG_SPAN = 30

# names shown in the figures, in Portuguese as in the text (the file names keep the
# code names); anything missing here is shown as it is
NAMES = {"sbf": "SBF", "cbf": "CBF", "pbf": "PBF", "rdbf": "rDBF",
         "hybrid_512": "híbrido 512", "hybrid_4096": "híbrido 4096",
         "hybrid_c_512": "híbrido c 512", "hybrid_c_4096": "híbrido c 4096",
         "baseline": "linha de base", "none": "sem filtro"}
STRATEGIES = {"serial": "serial", "partition": "por partição", "atomic": "OR atômico",
              "lock_global": "trava global", "lock_shard": "trava por bloco"}
SIZINGS = {"same_fpr": "mesma FPR", "same_m": "mesma memória", "none": "sem dimensionamento"}
TYPES = {"uuid": "UUID", "cpf": "CPF", "cnpj": "CNPJ", "url": "URL"}
# panel titles by column (the table headers of analyze.py stay as they are)
PANELS = {"bits_per_key": "bits por chave", "insert_ns_op": "inserção (ns/op)",
          "lookup_exist_ns_op": "busca existente (ns/op)",
          "lookup_inexist_ns_op": "busca negativa (ns/op)",
          "hash_lookup_inexist": "hashes por busca negativa", "fill_ratio": "ocupação",
          "collision_rate": "taxa de colisões (%)", "delete_ns_op": "deleção (ns/op)",
          "del_still_present": "apagadas que ainda respondem presente",
          "fn_after_delete": "falsos negativos após a deleção", "mem_bytes": "memória (B)",
          "majflt": "faltas de página maiores", "minflt": "faltas de página menores",
          "p50_us": "mediana (µs)", "p99_us": "percentil 99 (µs)",
          "gk_mem_bytes": "memória do porteiro (B)"}


def _name(flt):
    return NAMES.get(flt, flt)


def _panel(col, header):
    return PANELS.get(col, header)


def _ident(typ, n=None, sizing=None):
    parts = [TYPES.get(typ, typ)]
    if n is not None:
        parts.append(f"N = {n:,}".replace(",", "."))
    if sizing is not None:
        parts.append(SIZINGS.get(sizing, sizing))
    return " · ".join(parts)


def _slug(*parts):
    s = "_".join(str(p) for p in parts if p is not None and p != "")
    return re.sub(r"[^A-Za-z0-9.+-]+", "-", s).strip("-")


def _save(fig, outdir, name, formats):
    outdir.mkdir(parents=True, exist_ok=True)
    paths = []
    for ext in formats:
        path = outdir / f"{name}.{ext}"
        fig.savefig(path, dpi=200)
        paths.append(path)
    plt.close(fig)
    return paths


def _figures(records, ident_cols, unique_cols):
    """Split records into figures: one per value of ident_cols and env_key.

    Records of one figure may come from different config_keys (e.g. iobench
    runs with another neg_ratio) as long as each unique_cols value appears once;
    if two configs collide (another dataset or parameter) the figure is split
    per config_key and the key goes into the file name.
    """
    figs = {}
    for r in records:
        figs.setdefault(tuple(r[c] for c in ident_cols) + (r["env_key"],), []).append(r)
    envs = {}
    for key in figs:
        envs.setdefault(key[:-1], set()).add(key[-1])
    out = []
    for key, recs in figs.items():
        ident, env = key[:-1], key[-1]
        env_tag = env[:8] if len(envs[ident]) > 1 else None
        seen = [tuple(r[c] for c in unique_cols) for r in recs]
        if len(seen) == len(set(seen)):
            out.append((ident, env_tag, recs))
            continue
        for ck in sorted({r["config_key"] for r in recs}):
            sub = [r for r in recs if r["config_key"] == ck]
            out.append((ident, "-".join(t for t in (ck[:8], env_tag) if t), sub))
    return out


def _values(recs, col, scale):
    means, cis = [], []
    for r in recs:
        st = r[col]
        means.append(None if st["mean"] is None else st["mean"] * scale)
        cis.append(0.0 if not st["ci"] else st["ci"] * scale)
    return means, cis


def _use_log(values):
    pos = [v for v in values if v is not None and v > 0]
    return len(pos) > 1 and max(pos) / min(pos) > LOG_SPAN


def _fmt(v, dec):
    if v is None:
        return "-"
    if abs(v) >= 1000:
        return f"{v:,.0f}".replace(",", ".")
    return f"{v:.{dec}f}".replace(".", ",")


def _hbars(ax, labels, series, title, dec):
    """series: list of (name, color, means, cis). One horizontal bar per label/series."""
    n = len(series)
    band = 0.8
    h = min(band / n, 0.36)
    all_vals = [v for _, _, means, _ in series for v in means if v is not None]
    log = _use_log(all_vals)
    for i, (name, color, means, cis) in enumerate(series):
        offs = (i - (n - 1) / 2) * h
        ys = [y + offs for y in range(len(labels))]
        vals = [0 if v is None else v for v in means]
        ax.barh(ys, vals, height=h * 0.9, color=color, label=name, zorder=2,
                xerr=[c if c else 0 for c in cis], ecolor=INK_2,
                error_kw={"elinewidth": 0.6, "capsize": 1.5})
        if n == 1:  # direct value labels only when they do not crowd the panel
            for y, v, c in zip(ys, means, cis):
                ax.text((v or 0) + (c or 0), y, " " + _fmt(v, dec), va="center", ha="left",
                        fontsize=7, color=INK_2)
    ax.set_yticks(range(len(labels)))
    ax.set_yticklabels(labels)
    ax.invert_yaxis()
    ax.grid(axis="y", visible=False)
    if log:
        ax.set_xscale("log")
        # below ~2 decades matplotlib also labels minor ticks, and they overlap
        ax.xaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    elif n == 1 and all_vals:  # room for the value labels
        ax.set_xlim(0, max(all_vals) * 1.25 or 1)
    ax.set_title(title + (" (log)" if log else ""), loc="left", color=INK)


# ------------------------------------------------------------------ bench

def bench(records, metrics, outdir, formats):
    """One figure per (type, N, sizing): a panel per metric, filters on the y axis;
    plus the memory x FPR trade-off when both metrics are available."""
    plt.rcParams.update(STYLE)
    outdir = Path(outdir)
    paths = []
    cols = [m[0] for m in metrics]
    # the exact baseline has no sizing ("none"); it is drawn as a reference in
    # every sizing figure of the same runs instead of in a figure of its own
    refs = [r for r in records if r["sizing"] == "none"]
    sized = [r for r in records if r["sizing"] != "none"] or refs
    for (typ, n, sizing), tag, recs in _figures(sized, ["type", "n", "sizing"], ["filter"]):
        labels = [_name(r["filter"]) for r in recs]
        if sizing != "none":
            keys = {(r["config_key"], r["env_key"]) for r in recs}
            same = [r for r in refs if r["type"] == typ and r["n"] == n and r["env_key"]
                    in {k[1] for k in keys}]
            exact = [r for r in same if (r["config_key"], r["env_key"]) in keys]
            # a baseline run with fewer repetitions (it pages and takes hours) has its
            # own config_key: still drawn, with its sample count in the label
            extra = exact or same
            recs = recs + extra
            labels += [_name(r["filter"]) if r in exact
                       else f'{_name(r["filter"])} ({r["samples"]} amostras)'
                       for r in extra]
        panels = [m for m in metrics if m[0] != "fpr_theory"]
        ncol = min(3, len(panels))
        nrow = math.ceil(len(panels) / ncol)
        fig, axes = plt.subplots(nrow, ncol, squeeze=False,
                                 figsize=(3.0 * ncol, (0.22 * len(labels) + 0.9) * nrow))
        for ax, (col, header, scale, dec) in zip(axes.flat, panels):
            means, cis = _values(recs, col, scale)
            title = _panel(col, header)
            if col == "fpr" and "fpr_theory" in cols:
                theo, _ = _values(recs, "fpr_theory", 100)
                ax.scatter([math.nan if t is None else t for t in theo], range(len(labels)),
                           marker="|", s=90, color=INK, linewidths=1.2, zorder=3)
                title += " · | = teórica"
            _hbars(ax, labels, [(header, SERIES[0], means, cis)], title, dec)
        for ax in list(axes.flat)[len(panels):]:
            ax.set_visible(False)
        fig.suptitle(f"{_ident(typ, n, sizing)} · média e IC 95%",
                     x=0.01, ha="left", color=INK)
        fig.tight_layout()
        paths += _save(fig, outdir, _slug("bench", typ, f"n{n}", sizing, tag), formats)

        if {"bits_per_key", "fpr"} <= set(cols) and sizing != "none":
            approx = [r for r in recs if r["sizing"] != "none"]  # exact baseline: FPR 0
            paths += _tradeoff(approx, outdir, _slug("tradeoff", typ, f"n{n}", sizing, tag),
                               _ident(typ, n, sizing), formats)
    paths += _scaling(records, metrics, outdir, formats)
    return paths


SCALING_COLS = [("insert_ns_op", "inserção"), ("lookup_exist_ns_op", "busca existente"),
                ("lookup_inexist_ns_op", "busca negativa")]


def _scaling(records, metrics, outdir, formats):
    """Time per operation against N, one line per structure (the exact baseline
    included), log y: how each structure scales and where the baseline breaks.
    One figure per (type, sizing, machine); only when the data has several N."""
    cols = [c for c in SCALING_COLS if c[0] in {m[0] for m in metrics}]
    if not cols:
        return []
    paths = []
    refs = [r for r in records if r["sizing"] == "none"]
    groups = {}
    for r in records:
        if r["sizing"] != "none":
            groups.setdefault((r["type"], r["sizing"], r["env_key"]), []).append(r)
    for (typ, sizing, env), recs in sorted(groups.items()):
        recs = recs + [r for r in refs if r["type"] == typ and r["env_key"] == env]
        ns = sorted({r["n"] for r in recs})
        if len(ns) < 2:
            continue
        lines = {}
        for r in recs:  # one record per (structure, N): the one with the most samples
            key = (r["filter"], r["n"])
            if key not in lines or r["samples"] > lines[key]["samples"]:
                lines[key] = r
        filters = list(dict.fromkeys(r["filter"] for r in recs))
        fig, axes = plt.subplots(1, len(cols), squeeze=False, figsize=(3.4 * len(cols), 3.4))
        for ax, (col, title) in zip(axes.flat, cols):
            for i, flt in enumerate(filters):
                pts = [(n, lines[(flt, n)][col]) for n in ns if (flt, n) in lines]
                pts = [(n, st) for n, st in pts if st["mean"] is not None]
                if not pts:
                    continue
                xs = [n / 1e6 for n, _ in pts]
                ys = [st["mean"] for _, st in pts]
                err = [st["ci"] or 0.0 for _, st in pts]
                exact = flt == "baseline"
                ax.errorbar(xs, ys, yerr=err, marker="s" if exact else "o", markersize=3,
                            linewidth=1.8 if exact else 1.1, capsize=2,
                            color=INK if exact else SERIES[i % len(SERIES)], label=_name(flt))
            ax.set_yscale("log")
            ax.set_xticks([n / 1e6 for n in ns])
            ax.set_xticklabels([f"{n / 1e6:g}" for n in ns])
            ax.set_xlabel("N (milhões de chaves)")
            ax.set_title(f"{title} (ns/op, log)", loc="left", color=INK)
        handles, labels = axes.flat[0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="lower center", ncol=5, fontsize=7,
                   bbox_to_anchor=(0.5, -0.1))
        fig.suptitle(f"{_ident(typ, sizing=sizing)} · tempo por operação × N · média e IC 95%",
                     x=0.01, ha="left", color=INK)
        fig.tight_layout(rect=(0, 0, 1, 0.94))
        paths += _save(fig, outdir, _slug("scaling", typ, sizing, env[:8]), formats)
    return paths


def _tradeoff(recs, outdir, name, title, formats):
    """Memory against measured FPR: where each model sits on the trade-off.

    Structures with nearly the same memory (SBF, PBF and the bit hybrids; CBF and
    the counting hybrids) sit on top of each other, so each group gets one label
    below it, a line per structure with its FPR, instead of a label per point."""
    fig, ax = plt.subplots(figsize=(5.0, 3.4))
    pts = sorted((r["bits_per_key"]["mean"], r["fpr"]["mean"] * 100, _name(r["filter"]))
                 for r in recs if r["bits_per_key"]["mean"] is not None
                 and r["fpr"]["mean"] is not None)
    xs = [p[0] for p in pts]
    ax.scatter(xs, [p[1] for p in pts], s=28, color=SERIES[0], edgecolors="white",
               linewidths=1.5, zorder=3)
    log = _use_log(xs)
    if log:
        ax.set_xscale("log")
    groups = []
    for p in pts:  # points within 15% of memory of the group's first one
        if groups and p[0] <= groups[-1][0][0] * 1.15:
            groups[-1].append(p)
        else:
            groups.append([p])
    lo, hi = (min(xs), max(xs)) if xs else (0, 1)
    for g in groups:
        gx = sum(p[0] for p in g) / len(g)
        text = "\n".join(f"{p[2]}: {_fmt(p[1], 3)}%" for p in sorted(g, key=lambda p: -p[1]))
        right = hi > lo and (gx - lo) / (hi - lo) > 0.7
        ax.annotate(text, (gx, min(p[1] for p in g)), xytext=(0, -16),
                    textcoords="offset points", va="top", ha="right" if right else "left",
                    fontsize=7, color=INK_2, linespacing=1.3,
                    arrowprops=dict(arrowstyle="-", color=GRID, linewidth=0.8))
    ax.set_xlabel("bits por chave" + (" (log)" if log else ""))
    ax.set_ylabel("FPR medida (%)")
    ax.set_ylim(0, max([p[1] for p in pts] + [0.0]) * 1.1 or 1.0)  # room for the top points
    ax.set_title(f"{title} · memória × FPR", loc="left", color=INK)
    fig.tight_layout()
    return _save(fig, outdir, name, formats)


# ------------------------------------------------------------------ io

def io(records, metrics, outdir, formats):
    """One figure per (type, N, device, direct, T): a panel per metric, gatekeepers
    on the y axis, one bar per negative-query ratio."""
    plt.rcParams.update(STYLE)
    outdir = Path(outdir)
    paths = []
    ident_cols = ["type", "n", "store_model", "direct", "tables"]
    for (typ, n, model, direct, tables), tag, recs in _figures(records, ident_cols,
                                                               ["gatekeeper", "neg_ratio"]):
        gks = list(dict.fromkeys(r["gatekeeper"] for r in recs))
        negs = sorted({r["neg_ratio"] for r in recs})
        by = {(r["gatekeeper"], r["neg_ratio"]): r for r in recs}
        ncol = min(3, len(metrics))
        nrow = math.ceil(len(metrics) / ncol)
        fig, axes = plt.subplots(nrow, ncol, squeeze=False,
                                 figsize=(3.0 * ncol, (0.2 * len(gks) * len(negs) + 0.9) * nrow))
        for ax, (col, header, scale, dec) in zip(axes.flat, metrics):
            series = []
            for i, neg in enumerate(negs):
                rs = [by.get((g, neg)) for g in gks]
                means = [None if r is None or r[col]["mean"] is None else r[col]["mean"] * scale
                         for r in rs]
                cis = [0.0 if r is None or not r[col]["ci"] else r[col]["ci"] * scale for r in rs]
                series.append((f"{neg:.0%} negativas", SERIES[i % len(SERIES)], means, cis))
            _hbars(ax, [_name(g) for g in gks], series, _panel(col, header), dec)
        for ax in list(axes.flat)[len(metrics):]:
            ax.set_visible(False)
        handles, labels = axes.flat[0].get_legend_handles_labels()
        if len(negs) > 1:
            fig.legend(handles, labels, loc="upper right", ncol=len(negs), fontsize=8)
        mode = "O_DIRECT" if direct else "page cache"
        fig.suptitle(f"{_ident(typ, n)} · {model or 'disco ?'} · {mode} · T = {tables} tabelas"
                     " · média e IC 95%",
                     x=0.01, ha="left", color=INK)
        fig.tight_layout(rect=(0, 0, 1, 0.97))
        paths += _save(fig, outdir, _slug("io", typ, f"n{n}", model, "direct" if direct else "cached",
                                          f"T{tables}", tag), formats)
    return paths


# ------------------------------------------------------------------ threads

def threads(records, metrics, outdir, formats):
    """One figure per (type, N): speedup over the serial run against the number of
    threads, a line per structure x strategy, for insert and negative lookup; the
    diagonal is the ideal (linear) speedup."""
    plt.rcParams.update(STYLE)
    outdir = Path(outdir)
    paths = []
    panels = [("speedup_insert", "aceleração · inserção"),
              ("speedup_lookup", "aceleração · busca negativa")]
    groups = {}
    for r in records:
        if r["strategy"] == "serial":
            continue
        groups.setdefault((r["type"], r["n"], r["env_key"]), []).append(r)
    for (typ, n, env), recs in sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        lines = {}
        for r in recs:
            lines.setdefault((r["filter"], r["strategy"]), []).append(r)
        tmax = max(r["threads"] for r in recs)
        fig, axes = plt.subplots(1, len(panels), squeeze=False, figsize=(3.6 * len(panels), 3.2))
        for ax, (col, title) in zip(axes.flat, panels):
            ax.plot([1, tmax], [1, tmax], color=INK_2, linewidth=0.8, linestyle="--",
                    label="ideal (linear)")
            for i, ((flt, strat), rs) in enumerate(sorted(lines.items())):
                rs = sorted(rs, key=lambda r: r["threads"])
                xs = [r["threads"] for r in rs if r[col]["mean"] is not None]
                ys = [r[col]["mean"] for r in rs if r[col]["mean"] is not None]
                ax.plot(xs, ys, marker="o", markersize=3, linewidth=1.2,
                        color=SERIES[i % len(SERIES)],
                        label=f"{_name(flt)} · {STRATEGIES.get(strat, strat)}")
            ax.axhline(1.0, color=GRID, linewidth=0.8)
            ax.set_xticks(sorted({r["threads"] for r in recs}))
            ax.set_xlabel("threads (t)")
            ax.set_title(title, loc="left", color=INK)
        handles, labels = axes.flat[0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="lower center", ncol=3, fontsize=7,
                   bbox_to_anchor=(0.5, -0.12))
        fig.suptitle(f"{_ident(typ, n)} · aceleração = média serial ÷ média com t threads",
                     x=0.01, ha="left", color=INK)
        fig.tight_layout(rect=(0, 0, 1, 0.95))
        paths += _save(fig, outdir, _slug("threads", typ, f"n{n}", env[:8]), formats)
    return paths
