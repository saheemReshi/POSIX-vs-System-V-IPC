"""
plot_comparison.py — POSIX vs System V Message Queue Benchmark Visualiser
=========================================================================
Reads the eight CSV files produced by make run_all from both suites and
writes one PNG per benchmark family into the directory given by --outdir
(default: plots/).

Usage
-----
    python3 plot_comparison.py                         # uses default paths
    python3 plot_comparison.py --posix path/to/posix/results \
                                --sysv  path/to/sysv/results  \
                                --outdir my_plots/

Dependencies
------------
    pip install pandas matplotlib
"""

import argparse
import os
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# ── CSV column names (same for both suites) ──────────────────────────────────
COLS = [
    "mechanism", "benchmark", "n_procs", "msg_size_bytes", "placement",
    "iterations", "avg_ns", "p50_ns", "p99_ns", "min_ns", "max_ns",
    "throughput_msg_s", "throughput_MB_s",
]

# ── Colour palette ────────────────────────────────────────────────────────────
C_POSIX = "#2196F3"   # blue
C_SYSV  = "#F44336"   # red

# ── Helpers ───────────────────────────────────────────────────────────────────

def load(path: str) -> pd.DataFrame:
    df = pd.read_csv(path, header=None, names=COLS)
    return df


def save(fig: plt.Figure, outdir: str, name: str) -> None:
    os.makedirs(outdir, exist_ok=True)
    p = os.path.join(outdir, name)
    fig.savefig(p, dpi=150, bbox_inches="tight")
    print(f"  saved → {p}")
    plt.close(fig)


def bar_group(ax, positions, vals_a, vals_b, label_a, label_b,
              color_a=C_POSIX, color_b=C_SYSV, width=0.35):
    """Draw two grouped bars side by side."""
    ax.bar(positions - width/2, vals_a, width, label=label_a,
           color=color_a, alpha=0.88, edgecolor="white", linewidth=0.6)
    ax.bar(positions + width/2, vals_b, width, label=label_b,
           color=color_b, alpha=0.88, edgecolor="white", linewidth=0.6)


def style(ax, title, xlabel, ylabel, xticks=None, xlabels=None, legend=True):
    ax.set_title(title, fontsize=13, fontweight="bold", pad=10)
    ax.set_xlabel(xlabel, fontsize=11)
    ax.set_ylabel(ylabel, fontsize=11)
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(
        lambda x, _: f"{x:,.0f}" if x >= 1000 else f"{x:.1f}"))
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    ax.spines[["top", "right"]].set_visible(False)
    if xticks is not None:
        ax.set_xticks(xticks)
    if xlabels is not None:
        ax.set_xticklabels(xlabels)
    if legend:
        ax.legend(framealpha=0.85, fontsize=10)


# ═════════════════════════════════════════════════════════════════════════════
# A — Ping-Pong Latency
# ═════════════════════════════════════════════════════════════════════════════

def plot_pingpong(posix_df: pd.DataFrame, sysv_df: pd.DataFrame,
                 outdir: str) -> None:
    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    fig.suptitle("Ping-Pong Latency  —  POSIX vs System V MQ",
                 fontsize=14, fontweight="bold", y=1.02)

    sizes   = posix_df["msg_size_bytes"].values
    pos     = np.arange(len(sizes))
    xlabels = [str(s) for s in sizes]

    metrics = [
        ("avg_ns",  "Average One-Way Latency",  "Latency (ns)"),
        ("p50_ns",  "Median (p50) Latency",      "Latency (ns)"),
        ("p99_ns",  "p99 Latency",               "Latency (ns)"),
    ]

    for ax, (col, title, ylabel) in zip(axes, metrics):
        bar_group(ax, pos,
                  posix_df[col].values, sysv_df[col].values,
                  "POSIX MQ", "System V MQ")
        style(ax, title, "Message Size (bytes)", ylabel,
              xticks=pos, xlabels=xlabels)

    fig.tight_layout()
    save(fig, outdir, "pingpong_latency.png")


# ═════════════════════════════════════════════════════════════════════════════
# B — Throughput
# ═════════════════════════════════════════════════════════════════════════════

def plot_throughput(posix_df: pd.DataFrame, sysv_df: pd.DataFrame,
                   outdir: str) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    fig.suptitle("Producer→Consumer Throughput  —  POSIX vs System V MQ",
                 fontsize=14, fontweight="bold", y=1.02)

    sizes   = posix_df["msg_size_bytes"].values
    pos     = np.arange(len(sizes))
    xlabels = [str(s) for s in sizes]

    # msgs/s
    ax = axes[0]
    bar_group(ax, pos,
              posix_df["throughput_msg_s"].values / 1e6,
              sysv_df["throughput_msg_s"].values  / 1e6,
              "POSIX MQ", "System V MQ")
    style(ax, "Message Rate", "Message Size (bytes)", "Million msgs/s",
          xticks=pos, xlabels=xlabels)

    # MB/s
    ax = axes[1]
    bar_group(ax, pos,
              posix_df["throughput_MB_s"].values,
              sysv_df["throughput_MB_s"].values,
              "POSIX MQ", "System V MQ")
    style(ax, "Bandwidth", "Message Size (bytes)", "MB/s",
          xticks=pos, xlabels=xlabels)

    fig.tight_layout()
    save(fig, outdir, "throughput.png")


