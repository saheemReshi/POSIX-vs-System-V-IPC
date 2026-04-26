#!/usr/bin/env python3

import argparse
from pathlib import Path
import re

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


# -----------------------------
# Config
# -----------------------------
COLORS = {
    "mq_posix": "tab:blue",
    "mq_sysv": "tab:red",
    "shm_posix": "tab:green",
    "shm_sysv": "tab:purple",
}

OUT_TABLES = "tables"
OUT_FIGS = "figures"


# -----------------------------
# Helpers
# -----------------------------
def placement_group(p):
    if str(p).startswith("same_core"):
        return "same_core"
    if str(p).startswith("cross_core"):
        return "cross_core"
    return p


def queue_depth(p):
    m = re.search(r"_q(\d+)", str(p))
    return int(m.group(1)) if m else None


# -----------------------------
# Load
# -----------------------------
def load_data(path):
    df = pd.read_csv(path)

    # numeric cleanup
    for col in df.columns:
        df[col] = pd.to_numeric(df[col], errors="ignore")

    df["placement_group"] = df["placement"].map(placement_group)
    df["queue_depth"] = df["placement"].map(queue_depth)

    return df


# -----------------------------
# Tables (minimal but useful)
# -----------------------------
def make_tables(df, outdir):
    out = outdir / OUT_TABLES
    out.mkdir(parents=True, exist_ok=True)

    # Core summary (THIS is what matters)
    summary = (
        df.groupby(
            ["mechanism", "benchmark", "placement_group", "msg_size_bytes"]
        )
        .agg(
            runs=("run_id", "nunique"),
            p50_mean=("p50_ns", "mean"),
            p50_std=("p50_ns", "std"),
            p99_mean=("p99_ns", "mean"),
            p99_std=("p99_ns", "std"),
            thr_mean=("throughput_MB_s", "mean"),
        )
        .reset_index()
    )

    summary["p50_cv"] = summary["p50_std"] / summary["p50_mean"]
    summary["p99_cv"] = summary["p99_std"] / summary["p99_mean"]

    summary.to_csv(out / "summary.csv", index=False)

    # Peak throughput
    tp = df[df["benchmark"] == "throughput"]
    if not tp.empty:
        peak = (
            tp.groupby(["mechanism", "placement_group", "queue_depth"])
            .agg(peak_MB_s=("throughput_MB_s", "max"))
            .reset_index()
        )
        peak.to_csv(out / "throughput_peak.csv", index=False)


# -----------------------------
# Plot: Pingpong (clean split)
# -----------------------------
def plot_pingpong(df, outdir):
    figs = outdir / OUT_FIGS
    figs.mkdir(parents=True, exist_ok=True)

    d = df[df["benchmark"] == "pingpong"]

    for placement in ["same_core", "cross_core"]:
        sub = d[d["placement_group"] == placement]
        if sub.empty:
            continue

        fig, ax = plt.subplots(figsize=(6, 4))

        for mech in ["mq_posix", "mq_sysv"]:
            m = sub[sub["mechanism"] == mech]
            if m.empty:
                continue

            g = (
                m.groupby("msg_size_bytes")
                .agg(p50=("p50_ns", "mean"), p99=("p99_ns", "mean"))
                .reset_index()
                .sort_values("msg_size_bytes")
            )

            ax.plot(g["msg_size_bytes"], g["p50"],
                    label=f"{mech} p50",
                    color=COLORS[mech],
                    linestyle="-")

            ax.plot(g["msg_size_bytes"], g["p99"],
                    label=f"{mech} p99",
                    color=COLORS[mech],
                    linestyle="--")

        ax.set_xscale("log")
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel("Latency (ns)")
        ax.set_title(f"MQ Pingpong ({placement})")
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8)

        plt.tight_layout()
        plt.savefig(figs / f"mq_pingpong_{placement}.png", dpi=180)
        plt.close()


# -----------------------------
# Plot: SHM vs MQ (64B only)
# -----------------------------
def plot_shm_vs_mq(df, outdir):
    figs = outdir / OUT_FIGS

    d = df[(df["benchmark"] == "pingpong") & (df["msg_size_bytes"] == 64)]

    if d.empty:
        return

    fig, ax = plt.subplots(figsize=(6, 4))

    for mech in ["mq_posix", "mq_sysv", "shm_posix", "shm_sysv"]:
        m = d[d["mechanism"] == mech]
        if m.empty:
            continue

        g = (
            m.groupby("placement_group")
            .agg(p50=("p50_ns", "mean"), p99=("p99_ns", "mean"))
            .reset_index()
        )

        ax.plot(g["placement_group"], g["p50"],
                label=f"{mech} p50",
                marker="o")

        ax.plot(g["placement_group"], g["p99"],
                label=f"{mech} p99",
                linestyle="--")

    ax.set_title("SHM vs MQ (64B)")
    ax.set_ylabel("Latency (ns)")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)

    plt.tight_layout()
    plt.savefig(figs / "shm_vs_mq_64B.png", dpi=180)
    plt.close()


