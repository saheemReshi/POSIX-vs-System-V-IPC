#!/usr/bin/env python3
"""
analyze.py — Paper-grade analysis of POSIX vs SysV IPC benchmarks.

Inputs (auto-discovered under results/):
  results/raw/combined/all_runs.csv   — required (timing data)
  results/perf/{posix,sysv}/all_perf.csv  — optional (hardware counters)
  results/strace/{posix,sysv}/strace_summary.txt  — optional (syscall mix)

Outputs (under results/analysis/):
  tables/master_comparison.csv        — POSIX vs SysV per cell with p, ratio, CI
  tables/per_config_summary.csv       — full descriptive stats per cell
  tables/coverage.csv                 — runs per cell sanity check
  tables/perf_summary.csv             — joined perf metrics (if perf data found)
  figures/*.{png,pdf}                 — comparison plots
  analysis_report.md                  — narrative summary

Statistical methods:
  Bootstrap 95% CIs on median latency / throughput (10 000 resamples).
  Welch's t-test (parametric) and Mann-Whitney U (non-parametric).
  Cliff's delta effect size (paired with U).
  Speedup ratio = median(SysV)/median(POSIX), bootstrap CI on the ratio.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from pathlib import Path
from typing import Iterable

import numpy as np

try:
    import pandas as pd
except ImportError as exc:
    raise SystemExit("pandas required: pip install pandas") from exc

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError as exc:
    raise SystemExit("matplotlib required: pip install matplotlib") from exc

# scipy is optional — we have pure-numpy fallbacks for the tests we need.
try:
    from scipy import stats as _scipy_stats
    HAVE_SCIPY = True
except ImportError:
    HAVE_SCIPY = False


# ── constants ───────────────────────────────────────────────────────────────

NUMERIC_COLS = [
    "n_procs", "msg_size_bytes", "run_id", "iterations",
    "avg_ns", "p50_ns", "p95_ns", "p99_ns", "p999_ns", "min_ns", "max_ns",
    "throughput_msg_s", "throughput_MB_s", "mem_delta_kb",
]

PLACEMENT_ORDER = ["same_core", "same_l2", "same_l3", "diff_l3", "cross_core"]
MQ_MECHS  = ["mq_posix",  "mq_sysv"]
SHM_MECHS = ["shm_posix", "shm_sysv"]
ALL_MECHS = MQ_MECHS + SHM_MECHS

MECH_COLORS = {
    "mq_posix":  "#1f77b4",
    "mq_sysv":   "#d62728",
    "shm_posix": "#2ca02c",
    "shm_sysv":  "#9467bd",
}

BOOTSTRAP_N = 10_000
RNG = np.random.default_rng(0xC0FFEE)


# ── CLI ─────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    here = Path(__file__).resolve().parent
    root = here.parent
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", default=str(root / "results/raw/combined/all_runs.csv"))
    p.add_argument("--perf-posix", default=str(root / "results/perf/posix/all_perf.csv"))
    p.add_argument("--perf-sysv",  default=str(root / "results/perf/sysv/all_perf.csv"))
    p.add_argument("--strace-posix", default=str(root / "results/strace/posix/strace_summary.txt"))
    p.add_argument("--strace-sysv",  default=str(root / "results/strace/sysv/strace_summary.txt"))
    p.add_argument("--outdir", default=str(root / "results/analysis"))
    p.add_argument("--format", choices=["png", "pdf", "both"], default="both")
    p.add_argument("--bootstrap", type=int, default=BOOTSTRAP_N,
                   help=f"bootstrap resamples (default {BOOTSTRAP_N})")
    return p.parse_args()


# ── data loading & normalisation ────────────────────────────────────────────

def parse_placement_group(p: str) -> str:
    s = str(p)
    for tag in PLACEMENT_ORDER:
        if s.startswith(tag):
            return tag
    return s

def parse_queue_depth(p: str) -> float:
    m = re.search(r"_q(\d+)", str(p))
    return float(m.group(1)) if m else np.nan

def parse_scalability_n(b: str) -> float:
    m = re.match(r"(?:fanin|pairs)_n(\d+)$", str(b))
    return float(m.group(1)) if m else np.nan

def parse_scalability_kind(b: str) -> str:
    if str(b).startswith("fanin_n"): return "fanin"
    if str(b).startswith("pairs_n"): return "pairs"
    return ""

def load_data(path: Path) -> pd.DataFrame:
    if not path.exists():
        raise SystemExit(f"Combined CSV not found: {path}")
    df = pd.read_csv(path, comment="#")
    required = {"mechanism", "benchmark", "placement"}
    missing = required - set(df.columns)
    if missing:
        raise SystemExit(f"Missing columns in CSV: {sorted(missing)}")
    for c in NUMERIC_COLS:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    df["placement_group"] = df["placement"].map(parse_placement_group)
    df["queue_depth"]     = df["placement"].map(parse_queue_depth)
    df["scal_n"]          = df["benchmark"].map(parse_scalability_n)
    df["scal_kind"]       = df["benchmark"].map(parse_scalability_kind)
    return df


# ── bootstrap & stats ───────────────────────────────────────────────────────

def bootstrap_ci(values: np.ndarray, statistic, n_resample: int = BOOTSTRAP_N,
                 ci: float = 0.95) -> tuple[float, float, float]:
    """Returns (point estimate, lo, hi) for `statistic(values)` via bootstrap."""
    v = np.asarray(values, dtype=float)
    v = v[np.isfinite(v)]
    if len(v) == 0:
        return (np.nan, np.nan, np.nan)
    point = float(statistic(v))
    if len(v) < 2:
        return (point, point, point)
    idx = RNG.integers(0, len(v), size=(n_resample, len(v)))
    boot = np.array([statistic(v[i]) for i in idx])
    lo, hi = np.quantile(boot, [(1 - ci) / 2, 1 - (1 - ci) / 2])
    return (point, float(lo), float(hi))

def bootstrap_ratio_ci(num: np.ndarray, den: np.ndarray,
                        n_resample: int = BOOTSTRAP_N,
                        ci: float = 0.95) -> tuple[float, float, float]:
    """Bootstrap CI for median(num)/median(den)."""
    num = np.asarray(num, dtype=float); num = num[np.isfinite(num)]
    den = np.asarray(den, dtype=float); den = den[np.isfinite(den)]
    if len(num) < 2 or len(den) < 2:
        if len(num) and len(den):
            mn, md = np.median(num), np.median(den)
            r = mn / md if md else np.nan
            return (r, r, r)
        return (np.nan, np.nan, np.nan)
    point = float(np.median(num) / np.median(den)) if np.median(den) else np.nan
    ratios = np.empty(n_resample, dtype=float)
    for i in range(n_resample):
        a = num[RNG.integers(0, len(num), len(num))]
        b = den[RNG.integers(0, len(den), len(den))]
        mb = np.median(b)
        ratios[i] = (np.median(a) / mb) if mb else np.nan
    lo, hi = np.nanquantile(ratios, [(1 - ci) / 2, 1 - (1 - ci) / 2])
    return (point, float(lo), float(hi))

def cliffs_delta(a: np.ndarray, b: np.ndarray) -> float:
    """Cliff's delta in [-1, +1]: P(a>b) - P(a<b)."""
    a = np.asarray(a, dtype=float); a = a[np.isfinite(a)]
    b = np.asarray(b, dtype=float); b = b[np.isfinite(b)]
    if len(a) == 0 or len(b) == 0:
        return np.nan
    # Vectorised: compare every pair
    diff = a[:, None] - b[None, :]
    n_gt = (diff > 0).sum()
    n_lt = (diff < 0).sum()
    return float((n_gt - n_lt) / (len(a) * len(b)))