# ═════════════════════════════════════════════════════════════════════════════
# C1 — Fan-in Scalability
# ═════════════════════════════════════════════════════════════════════════════

def plot_scalability_c1(posix_df: pd.DataFrame, sysv_df: pd.DataFrame,
                        outdir: str) -> None:
    # extract N from benchmark name "fanin_nN"
    posix_df = posix_df.copy()
    sysv_df  = sysv_df.copy()
    posix_df["n_prod"] = posix_df["benchmark"].str.extract(r"fanin_n(\d+)").astype(int)
    sysv_df["n_prod"]  = sysv_df["benchmark"].str.extract(r"fanin_n(\d+)").astype(int)
    posix_df = posix_df.sort_values("n_prod")
    sysv_df  = sysv_df.sort_values("n_prod")

    ns      = posix_df["n_prod"].values
    pos     = np.arange(len(ns))
    xlabels = [str(n) for n in ns]

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    fig.suptitle("Fan-in Scalability (C1)  —  POSIX vs System V MQ",
                 fontsize=14, fontweight="bold", y=1.02)

    ax = axes[0]
    bar_group(ax, pos,
              posix_df["throughput_msg_s"].values / 1e6,
              sysv_df["throughput_msg_s"].values  / 1e6,
              "POSIX MQ", "System V MQ")
    style(ax, "Throughput vs Producer Count",
          "Number of Producers", "Million msgs/s",
          xticks=pos, xlabels=xlabels)

    ax = axes[1]
    bar_group(ax, pos,
              posix_df["avg_ns"].values,
              sysv_df["avg_ns"].values,
              "POSIX MQ", "System V MQ")
    style(ax, "Avg Inter-Message Time vs Producer Count",
          "Number of Producers", "ns / message",
          xticks=pos, xlabels=xlabels)

    fig.tight_layout()
    save(fig, outdir, "scalability_c1_fanin.png")


# ═════════════════════════════════════════════════════════════════════════════
# C2 — Parallel Pairs Scalability
# ═════════════════════════════════════════════════════════════════════════════

def plot_scalability_c2(posix_df: pd.DataFrame, sysv_df: pd.DataFrame,
                        outdir: str) -> None:
    posix_df = posix_df.copy()
    sysv_df  = sysv_df.copy()
    posix_df["n_pairs"] = posix_df["benchmark"].str.extract(r"pairs_n(\d+)").astype(int)
    sysv_df["n_pairs"]  = sysv_df["benchmark"].str.extract(r"pairs_n(\d+)").astype(int)
    posix_df = posix_df.sort_values("n_pairs")
    sysv_df  = sysv_df.sort_values("n_pairs")

    ns      = posix_df["n_pairs"].values
    pos     = np.arange(len(ns))
    xlabels = [str(n) for n in ns]

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    fig.suptitle("Parallel Pairs Scalability (C2)  —  POSIX vs System V MQ",
                 fontsize=14, fontweight="bold", y=1.02)

    metrics = [
        ("avg_ns", "Avg One-Way Latency",    "Latency (ns)"),
        ("p50_ns", "Median (p50) Latency",   "Latency (ns)"),
        ("p99_ns", "p99 Latency",            "Latency (ns)"),
    ]

    for ax, (col, title, ylabel) in zip(axes, metrics):
        bar_group(ax, pos,
                  posix_df[col].values, sysv_df[col].values,
                  "POSIX MQ", "System V MQ")
        style(ax, title, "Number of Pairs", ylabel,
              xticks=pos, xlabels=xlabels)

    fig.tight_layout()
    save(fig, outdir, "scalability_c2_pairs.png")


# ═════════════════════════════════════════════════════════════════════════════
# Summary — one figure, all four benchmarks side by side
# ═════════════════════════════════════════════════════════════════════════════

