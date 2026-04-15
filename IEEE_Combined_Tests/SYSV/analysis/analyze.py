#!/usr/bin/env python3
"""
analyze.py — IEEE-level statistical analysis for the IPC benchmark suite.

Consumes: results/all_runs.csv  (produced by analysis/run_experiment.sh)
          results/perf_stat.csv (produced by analysis/perf_wrap.sh, optional)

Produces:
  results/tables/         — LaTeX-ready .tex tables for the paper
  results/figures/        — PDF and PNG figures
  results/stats_report.txt — human-readable summary with hypothesis tests

Statistical methods
-------------------
  - Per-configuration mean ± standard deviation across 30 runs.
  - 95% confidence interval via t-distribution (scipy.stats.t.interval).
  - Wilcoxon signed-rank test (paired) and Mann-Whitney U test (unpaired)
    for POSIX MQ vs. System V MQ and POSIX MQ vs. shm_posix comparisons.
  - Full empirical CDF for tail latency: p50, p95, p99, p99.9 from all runs
    pooled, then per-run distribution shown as box-and-whisker plots.

Usage
-----
  python3 analysis/analyze.py [options]

  --input FILE      CSV file (default: results/all_runs.csv)
  --sysv  FILE      System V results CSV for cross-mechanism comparison
  --perf  FILE      perf stat CSV (default: results/perf_stat.csv)
  --outdir DIR      output directory (default: results/)
  --format {pdf,png,both}  figure format (default: both)
  --no-latex        skip LaTeX table generation
  --alpha FLOAT     significance level for hypothesis tests (default: 0.05)
  --help            print this message and exit
"""

import argparse
import csv
import os
import sys
import warnings
from collections import defaultdict
from pathlib import Path

import numpy as np

# ── Optional imports with graceful degradation ───────────────────────────────
try:
    import scipy.stats as st
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False
    warnings.warn("scipy not found. Hypothesis tests disabled. "
                  "Install with: pip install scipy")

try:
    import matplotlib
    matplotlib.use("Agg")          # headless rendering
    import matplotlib.pyplot as plt
    import matplotlib.ticker as mticker
    HAS_MPL = True
except ImportError:
    HAS_MPL = False
    warnings.warn("matplotlib not found. Figures disabled. "
                  "Install with: pip install matplotlib")

try:
    import pandas as pd
    HAS_PD = True
except ImportError:
    HAS_PD = False
    warnings.warn("pandas not found. Some analyses disabled. "
                  "Install with: pip install pandas")

# ── Constants ────────────────────────────────────────────────────────────────

PERCENTILE_COLS = ["p50_ns", "p95_ns", "p99_ns", "p999_ns"]
PERCENTILE_LABELS = ["p50", "p95", "p99", "p99.9"]
LATENCY_COLS = ["avg_ns", "p50_ns", "p95_ns", "p99_ns", "p999_ns",
                "min_ns", "max_ns"]
THROUGHPUT_COLS = ["throughput_msg_s", "throughput_MB_s"]
NUMERIC_COLS = LATENCY_COLS + THROUGHPUT_COLS + ["mem_delta_kb"]

# ── Data loading ─────────────────────────────────────────────────────────────

def load_csv(path: str) -> list[dict]:
    """Load CSV, return list of row dicts with numeric fields cast."""
    rows = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            # Skip comment lines emitted by priority_test
            if row.get("mechanism", "").startswith("#"):
                continue
            for col in NUMERIC_COLS:
                if col in row:
                    try:
                        row[col] = float(row[col])
                    except (ValueError, TypeError):
                        row[col] = float("nan")
            rows.append(row)
    return rows


def group_by(rows: list[dict], keys: list[str]) -> dict:
    """Group rows by a tuple of key values."""
    groups: dict = defaultdict(list)
    for row in rows:
        k = tuple(row.get(kk, "") for kk in keys)
        groups[k].append(row)
    return dict(groups)

# ── Statistical helpers ──────────────────────────────────────────────────────

def ci95(values: np.ndarray) -> tuple[float, float]:
    """95% confidence interval using t-distribution."""
    n = len(values)
    if n < 2:
        return (float(values[0]), float(values[0])) if n == 1 else (0.0, 0.0)
    mean = float(np.mean(values))
    se   = float(st.sem(values))
    lo, hi = st.t.interval(0.95, df=n-1, loc=mean, scale=se)
    return (lo, hi)


def summarise(rows: list[dict], col: str) -> dict:
    """Return mean, std, 95% CI, n for a numeric column across rows."""
    vals = np.array([r[col] for r in rows if not np.isnan(r.get(col, float("nan")))])
    if len(vals) == 0:
        return {"mean": float("nan"), "std": 0, "ci_lo": float("nan"),
                "ci_hi": float("nan"), "n": 0}
    mean = float(np.mean(vals))
    std  = float(np.std(vals, ddof=1)) if len(vals) > 1 else 0.0
    if HAS_SCIPY and len(vals) > 1:
        ci_lo, ci_hi = ci95(vals)
    else:
        ci_lo = ci_hi = mean
    return {"mean": mean, "std": std, "ci_lo": ci_lo, "ci_hi": ci_hi,
            "n": len(vals)}


