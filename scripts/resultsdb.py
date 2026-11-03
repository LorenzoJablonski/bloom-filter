"""SQLite results database shared by run.py and analyze.py.

Layout
  runs            one row per bloom_app invocation: what ran, on which code,
                  machine, device and dataset (the context needed to decide
                  whether two runs are comparable)
  filter_results  rows of the --benchmark CSV, linked to runs.run_id
  io_results      rows of the --iobench CSV, linked to runs.run_id
  filters         fixed characteristics of each registered structure

Comparability
  runs.config_key hashes everything that defines *what* was measured
  (protocol, experiment, dataset content, parameters); runs.env_key hashes
  *where* it was measured (host, CPU, RAM limit, storage device). Runs are only
  grouped together when both keys match. runs.git_dirty marks runs made with
  uncommitted changes to tracked files, kept apart from official results.

The raw CSV of every run stays in results/raw/ and is the primary record; the
database can always be rebuilt from it plus the runs metadata.
"""

import csv
import hashlib
import json
import sqlite3
from pathlib import Path

SCHEMA_VERSION = 1

# CSV columns exactly as written by src/benchmark.c and src/iobench.c.
# If the C side changes them, update here and bump BF_PROTOCOL_VERSION.
BENCH_COLUMNS = [
    ("type", "TEXT"), ("filter", "TEXT"), ("sizing", "TEXT"), ("rep", "INTEGER"),
    ("n", "INTEGER"), ("n_exist", "INTEGER"), ("n_inexist", "INTEGER"), ("p_target", "REAL"),
    ("m", "INTEGER"), ("cells", "INTEGER"), ("k", "INTEGER"), ("mem_bytes", "INTEGER"),
    ("bits_per_key", "REAL"), ("insert_ms", "REAL"), ("lookup_exist_ms", "REAL"),
    ("lookup_inexist_ms", "REAL"), ("hash_insert", "REAL"), ("hash_lookup_exist", "REAL"),
    ("hash_lookup_inexist", "REAL"), ("fn", "INTEGER"), ("fp", "INTEGER"), ("fpr", "REAL"),
    ("fpr_theory", "REAL"), ("fill_ratio", "REAL"),
]

IO_COLUMNS = [
    ("type", "TEXT"), ("gatekeeper", "TEXT"), ("rep", "INTEGER"), ("n", "INTEGER"),
    ("tables", "INTEGER"), ("neg_ratio", "REAL"), ("queries", "INTEGER"), ("direct", "INTEGER"),
    ("value_size", "INTEGER"), ("blocks", "INTEGER"), ("gk_mem_bytes", "INTEGER"),
    ("index_mem_bytes", "INTEGER"), ("k", "INTEGER"), ("disk_reads", "INTEGER"),
    ("wasted_reads", "INTEGER"), ("reads_per_query", "REAL"), ("proc_read_bytes", "INTEGER"),
    ("total_ms", "REAL"), ("mean_us", "REAL"), ("p50_us", "REAL"), ("p99_us", "REAL"),
    ("errors", "INTEGER"),
]

RESULT_TABLES = {
    "benchmark": ("filter_results", BENCH_COLUMNS),
    "iobench": ("io_results", IO_COLUMNS),
}

# Parameters that define what a run measured (part of config_key).
CONFIG_FIELDS = [
    "protocol", "experiment", "dataset_type", "dataset_sha256", "n_override", "p_target",
    "reps", "sizing", "tables", "neg_ratio", "queries", "value_size", "direct",
]
# Where it was measured (env_key). store_* only matter for iobench but are
# harmless for the in-memory benchmark (they are NULL there).
ENV_FIELDS = [
    "host", "cpu_model", "cpu_count", "mem_total_kb", "mem_limit_bytes", "virt",
    "store_fstype", "store_model", "store_rotational",
]

