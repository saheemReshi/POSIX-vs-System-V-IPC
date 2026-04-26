#!/usr/bin/env python3
"""
Analyze the compact POSIX vs SysV benchmark suite.

Inputs:
  results/raw/combined/all_runs.csv

Outputs (under results/analysis by default):
  - tables/summary_by_config.csv
  - tables/coverage_by_test.csv
  - tables/mq_pingpong_p99.csv
  - tables/shm_pingpong_p99.csv
  - tables/mq_throughput_peak.csv
  - figures/*.png (or pdf)
  - analysis_report.md
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

import numpy as np

try:
    import pandas as pd
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pandas is required. Install with: pip install pandas") from exc

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover
    raise SystemExit("matplotlib is required. Install with: pip install matplotlib") from exc


NUMERIC_COLS = [
    "n_procs",
    "msg_size_bytes",
    "run_id",
    "iterations",
    "avg_ns",
    "p50_ns",
    "p95_ns",
    "p99_ns",
    "p999_ns",
    "min_ns",
    "max_ns",
    "throughput_msg_s",
    "throughput_MB_s",
    "mem_delta_kb",
]

MECH_COLORS = {
    "mq_posix": "#1f77b4",
    "mq_sysv": "#d62728",
    "shm_posix": "#2ca02c",
    "shm_sysv": "#9467bd",
}


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    root_dir = script_dir.parent
    parser = argparse.ArgumentParser(description="Analyze compact benchmark results")
    parser.add_argument(
        "--input",
        default=str(root_dir / "results/raw/combined/all_runs.csv"),
        help="Combined CSV file",
    )
    parser.add_argument(
        "--outdir",
        default=str(root_dir / "results/analysis"),
        help="Output directory for tables/figures/report",
    )
    parser.add_argument(
        "--format",
        choices=["png", "pdf", "both"],
        default="png",
        help="Figure output format",
    )
    return parser.parse_args()


def placement_group(value: str) -> str:
    s = str(value)
    if s.startswith("same_core"):
        return "same_core"
    if s.startswith("cross_core"):
        return "cross_core"
    return s


def queue_depth(value: str) -> float:
    m = re.search(r"_q(\d+)", str(value))
    if not m:
        return np.nan
    return float(m.group(1))


def save_fig(fig: plt.Figure, stem: Path, fmt: str) -> None:
    stem.parent.mkdir(parents=True, exist_ok=True)
    if fmt in ("png", "both"):
        fig.savefig(stem.with_suffix(".png"), bbox_inches="tight", dpi=180)
    if fmt in ("pdf", "both"):
        fig.savefig(stem.with_suffix(".pdf"), bbox_inches="tight")
    plt.close(fig)


def load_data(path: Path) -> pd.DataFrame:
    if not path.exists():
        raise SystemExit(f"Input CSV not found: {path}")

    df = pd.read_csv(path, comment="#")

    required = {"mechanism", "benchmark", "placement"}
    missing = required - set(df.columns)
    if missing:
        raise SystemExit(f"Missing required columns in CSV: {sorted(missing)}")

    for col in NUMERIC_COLS:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")

    df["placement_group"] = df["placement"].map(placement_group)
    df["queue_depth"] = df["placement"].map(queue_depth)

    return df


def write_tables(df: pd.DataFrame, out_tables: Path) -> None:
    out_tables.mkdir(parents=True, exist_ok=True)

    summary_cols = [
        "mechanism",
        "benchmark",
        "placement_group",
        "msg_size_bytes",
        "n_procs",
    ]

    summary = (
        df.groupby(summary_cols, dropna=False)
        .agg(
            runs=("run_id", "nunique"),
            avg_ns_mean=("avg_ns", "mean"),
            avg_ns_std=("avg_ns", "std"),
            p99_ns_mean=("p99_ns", "mean"),
            p99_ns_std=("p99_ns", "std"),
            throughput_MB_s_mean=("throughput_MB_s", "mean"),
            throughput_MB_s_std=("throughput_MB_s", "std"),
        )
        .reset_index()
        .sort_values(summary_cols)
    )
    summary.to_csv(out_tables / "summary_by_config.csv", index=False)

    coverage = (
        df.groupby(["mechanism", "benchmark"], dropna=False)
        .agg(rows=("benchmark", "size"), runs=("run_id", "nunique"))
        .reset_index()
        .sort_values(["benchmark", "mechanism"])
    )
    coverage.to_csv(out_tables / "coverage_by_test.csv", index=False)

    mq_pp = df[(df["benchmark"] == "pingpong") & (df["mechanism"].isin(["mq_posix", "mq_sysv"]))]
    if not mq_pp.empty:
        mq_pp_tbl = (
            mq_pp.groupby(["mechanism", "placement_group", "msg_size_bytes"], dropna=False)
            .agg(p99_ns_mean=("p99_ns", "mean"), p99_ns_std=("p99_ns", "std"))
            .reset_index()
            .sort_values(["placement_group", "msg_size_bytes", "mechanism"])
        )
        mq_pp_tbl.to_csv(out_tables / "mq_pingpong_p99.csv", index=False)

    shm_pp = df[(df["benchmark"] == "pingpong") & (df["mechanism"].isin(["shm_posix", "shm_sysv"]))]
    if not shm_pp.empty:
        shm_pp_tbl = (
            shm_pp.groupby(["mechanism", "placement_group", "msg_size_bytes"], dropna=False)
            .agg(p99_ns_mean=("p99_ns", "mean"), p99_ns_std=("p99_ns", "std"))
            .reset_index()
            .sort_values(["placement_group", "msg_size_bytes", "mechanism"])
        )
        shm_pp_tbl.to_csv(out_tables / "shm_pingpong_p99.csv", index=False)

    tp = df[(df["benchmark"] == "throughput") & (df["mechanism"].isin(["mq_posix", "mq_sysv"]))]
    if not tp.empty:
        peak = (
            tp.groupby(["mechanism", "placement_group", "queue_depth"], dropna=False)
            .agg(peak_MB_s=("throughput_MB_s", "max"), mean_MB_s=("throughput_MB_s", "mean"))
            .reset_index()
            .sort_values(["placement_group", "queue_depth", "mechanism"])
        )
        peak.to_csv(out_tables / "mq_throughput_peak.csv", index=False)


def plot_pingpong(df: pd.DataFrame, mechanisms: list[str], title_prefix: str, out_stem: Path, fmt: str) -> None:
    plot_df = df[(df["benchmark"] == "pingpong") & (df["mechanism"].isin(mechanisms))]
    if plot_df.empty:
        return

    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2), sharey=False)
    placements = ["same_core", "cross_core"]

    for ax, placement in zip(axes, placements):
        d = plot_df[plot_df["placement_group"] == placement]
        if d.empty:
            ax.set_title(f"{placement} (no data)")
            ax.set_xlabel("Message size (bytes)")
            ax.set_ylabel("p99 latency (ns)")
            continue

        for mech in mechanisms:
            m = d[d["mechanism"] == mech]
            if m.empty:
                continue
            g = (
                m.groupby("msg_size_bytes", dropna=False)
                .agg(mean=("p99_ns", "mean"), std=("p99_ns", "std"))
                .reset_index()
                .sort_values("msg_size_bytes")
            )
            ax.errorbar(
                g["msg_size_bytes"],
                g["mean"],
                yerr=g["std"].fillna(0.0),
                marker="o",
                linewidth=1.6,
                capsize=4,
                label=mech,
                color=MECH_COLORS.get(mech, None),
            )

        ax.set_title(placement)
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel("p99 latency (ns)")
        ax.grid(True, alpha=0.35, linestyle="--")
        ax.legend()

    fig.suptitle(title_prefix)
    save_fig(fig, out_stem, fmt)


def plot_throughput(df: pd.DataFrame, out_stem: Path, fmt: str) -> None:
    tp = df[(df["benchmark"] == "throughput") & (df["mechanism"].isin(["mq_posix", "mq_sysv"]))]
    tp = tp[tp["placement_group"].isin(["same_core", "cross_core"])]
    if tp.empty:
        return

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5), sharey=True)

    for ax, placement in zip(axes, ["same_core", "cross_core"]):
        d = tp[tp["placement_group"] == placement]
        if d.empty:
            ax.set_title(f"{placement} (no data)")
            continue

        for mech in ["mq_posix", "mq_sysv"]:
            dm = d[d["mechanism"] == mech]
            for depth in sorted(dm["queue_depth"].dropna().unique()):
                dd = dm[dm["queue_depth"] == depth]
                g = (
                    dd.groupby("msg_size_bytes", dropna=False)
                    .agg(mean=("throughput_MB_s", "mean"))
                    .reset_index()
                    .sort_values("msg_size_bytes")
                )
                if g.empty:
                    continue
                ax.plot(
                    g["msg_size_bytes"],
                    g["mean"],
                    marker="o",
                    linewidth=1.4,
                    label=f"{mech} depth={int(depth)}",
                )

        ax.set_title(placement)
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel("Throughput (MB/s)")
        ax.grid(True, alpha=0.35, linestyle="--")
        ax.legend(fontsize=7)

    fig.suptitle("MQ throughput vs message size")
    save_fig(fig, out_stem, fmt)


def plot_scalability(df: pd.DataFrame, benchmark_prefix: str, metric: str, title: str, out_stem: Path, fmt: str) -> None:
    sub = df[
        df["benchmark"].astype(str).str.startswith(benchmark_prefix)
        & df["mechanism"].isin(["mq_posix", "mq_sysv"])
    ]
    if sub.empty:
        return

    # Keep one payload to make the plot easier to compare.
    preferred_size = 64.0
    if preferred_size in set(sub["msg_size_bytes"].dropna().unique()):
        sub = sub[sub["msg_size_bytes"] == preferred_size]
    else:
        first_size = sorted(sub["msg_size_bytes"].dropna().unique())[0]
        sub = sub[sub["msg_size_bytes"] == first_size]

    if sub.empty:
        return

    fig, ax = plt.subplots(figsize=(6.5, 4.2))

    for mech in ["mq_posix", "mq_sysv"]:
        d = sub[sub["mechanism"] == mech]
        if d.empty:
            continue
        g = (
            d.groupby("n_procs", dropna=False)
            .agg(mean=(metric, "mean"), std=(metric, "std"))
            .reset_index()
            .sort_values("n_procs")
        )
        ax.errorbar(
            g["n_procs"],
            g["mean"],
            yerr=g["std"].fillna(0.0),
            marker="o",
            linewidth=1.6,
            capsize=4,
            label=mech,
            color=MECH_COLORS.get(mech, None),
        )

    ax.set_title(title)
    ax.set_xlabel("Concurrent producers / pairs")
    ax.set_ylabel("Metric")
    if metric == "throughput_msg_s":
        ax.set_ylabel("Throughput (msg/s)")
    elif metric == "p99_ns":
        ax.set_ylabel("p99 latency (ns)")

    ax.grid(True, alpha=0.35, linestyle="--")
    ax.legend()
    save_fig(fig, out_stem, fmt)


def write_report(df: pd.DataFrame, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)

    lines = []
    lines.append("# Compact Benchmark Analysis Report")
    lines.append("")
    lines.append(f"Total rows: {len(df)}")
    lines.append("")

    mechs = sorted(df["mechanism"].dropna().unique())
    lines.append("## Rows per mechanism")
    for mech in mechs:
        lines.append(f"- {mech}: {len(df[df['mechanism'] == mech])}")
    lines.append("")

    lines.append("## Run coverage by test")
    cov = (
        df.groupby(["mechanism", "benchmark"], dropna=False)
        .agg(rows=("benchmark", "size"), runs=("run_id", "nunique"))
        .reset_index()
        .sort_values(["benchmark", "mechanism"])
    )
    for _, row in cov.iterrows():
        lines.append(
            f"- {row['benchmark']} | {row['mechanism']} -> rows={int(row['rows'])}, runs={int(row['runs'])}"
        )

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()

    input_path = Path(args.input).resolve()
    outdir = Path(args.outdir).resolve()
    figures_dir = outdir / "figures"
    tables_dir = outdir / "tables"

    plt.style.use("ggplot")

    df = load_data(input_path)

    write_tables(df, tables_dir)

    plot_pingpong(
        df,
        mechanisms=["mq_posix", "mq_sysv"],
        title_prefix="MQ ping-pong p99 latency: POSIX vs SysV",
        out_stem=figures_dir / "mq_pingpong_p99",
        fmt=args.format,
    )

    plot_pingpong(
        df,
        mechanisms=["shm_posix", "shm_sysv"],
        title_prefix="SHM ping-pong p99 latency: POSIX vs SysV",
        out_stem=figures_dir / "shm_pingpong_p99",
        fmt=args.format,
    )

    plot_throughput(df, figures_dir / "mq_throughput_mb_s", args.format)

    plot_scalability(
        df,
        benchmark_prefix="fanin_n",
        metric="throughput_msg_s",
        title="MQ fan-in scalability (64B if available)",
        out_stem=figures_dir / "mq_fanin_throughput",
        fmt=args.format,
    )

    plot_scalability(
        df,
        benchmark_prefix="pairs_n",
        metric="p99_ns",
        title="MQ parallel pairs scalability (64B if available)",
        out_stem=figures_dir / "mq_pairs_p99",
        fmt=args.format,
    )

    write_report(df, outdir / "analysis_report.md")

    print("Analysis complete")
    print(f"Input   : {input_path}")
    print(f"Tables  : {tables_dir}")
    print(f"Figures : {figures_dir}")


if __name__ == "__main__":
    main()