def wilcoxon_test(a_rows, b_rows, col, alpha=0.05):
    """
    Wilcoxon signed-rank test (paired) if equal n, else Mann-Whitney U.
    Returns (statistic, p_value, significant, test_name).
    """
    if not HAS_SCIPY:
        return (float("nan"), float("nan"), False, "unavailable")

    a = np.array([r[col] for r in a_rows if not np.isnan(r.get(col, float("nan")))])
    b = np.array([r[col] for r in b_rows if not np.isnan(r.get(col, float("nan")))])

    if len(a) == 0 or len(b) == 0:
        return (float("nan"), float("nan"), False, "insufficient_data")

    if len(a) == len(b):
        stat, p = st.wilcoxon(a, b, alternative="two-sided")
        name = "Wilcoxon signed-rank"
    else:
        stat, p = st.mannwhitneyu(a, b, alternative="two-sided")
        name = "Mann-Whitney U"

    return (float(stat), float(p), bool(p < alpha), name)

# ── LaTeX helpers ─────────────────────────────────────────────────────────────

def latex_pm(mean: float, std: float, unit: str = "ns",
             scale: float = 1.0) -> str:
    """Format mean ± std in LaTeX math mode."""
    m = mean / scale
    s = std  / scale
    if abs(m) >= 1e6:
        return rf"${m/1e6:.2f} \pm {s/1e6:.2f}$\,M{unit}"
    if abs(m) >= 1e3:
        return rf"${m/1e3:.1f} \pm {s/1e3:.1f}$\,k{unit}"
    return rf"${m:.1f} \pm {s:.1f}$\,{unit}"


def write_latex_table(path: str, headers: list[str],
                      rows: list[list[str]], caption: str, label: str):
    """Write a complete LaTeX table environment to path."""
    ncols = len(headers)
    col_fmt = "l" + "r" * (ncols - 1)

    lines = [
        r"\begin{table}[!t]",
        r"\centering",
        rf"\caption{{{caption}}}",
        rf"\label{{{label}}}",
        rf"\begin{{tabular}}{{{col_fmt}}}",
        r"\toprule",
        " & ".join(rf"\textbf{{{h}}}" for h in headers) + r" \\",
        r"\midrule",
    ]
    for row in rows:
        lines.append(" & ".join(str(c) for c in row) + r" \\")
    lines += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"  [LaTeX] {path}")

# ── Figure helpers ────────────────────────────────────────────────────────────

FIG_STYLE = {
    "font.size":        9,
    "axes.titlesize":   9,
    "axes.labelsize":   9,
    "xtick.labelsize":  8,
    "ytick.labelsize":  8,
    "legend.fontsize":  8,
    "lines.linewidth":  1.2,
    "figure.dpi":       150,
}

COLORS = {
    "mq_posix":  "#1f77b4",
    "shm_posix": "#2ca02c",
    "mq_sysv":   "#d62728",
}


def save_fig(fig, path_stem: str, fmt: str):
    os.makedirs(os.path.dirname(path_stem) or ".", exist_ok=True)
    if fmt in ("pdf", "both"):
        fig.savefig(path_stem + ".pdf", bbox_inches="tight")
    if fmt in ("png", "both"):
        fig.savefig(path_stem + ".png", bbox_inches="tight", dpi=200)
    plt.close(fig)
    print(f"  [figure] {path_stem}.{fmt if fmt != 'both' else 'pdf/png'}")


# ═══════════════════════════════════════════════════════════════════════════════
# Analysis modules
# ═══════════════════════════════════════════════════════════════════════════════

