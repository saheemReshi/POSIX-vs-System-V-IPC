#!/usr/bin/env python3
"""
Publication-oriented analysis for IPC benchmark results.

Input CSV (default):
  results/raw/combined/all_runs.csv

Outputs (default under results/analysis_v2):
  - tables/summary_by_config.csv
  - tables/latency_percentiles.csv
  - tables/throughput_peak.csv
  - tables/scalability_summary.csv
  - figures/*.png (or pdf)
  - analysis_report.md
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Dict

import numpy as np
import pandas as pd

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


REQUIRED_COLUMNS = [
    "mechanism",
    "benchmark",
    "placement",
    "msg_size_bytes",
    "n_procs",
    "run_id",
    "avg_ns",
    "p50_ns",
    "p95_ns",
    "p99_ns",
    "p999_ns",
    "throughput_MB_s",
    "throughput_msg_s",
]

NUMERIC_COLUMNS = [
    "msg_size_bytes",
    "n_procs",
    "run_id",
    "avg_ns",
    "p50_ns",
    "p95_ns",
    "p99_ns",
    "p999_ns",
    "throughput_MB_s",
    "throughput_msg_s",
    "iterations",
    "min_ns",
    "max_ns",
    "mem_delta_kb",
]

MECH_COLORS = {
    "mq_posix": "#1f77b4",  # blue
    "mq_sysv": "#d62728",  # red
    "shm_posix": "#2ca02c",  # green
    "shm_sysv": "#9467bd",  # purple
}

PLACEMENT_ORDER = ["same_core", "cross_core"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Analyze IPC benchmark CSV (v2)")
    parser.add_argument(
        "--input",
        default="results/raw/combined/all_runs.csv",
        help="Input CSV path",
    )
    parser.add_argument(
        "--outdir",
        default="results/analysis_v2",
        help="Output directory",
    )
    parser.add_argument(
        "--format",
        choices=["png", "pdf", "both"],
        default="png",
        help="Figure format",
    )
    return parser.parse_args()


def _configure_plot_style() -> None:
    plt.style.use("seaborn-v0_8-whitegrid")
    plt.rcParams.update(
        {
            "font.size": 11,
            "axes.titlesize": 12,
            "axes.labelsize": 11,
            "legend.fontsize": 9,
            "xtick.labelsize": 10,
            "ytick.labelsize": 10,
            "figure.dpi": 140,
        }
    )


def _extract_placement_group(value: str) -> str:
    s = str(value)
    if "same_core" in s:
        return "same_core"
    if "cross_core" in s:
        return "cross_core"
    return "other"


def _extract_queue_depth(value: str) -> float:
    m = re.search(r"_q(\d+)", str(value))
    if not m:
        return np.nan
    return float(m.group(1))


def _ensure_numeric(df: pd.DataFrame, cols: list[str]) -> pd.DataFrame:
    for col in cols:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    return df


def _safe_cv(mean_series: pd.Series, std_series: pd.Series) -> pd.Series:
    mean_safe = mean_series.replace(0, np.nan)
    return std_series / mean_safe


def _save_figure(fig: plt.Figure, path_no_suffix: Path, out_format: str) -> None:
    path_no_suffix.parent.mkdir(parents=True, exist_ok=True)
    if out_format in ("png", "both"):
        fig.savefig(path_no_suffix.with_suffix(".png"), bbox_inches="tight", dpi=180)
    if out_format in ("pdf", "both"):
        fig.savefig(path_no_suffix.with_suffix(".pdf"), bbox_inches="tight")
    plt.close(fig)


def _preferred_msg_size(df: pd.DataFrame, default_size: int = 64) -> float:
    sizes = sorted(df["msg_size_bytes"].dropna().unique())
    if not sizes:
        return float(default_size)
    if default_size in sizes:
        return float(default_size)
    return float(min(sizes, key=lambda x: abs(x - default_size)))


def _coverage_table(df: pd.DataFrame) -> pd.DataFrame:
    return (
        df.groupby(["mechanism", "benchmark"], dropna=False, observed=False)
        .agg(
            rows=("benchmark", "size"),
            unique_runs=("run_id", "nunique"),
            placements=("placement_group", "nunique"),
            msg_sizes=("msg_size_bytes", "nunique"),
        )
        .reset_index()
        .sort_values(["benchmark", "mechanism"])
    )


def load_data(input_path: str | Path) -> pd.DataFrame:
    path = Path(input_path)
    if not path.exists():
        raise SystemExit(f"Input CSV not found: {path}")

    df = pd.read_csv(path, comment="#")

    missing = [c for c in REQUIRED_COLUMNS if c not in df.columns]
    if missing:
        raise SystemExit(f"Missing required columns: {missing}")

    df = _ensure_numeric(df, NUMERIC_COLUMNS)

    df["placement_group"] = df["placement"].map(_extract_placement_group)
    df["queue_depth"] = df["placement"].map(_extract_queue_depth)

    # Keep mechanism ordering stable for tables and plots.
    mech_order = ["mq_posix", "mq_sysv", "shm_posix", "shm_sysv"]
    df["mechanism"] = pd.Categorical(df["mechanism"], categories=mech_order, ordered=True)

    return df


def compute_tables(df: pd.DataFrame, outdir: str | Path) -> Dict[str, pd.DataFrame]:
    outdir = Path(outdir)
    tables_dir = outdir / "tables"
    tables_dir.mkdir(parents=True, exist_ok=True)

    # 1) summary_by_config.csv
    summary_group = [
        "mechanism",
        "benchmark",
        "placement_group",
        "msg_size_bytes",
        "n_procs",
        "queue_depth",
    ]

    summary = (
        df.groupby(summary_group, dropna=False, observed=False)
        .agg(
            runs=("run_id", "nunique"),
            avg_ns_mean=("avg_ns", "mean"),
            avg_ns_std=("avg_ns", "std"),
            p50_ns_mean=("p50_ns", "mean"),
            p50_ns_std=("p50_ns", "std"),
            p99_ns_mean=("p99_ns", "mean"),
            p99_ns_std=("p99_ns", "std"),
            throughput_MB_s_mean=("throughput_MB_s", "mean"),
            throughput_MB_s_std=("throughput_MB_s", "std"),
        )
        .reset_index()
    )

    summary["avg_ns_cv"] = _safe_cv(summary["avg_ns_mean"], summary["avg_ns_std"])
    summary["p50_ns_cv"] = _safe_cv(summary["p50_ns_mean"], summary["p50_ns_std"])
    summary["p99_ns_cv"] = _safe_cv(summary["p99_ns_mean"], summary["p99_ns_std"])
    summary["throughput_MB_s_cv"] = _safe_cv(
        summary["throughput_MB_s_mean"], summary["throughput_MB_s_std"]
    )

    summary = summary.sort_values(summary_group)
    summary.to_csv(tables_dir / "summary_by_config.csv", index=False)

    # 2) latency_percentiles.csv (focused on pingpong latency signals)
    latency_src = df[df["benchmark"] == "pingpong"].copy()
    latency_percentiles = (
        latency_src.groupby(["mechanism", "placement_group", "msg_size_bytes"], dropna=False, observed=False)
        .agg(
            p50_mean=("p50_ns", "mean"),
            p95_mean=("p95_ns", "mean"),
            p99_mean=("p99_ns", "mean"),
            p999_mean=("p999_ns", "mean"),
        )
        .reset_index()
        .sort_values(["placement_group", "msg_size_bytes", "mechanism"])
    )
    latency_percentiles.to_csv(tables_dir / "latency_percentiles.csv", index=False)

    # 3) throughput_peak.csv (throughput benchmark only)
    tp = df[df["benchmark"] == "throughput"].copy()
    if tp.empty:
        throughput_peak = pd.DataFrame(
            columns=[
                "mechanism",
                "placement_group",
                "queue_depth",
                "peak_throughput_MB_s",
                "peak_msg_size_bytes",
            ]
        )
    else:
        idx = (
            tp.groupby(["mechanism", "placement_group", "queue_depth"], dropna=False, observed=False)[
                "throughput_MB_s"
            ]
            .idxmax()
            .dropna()
        )
        throughput_peak = tp.loc[idx, ["mechanism", "placement_group", "queue_depth", "throughput_MB_s", "msg_size_bytes"]]
        throughput_peak = throughput_peak.rename(
            columns={
                "throughput_MB_s": "peak_throughput_MB_s",
                "msg_size_bytes": "peak_msg_size_bytes",
            }
        ).sort_values(["mechanism", "placement_group", "queue_depth"])

    throughput_peak.to_csv(tables_dir / "throughput_peak.csv", index=False)

    # 4) scalability_summary.csv
    fanin = df[df["benchmark"].astype(str).str.startswith("fanin_n")].copy()
    fanin_summary = (
        fanin.groupby(["mechanism", "n_procs", "msg_size_bytes"], dropna=False, observed=False)
        .agg(mean=("throughput_msg_s", "mean"), std=("throughput_msg_s", "std"))
        .reset_index()
    )
    fanin_summary["metric"] = "throughput_msg_s"
    fanin_summary["benchmark_family"] = "fanin"
    fanin_summary["cv"] = _safe_cv(fanin_summary["mean"], fanin_summary["std"])

    pairs = df[df["benchmark"].astype(str).str.startswith("pairs_n")].copy()
    pairs_summary = (
        pairs.groupby(["mechanism", "n_procs", "msg_size_bytes", "placement_group"], dropna=False, observed=False)
        .agg(mean=("p99_ns", "mean"), std=("p99_ns", "std"))
        .reset_index()
    )
    pairs_summary["metric"] = "p99_ns"
    pairs_summary["benchmark_family"] = "pairs"
    pairs_summary["cv"] = _safe_cv(pairs_summary["mean"], pairs_summary["std"])

    if "placement_group" not in fanin_summary.columns:
        fanin_summary["placement_group"] = "other"

    scalability_summary = pd.concat([fanin_summary, pairs_summary], ignore_index=True, sort=False)
    keep_cols = [
        "mechanism",
        "benchmark_family",
        "metric",
        "placement_group",
        "n_procs",
        "msg_size_bytes",
        "mean",
        "std",
        "cv",
    ]
    scalability_summary = scalability_summary[keep_cols].sort_values(
        ["benchmark_family", "metric", "placement_group", "n_procs", "msg_size_bytes", "mechanism"]
    )
    scalability_summary.to_csv(tables_dir / "scalability_summary.csv", index=False)

    return {
        "summary_by_config": summary,
        "latency_percentiles": latency_percentiles,
        "throughput_peak": throughput_peak,
        "scalability_summary": scalability_summary,
        "coverage": _coverage_table(df),
    }


def plot_pingpong(df: pd.DataFrame, outdir: str | Path, fig_format: str) -> None:
    outdir = Path(outdir)
    figdir = outdir / "figures"
    figdir.mkdir(parents=True, exist_ok=True)

    # A) MQ pingpong split by placement, p50 and p99 only.
    mq = df[(df["benchmark"] == "pingpong") & (df["mechanism"].isin(["mq_posix", "mq_sysv"]))]

    for placement in PLACEMENT_ORDER:
        d = mq[mq["placement_group"] == placement]
        fig, ax = plt.subplots(figsize=(7.2, 4.8))

        if d.empty:
            ax.text(0.5, 0.5, f"No data for {placement}", ha="center", va="center", transform=ax.transAxes)
        else:
            for mech in ["mq_posix", "mq_sysv"]:
                m = d[d["mechanism"] == mech]
                if m.empty:
                    continue
                g = (
                    m.groupby("msg_size_bytes", dropna=False, observed=False)
                    .agg(
                        p50_mean=("p50_ns", "mean"),
                        p99_mean=("p99_ns", "mean"),
                    )
                    .reset_index()
                    .sort_values("msg_size_bytes")
                )
                c = MECH_COLORS[mech]
                ax.plot(g["msg_size_bytes"], g["p50_mean"], marker="o", linestyle="-", linewidth=1.9, color=c, label=f"{mech} p50")
                ax.plot(g["msg_size_bytes"], g["p99_mean"], marker="o", linestyle="--", linewidth=1.9, color=c, label=f"{mech} p99")

        ax.set_xscale("log", base=2)
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel("Latency (ns)")
        ax.set_title(f"MQ pingpong latency - {placement}")
        ax.grid(True, linestyle="--", alpha=0.35)
        ax.legend(frameon=True, loc="best")
        fig.tight_layout()

        _save_figure(fig, figdir / f"mq_pingpong_{placement}", fig_format)

    # B) SHM vs MQ comparison at fixed size (prefer 64B)
    pp_all = df[df["benchmark"] == "pingpong"].copy()
    if not pp_all.empty:
        fixed_size = _preferred_msg_size(pp_all, default_size=64)
        comp = pp_all[(pp_all["msg_size_bytes"] == fixed_size) & (pp_all["placement_group"].isin(PLACEMENT_ORDER))]

        fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.5), sharex=True)
        metrics = [("p50_ns", "p50 latency (ns)"), ("p99_ns", "p99 latency (ns)")]

        for ax, (metric, ylabel) in zip(axes, metrics):
            if comp.empty:
                ax.text(0.5, 0.5, "No data", ha="center", va="center", transform=ax.transAxes)
                ax.set_ylabel(ylabel)
                ax.set_xticks([0, 1], PLACEMENT_ORDER)
                continue

            g = (
                comp.groupby(["mechanism", "placement_group"], dropna=False, observed=False)
                .agg(val=(metric, "mean"))
                .reset_index()
            )

            for mech in ["mq_posix", "mq_sysv", "shm_posix", "shm_sysv"]:
                gm = g[g["mechanism"] == mech]
                if gm.empty:
                    continue
                x = []
                y = []
                for i, p in enumerate(PLACEMENT_ORDER):
                    row = gm[gm["placement_group"] == p]
                    if row.empty:
                        continue
                    x.append(i)
                    y.append(float(row["val"].iloc[0]))
                if not x:
                    continue
                ax.plot(x, y, marker="o", linewidth=1.8, color=MECH_COLORS[mech], label=mech)

            ax.set_xticks(range(len(PLACEMENT_ORDER)), PLACEMENT_ORDER)
            ax.set_ylabel(ylabel)
            ax.grid(True, linestyle="--", alpha=0.35)

        axes[0].set_title(f"SHM vs MQ p50 at {int(fixed_size)}B")
        axes[1].set_title(f"SHM vs MQ p99 at {int(fixed_size)}B")

        handles, labels = axes[0].get_legend_handles_labels()
        if handles:
            fig.legend(handles, labels, loc="upper center", ncol=4, bbox_to_anchor=(0.5, 1.04), frameon=True)
        fig.tight_layout(rect=[0, 0, 1, 0.95])

        _save_figure(fig, figdir / "shm_vs_mq_64B", fig_format)


def plot_throughput(df: pd.DataFrame, outdir: str | Path, fig_format: str) -> None:
    outdir = Path(outdir)
    figdir = outdir / "figures"
    figdir.mkdir(parents=True, exist_ok=True)

    tp = df[(df["benchmark"] == "throughput") & (df["mechanism"].isin(["mq_posix", "mq_sysv"]))]
    depths = sorted(tp["queue_depth"].dropna().unique())

    for depth in depths:
        d_depth = tp[tp["queue_depth"] == depth]
        if d_depth.empty:
            continue

        placements = [p for p in PLACEMENT_ORDER if p in set(d_depth["placement_group"].unique())]
        if not placements:
            placements = ["other"]

        fig, axes = plt.subplots(1, len(placements), figsize=(6.2 * len(placements), 4.6), sharey=True)
        if len(placements) == 1:
            axes = [axes]

        for ax, placement in zip(axes, placements):
            dp = d_depth[d_depth["placement_group"] == placement]
            if dp.empty:
                ax.text(0.5, 0.5, f"No data for {placement}", ha="center", va="center", transform=ax.transAxes)
                ax.set_title(placement)
                continue

            for mech in ["mq_posix", "mq_sysv"]:
                m = dp[dp["mechanism"] == mech]
                if m.empty:
                    continue
                g = (
                    m.groupby("msg_size_bytes", dropna=False, observed=False)
                    .agg(mean=("throughput_MB_s", "mean"), std=("throughput_MB_s", "std"), runs=("run_id", "nunique"))
                    .reset_index()
                    .sort_values("msg_size_bytes")
                )

                # Error bars only when enough repeated runs and not cluttered.
                use_err = (g["runs"].max() > 1) and (len(g) <= 8)
                if use_err:
                    ax.errorbar(
                        g["msg_size_bytes"],
                        g["mean"],
                        yerr=g["std"].fillna(0.0),
                        marker="o",
                        linewidth=1.8,
                        capsize=3,
                        color=MECH_COLORS[mech],
                        label=mech,
                    )
                else:
                    ax.plot(
                        g["msg_size_bytes"],
                        g["mean"],
                        marker="o",
                        linewidth=1.8,
                        color=MECH_COLORS[mech],
                        label=mech,
                    )

            ax.set_xscale("log", base=2)
            ax.set_title(placement)
            ax.set_xlabel("Message size (bytes)")
            ax.grid(True, linestyle="--", alpha=0.35)
            ax.legend(loc="best", frameon=True)

        axes[0].set_ylabel("Throughput (MB/s)")
        fig.suptitle(f"MQ throughput at queue depth {int(depth)}")
        fig.tight_layout(rect=[0, 0, 1, 0.95])
        _save_figure(fig, figdir / f"throughput_depth_{int(depth)}", fig_format)


def plot_scalability(df: pd.DataFrame, outdir: str | Path, fig_format: str) -> None:
    outdir = Path(outdir)
    figdir = outdir / "figures"
    figdir.mkdir(parents=True, exist_ok=True)

    # C1 fan-in: make one combined plot at preferred message size (readable).
    fanin = df[df["benchmark"].astype(str).str.startswith("fanin_n") & df["mechanism"].isin(["mq_posix", "mq_sysv"])]
    if not fanin.empty:
        target_size = _preferred_msg_size(fanin, default_size=64)
        fanin_s = fanin[fanin["msg_size_bytes"] == target_size]

        fig, ax = plt.subplots(figsize=(7.0, 4.8))
        for mech in ["mq_posix", "mq_sysv"]:
            m = fanin_s[fanin_s["mechanism"] == mech]
            if m.empty:
                continue
            g = (
                m.groupby("n_procs", dropna=False, observed=False)
                .agg(mean=("throughput_msg_s", "mean"), std=("throughput_msg_s", "std"), runs=("run_id", "nunique"))
                .reset_index()
                .sort_values("n_procs")
            )
            use_err = (g["runs"].max() > 1) and (len(g) <= 10)
            if use_err:
                ax.errorbar(
                    g["n_procs"],
                    g["mean"],
                    yerr=g["std"].fillna(0.0),
                    marker="o",
                    linewidth=1.9,
                    capsize=3,
                    color=MECH_COLORS[mech],
                    label=mech,
                )
            else:
                ax.plot(
                    g["n_procs"],
                    g["mean"],
                    marker="o",
                    linewidth=1.9,
                    color=MECH_COLORS[mech],
                    label=mech,
                )

        ax.set_xlabel("n_procs")
        ax.set_ylabel("Throughput (msg/s)")
        ax.set_title(f"Fan-in scalability at {int(target_size)}B")
        ax.grid(True, linestyle="--", alpha=0.35)
        ax.legend(loc="best", frameon=True)
        fig.tight_layout()
        _save_figure(fig, figdir / "scalability_fanin_throughput", fig_format)

    # C2 pairs: separate by same_core/cross_core if such labels exist.
    pairs = df[df["benchmark"].astype(str).str.startswith("pairs_n") & df["mechanism"].isin(["mq_posix", "mq_sysv"])]
    if not pairs.empty:
        target_size = _preferred_msg_size(pairs, default_size=64)
        pairs_s = pairs[pairs["msg_size_bytes"] == target_size]

        pair_place_groups = [p for p in PLACEMENT_ORDER if p in set(pairs_s["placement_group"].unique())]

        if pair_place_groups:
            for placement in pair_place_groups:
                dp = pairs_s[pairs_s["placement_group"] == placement]
                if dp.empty:
                    continue

                fig, ax = plt.subplots(figsize=(7.0, 4.8))
                for mech in ["mq_posix", "mq_sysv"]:
                    m = dp[dp["mechanism"] == mech]
                    if m.empty:
                        continue
                    g = (
                        m.groupby("n_procs", dropna=False, observed=False)
                        .agg(mean=("p99_ns", "mean"), std=("p99_ns", "std"), runs=("run_id", "nunique"))
                        .reset_index()
                        .sort_values("n_procs")
                    )
                    use_err = (g["runs"].max() > 1) and (len(g) <= 10)
                    if use_err:
                        ax.errorbar(
                            g["n_procs"],
                            g["mean"],
                            yerr=g["std"].fillna(0.0),
                            marker="o",
                            linewidth=1.9,
                            capsize=3,
                            color=MECH_COLORS[mech],
                            label=mech,
                        )
                    else:
                        ax.plot(
                            g["n_procs"],
                            g["mean"],
                            marker="o",
                            linewidth=1.9,
                            color=MECH_COLORS[mech],
                            label=mech,
                        )

                ax.set_xlabel("n_procs")
                ax.set_ylabel("p99 latency (ns)")
                ax.set_title(f"Parallel pairs p99 at {int(target_size)}B - {placement}")
                ax.grid(True, linestyle="--", alpha=0.35)
                ax.legend(loc="best", frameon=True)
                fig.tight_layout()
                _save_figure(fig, figdir / f"scalability_pairs_p99_{placement}", fig_format)
        else:
            fig, ax = plt.subplots(figsize=(7.0, 4.8))
            for mech in ["mq_posix", "mq_sysv"]:
                m = pairs_s[pairs_s["mechanism"] == mech]
                if m.empty:
                    continue
                g = (
                    m.groupby("n_procs", dropna=False, observed=False)
                    .agg(mean=("p99_ns", "mean"), std=("p99_ns", "std"), runs=("run_id", "nunique"))
                    .reset_index()
                    .sort_values("n_procs")
                )
                use_err = (g["runs"].max() > 1) and (len(g) <= 10)
                if use_err:
                    ax.errorbar(
                        g["n_procs"],
                        g["mean"],
                        yerr=g["std"].fillna(0.0),
                        marker="o",
                        linewidth=1.9,
                        capsize=3,
                        color=MECH_COLORS[mech],
                        label=mech,
                    )
                else:
                    ax.plot(
                        g["n_procs"],
                        g["mean"],
                        marker="o",
                        linewidth=1.9,
                        color=MECH_COLORS[mech],
                        label=mech,
                    )

            ax.set_xlabel("n_procs")
            ax.set_ylabel("p99 latency (ns)")
            ax.set_title(f"Parallel pairs p99 at {int(target_size)}B")
            ax.grid(True, linestyle="--", alpha=0.35)
            ax.legend(loc="best", frameon=True)
            fig.tight_layout()
            _save_figure(fig, figdir / "scalability_pairs_p99", fig_format)


def _insight_latency_posix_vs_sysv(df: pd.DataFrame) -> str:
    sub = df[
        (df["benchmark"] == "pingpong")
        & (df["mechanism"].isin(["mq_posix", "mq_sysv"]))
        & (df["placement_group"] == "same_core")
        & (df["msg_size_bytes"] == 64)
    ]
    if sub.empty:
        return "Latency insight unavailable (missing 64B same_core pingpong rows)."

    agg = sub.groupby("mechanism", dropna=False, observed=False)["p99_ns"].mean()
    if "mq_posix" not in agg.index or "mq_sysv" not in agg.index:
        return "Latency insight unavailable (requires both mq_posix and mq_sysv)."

    posix = float(agg.loc["mq_posix"])
    sysv = float(agg.loc["mq_sysv"])
    if sysv <= 0:
        return "Latency insight unavailable (invalid SYSV p99 baseline)."

    delta_pct = (sysv - posix) / sysv * 100.0
    if posix < sysv:
        return (
            f"At 64B same_core pingpong, POSIX MQ p99 is {delta_pct:.1f}% lower than "
            f"SYSV MQ ({posix:.0f} ns vs {sysv:.0f} ns)."
        )
    return (
        f"At 64B same_core pingpong, POSIX MQ p99 is {-delta_pct:.1f}% higher than "
        f"SYSV MQ ({posix:.0f} ns vs {sysv:.0f} ns)."
    )


def _insight_throughput(df: pd.DataFrame) -> str:
    tp = df[
        (df["benchmark"] == "throughput")
        & (df["mechanism"].isin(["mq_posix", "mq_sysv"]))
        & (df["placement_group"] == "same_core")
    ]
    if tp.empty:
        return "Throughput insight unavailable (missing same_core throughput rows)."

    peak = tp.groupby("mechanism", dropna=False, observed=False)["throughput_MB_s"].max()
    if "mq_posix" not in peak.index or "mq_sysv" not in peak.index:
        return "Throughput insight unavailable (requires both mq_posix and mq_sysv)."

    posix = float(peak.loc["mq_posix"])
    sysv = float(peak.loc["mq_sysv"])
    if posix <= 0 or sysv <= 0:
        return "Throughput insight unavailable (invalid throughput values)."

    if posix > sysv:
        pct = (posix - sysv) / sysv * 100.0
        return f"Peak same_core throughput favors POSIX MQ by {pct:.1f}% ({posix:.1f} MB/s vs {sysv:.1f} MB/s)."

    pct = (sysv - posix) / posix * 100.0
    return f"Peak same_core throughput favors SYSV MQ by {pct:.1f}% ({sysv:.1f} MB/s vs {posix:.1f} MB/s)."


def _insight_scalability(df: pd.DataFrame) -> str:
    fanin = df[df["benchmark"].astype(str).str.startswith("fanin_n") & df["mechanism"].isin(["mq_posix", "mq_sysv"])]
    pairs = df[df["benchmark"].astype(str).str.startswith("pairs_n") & df["mechanism"].isin(["mq_posix", "mq_sysv"])]

    insights = []

    if not fanin.empty:
        size = _preferred_msg_size(fanin, 64)
        f = fanin[fanin["msg_size_bytes"] == size]
        if not f.empty:
            nmax = f["n_procs"].max()
            fn = f[f["n_procs"] == nmax].groupby("mechanism", dropna=False, observed=False)["throughput_msg_s"].mean()
            if "mq_posix" in fn.index and "mq_sysv" in fn.index and float(fn.loc["mq_sysv"]) > 0:
                posix = float(fn.loc["mq_posix"])
                sysv = float(fn.loc["mq_sysv"])
                pct = (posix - sysv) / sysv * 100.0
                direction = "higher" if pct >= 0 else "lower"
                insights.append(
                    f"At high fan-in load (n_procs={int(nmax)}, {int(size)}B), POSIX throughput is {abs(pct):.1f}% {direction} than SYSV."
                )

    if not pairs.empty:
        size = _preferred_msg_size(pairs, 64)
        p = pairs[pairs["msg_size_bytes"] == size]
        if not p.empty:
            nmax = p["n_procs"].max()
            pn = p[p["n_procs"] == nmax].groupby("mechanism", dropna=False, observed=False)["p99_ns"].mean()
            if "mq_posix" in pn.index and "mq_sysv" in pn.index and float(pn.loc["mq_sysv"]) > 0:
                posix = float(pn.loc["mq_posix"])
                sysv = float(pn.loc["mq_sysv"])
                pct = (sysv - posix) / sysv * 100.0
                if pct >= 0:
                    insights.append(
                        f"At high parallel-pairs load (n_procs={int(nmax)}, {int(size)}B), POSIX p99 latency is {pct:.1f}% lower than SYSV."
                    )
                else:
                    insights.append(
                        f"At high parallel-pairs load (n_procs={int(nmax)}, {int(size)}B), POSIX p99 latency is {-pct:.1f}% higher than SYSV."
                    )

    if not insights:
        return "Scalability insight unavailable (insufficient fanin/pairs data for both mechanisms)."
    return " ".join(insights)


def write_report(df: pd.DataFrame, table_map: Dict[str, pd.DataFrame], outdir: str | Path) -> None:
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    report_path = outdir / "analysis_report.md"

    total_rows = len(df)
    total_runs = (
        df[["mechanism", "benchmark", "run_id"]]
        .dropna()
        .drop_duplicates()
        .shape[0]
    )

    coverage = table_map["coverage"]

    lines = []
    lines.append("# IPC Benchmark Analysis v2")
    lines.append("")
    lines.append("## Dataset Summary")
    lines.append(f"- Total rows: {total_rows}")
    lines.append(f"- Total unique (mechanism, benchmark, run_id): {total_runs}")
    lines.append("")

    lines.append("## Coverage Summary")
    if coverage.empty:
        lines.append("No coverage rows available.")
    else:
        lines.append("| mechanism | benchmark | rows | unique_runs | placements | msg_sizes |")
        lines.append("|---|---:|---:|---:|---:|---:|")
        for _, r in coverage.iterrows():
            lines.append(
                f"| {r['mechanism']} | {r['benchmark']} | {int(r['rows'])} | {int(r['unique_runs'])} | {int(r['placements'])} | {int(r['msg_sizes'])} |"
            )
    lines.append("")

    lines.append("## Key Observations")
    lines.append(f"- { _insight_latency_posix_vs_sysv(df) }")
    lines.append(f"- { _insight_throughput(df) }")
    lines.append(f"- { _insight_scalability(df) }")
    lines.append("")

    lines.append("## Notes")
    lines.append("- Plots are split to avoid crowding and keep comparisons readable.")
    lines.append("- Message-size axes use log scale where size sweeps are shown.")
    lines.append("- Error bars are included only when repeated-run variance can be shown without clutter.")
    lines.append("")

    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    args = parse_args()
    _configure_plot_style()

    df = load_data(args.input)
    outdir = Path(args.outdir)

    table_map = compute_tables(df, outdir)
    plot_pingpong(df, outdir, args.format)
    plot_throughput(df, outdir, args.format)
    plot_scalability(df, outdir, args.format)
    write_report(df, table_map, outdir)

    print(f"Analysis complete. Output written to: {outdir}")


if __name__ == "__main__":
    main()