def plot_summary(posix_ping, sysv_ping,
                 posix_tput, sysv_tput,
                 posix_c1,   sysv_c1,
                 posix_c2,   sysv_c2,
                 outdir: str) -> None:

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle("POSIX vs System V Message Queue  —  Summary",
                 fontsize=15, fontweight="bold", y=1.01)

    # ── A: ping-pong avg latency vs msg size ─────────────────────────────────
    ax = axes[0, 0]
    sizes = posix_ping["msg_size_bytes"].values
    ax.plot(sizes, posix_ping["avg_ns"].values, "o-",
            color=C_POSIX, label="POSIX MQ", linewidth=2, markersize=7)
    ax.plot(sizes, sysv_ping["avg_ns"].values,  "s-",
            color=C_SYSV,  label="System V MQ", linewidth=2, markersize=7)
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    style(ax, "Ping-Pong: Avg Latency vs Message Size",
          "Message Size (bytes)", "One-way Latency (ns)")

    # ── B: throughput bandwidth vs msg size ───────────────────────────────────
    ax = axes[0, 1]
    sizes = posix_tput["msg_size_bytes"].values
    ax.plot(sizes, posix_tput["throughput_MB_s"].values, "o-",
            color=C_POSIX, label="POSIX MQ", linewidth=2, markersize=7)
    ax.plot(sizes, sysv_tput["throughput_MB_s"].values,  "s-",
            color=C_SYSV,  label="System V MQ", linewidth=2, markersize=7)
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    style(ax, "Throughput: Bandwidth vs Message Size",
          "Message Size (bytes)", "Bandwidth (MB/s)")

    # ── C1: fan-in throughput vs N producers ─────────────────────────────────
    ax = axes[1, 0]
    pc1 = posix_c1.copy(); sc1 = sysv_c1.copy()
    pc1["n"] = pc1["benchmark"].str.extract(r"fanin_n(\d+)").astype(int)
    sc1["n"] = sc1["benchmark"].str.extract(r"fanin_n(\d+)").astype(int)
    pc1 = pc1.sort_values("n"); sc1 = sc1.sort_values("n")
    ax.plot(pc1["n"], pc1["throughput_msg_s"] / 1e6, "o-",
            color=C_POSIX, label="POSIX MQ", linewidth=2, markersize=7)
    ax.plot(sc1["n"], sc1["throughput_msg_s"] / 1e6, "s-",
            color=C_SYSV,  label="System V MQ", linewidth=2, markersize=7)
    style(ax, "Fan-in (C1): Throughput vs Producers",
          "Number of Producers", "Million msgs/s")
    ax.set_xticks(pc1["n"].values)

    # ── C2: parallel pairs avg latency vs N pairs ────────────────────────────
    ax = axes[1, 1]
    pc2 = posix_c2.copy(); sc2 = sysv_c2.copy()
    pc2["n"] = pc2["benchmark"].str.extract(r"pairs_n(\d+)").astype(int)
    sc2["n"] = sc2["benchmark"].str.extract(r"pairs_n(\d+)").astype(int)
    pc2 = pc2.sort_values("n"); sc2 = sc2.sort_values("n")
    ax.plot(pc2["n"], pc2["avg_ns"], "o-",
            color=C_POSIX, label="POSIX MQ", linewidth=2, markersize=7)
    ax.plot(sc2["n"], sc2["avg_ns"], "s-",
            color=C_SYSV,  label="System V MQ", linewidth=2, markersize=7)
    style(ax, "Parallel Pairs (C2): Avg Latency vs Pairs",
          "Number of Pairs", "One-way Latency (ns)")
    ax.set_xticks(pc2["n"].values)

    fig.tight_layout()
    save(fig, outdir, "summary.png")


# ═════════════════════════════════════════════════════════════════════════════
# main
# ═════════════════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="Plot POSIX vs System V MQ benchmark comparison")
    parser.add_argument("--posix",  default="posix_results",
                        help="Directory containing POSIX result CSVs")
    parser.add_argument("--sysv",   default="sysv_results",
                        help="Directory containing System V result CSVs")
    parser.add_argument("--outdir", default="plots",
                        help="Output directory for PNG files")
    args = parser.parse_args()

    def p(name): return os.path.join(args.posix, name)
    def s(name): return os.path.join(args.sysv,  name)

    print("Loading CSVs...")
    posix_ping = load(p("pingpong.csv"))
    sysv_ping  = load(s("pingpong.csv"))
    posix_tput = load(p("throughput.csv"))
    sysv_tput  = load(s("throughput.csv"))
    posix_c1   = load(p("scalability_c1.csv"))
    sysv_c1    = load(s("scalability_c1.csv"))
    posix_c2   = load(p("scalability_c2.csv"))
    sysv_c2    = load(s("scalability_c2.csv"))

    print("Generating plots...")
    plot_pingpong(posix_ping, sysv_ping, args.outdir)
    plot_throughput(posix_tput, sysv_tput, args.outdir)
    plot_scalability_c1(posix_c1, sysv_c1, args.outdir)
    plot_scalability_c2(posix_c2, sysv_c2, args.outdir)
    plot_summary(posix_ping, sysv_ping,
                 posix_tput, sysv_tput,
                 posix_c1,   sysv_c1,
                 posix_c2,   sysv_c2,
                 args.outdir)

    print(f"\nDone — {len(os.listdir(args.outdir))} plots in '{args.outdir}/'")


if __name__ == "__main__":
    main()