def analyse_pingpong_latency(rows, outdir, fmt, alpha, report_lines):
    """Per-placement, per-message-size latency tables and figures."""
    report_lines.append("\n" + "="*70)
    report_lines.append("PING-PONG LATENCY ANALYSIS")
    report_lines.append("="*70)

    pp_rows = [r for r in rows if "pingpong" in r.get("benchmark", "")]
    if not pp_rows:
        report_lines.append("  No pingpong rows found.")
        return

    by_mech_place_size = group_by(pp_rows,
        ["mechanism", "placement", "msg_size_bytes"])

    placements = sorted({r["placement"] for r in pp_rows})
    sizes      = sorted({int(r["msg_size_bytes"]) for r in pp_rows},
                        key=int)
    mechanisms = sorted({r["mechanism"] for r in pp_rows})

    # ── Table: mean±std for avg_ns and p99_ns per config ──────────────────
    for placement in placements:
        headers = ["Mechanism", "Size (B)"] + \
                  [f"{p} (ns)" for p in PERCENTILE_LABELS] + ["Tput (MB/s)"]
        table_rows = []

        for mech in mechanisms:
            for sz in sizes:
                key = (mech, placement, str(sz))
                grp = by_mech_place_size.get(key, [])
                if not grp:
                    continue
                row_out = [mech, sz]
                for col in PERCENTILE_COLS:
                    s = summarise(grp, col)
                    row_out.append(
                        rf"${s['mean']:.0f} \pm {s['std']:.0f}$"
                        if s['n'] > 1 else
                        rf"${s['mean']:.0f}$"
                    )
                s_tp = summarise(grp, "throughput_MB_s")
                row_out.append(rf"${s_tp['mean']:.1f} \pm {s_tp['std']:.2f}$")
                table_rows.append(row_out)

            # Blank separator between mechanisms
            if mech != mechanisms[-1]:
                table_rows.append([""] * len(headers))

        write_latex_table(
            f"{outdir}/tables/pingpong_latency_{placement}.tex",
            headers, table_rows,
            caption=f"Ping-Pong one-way latency ({placement} placement), "
                    r"mean $\pm$ std across 30 runs.",
            label=f"tab:pp_latency_{placement}"
        )

    # ── Figure: latency vs message size, one subplot per placement ────────
    if not HAS_MPL:
        return

    with plt.style.context(FIG_STYLE):
        ncols = len(placements) or 1
        fig, axes = plt.subplots(1, ncols,
                                  figsize=(3.5 * ncols, 3.0),
                                  sharey=False)
        if ncols == 1:
            axes = [axes]

        for ax, placement in zip(axes, placements):
            for mech in mechanisms:
                means, stds = [], []
                valid_sizes = []
                for sz in sizes:
                    key = (mech, placement, str(sz))
                    grp = by_mech_place_size.get(key, [])
                    if not grp:
                        continue
                    s = summarise(grp, "p99_ns")
                    means.append(s["mean"]); stds.append(s["std"])
                    valid_sizes.append(sz)
                if not means:
                    continue
                ax.errorbar(
                    valid_sizes, means, yerr=stds,
                    label=mech, color=COLORS.get(mech, None),
                    marker="o", capsize=3, linewidth=1.2
                )
            ax.set_xscale("log", base=2)
            ax.set_xlabel("Message size (bytes)")
            ax.set_ylabel("p99 one-way latency (ns)")
            ax.set_title(placement.replace("_", " "))
            ax.xaxis.set_major_formatter(
                mticker.FuncFormatter(lambda x, _: f"{int(x)}"))
            ax.legend()
            ax.grid(True, linestyle="--", alpha=0.4)

        fig.suptitle("Ping-Pong p99 Latency vs Message Size", y=1.02)
        fig.tight_layout()
        save_fig(fig, f"{outdir}/figures/pingpong_p99_vs_size", fmt)

    # ── Optional hypothesis tests for POSIX-vs-SHM datasets ───────────────
    if "mq_posix" in mechanisms and "shm_posix" in mechanisms:
        report_lines.append("\nHypothesis tests (mq_posix vs shm_posix), p99_ns")
        report_lines.append(f"  Significance level α = {alpha}")
        report_lines.append(f"  {'Placement':<16} {'Size':>6}  "
                            f"{'Test':<22} {'stat':>10}  {'p':>8}  sig?")
        report_lines.append("  " + "-"*72)

        for placement in placements:
            for sz in sizes:
                mq_grp  = by_mech_place_size.get(("mq_posix",  placement, str(sz)), [])
                shm_grp = by_mech_place_size.get(("shm_posix", placement, str(sz)), [])
                if not mq_grp or not shm_grp:
                    continue
                stat, p, sig, name = wilcoxon_test(mq_grp, shm_grp, "p99_ns", alpha)
                report_lines.append(
                    f"  {placement:<16} {sz:>6}  {name:<22} {stat:>10.2f}  "
                    f"{p:>8.4f}  {'YES ***' if sig else 'no'}")
    else:
        report_lines.append("\nHypothesis tests skipped: mq_posix+shm_posix pair not present.")


def analyse_throughput(rows, outdir, fmt, alpha, report_lines):
    """Throughput vs queue depth and message size."""
    report_lines.append("\n" + "="*70)
    report_lines.append("THROUGHPUT ANALYSIS")
    report_lines.append("="*70)

    tp_rows = [r for r in rows if r.get("benchmark") == "throughput"]
    if not tp_rows:
        report_lines.append("  No throughput rows found.")
        return

    # Parse queue depth from placement label (e.g. "same_socket_q64")
    for r in tp_rows:
        p = r.get("placement", "")
        r["queue_depth"] = int(p.split("_q")[-1]) if "_q" in p else 0

    by_depth_size = group_by(tp_rows, ["queue_depth", "msg_size_bytes"])
    depths = sorted({r["queue_depth"] for r in tp_rows})
    sizes  = sorted({int(r["msg_size_bytes"]) for r in tp_rows}, key=int)

    headers = ["Queue depth", "Size (B)", r"Msgs/s (mean$\pm$std)",
               r"MB/s (mean$\pm$std)", r"95\% CI MB/s"]
    table_rows = []

    for depth in depths:
        for sz in sizes:
            key = (depth, str(sz))
            grp = by_depth_size.get(key, [])
            if not grp:
                continue
            s_msg = summarise(grp, "throughput_msg_s")
            s_mb  = summarise(grp, "throughput_MB_s")
            table_rows.append([
                depth, sz,
                rf"${s_msg['mean']/1e6:.2f}M \pm {s_msg['std']/1e6:.3f}M$",
                rf"${s_mb['mean']:.1f} \pm {s_mb['std']:.2f}$",
                rf"$[{s_mb['ci_lo']:.1f}, {s_mb['ci_hi']:.1f}]$",
            ])
        table_rows.append([""] * len(headers))

    write_latex_table(
        f"{outdir}/tables/throughput.tex", headers, table_rows,
        caption=r"Producer-side throughput vs queue depth and message size, "
                r"mean $\pm$ std across 30 runs.",
        label="tab:throughput"
    )

    if not HAS_MPL:
        return

    with plt.style.context(FIG_STYLE):
        fig, ax = plt.subplots(figsize=(5, 3.5))
        for depth in depths:
            means, errs, valid = [], [], []
            for sz in sizes:
                grp = by_depth_size.get((depth, str(sz)), [])
                if not grp:
                    continue
                s = summarise(grp, "throughput_MB_s")
                means.append(s["mean"]); errs.append(s["std"])
                valid.append(sz)
            ax.errorbar(valid, means, yerr=errs, label=f"depth={depth}",
                        marker="s", capsize=3)
        ax.set_xscale("log", base=2)
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel("Throughput (MB/s)")
        ax.set_title("Message Queue Producer Throughput")
        ax.xaxis.set_major_formatter(
            mticker.FuncFormatter(lambda x, _: f"{int(x)}"))
        ax.legend()
        ax.grid(True, linestyle="--", alpha=0.4)
        fig.tight_layout()
        save_fig(fig, f"{outdir}/figures/throughput_MB_vs_size", fmt)

    report_lines.append("  Throughput table written.")