# Static description of each model, beyond what bloom_app --info reports.
FILTER_INFO = {
    "sbf": ("standard", "murmurhash2", 0, "k bits in one bit vector"),
    "cbf": ("counting", "murmurhash2", 1, "4-bit saturating counters"),
    "pbf": ("partitioned", "murmurhash2", 0, "k partitions, 64-byte aligned"),
    "rdbf": ("2d", "murmurhash2", 0, "robustBF Eq. 1: X x Y 64-bit cells, beta=61"),
    "hybrid_512": ("hybrid", "murmurhash64a", 0, "key-sharded prime matrix, 512-bit shards"),
    "hybrid_4096": ("hybrid", "murmurhash64a", 0, "key-sharded prime matrix, 4096-bit shards"),
    "hybrid_c_512": ("hybrid", "murmurhash64a", 1, "hybrid with 4-bit counters, 512 cells"),
    "hybrid_c_4096": ("hybrid", "murmurhash64a", 1, "hybrid with 4-bit counters, 4096 cells"),
    "baseline": ("exact", "uthash", 0, "deterministic hash table (uthash)"),
}

RUN_COLUMNS = [
    ("run_id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
    ("batch_id", "TEXT NOT NULL"),      # one run.py invocation
    ("label", "TEXT"),                  # free-form campaign name
    ("experiment", "TEXT NOT NULL"),    # benchmark | iobench
    ("status", "TEXT NOT NULL"),        # ok | failed
    ("exit_code", "INTEGER"),
    ("started_at", "TEXT"), ("finished_at", "TEXT"), ("duration_s", "REAL"),
    ("cmdline", "TEXT"),
    ("raw_csv", "TEXT"),
    ("config_key", "TEXT"), ("env_key", "TEXT"),
    # code
    ("protocol", "INTEGER"), ("schema_version", "INTEGER"),
    ("git_commit", "TEXT"), ("git_branch", "TEXT"), ("git_dirty", "INTEGER"),
    ("binary_sha256", "TEXT"), ("cc_version", "TEXT"),
    # machine
    ("host", "TEXT"), ("cpu_model", "TEXT"), ("cpu_count", "INTEGER"),
    ("mem_total_kb", "INTEGER"), ("mem_limit_bytes", "INTEGER"), ("kernel", "TEXT"),
    ("virt", "TEXT"),
    # storage behind store_dir (iobench)
    ("store_dir", "TEXT"), ("store_fstype", "TEXT"), ("store_fsopts", "TEXT"),
    ("store_device", "TEXT"), ("store_model", "TEXT"), ("store_rotational", "INTEGER"),
    # dataset
    ("dataset_type", "TEXT"), ("dataset_sha256", "TEXT"), ("dataset_meta", "TEXT"),
    ("dataset_n_ins", "INTEGER"), ("dataset_n_exist", "INTEGER"), ("dataset_n_inexist", "INTEGER"),
    # parameters (NULL when not applicable to the experiment)
    ("n_override", "INTEGER"), ("p_target", "REAL"), ("reps", "INTEGER"), ("sizing", "TEXT"),
    ("tables", "INTEGER"), ("neg_ratio", "REAL"), ("queries", "INTEGER"),
    ("value_size", "INTEGER"), ("direct", "INTEGER"),
]


def _result_ddl(table, columns):
    cols = ",\n  ".join(f"{name} {typ}" for name, typ in columns)
    return (f"CREATE TABLE IF NOT EXISTS {table} (\n"
            f"  run_id INTEGER NOT NULL REFERENCES runs(run_id) ON DELETE CASCADE,\n"
            f"  {cols}\n);\n"
            f"CREATE INDEX IF NOT EXISTS {table}_run ON {table}(run_id);\n")


def _schema_sql():
    runs = ",\n  ".join(f"{name} {typ}" for name, typ in RUN_COLUMNS)
    sql = [
        "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);",
        f"CREATE TABLE IF NOT EXISTS runs (\n  {runs}\n);",
        "CREATE INDEX IF NOT EXISTS runs_config ON runs(config_key, env_key);",
        "CREATE TABLE IF NOT EXISTS filters (\n"
        "  name TEXT PRIMARY KEY, family TEXT, hash TEXT, exact INTEGER,\n"
        "  supports_delete INTEGER, fixed_k INTEGER, counting INTEGER, description TEXT\n);",
        _result_ddl("filter_results", BENCH_COLUMNS),
        _result_ddl("io_results", IO_COLUMNS),
        # Joined rows, ready for grouping. Only successful runs.
        "CREATE VIEW IF NOT EXISTS v_bench AS\n"
        "  SELECT r.run_id, r.batch_id, r.label, r.config_key, r.env_key, r.protocol,\n"
        "         r.git_commit, r.git_dirty, r.host, r.started_at, f.*,\n"
        "         1e6 * f.insert_ms / f.n AS insert_ns_op,\n"
        "         1e6 * f.lookup_exist_ms / f.n_exist AS lookup_exist_ns_op,\n"
        "         1e6 * f.lookup_inexist_ms / f.n_inexist AS lookup_inexist_ns_op\n"
        "  FROM filter_results f JOIN runs r USING (run_id) WHERE r.status = 'ok';",
        "CREATE VIEW IF NOT EXISTS v_io AS\n"
        "  SELECT r.run_id, r.batch_id, r.label, r.config_key, r.env_key, r.protocol,\n"
        "         r.git_commit, r.git_dirty, r.host, r.started_at,\n"
        "         r.store_model, r.store_rotational, i.*\n"
        "  FROM io_results i JOIN runs r USING (run_id) WHERE r.status = 'ok';",
    ]
    return "\n".join(sql)


def connect(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(path)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA foreign_keys = ON")
    con.executescript(_schema_sql())
    row = con.execute("SELECT value FROM meta WHERE key = 'schema_version'").fetchone()
    if row is None:
        con.execute("INSERT INTO meta VALUES ('schema_version', ?)", (str(SCHEMA_VERSION),))
        con.commit()
    elif int(row[0]) != SCHEMA_VERSION:
        raise SystemExit(f"{path}: schema version {row[0]}, expected {SCHEMA_VERSION}; "
                         "rebuild the database or migrate it")
    return con


def sync_filters(con, info_filters):
    """info_filters: list of (name, exact, supports_delete, fixed_k) from bloom_app --info."""
    for name, exact, delete, fixed_k in info_filters:
        family, hsh, counting, desc = FILTER_INFO.get(name, (None, None, None, None))
        con.execute(
            "INSERT INTO filters VALUES (?,?,?,?,?,?,?,?) ON CONFLICT(name) DO UPDATE SET "
            "family=excluded.family, hash=excluded.hash, exact=excluded.exact, "
            "supports_delete=excluded.supports_delete, fixed_k=excluded.fixed_k, "
            "counting=excluded.counting, description=excluded.description",
            (name, family, hsh, exact, delete, fixed_k, counting, desc))


def key_of(run, fields):
    payload = json.dumps({f: run.get(f) for f in fields}, sort_keys=True)
    return hashlib.sha256(payload.encode()).hexdigest()[:16]


def insert_run(con, run):
    run = dict(run)
    run["config_key"] = key_of(run, CONFIG_FIELDS)
    run["env_key"] = key_of(run, ENV_FIELDS)
    run["schema_version"] = SCHEMA_VERSION
    cols = [c for c, _ in RUN_COLUMNS if c != "run_id" and c in run]
    unknown = set(run) - {c for c, _ in RUN_COLUMNS}
    if unknown:
        raise ValueError(f"unknown run fields: {sorted(unknown)}")
    cur = con.execute(f"INSERT INTO runs ({', '.join(cols)}) VALUES ({', '.join('?' * len(cols))})",
                      [run[c] for c in cols])
    return cur.lastrowid


def _convert(value, typ):
    if value == "":
        return None
    if typ == "INTEGER":
        return int(value)
    if typ == "REAL":
        return float(value)
    return value


def ingest_csv(con, run_id, experiment, csv_path):
    """Load one raw CSV; the header must match the expected columns exactly."""
    table, columns = RESULT_TABLES[experiment]
    names = [c for c, _ in columns]
    with open(csv_path, newline="") as fh:
        reader = csv.reader(fh)
        header = next(reader, None)
        if header != names:
            raise ValueError(f"{csv_path}: unexpected header\n  got      {header}\n"
                             f"  expected {names}")
        rows = []
        for line in reader:
            if not line:
                continue
            if len(line) != len(names):
                raise ValueError(f"{csv_path}: row with {len(line)} fields: {line}")
            rows.append([run_id] + [_convert(v, t) for v, (_, t) in zip(line, columns)])
    con.executemany(f"INSERT INTO {table} VALUES ({', '.join('?' * (len(names) + 1))})", rows)
    return len(rows)