# -----------------------------
# Plot: Throughput (split by depth)
# -----------------------------
def plot_throughput(df, outdir):
    figs = outdir / OUT_FIGS

    tp = df[df["benchmark"] == "throughput"]

    if tp.empty:
        return

    depths = sorted(tp["queue_depth"].dropna().unique())

    for depth in depths:
        fig, ax = plt.subplots(figsize=(6, 4))

        d = tp[tp["queue_depth"] == depth]

        for mech in ["mq_posix", "mq_sysv"]:
            m = d[d["mechanism"] == mech]
            if m.empty:
                continue

            g = (
                m.groupby("msg_size_bytes")
                .agg(thr=("throughput_MB_s", "mean"))
                .reset_index()
                .sort_values("msg_size_bytes")
            )

            ax.plot(g["msg_size_bytes"], g["thr"],
                    label=mech,
                    color=COLORS[mech])

        ax.set_xscale("log")
        ax.set_xlabel("Message size (bytes)")
        ax.set_ylabel("Throughput (MB/s)")
        ax.set_title(f"Throughput (depth={depth})")
        ax.grid(True, alpha=0.3)
        ax.legend()

        plt.tight_layout()
        plt.savefig(figs / f"throughput_depth_{depth}.png", dpi=180)
        plt.close()


# -----------------------------
# Plot: Scalability
# -----------------------------
def plot_scalability(df, outdir):
    figs = outdir / OUT_FIGS

    # Fan-in (throughput)
    fanin = df[df["benchmark"].str.startswith("fanin")]
    if not fanin.empty:
        fig, ax = plt.subplots(figsize=(6, 4))

        for mech in ["mq_posix", "mq_sysv"]:
            m = fanin[fanin["mechanism"] == mech]
            if m.empty:
                continue

            g = (
                m.groupby("n_procs")
                .agg(thr=("throughput_msg_s", "mean"))
                .reset_index()
            )

            ax.plot(g["n_procs"], g["thr"],
                    label=mech,
                    color=COLORS[mech],
                    marker="o")

        ax.set_title("Fan-in Scalability")
        ax.set_xlabel("Producers")
        ax.set_ylabel("Throughput (msg/s)")
        ax.grid(True, alpha=0.3)
        ax.legend()

        plt.tight_layout()
        plt.savefig(figs / "fanin_scalability.png", dpi=180)
        plt.close()

    # Parallel pairs (latency)
    pairs = df[df["benchmark"].str.startswith("pairs")]
    if not pairs.empty:
        fig, ax = plt.subplots(figsize=(6, 4))

        for mech in ["mq_posix", "mq_sysv"]:
            m = pairs[pairs["mechanism"] == mech]
            if m.empty:
                continue

            g = (
                m.groupby("n_procs")
                .agg(p99=("p99_ns", "mean"))
                .reset_index()
            )

            ax.plot(g["n_procs"], g["p99"],
                    label=mech,
                    color=COLORS[mech],
                    marker="o")

        ax.set_title("Parallel Pairs (p99 latency)")
        ax.set_xlabel("Pairs")
        ax.set_ylabel("p99 latency (ns)")
        ax.grid(True, alpha=0.3)
        ax.legend()

        plt.tight_layout()
        plt.savefig(figs / "pairs_latency.png", dpi=180)
        plt.close()


# -----------------------------
# Report (simple, useful)
# -----------------------------
def write_report(df, outdir):
    out = outdir / "analysis_report.md"

    lines = []
    lines.append("# IPC Benchmark Report\n")

    lines.append(f"Total rows: {len(df)}\n")

    for mech in df["mechanism"].unique():
        lines.append(f"- {mech}: {len(df[df['mechanism']==mech])} rows")

    out.write_text("\n".join(lines))


# -----------------------------
# Main
# -----------------------------
def main():
    # Resolve paths relative to the project root (parent of scripts/).
    root_dir = Path(__file__).resolve().parent.parent
    default_input = root_dir / "results" / "raw" / "combined" / "all_runs.csv"
    default_outdir = root_dir / "results" / "analysis_v3"

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        default=str(default_input),
    )
    parser.add_argument("--outdir", default=str(default_outdir))
    args = parser.parse_args()

    df = load_data(Path(args.input))
    outdir = Path(args.outdir)

    make_tables(df, outdir)
    plot_pingpong(df, outdir)
    plot_shm_vs_mq(df, outdir)
    plot_throughput(df, outdir)
    plot_scalability(df, outdir)
    write_report(df, outdir)

    print("Done →", outdir)


if __name__ == "__main__":
    main()