def analyse_scalability(rows, outdir, fmt, alpha, report_lines):
    """Fan-in and parallel-pairs scalability."""
    report_lines.append("\n" + "="*70)
    report_lines.append("SCALABILITY ANALYSIS")
    report_lines.append("="*70)

    fanin_rows = [r for r in rows if "fanin" in r.get("benchmark", "")]
    pairs_rows = [r for r in rows if "pairs" in r.get("benchmark", "")]

    def _n_from_bench(bench_name):
        """Extract N from bench names like fanin_n4 or pairs_n2."""
        parts = bench_name.split("_n")
        return int(parts[-1]) if len(parts) > 1 else 0

    for r in fanin_rows + pairs_rows:
        r["n_workers"] = _n_from_bench(r.get("benchmark", ""))

    if HAS_MPL and fanin_rows:
        with plt.style.context(FIG_STYLE):
            fig, axes = plt.subplots(1, 2, figsize=(7, 3.2))

            # Fan-in throughput
            by_n_s = group_by(fanin_rows, ["n_workers", "msg_size_bytes"])
            ns  = sorted({r["n_workers"] for r in fanin_rows})
            szs = sorted({int(r["msg_size_bytes"]) for r in fanin_rows}, key=int)
            ax = axes[0]
            for sz in szs:
                means, errs, valid_ns = [], [], []
                for n in ns:
                    grp = by_n_s.get((n, str(sz)), [])
                    if not grp:
                        continue
                    s = summarise(grp, "throughput_MB_s")
                    means.append(s["mean"]); errs.append(s["std"])
                    valid_ns.append(n)
                ax.errorbar(valid_ns, means, yerr=errs,
                            label=f"{sz}B", marker="o", capsize=3)
            ax.set_xlabel("Number of producers")
            ax.set_ylabel("Throughput (MB/s)")
            ax.set_title("Fan-in Scalability")
            ax.legend(title="msg size")
            ax.grid(True, linestyle="--", alpha=0.4)

            # Parallel pairs p99 latency
            by_n_s_p = group_by(pairs_rows, ["n_workers", "msg_size_bytes"])
            ns_p  = sorted({r["n_workers"] for r in pairs_rows})
            szs_p = sorted({int(r["msg_size_bytes"]) for r in pairs_rows}, key=int)
            ax = axes[1]
            for sz in szs_p:
                means, errs, valid_ns = [], [], []
                for n in ns_p:
                    grp = by_n_s_p.get((n, str(sz)), [])
                    if not grp:
                        continue
                    s = summarise(grp, "p99_ns")
                    means.append(s["mean"]); errs.append(s["std"])
                    valid_ns.append(n)
                ax.errorbar(valid_ns, means, yerr=errs,
                            label=f"{sz}B", marker="^", capsize=3)
            ax.set_xlabel("Number of pairs")
            ax.set_ylabel("p99 one-way latency (ns)")
            ax.set_title("Parallel Pairs: p99 under Contention")
            ax.legend(title="msg size")
            ax.grid(True, linestyle="--", alpha=0.4)

            fig.suptitle("Message Queue Scalability", y=1.01)
            fig.tight_layout()
            save_fig(fig, f"{outdir}/figures/scalability", fmt)

    report_lines.append("  Scalability figures written.")