def cliffs_magnitude(d: float) -> str:
    a = abs(d)
    if a < 0.147:  return "negligible"
    if a < 0.33:   return "small"
    if a < 0.474:  return "medium"
    return "large"

def welch_t(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    a = np.asarray(a, dtype=float); a = a[np.isfinite(a)]
    b = np.asarray(b, dtype=float); b = b[np.isfinite(b)]
    if len(a) < 2 or len(b) < 2:
        return (np.nan, np.nan)
    if HAVE_SCIPY:
        t, p = _scipy_stats.ttest_ind(a, b, equal_var=False)
        return (float(t), float(p))
    # Pure numpy Welch's t (no exact p without t distribution; use normal approx)
    m1, m2 = a.mean(), b.mean()
    v1, v2 = a.var(ddof=1), b.var(ddof=1)
    n1, n2 = len(a), len(b)
    se = math.sqrt(v1/n1 + v2/n2)
    t = (m1 - m2) / se if se else np.nan
    p = 2 * (1 - 0.5 * (1 + math.erf(abs(t) / math.sqrt(2)))) if np.isfinite(t) else np.nan
    return (float(t), float(p))

def mannwhitney(a: np.ndarray, b: np.ndarray) -> tuple[float, float]:
    a = np.asarray(a, dtype=float); a = a[np.isfinite(a)]
    b = np.asarray(b, dtype=float); b = b[np.isfinite(b)]
    if len(a) < 1 or len(b) < 1:
        return (np.nan, np.nan)
    if HAVE_SCIPY:
        u, p = _scipy_stats.mannwhitneyu(a, b, alternative="two-sided")
        return (float(u), float(p))
    # Pure-numpy U with normal approximation (no tie correction) — adequate
    # for the n ≈ 20 we have per cell.  scipy strongly preferred.
    n1, n2 = len(a), len(b)
    ranks = pd.Series(np.concatenate([a, b])).rank().values
    R1 = ranks[:n1].sum()
    U1 = R1 - n1 * (n1 + 1) / 2
    U  = min(U1, n1 * n2 - U1)
    mu = n1 * n2 / 2
    sigma = math.sqrt(n1 * n2 * (n1 + n2 + 1) / 12)
    z = (U - mu) / sigma if sigma else 0.0
    p = 2 * (1 - 0.5 * (1 + math.erf(abs(z) / math.sqrt(2))))
    return (float(U), float(p))


# ── per-cell aggregation ────────────────────────────────────────────────────

def cv(v: np.ndarray) -> float:
    v = np.asarray(v, dtype=float); v = v[np.isfinite(v)]
    if len(v) < 2 or v.mean() == 0:
        return np.nan
    return float(v.std(ddof=1) / v.mean())

def aggregate_cell(values: np.ndarray, n_boot: int) -> dict:
    v = np.asarray(values, dtype=float); v = v[np.isfinite(v)]
    if len(v) == 0:
        return dict(n=0, mean=np.nan, std=np.nan, median=np.nan,
                    p95=np.nan, ci_lo=np.nan, ci_hi=np.nan, cv=np.nan)
    med, lo, hi = bootstrap_ci(v, np.median, n_resample=n_boot)
    return dict(
        n      = int(len(v)),
        mean   = float(v.mean()),
        std    = float(v.std(ddof=1)) if len(v) > 1 else 0.0,
        median = med,
        p95    = float(np.quantile(v, 0.95)),
        ci_lo  = lo,
        ci_hi  = hi,
        cv     = cv(v),
    )


# ── master comparison table ─────────────────────────────────────────────────

LATENCY_METRICS = ["p50_ns", "p95_ns", "p99_ns", "p999_ns"]

def build_master_table(df: pd.DataFrame, n_boot: int) -> pd.DataFrame:
    """For each (benchmark-class, placement, msg_size, queue_depth, n_procs),
    compare POSIX vs SysV on the appropriate metric and emit one row.
    Latency benchmarks emit one row per percentile (p50/p95/p99/p99.9)."""
    rows = []

    # Pingpong rows (latency at multiple percentiles)
    pp = df[df["benchmark"] == "pingpong"]
    if not pp.empty:
        for (place, size), cell in pp.groupby(["placement_group", "msg_size_bytes"]):
            for fam, mechs, kind in (("MQ", MQ_MECHS, "mq"), ("SHM", SHM_MECHS, "shm")):
                for metric in LATENCY_METRICS:
                    a = cell[cell["mechanism"] == mechs[0]][metric].values
                    b = cell[cell["mechanism"] == mechs[1]][metric].values
                    rows.append(_compare_row(
                        family=fam, kind=kind, benchmark="pingpong",
                        place=place, msg_size=size, queue_depth=np.nan, n_procs=np.nan,
                        metric=metric,
                        posix=a, sysv=b, n_boot=n_boot,
                        posix_mech=mechs[0], sysv_mech=mechs[1],
                    ))

    # Throughput rows (throughput_MB_s, peak orientation)
    tp = df[df["benchmark"] == "throughput"]
    if not tp.empty:
        for (place, depth, size), cell in tp.groupby(
                ["placement_group", "queue_depth", "msg_size_bytes"]):
            for fam, mechs, kind in (("MQ", MQ_MECHS, "mq"), ("SHM", SHM_MECHS, "shm")):
                a = cell[cell["mechanism"] == mechs[0]]["throughput_MB_s"].values
                b = cell[cell["mechanism"] == mechs[1]]["throughput_MB_s"].values
                rows.append(_compare_row(
                    family=fam, kind=kind, benchmark="throughput",
                    place=place, msg_size=size, queue_depth=depth, n_procs=np.nan,
                    metric="throughput_MB_s",
                    posix=a, sysv=b, n_boot=n_boot,
                    posix_mech=mechs[0], sysv_mech=mechs[1],
                    higher_is_better=True,
                ))

    # Scalability fan-in (throughput) and pairs (p99)
    fanin = df[df["scal_kind"] == "fanin"]
    if not fanin.empty:
        for (n, size), cell in fanin.groupby(["scal_n", "msg_size_bytes"]):
            a = cell[cell["mechanism"] == "mq_posix"]["throughput_MB_s"].values
            b = cell[cell["mechanism"] == "mq_sysv"]["throughput_MB_s"].values
            rows.append(_compare_row(
                family="MQ", kind="mq", benchmark="fanin",
                place="", msg_size=size, queue_depth=np.nan, n_procs=n,
                metric="throughput_MB_s",
                posix=a, sysv=b, n_boot=n_boot,
                posix_mech="mq_posix", sysv_mech="mq_sysv",
                higher_is_better=True,
            ))

    pairs = df[df["scal_kind"] == "pairs"]
    if not pairs.empty:
        for (n, size), cell in pairs.groupby(["scal_n", "msg_size_bytes"]):
            for fam, mechs, kind in (("MQ", MQ_MECHS, "mq"), ("SHM", SHM_MECHS, "shm")):
                for metric in LATENCY_METRICS:
                    a = cell[cell["mechanism"] == mechs[0]][metric].values
                    b = cell[cell["mechanism"] == mechs[1]][metric].values
                    rows.append(_compare_row(
                        family=fam, kind=kind, benchmark="pairs",
                        place="", msg_size=size, queue_depth=np.nan, n_procs=n,
                        metric=metric,
                        posix=a, sysv=b, n_boot=n_boot,
                        posix_mech=mechs[0], sysv_mech=mechs[1],
                    ))

    return pd.DataFrame(rows)

def _compare_row(*, family, kind, benchmark, place, msg_size, queue_depth, n_procs,
                  metric, posix, sysv, n_boot, posix_mech, sysv_mech,
                  higher_is_better=False) -> dict:
    a = aggregate_cell(posix, n_boot)
    b = aggregate_cell(sysv,  n_boot)
    t_stat, t_p = welch_t(posix, sysv)
    u_stat, u_p = mannwhitney(posix, sysv)
    delta = cliffs_delta(posix, sysv)
    # Ratio orientation: sysv/posix.  >1 means sysv is "more" of the metric.
    # For latency lower=better: ratio>1 means sysv slower; <1 means sysv faster.
    # For throughput higher=better: ratio>1 means sysv has higher throughput.
    ratio_pt, ratio_lo, ratio_hi = bootstrap_ratio_ci(sysv, posix, n_boot)
    # Headline winner string
    if not np.isfinite(a["median"]) or not np.isfinite(b["median"]):
        winner = "n/a"
    elif higher_is_better:
        winner = sysv_mech if b["median"] > a["median"] else posix_mech
    else:
        winner = sysv_mech if b["median"] < a["median"] else posix_mech
    return {
        "family":   family,
        "kind":     kind,
        "benchmark": benchmark,
        "placement": place,
        "msg_size": msg_size,
        "queue_depth": queue_depth,
        "n_procs":  n_procs,
        "metric":   metric,
        "n_runs_posix": a["n"],
        "n_runs_sysv":  b["n"],
        f"{posix_mech}_median": a["median"],
        f"{posix_mech}_ci_lo":  a["ci_lo"],
        f"{posix_mech}_ci_hi":  a["ci_hi"],
        f"{posix_mech}_cv":     a["cv"],
        f"{sysv_mech}_median":  b["median"],
        f"{sysv_mech}_ci_lo":   b["ci_lo"],
        f"{sysv_mech}_ci_hi":   b["ci_hi"],
        f"{sysv_mech}_cv":      b["cv"],
        "ratio_sysv_over_posix":     ratio_pt,
        "ratio_ci_lo": ratio_lo,
        "ratio_ci_hi": ratio_hi,
        "welch_t":     t_stat,
        "welch_p":     t_p,
        "u_stat":      u_stat,
        "mannwhitney_p": u_p,
        "cliffs_delta": delta,
        "cliffs_magnitude": cliffs_magnitude(delta) if np.isfinite(delta) else "n/a",
        "winner": winner,
    }


# ── per-config descriptive table ────────────────────────────────────────────

def build_per_config_table(df: pd.DataFrame, n_boot: int) -> pd.DataFrame:
    rows = []
    metrics_for_bench = {
        "pingpong":   ["avg_ns", "p50_ns", "p95_ns", "p99_ns", "p999_ns",
                        "throughput_MB_s"],
        "throughput": ["throughput_msg_s", "throughput_MB_s"],
    }
    keys = ["mechanism", "benchmark", "placement_group", "msg_size_bytes",
            "queue_depth", "scal_n"]
    for k, cell in df.groupby(keys, dropna=False):
        mech, bench, place, size, depth, n = k
        bench_kind = bench if bench in metrics_for_bench \
                     else ("scalability" if str(bench).startswith(("fanin_n", "pairs_n")) else bench)
        if bench_kind == "scalability":
            metrics = ["p99_ns", "throughput_msg_s", "throughput_MB_s"]
        else:
            metrics = metrics_for_bench.get(bench_kind, ["avg_ns", "throughput_MB_s"])
        row = {
            "mechanism": mech, "benchmark": bench, "placement": place,
            "msg_size": size, "queue_depth": depth, "n_procs": n,
            "n_runs": int(cell["run_id"].nunique()),
        }
        for m in metrics:
            if m not in cell.columns:
                continue
            agg = aggregate_cell(cell[m].values, n_boot)
            row[f"{m}_median"] = agg["median"]
            row[f"{m}_ci_lo"]  = agg["ci_lo"]
            row[f"{m}_ci_hi"]  = agg["ci_hi"]
            row[f"{m}_mean"]   = agg["mean"]
            row[f"{m}_std"]    = agg["std"]
            row[f"{m}_cv"]     = agg["cv"]
        rows.append(row)
    return pd.DataFrame(rows)


# ── coverage table ──────────────────────────────────────────────────────────

def build_coverage_table(df: pd.DataFrame) -> pd.DataFrame:
    return (df.groupby(["mechanism", "benchmark"], dropna=False)
              .agg(rows=("benchmark", "size"),
                   runs=("run_id", "nunique"))
              .reset_index()
              .sort_values(["benchmark", "mechanism"]))


# ── plotting ────────────────────────────────────────────────────────────────

def save_fig(fig, stem: Path, fmt: str) -> None:
    stem.parent.mkdir(parents=True, exist_ok=True)
    if fmt in ("png", "both"):
        fig.savefig(stem.with_suffix(".png"), bbox_inches="tight", dpi=180)
    if fmt in ("pdf", "both"):
        fig.savefig(stem.with_suffix(".pdf"), bbox_inches="tight")
    plt.close(fig)

def _pretty_metric(metric: str) -> str:
    return (metric.replace("_ns", "")
                  .replace("p999", "p99.9")
                  .replace("p", "p"))

def plot_latency_vs_size(df: pd.DataFrame, mechs: list[str], title: str,
                          out_stem: Path, fmt: str,
                          metric: str = "p99_ns") -> None:
    """Latency-percentile vs message size, one subplot per placement."""
    pp = df[(df["benchmark"] == "pingpong") & (df["mechanism"].isin(mechs))]
    if pp.empty:
        return
    placements = [p for p in PLACEMENT_ORDER if p in pp["placement_group"].unique()]
    if not placements:
        return
    fig, axes = plt.subplots(1, len(placements),
                              figsize=(5 * len(placements), 4.2),
                              sharey=False, squeeze=False)
    axes = axes[0]
    for ax, place in zip(axes, placements):
        d = pp[pp["placement_group"] == place]
        for mech in mechs:
            m = d[d["mechanism"] == mech]
            if m.empty:
                continue
            g = (m.groupby("msg_size_bytes")[metric]
                  .agg(median="median",
                        q1=lambda s: s.quantile(0.25),
                        q3=lambda s: s.quantile(0.75))
                  .reset_index().sort_values("msg_size_bytes"))
            ax.plot(g["msg_size_bytes"], g["median"], marker="o",
                     label=mech, color=MECH_COLORS.get(mech))
            ax.fill_between(g["msg_size_bytes"], g["q1"], g["q3"],
                             color=MECH_COLORS.get(mech), alpha=0.15)
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel(f"{_pretty_metric(metric)} latency (ns)")
        ax.set_title(place)
        ax.grid(True, which="both", alpha=0.3, linestyle="--")
        ax.legend(fontsize=9)
    fig.suptitle(f"{title} ({_pretty_metric(metric)})")
    save_fig(fig, out_stem, fmt)

def plot_throughput_vs_size(df: pd.DataFrame, mechs: list[str], title: str,
                              out_stem: Path, fmt: str) -> None:
    tp = df[(df["benchmark"] == "throughput") & (df["mechanism"].isin(mechs))]
    if tp.empty:
        return
    placements = [p for p in PLACEMENT_ORDER if p in tp["placement_group"].unique()]
    if not placements:
        return
    fig, axes = plt.subplots(1, len(placements),
                              figsize=(5 * len(placements), 4.2),
                              sharey=True, squeeze=False)
    axes = axes[0]
    for ax, place in zip(axes, placements):
        d = tp[tp["placement_group"] == place]
        for mech in mechs:
            dm = d[d["mechanism"] == mech]
            depths = sorted(dm["queue_depth"].dropna().unique())
            for q in depths:
                dd = dm[dm["queue_depth"] == q]
                g = (dd.groupby("msg_size_bytes")["throughput_MB_s"]
                       .median().reset_index().sort_values("msg_size_bytes"))
                ax.plot(g["msg_size_bytes"], g["throughput_MB_s"],
                         marker="o", linewidth=1.2,
                         label=f"{mech} q={int(q)}")
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel("Throughput (MB/s)")
        ax.set_title(place)
        ax.grid(True, which="both", alpha=0.3, linestyle="--")
        ax.legend(fontsize=7, ncol=2)
    fig.suptitle(title)
    save_fig(fig, out_stem, fmt)

def plot_scalability_fanin(df: pd.DataFrame, out_stem: Path, fmt: str) -> None:
    sub = df[df["scal_kind"] == "fanin"]
    if sub.empty:
        return
    sizes = sorted(sub["msg_size_bytes"].dropna().unique())
    fig, axes = plt.subplots(1, len(sizes), figsize=(4.5 * len(sizes), 4),
                              sharey=False, squeeze=False)
    axes = axes[0]
    for ax, size in zip(axes, sizes):
        d = sub[sub["msg_size_bytes"] == size]
        for mech in MQ_MECHS:
            m = d[d["mechanism"] == mech]
            if m.empty: continue
            g = (m.groupby("scal_n")["throughput_MB_s"]
                  .agg(median="median", q1=lambda s: s.quantile(0.25),
                        q3=lambda s: s.quantile(0.75))
                  .reset_index().sort_values("scal_n"))
            ax.plot(g["scal_n"], g["median"], marker="o",
                     label=mech, color=MECH_COLORS.get(mech))
            ax.fill_between(g["scal_n"], g["q1"], g["q3"],
                             color=MECH_COLORS.get(mech), alpha=0.15)
        ax.set_xlabel("Producers (N)")
        ax.set_ylabel("Throughput (MB/s)")
        ax.set_title(f"size={int(size)}B")
        ax.set_xscale("log", base=2)
        ax.grid(True, which="both", alpha=0.3, linestyle="--")
        ax.legend(fontsize=9)
    fig.suptitle("MQ fan-in scalability")
    save_fig(fig, out_stem, fmt)

def plot_scalability_pairs(df: pd.DataFrame, mechs: list[str], title: str,
                            out_stem: Path, fmt: str,
                            metric: str = "p99_ns") -> None:
    sub = df[(df["scal_kind"] == "pairs") & (df["mechanism"].isin(mechs))]
    if sub.empty:
        return
    sizes = sorted(sub["msg_size_bytes"].dropna().unique())
    fig, axes = plt.subplots(1, len(sizes), figsize=(4.5 * len(sizes), 4),
                              sharey=False, squeeze=False)
    axes = axes[0]
    for ax, size in zip(axes, sizes):
        d = sub[sub["msg_size_bytes"] == size]
        for mech in mechs:
            m = d[d["mechanism"] == mech]
            if m.empty: continue
            g = (m.groupby("scal_n")[metric]
                  .agg(median="median",
                        q1=lambda s: s.quantile(0.25),
                        q3=lambda s: s.quantile(0.75))
                  .reset_index().sort_values("scal_n"))
            ax.plot(g["scal_n"], g["median"], marker="o",
                     label=mech, color=MECH_COLORS.get(mech))
            ax.fill_between(g["scal_n"], g["q1"], g["q3"],
                             color=MECH_COLORS.get(mech), alpha=0.15)
        ax.set_xlabel("Pairs (N)")
        ax.set_ylabel(f"{_pretty_metric(metric)} latency (ns)")
        ax.set_title(f"size={int(size)}B")
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.grid(True, which="both", alpha=0.3, linestyle="--")
        ax.legend(fontsize=9)
    fig.suptitle(f"{title} ({_pretty_metric(metric)})")
    save_fig(fig, out_stem, fmt)

def plot_tail_grid(df: pd.DataFrame, mechs: list[str], title: str,
                    out_stem: Path, fmt: str) -> None:
    """Tail-percentile overlay (p50/p95/p99/p99.9) as a grid:
    rows = placement, columns = message size.  Replaces the single-cell
    overlay so the paper can read the tail story across the matrix."""
    pp = df[(df["benchmark"] == "pingpong") & (df["mechanism"].isin(mechs))]
    if pp.empty:
        return
    placements = [p for p in PLACEMENT_ORDER if p in pp["placement_group"].unique()]
    sizes = sorted(pp["msg_size_bytes"].dropna().unique())
    if not placements or not sizes:
        return
    cols = ["p50_ns", "p95_ns", "p99_ns", "p999_ns"]
    labels = ["p50", "p95", "p99", "p99.9"]

    fig, axes = plt.subplots(len(placements), len(sizes),
                              figsize=(2.6 * len(sizes), 2.6 * len(placements)),
                              sharey="row", squeeze=False)
    for i, place in enumerate(placements):
        for j, size in enumerate(sizes):
            ax = axes[i][j]
            d = pp[(pp["placement_group"] == place) &
                    (pp["msg_size_bytes"] == size)]
            for mech in mechs:
                m = d[d["mechanism"] == mech]
                if m.empty: continue
                meds = [m[c].median() for c in cols]
                ax.plot(labels, meds, marker="o", linewidth=1.4,
                         markersize=4, label=mech,
                         color=MECH_COLORS.get(mech))
            ax.set_yscale("log")
            ax.grid(True, which="both", alpha=0.3, linestyle="--")
            if i == 0:
                ax.set_title(f"{int(size)}B", fontsize=10)
            if j == 0:
                ax.set_ylabel(f"{place}\nLatency (ns)", fontsize=9)
            else:
                ax.tick_params(labelleft=False)
            ax.tick_params(axis="x", labelsize=8)
            if i == len(placements) - 1:
                ax.set_xticks(labels)
            else:
                ax.set_xticklabels([])
    # one legend for the whole grid
    handles, lbls = axes[0][0].get_legend_handles_labels()
    if handles:
        fig.legend(handles, lbls, loc="upper right", fontsize=9, ncol=len(mechs))
    fig.suptitle(title)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    save_fig(fig, out_stem, fmt)


def plot_speedup_ratio(df: pd.DataFrame, mechs: list[str], title: str,
                        out_stem: Path, fmt: str,
                        metric: str = "p50_ns") -> None:
    """Plot ratio sysv/posix vs message size, per placement, with
    bootstrap-style IQR band derived from per-run pairing (median per run_id)."""
    pp = df[(df["benchmark"] == "pingpong") & (df["mechanism"].isin(mechs))]
    if pp.empty or len(mechs) != 2:
        return
    posix_mech, sysv_mech = mechs[0], mechs[1]
    placements = [p for p in PLACEMENT_ORDER if p in pp["placement_group"].unique()]
    if not placements:
        return

    fig, axes = plt.subplots(1, len(placements),
                              figsize=(5 * len(placements), 4.0),
                              sharey=True, squeeze=False)
    axes = axes[0]
    for ax, place in zip(axes, placements):
        d = pp[pp["placement_group"] == place]
        sizes = sorted(d["msg_size_bytes"].dropna().unique())
        med = []; lo = []; hi = []; xs = []
        for s in sizes:
            ds = d[d["msg_size_bytes"] == s]
            a = ds[ds["mechanism"] == posix_mech][metric].values
            b = ds[ds["mechanism"] == sysv_mech][metric].values
            if len(a) < 2 or len(b) < 2:
                continue
            r_pt, r_lo, r_hi = bootstrap_ratio_ci(b, a, n_resample=2000)
            xs.append(s); med.append(r_pt); lo.append(r_lo); hi.append(r_hi)
        if not xs:
            continue
        med = np.array(med); lo = np.array(lo); hi = np.array(hi)
        ax.plot(xs, med, marker="o", linewidth=1.6, color="#444")
        ax.fill_between(xs, lo, hi, color="#888", alpha=0.25)
        ax.axhline(1.0, linestyle="--", color="red", linewidth=0.8, alpha=0.7)
        ax.set_xscale("log", base=2)
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel(f"{sysv_mech} / {posix_mech}  ({_pretty_metric(metric)})")
        ax.set_title(place)
        ax.grid(True, which="both", alpha=0.3, linestyle="--")
    fig.suptitle(f"{title} — ratio {sysv_mech}/{posix_mech}, {_pretty_metric(metric)} "
                  "(<1 ⇒ SysV faster, >1 ⇒ POSIX faster)")
    save_fig(fig, out_stem, fmt)


def plot_throughput_ratio(df: pd.DataFrame, mechs: list[str], title: str,
                            out_stem: Path, fmt: str) -> None:
    """Throughput ratio sysv/posix vs message size, one line per depth, per
    placement.  Mirrors plot_speedup_ratio but for the throughput benchmark."""
    tp = df[(df["benchmark"] == "throughput") & (df["mechanism"].isin(mechs))]
    if tp.empty or len(mechs) != 2:
        return
    posix_mech, sysv_mech = mechs[0], mechs[1]
    placements = [p for p in PLACEMENT_ORDER if p in tp["placement_group"].unique()]
    if not placements:
        return
    fig, axes = plt.subplots(1, len(placements),
                              figsize=(5 * len(placements), 4.0),
                              sharey=True, squeeze=False)
    axes = axes[0]
    for ax, place in zip(axes, placements):
        d = tp[tp["placement_group"] == place]
        depths = sorted(d["queue_depth"].dropna().unique())
        for depth in depths:
            dd = d[d["queue_depth"] == depth]
            sizes = sorted(dd["msg_size_bytes"].dropna().unique())
            xs = []; med = []
            for s in sizes:
                ds = dd[dd["msg_size_bytes"] == s]
                a = ds[ds["mechanism"] == posix_mech]["throughput_MB_s"].values
                b = ds[ds["mechanism"] == sysv_mech]["throughput_MB_s"].values
                if len(a) < 2 or len(b) < 2:
                    continue
                ma = np.median(a); mb = np.median(b)
                if not (np.isfinite(ma) and np.isfinite(mb) and ma > 0):
                    continue
                xs.append(s); med.append(mb / ma)
            if xs:
                ax.plot(xs, med, marker="o", linewidth=1.2,
                         label=f"q={int(depth)}")
        ax.axhline(1.0, linestyle="--", color="red", linewidth=0.8, alpha=0.7)
        ax.set_xscale("log", base=2)
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel(f"{sysv_mech} / {posix_mech}  (MB/s)")
        ax.set_title(place)
        ax.grid(True, which="both", alpha=0.3, linestyle="--")
        ax.legend(fontsize=8, ncol=2)
    fig.suptitle(f"{title} — throughput ratio {sysv_mech}/{posix_mech}")
    save_fig(fig, out_stem, fmt)


# ── perf integration ────────────────────────────────────────────────────────

def load_perf(posix_path: Path, sysv_path: Path) -> pd.DataFrame | None:
    frames = []
    for path in (posix_path, sysv_path):
        if path.exists():
            try:
                frames.append(pd.read_csv(path))
            except Exception as e:
                print(f"  warning: failed to read {path}: {e}", file=sys.stderr)
    if not frames:
        return None
    df = pd.concat(frames, ignore_index=True)
    df["value"] = pd.to_numeric(df["value"], errors="coerce")
    return df

def summarise_perf(perf: pd.DataFrame) -> pd.DataFrame:
    """One row per (mechanism, benchmark, placement, msg_size, queue_depth,
    n_procs, event) with mean ± std across runs."""
    keys = ["mechanism", "benchmark", "placement", "msg_size", "queue_depth",
            "n_procs", "event"]
    return (perf.groupby(keys, dropna=False)
                .agg(value_mean=("value", "mean"),
                     value_std=("value", "std"),
                     n_runs=("run_id", "nunique"))
                .reset_index())


# ── strace integration ─────────────────────────────────────────────────────

def parse_strace_summary(path: Path) -> list[dict]:
    """Parse `strace -c` summary blocks delimited by '====' headers.
    Returns one record per (block, syscall) row."""
    if not path.exists():
        return []
    txt = path.read_text(errors="replace")
    blocks = re.split(r"={20,}\n", txt)
    records = []
    current_header = ""
    for blk in blocks:
        # The first line of each non-empty block is the header (e.g. "MQ pingpong | same_core | 64B")
        lines = [ln.rstrip() for ln in blk.splitlines() if ln.strip()]
        if not lines:
            continue
        # Heuristic: a syscall summary contains a header line "% time   seconds  usecs/call ..."
        if any(re.search(r"%\s*time", ln) for ln in lines):
            # Find the table within these lines
            in_table = False
            for ln in lines:
                if re.search(r"%\s*time\s+seconds", ln):
                    in_table = True
                    continue
                if not in_table:
                    continue
                if ln.startswith("------") or ln.startswith("100.00"):
                    continue
                # Row: "%time seconds usecs/call calls errors syscall"
                parts = ln.split()
                if len(parts) < 5:
                    continue
                try:
                    pct = float(parts[0]); secs = float(parts[1])
                    usecs = float(parts[2]); calls = int(parts[3])
                except ValueError:
                    continue
                errors_n = 0
                syscall = parts[-1]
                if len(parts) >= 6 and parts[-2].isdigit():
                    errors_n = int(parts[-2])
                records.append({
                    "section":  current_header,
                    "syscall":  syscall,
                    "pct_time": pct,
                    "seconds":  secs,
                    "usecs_call": usecs,
                    "calls":    calls,
                    "errors":   errors_n,
                })
        else:
            # Treat the first non-comment line as a header for the next table
            for ln in lines:
                if not ln.startswith("#") and "|" in ln:
                    current_header = ln.strip()
                    break
            else:
                if lines and not lines[0].startswith("#"):
                    current_header = lines[0].strip()
    return records


# ── narrative report ───────────────────────────────────────────────────────

def write_report(df: pd.DataFrame, master: pd.DataFrame, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    lines = []
    lines.append("# POSIX vs System V IPC — Analysis Report")
    lines.append("")
    lines.append(f"- Total rows: {len(df)}")
    lines.append(f"- Mechanisms present: {sorted(df['mechanism'].dropna().unique())}")
    lines.append(f"- Benchmarks present: {sorted(df['benchmark'].dropna().unique())}")
    lines.append(f"- Placements present: {sorted(df['placement_group'].dropna().unique())}")
    lines.append("")
    lines.append("## Headline comparisons (significant cells, p < 0.05)")
    lines.append("")
    if not master.empty:
        sig = master[master["mannwhitney_p"] < 0.05].copy()
        sig = sig.sort_values(["family", "benchmark", "msg_size",
                                "queue_depth", "n_procs"])
        if sig.empty:
            lines.append("_No cells reached p<0.05 — sample size may be too small._")
        else:
            for _, r in sig.iterrows():
                tag = []
                if r.get("placement"):  tag.append(str(r["placement"]))
                if pd.notna(r.get("msg_size")): tag.append(f"{int(r['msg_size'])}B")
                if pd.notna(r.get("queue_depth")): tag.append(f"q={int(r['queue_depth'])}")
                if pd.notna(r.get("n_procs")):    tag.append(f"N={int(r['n_procs'])}")
                lines.append(
                    f"- **{r['family']} {r['benchmark']}** "
                    f"({', '.join(tag)}): "
                    f"ratio sysv/posix = {r['ratio_sysv_over_posix']:.2f}× "
                    f"[{r['ratio_ci_lo']:.2f}–{r['ratio_ci_hi']:.2f}], "
                    f"MW p={r['mannwhitney_p']:.1e}, "
                    f"Cliff δ={r['cliffs_delta']:.2f} ({r['cliffs_magnitude']}), "
                    f"winner={r['winner']}"
                )
    lines.append("")
    lines.append("## Coverage")
    lines.append("")
    cov = build_coverage_table(df)
    lines.append("| benchmark | mechanism | rows | runs |")
    lines.append("|---|---|---:|---:|")
    for _, r in cov.iterrows():
        lines.append(f"| {r['benchmark']} | {r['mechanism']} | {int(r['rows'])} | {int(r['runs'])} |")
    lines.append("")
    lines.append("## Notes")
    lines.append("")
    lines.append("- Statistics: medians + bootstrap 95% CIs (10000 resamples).")
    lines.append("- Tests: Welch's t (parametric) and Mann-Whitney U (non-parametric).")
    lines.append("- Effect size: Cliff's δ (negligible<0.147, small<0.33, medium<0.474, large≥0.474).")
    lines.append("- Run order is randomised by run_suite.sh to decouple drift from mechanism.")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ── main ────────────────────────────────────────────────────────────────────

def main() -> None:
    args = parse_args()
    in_path = Path(args.input)
    out_dir = Path(args.outdir)
    fig_dir = out_dir / "figures"
    tab_dir = out_dir / "tables"

    if not HAVE_SCIPY:
        print("note: scipy not installed; using numpy/erfc fallbacks for tests "
               "(install scipy for exact p-values)", file=sys.stderr)

    plt.style.use("ggplot")

    df = load_data(in_path)
    print(f"Loaded {len(df)} rows from {in_path}")

    # Tables
    coverage = build_coverage_table(df)
    coverage.to_csv(tab_dir / "coverage.csv", index=False)

    per_cfg = build_per_config_table(df, args.bootstrap)
    per_cfg.to_csv(tab_dir / "per_config_summary.csv", index=False)

    master = build_master_table(df, args.bootstrap)
    master.to_csv(tab_dir / "master_comparison.csv", index=False)
    print(f"Wrote {len(master)} comparison rows to master_comparison.csv")

    # Optional perf
    perf_posix = Path(args.perf_posix); perf_sysv = Path(args.perf_sysv)
    perf = load_perf(perf_posix, perf_sysv)
    if perf is not None and not perf.empty:
        ps = summarise_perf(perf)
        ps.to_csv(tab_dir / "perf_summary.csv", index=False)
        print(f"Wrote perf_summary.csv ({len(ps)} rows)")

    # Optional strace
    s_recs = []
    for p in (Path(args.strace_posix), Path(args.strace_sysv)):
        s_recs.extend(parse_strace_summary(p))
    if s_recs:
        sdf = pd.DataFrame(s_recs)
        sdf.to_csv(tab_dir / "strace_summary.csv", index=False)
        print(f"Wrote strace_summary.csv ({len(sdf)} rows)")

    # ── Latency-vs-size plots: one per percentile per family ───────────
    for metric in ("p50_ns", "p95_ns", "p99_ns", "p999_ns"):
        suffix = metric.replace("_ns", "").replace("p999", "p999")
        plot_latency_vs_size(df, MQ_MECHS,
                              "MQ ping-pong latency (POSIX vs SysV)",
                              fig_dir / f"mq_pingpong_{suffix}",
                              args.format, metric=metric)
        plot_latency_vs_size(df, SHM_MECHS,
                              "SHM ping-pong latency (POSIX vs SysV)",
                              fig_dir / f"shm_pingpong_{suffix}",
                              args.format, metric=metric)

    # ── Throughput-vs-size plots ────────────────────────────────────────
    plot_throughput_vs_size(df, MQ_MECHS,
                              "MQ throughput (POSIX vs SysV)",
                              fig_dir / "mq_throughput", args.format)
    plot_throughput_vs_size(df, SHM_MECHS,
                              "SHM throughput (POSIX vs SysV)",
                              fig_dir / "shm_throughput", args.format)

    # ── Scalability ─────────────────────────────────────────────────────
    plot_scalability_fanin(df, fig_dir / "mq_fanin", args.format)
    for metric in ("p50_ns", "p99_ns"):
        suffix = metric.replace("_ns", "")
        plot_scalability_pairs(df, MQ_MECHS,
                                "MQ parallel-pairs latency",
                                fig_dir / f"mq_pairs_{suffix}",
                                args.format, metric=metric)
        plot_scalability_pairs(df, SHM_MECHS,
                                "SHM parallel-pairs latency",
                                fig_dir / f"shm_pairs_{suffix}",
                                args.format, metric=metric)

    # ── Tail-percentile grid (replaces single-cell tail overlay) ────────
    plot_tail_grid(df, MQ_MECHS,
                    "MQ tail latency by placement × size",
                    fig_dir / "mq_tail_grid", args.format)
    plot_tail_grid(df, SHM_MECHS,
                    "SHM tail latency by placement × size",
                    fig_dir / "shm_tail_grid", args.format)

    # ── Speedup ratio plots (headline finding figure) ───────────────────
    for metric in ("p50_ns", "p99_ns"):
        suffix = metric.replace("_ns", "")
        plot_speedup_ratio(df, MQ_MECHS,
                            "MQ ping-pong speedup",
                            fig_dir / f"mq_pingpong_ratio_{suffix}",
                            args.format, metric=metric)
        plot_speedup_ratio(df, SHM_MECHS,
                            "SHM ping-pong speedup",
                            fig_dir / f"shm_pingpong_ratio_{suffix}",
                            args.format, metric=metric)
    plot_throughput_ratio(df, MQ_MECHS,
                           "MQ throughput",
                           fig_dir / "mq_throughput_ratio", args.format)
    plot_throughput_ratio(df, SHM_MECHS,
                           "SHM throughput",
                           fig_dir / "shm_throughput_ratio", args.format)

    write_report(df, master, out_dir / "analysis_report.md")

    print("\nAnalysis complete")
    print(f"  tables  → {tab_dir}")
    print(f"  figures → {fig_dir}")
    print(f"  report  → {out_dir / 'analysis_report.md'}")


if __name__ == "__main__":
    main()