def analyse_priority(rows, outdir, fmt, alpha, report_lines):
    """
    Priority test analysis.

    T1: head-of-line jump — HIGH vs LOW latency, plus jump distance comment lines.
    T2: backpressure divergence — HIGH vs LOW p99 under throttled consumer.
    T3: PPR vs N-producer — plot and table showing ordering preservation.
    """
    report_lines.append("\n" + "="*70)
    report_lines.append("PRIORITY TEST ANALYSIS (novel contribution)")
    report_lines.append("="*70)

    # ── T1 and T2: HIGH vs LOW latency comparison ──────────────────────────
    for test_tag, test_name in [("t1", "T1 head-of-line jump"),
                                  ("t2", "T2 backpressure")]:
        high_rows = [r for r in rows
                     if r.get("benchmark", "").startswith(f"priority_{test_tag}_high")]
        low_rows  = [r for r in rows
                     if r.get("benchmark", "").startswith(f"priority_{test_tag}_low")]

        if not high_rows and not low_rows:
            report_lines.append(f"  No {test_tag} rows found.")
            continue

        report_lines.append(f"\n--- {test_name} ---")

        sizes      = sorted({int(r["msg_size_bytes"]) for r in high_rows + low_rows},
                             key=int)
        placements = sorted({r["placement"] for r in high_rows + low_rows})

        by_sz_pl_high = group_by(high_rows, ["msg_size_bytes", "placement"])
        by_sz_pl_low  = group_by(low_rows,  ["msg_size_bytes", "placement"])

        headers = ["Placement", "Size (B)",
                   r"LOW p99 ns ($\pm$std)",
                   r"HIGH p99 ns ($\pm$std)",
                   r"Speedup", r"$p$-value", "Sig?"]
        table_rows = []

        report_lines.append(
            f"  {'Placement':<18} {'Size':>6}  "
            f"{'LOW p99':>10}  {'HIGH p99':>10}  {'ratio':>7}  {'p':>8}")
        report_lines.append("  " + "-"*66)

        for placement in placements:
            for sz in sizes:
                key = (str(sz), placement)
                hg = by_sz_pl_high.get(key, [])
                lg = by_sz_pl_low.get(key,  [])
                if not hg or not lg:
                    continue
                sh = summarise(hg, "p99_ns")
                sl = summarise(lg, "p99_ns")
                ratio = sl["mean"] / sh["mean"] if sh["mean"] else float("nan")
                _, p, sig, _ = wilcoxon_test(lg, hg, "p99_ns", alpha)

                table_rows.append([
                    placement, sz,
                    rf"${sl['mean']:.0f} \pm {sl['std']:.0f}$",
                    rf"${sh['mean']:.0f} \pm {sh['std']:.0f}$",
                    rf"${ratio:.2f}\times$",
                    rf"${p:.4f}$" if not np.isnan(p) else "N/A",
                    r"\checkmark" if sig else "---",
                ])
                report_lines.append(
                    f"  {placement:<18} {sz:>6}  "
                    f"{sl['mean']:>10.0f}  {sh['mean']:>10.0f}  "
                    f"{ratio:>7.2f}x  {p:>8.4f}  "
                    f"{'***' if sig else ''}")

        write_latex_table(
            f"{outdir}/tables/priority_{test_tag}.tex", headers, table_rows,
            caption=(
                rf"Priority {test_tag.upper()}: HIGH vs LOW p99 latency (ns), "
                rf"mean $\pm$ std across 30 runs. "
                rf"{'Concurrent alternating stream, depth=8.' if test_tag=='t1' else 'Throttled consumer (1 µs/msg), depth=8 — sustained backpressure.'}"
            ),
            label=f"tab:priority_{test_tag}"
        )

    # ── T1 jump-distance figure ────────────────────────────────────────────
    # Parse # T1_jump comment lines from the CSV (stored as rows where
    # mechanism starts with '#')
    # analyze.py skips comment rows on load, so we reconstruct from
    # the stderr summary embedded in the T1 benchmark label instead.
    # For the figure we use the T1 HIGH vs LOW p99 bar chart.
    t1_high = [r for r in rows
               if r.get("benchmark", "").startswith("priority_t1_high")]
    t1_low  = [r for r in rows
               if r.get("benchmark", "").startswith("priority_t1_low")]

    if HAS_MPL and (t1_high or t1_low):
        sizes = sorted({int(r["msg_size_bytes"]) for r in t1_high + t1_low},
                       key=int)
        with plt.style.context(FIG_STYLE):
            fig, ax = plt.subplots(figsize=(6, 3.5))
            x     = np.arange(len(sizes))
            width = 0.35
            low_m  = [summarise([r for r in t1_low  if int(r["msg_size_bytes"])==s],
                                "p99_ns")["mean"] for s in sizes]
            low_e  = [summarise([r for r in t1_low  if int(r["msg_size_bytes"])==s],
                                "p99_ns")["std"]  for s in sizes]
            high_m = [summarise([r for r in t1_high if int(r["msg_size_bytes"])==s],
                                "p99_ns")["mean"] for s in sizes]
            high_e = [summarise([r for r in t1_high if int(r["msg_size_bytes"])==s],
                                "p99_ns")["std"]  for s in sizes]
            ax.bar(x - width/2, low_m,  width, yerr=low_e,
                   label="LOW priority",  capsize=4, color="#d62728")
            ax.bar(x + width/2, high_m, width, yerr=high_e,
                   label="HIGH priority", capsize=4, color="#1f77b4")
            ax.set_xticks(x)
            ax.set_xticklabels([str(s) for s in sizes])
            ax.set_xlabel("Message size (bytes)")
            ax.set_ylabel("p99 one-way latency (ns)")
            ax.set_title("T1: HIGH vs LOW priority p99 latency\n"
                         "(concurrent stream, depth=8)")
            ax.legend()
            ax.grid(True, axis="y", linestyle="--", alpha=0.4)
            fig.tight_layout()
            save_fig(fig, f"{outdir}/figures/priority_t1_p99", fmt)

    # ── T2 backpressure figure ─────────────────────────────────────────────
    t2_high = [r for r in rows
               if r.get("benchmark", "").startswith("priority_t2_high")]
    t2_low  = [r for r in rows
               if r.get("benchmark", "").startswith("priority_t2_low")]

    if HAS_MPL and (t2_high or t2_low):
        sizes = sorted({int(r["msg_size_bytes"]) for r in t2_high + t2_low},
                       key=int)
        pcts  = ["p50_ns", "p95_ns", "p99_ns", "p999_ns"]
        pctlabels = ["p50", "p95", "p99", "p99.9"]
        with plt.style.context(FIG_STYLE):
            fig, axes = plt.subplots(1, len(sizes), figsize=(4*len(sizes), 3.5),
                                     sharey=False)
            if len(sizes) == 1:
                axes = [axes]
            for ax, sz in zip(axes, sizes):
                hg = [r for r in t2_high if int(r["msg_size_bytes"]) == sz]
                lg = [r for r in t2_low  if int(r["msg_size_bytes"]) == sz]
                hvals = [summarise(hg, p)["mean"] for p in pcts]
                lvals = [summarise(lg, p)["mean"] for p in pcts]
                x = np.arange(len(pcts))
                w = 0.35
                ax.bar(x - w/2, lvals, w, label="LOW",  color="#d62728")
                ax.bar(x + w/2, hvals, w, label="HIGH", color="#1f77b4")
                ax.set_xticks(x)
                ax.set_xticklabels(pctlabels)
                ax.set_xlabel("Percentile")
                ax.set_ylabel("Latency (ns)")
                ax.set_title(f"{sz}B — backpressure")
                ax.legend()
                ax.grid(True, axis="y", linestyle="--", alpha=0.4)
            fig.suptitle("T2: Backpressure latency divergence HIGH vs LOW",
                         y=1.02)
            fig.tight_layout()
            save_fig(fig, f"{outdir}/figures/priority_t2_backpressure", fmt)

    # ── T3: PPR vs N-producer ─────────────────────────────────────────────
    t3_rows = [r for r in rows
               if r.get("benchmark", "").startswith("priority_t3_n")]

    if not t3_rows:
        report_lines.append("\n  No T3 rows found.")
    else:
        def _n_from_t3(bench):
            # "priority_t3_n4" → 4
            try: return int(bench.split("_n")[-1])
            except: return 0

        for r in t3_rows:
            r["n_prod"] = _n_from_t3(r.get("benchmark", ""))

        sizes = sorted({int(r["msg_size_bytes"]) for r in t3_rows}, key=int)
        ns    = sorted({r["n_prod"] for r in t3_rows})

        report_lines.append("\n--- T3: PPR vs N-producers ---")
        report_lines.append(
            f"  {'N':>4}  {'Size':>6}  {'p50 (ns)':>10}  "
            f"{'p99 (ns)':>10}  {'mean±std p99':>18}")
        report_lines.append("  " + "-"*58)

        headers = ["N producers", "Size (B)",
                   r"p99 ns ($\pm$std)", r"p99.9 ns ($\pm$std)"]
        table_rows = []

        for sz in sizes:
            for n in ns:
                grp = [r for r in t3_rows
                       if r["n_prod"] == n and int(r["msg_size_bytes"]) == sz]
                if not grp:
                    continue
                s99  = summarise(grp, "p99_ns")
                s999 = summarise(grp, "p999_ns")
                table_rows.append([
                    n, sz,
                    rf"${s99['mean']:.0f} \pm {s99['std']:.0f}$",
                    rf"${s999['mean']:.0f} \pm {s999['std']:.0f}$",
                ])
                report_lines.append(
                    f"  {n:>4}  {sz:>6}  "
                    f"{s99['mean']:>10.0f}  "
                    f"{s99['mean']:>10.0f}  "
                    f"{s99['mean']:>8.0f}±{s99['std']:.0f}")

        write_latex_table(
            f"{outdir}/tables/priority_t3.tex", headers, table_rows,
            caption=r"T3: Per-consumer latency (ns) under N concurrent producers "
                    r"with start barrier, mean $\pm$ std across 30 runs. "
                    r"Each producer uses a distinct priority level (1..N). "
                    r"See stderr PPR values for ordering preservation ratio.",
            label="tab:priority_t3"
        )

        # PPR vs N figure (latency proxy — p99 vs N shows contention cost)
        if HAS_MPL:
            with plt.style.context(FIG_STYLE):
                fig, ax = plt.subplots(figsize=(5, 3.5))
                for sz in sizes:
                    means, errs, valid_ns = [], [], []
                    for n in ns:
                        grp = [r for r in t3_rows
                               if r["n_prod"] == n
                               and int(r["msg_size_bytes"]) == sz]
                        if not grp:
                            continue
                        s = summarise(grp, "p99_ns")
                        means.append(s["mean"]); errs.append(s["std"])
                        valid_ns.append(n)
                    ax.errorbar(valid_ns, means, yerr=errs,
                                label=f"{sz}B", marker="o", capsize=3)
                ax.set_xlabel("Number of concurrent producers (N)")
                ax.set_ylabel("p99 consumer latency (ns)")
                ax.set_title("T3: Consumer p99 Latency vs N Producers\n"
                             "(start barrier — genuine concurrency)")
                ax.legend(title="msg size")
                ax.grid(True, linestyle="--", alpha=0.4)
                fig.tight_layout()
                save_fig(fig, f"{outdir}/figures/priority_t3_vs_n", fmt)


def analyse_numa(rows, outdir, fmt, alpha, report_lines):
    """
    NUMA topology section: compare same_core, same_socket, cross_socket
    placements for available pingpong mechanisms.
    """
    report_lines.append("\n" + "="*70)
    report_lines.append("NUMA TOPOLOGY ANALYSIS")
    report_lines.append("="*70)

    pp_rows = [r for r in rows if "pingpong" in r.get("benchmark", "")]
    placements = sorted({r["placement"] for r in pp_rows}
                        - {"unspecified"})
    mechanisms = sorted({r["mechanism"] for r in pp_rows})
    sizes = [64, 1024]  # representative sizes

    if len(placements) < 2:
        report_lines.append("  Fewer than 2 placements found — "
                            "NUMA comparison requires same_core + "
                            "same_socket + cross_socket.")
        report_lines.append("  Run on a multi-core or multi-socket machine.")
        return

    by_mech_place_size = group_by(pp_rows,
        ["mechanism", "placement", "msg_size_bytes"])

    headers = ["Placement", "Mechanism", "Size (B)",
               r"avg (ns)", r"p50 (ns)", r"p99 (ns)", r"p99.9 (ns)"]
    table_rows = []

    for sz in sizes:
        for placement in placements:
            for mech in mechanisms:
                key = (mech, placement, str(sz))
                grp = by_mech_place_size.get(key, [])
                if not grp:
                    continue
                row_out = [placement, mech, sz]
                for col in ["avg_ns", "p50_ns", "p99_ns", "p999_ns"]:
                    s = summarise(grp, col)
                    row_out.append(rf"${s['mean']:.0f} \pm {s['std']:.0f}$")
                table_rows.append(row_out)
        table_rows.append([""] * len(headers))

    write_latex_table(
        f"{outdir}/tables/numa_placement.tex", headers, table_rows,
        caption=r"Effect of NUMA placement on IPC latency, mean $\pm$ std "
                r"across 30 runs.",
        label="tab:numa_placement"
    )

    report_lines.append("  NUMA placement table written.")


def analyse_memory(rows, outdir, report_lines):
    """Memory footprint analysis from mem_delta_kb column."""
    report_lines.append("\n" + "="*70)
    report_lines.append("MEMORY FOOTPRINT ANALYSIS")
    report_lines.append("="*70)

    mem_rows = [r for r in rows
                if r.get("mem_delta_kb", 0) and
                   abs(float(r.get("mem_delta_kb", 0))) > 0]

    if not mem_rows:
        report_lines.append("  No memory delta data (all zeros).")
        report_lines.append("  Note: kernel MQ pages are often charged to")
        report_lines.append("  the creating process at mq_open time, not during")
        report_lines.append("  the benchmark loop. Run with sudo to read accurate")
        report_lines.append("  /proc data, or use valgrind --tool=massif.")
        return

    by_mech_bench = group_by(mem_rows, ["mechanism", "benchmark"])
    report_lines.append(f"\n  {'Mechanism':<14} {'Benchmark':<22} "
                        f"{'mean delta KB':>14}  {'std':>8}")
    report_lines.append("  " + "-"*62)

    headers = ["Mechanism", "Benchmark", r"Mean $\Delta$VmRSS (KB)",
               r"Std (KB)"]
    table_rows = []
    for (mech, bench), grp in sorted(by_mech_bench.items()):
        s = summarise(grp, "mem_delta_kb")
        report_lines.append(f"  {mech:<14} {bench:<22} "
                             f"{s['mean']:>14.1f}  {s['std']:>8.1f}")
        table_rows.append([mech, bench,
                            rf"${s['mean']:.1f}$",
                            rf"${s['std']:.1f}$"])

    write_latex_table(
        f"{outdir}/tables/memory_footprint.tex", headers, table_rows,
        caption=r"VmRSS memory delta (KB) per IPC mechanism. "
                r"Lower values indicate smaller kernel-internal queue state.",
        label="tab:memory_footprint"
    )


def analyse_perf(perf_csv: str, outdir: str, report_lines: list[str]):
    """
    Parse perf stat CSV and produce IPC + TLB miss table.

    Expected columns (produced by analysis/perf_wrap.sh):
      mechanism, benchmark, msg_size_bytes, placement,
      cycles, instructions, ipc,
      cache_misses, cache_refs, cache_miss_rate,
      dtlb_load_misses, dtlb_loads, dtlb_miss_rate,
      context_switches, migrations
    """
    report_lines.append("\n" + "="*70)
    report_lines.append("HARDWARE PERFORMANCE COUNTERS (IPC / TLB)")
    report_lines.append("="*70)

    if not os.path.exists(perf_csv):
        report_lines.append(f"  {perf_csv} not found.")
        report_lines.append("  Run: make run_perf_wrap to generate it.")
        return

    rows = []
    with open(perf_csv, newline="") as f:
        for row in csv.DictReader(f):
            for col in ["ipc", "cache_miss_rate", "dtlb_miss_rate",
                        "context_switches", "cycles", "instructions"]:
                try:
                    row[col] = float(row[col])
                except (ValueError, KeyError):
                    row[col] = float("nan")
            rows.append(row)

    if not rows:
        report_lines.append("  perf CSV is empty.")
        return

    headers = ["Mechanism", "Benchmark", "Size (B)", "IPC",
               r"Cache miss \%", r"dTLB miss \%", "Ctx-sw"]
    table_rows = []

    for row in rows:
        table_rows.append([
            row.get("mechanism", ""),
            row.get("benchmark", ""),
            row.get("msg_size_bytes", ""),
            rf"${row['ipc']:.3f}$" if not np.isnan(row['ipc']) else "N/A",
            rf"${row['cache_miss_rate']*100:.2f}$"
                if not np.isnan(row['cache_miss_rate']) else "N/A",
            rf"${row['dtlb_miss_rate']*100:.2f}$"
                if not np.isnan(row['dtlb_miss_rate']) else "N/A",
            rf"${row['context_switches']:.0f}$"
                if not np.isnan(row['context_switches']) else "N/A",
        ])

    write_latex_table(
        f"{outdir}/tables/perf_counters.tex", headers, table_rows,
        caption=r"Hardware performance counters per IPC mechanism. "
                r"IPC = instructions per cycle. "
                r"dTLB miss rate = dTLB-load-misses / dTLB-loads. "
                r"Higher IPC and lower miss rates indicate more efficient "
                r"kernel paths.",
        label="tab:perf_counters"
    )

    report_lines.append("  perf counter table written.")

# ═══════════════════════════════════════════════════════════════════════════════
# Percentile CDF figure (pooled across all 30 runs)
# ═══════════════════════════════════════════════════════════════════════════════

def plot_percentile_cdf(rows, outdir, fmt, report_lines):
    """
    Plot empirical CDF of per-run p50/p95/p99/p99.9 for pingpong benchmarks.
    Shows the distribution of each percentile across the 30 runs, making
    variance visible.
    """
    if not HAS_MPL:
        return

    target_mechanism = "mq_sysv" if any(r.get("mechanism") == "mq_sysv" for r in rows) else "mq_posix"

    pp_rows = [r for r in rows
               if "pingpong" in r.get("benchmark", "") and
                  r.get("mechanism") == target_mechanism and
                  int(r.get("msg_size_bytes", 0)) == 64]

    placements = sorted({r["placement"] for r in pp_rows})
    if not placements:
        return

    with plt.style.context(FIG_STYLE):
        ncols = len(placements) or 1
        fig, axes = plt.subplots(1, ncols,
                                  figsize=(4 * ncols, 3.5),
                                  sharey=False)
        if ncols == 1:
            axes = [axes]

        for ax, placement in zip(axes, placements):
            grp = [r for r in pp_rows if r["placement"] == placement]
            for col, label in zip(PERCENTILE_COLS, PERCENTILE_LABELS):
                vals = sorted([r[col] for r in grp
                               if not np.isnan(r.get(col, float("nan")))])
                if not vals:
                    continue
                cdf = np.linspace(0, 1, len(vals))
                ax.plot(vals, cdf, label=label, linewidth=1.3)

            ax.set_xlabel("Latency (ns)")
            ax.set_ylabel("Empirical CDF")
            ax.set_title(f"{placement.replace('_', ' ')} — 64 B")
            ax.legend(title="percentile")
            ax.grid(True, linestyle="--", alpha=0.4)

        fig.suptitle("Empirical CDF of per-run latency percentiles "
                 f"({target_mechanism} pingpong, 30 runs)", y=1.02)
        fig.tight_layout()
        save_fig(fig, f"{outdir}/figures/pingpong_percentile_cdf", fmt)

    report_lines.append("  Percentile CDF figure written.")

# ═══════════════════════════════════════════════════════════════════════════════
# main
# ═══════════════════════════════════════════════════════════════════════════════

def parse_args():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--input",    default="results/all_runs.csv")
    p.add_argument("--sysv",     default=None,
                   help="System V results CSV for cross-mechanism comparison")
    p.add_argument("--perf",     default="results/perf_stat.csv")
    p.add_argument("--outdir",   default="results")
    p.add_argument("--format",   choices=["pdf", "png", "both"], default="both")
    p.add_argument("--no-latex", action="store_true")
    p.add_argument("--alpha",    type=float, default=0.05)
    return p.parse_args()


def main():
    args = parse_args()

    print(f"Loading {args.input} …")
    rows = load_csv(args.input)
    print(f"  {len(rows)} data rows loaded.")

    # Merge System V data if provided
    if args.sysv and os.path.exists(args.sysv):
        print(f"Loading System V results from {args.sysv} …")
        sysv_rows = load_csv(args.sysv)
        # Re-label mechanism field if not already set
        for r in sysv_rows:
            if r.get("mechanism") in ("", "mq_posix"):
                r["mechanism"] = "mq_sysv"
        rows += sysv_rows
        print(f"  {len(sysv_rows)} SysV rows merged.")

    os.makedirs(f"{args.outdir}/tables",  exist_ok=True)
    os.makedirs(f"{args.outdir}/figures", exist_ok=True)

    report_lines: list[str] = [
        "IPC Benchmark Suite — Statistical Analysis Report",
        f"Input: {args.input}",
        f"Total rows: {len(rows)}",
        f"Significance level α = {args.alpha}",
        f"scipy available: {HAS_SCIPY}",
        f"matplotlib available: {HAS_MPL}",
    ]

    print("\nRunning analyses …")
    analyse_pingpong_latency(rows, args.outdir, args.format,
                              args.alpha, report_lines)
    analyse_throughput(rows, args.outdir, args.format,
                        args.alpha, report_lines)
    analyse_scalability(rows, args.outdir, args.format,
                         args.alpha, report_lines)
    analyse_priority(rows, args.outdir, args.format,
                      args.alpha, report_lines)
    analyse_numa(rows, args.outdir, args.format,
                  args.alpha, report_lines)
    analyse_memory(rows, args.outdir, report_lines)
    analyse_perf(args.perf, args.outdir, report_lines)
    plot_percentile_cdf(rows, args.outdir, args.format, report_lines)

    report_path = f"{args.outdir}/stats_report.txt"
    with open(report_path, "w") as f:
        f.write("\n".join(report_lines) + "\n")

    print(f"\n[report] {report_path}")
    print("\nDone.")


if __name__ == "__main__":
    main()
