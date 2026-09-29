#!/usr/bin/env python3
"""
plot.py — Read memlab CSVs and generate matplotlib charts.

Usage:
    python scripts/plot.py --bench prefault
    python scripts/plot.py --bench thread_scaling
    python scripts/plot.py --bench false_sharing
    python scripts/plot.py --bench ctx_switch
    python scripts/plot.py --bench sched_sweep
    python scripts/plot.py --all

Output PNGs are written to results/.
Requires: matplotlib, pandas  (pip install matplotlib pandas)
"""

import argparse
import os
import sys
import pandas as pd
import matplotlib
matplotlib.use("Agg")   # headless — no display needed
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

RESULTS = os.path.join(os.path.dirname(__file__), "..", "results")

# ── Helpers ───────────────────────────────────────────────────────────────────

def load_csv(filename: str) -> pd.DataFrame:
    path = os.path.join(RESULTS, filename)
    if not os.path.exists(path):
        print(f"[skip] {path} not found — run the benchmark first.")
        return pd.DataFrame()
    # Skip comment lines (start with #)
    return pd.read_csv(path, comment="#")

def save(fig, name: str):
    path = os.path.join(RESULTS, name)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    print(f"[saved] {path}")
    plt.close(fig)

STYLE = {
    "figure.facecolor": "#0d1117",
    "axes.facecolor":   "#161b22",
    "axes.edgecolor":   "#30363d",
    "axes.labelcolor":  "#e6edf3",
    "xtick.color":      "#8b949e",
    "ytick.color":      "#8b949e",
    "text.color":       "#e6edf3",
    "grid.color":       "#21262d",
    "grid.linestyle":   "--",
    "grid.alpha":       0.5,
    "legend.facecolor": "#161b22",
    "legend.edgecolor": "#30363d",
}
plt.rcParams.update(STYLE)

PALETTE = ["#58a6ff", "#3fb950", "#f78166", "#d2a8ff", "#ffa657", "#79c0ff"]

# ── Prefault benchmark ────────────────────────────────────────────────────────

def plot_prefault():
    df = load_csv("prefault_bench.csv")
    if df.empty: return

    # Median across runs per policy
    grp = df.groupby("policy")[["p50_ns","p99_ns","p999_ns","startup_ms"]].median().reset_index()

    # Chart 1: latency percentiles grouped bar
    fig, ax = plt.subplots(figsize=(10, 5))
    x     = np.arange(len(grp))
    width = 0.25
    colors = PALETTE[:3]

    ax.bar(x - width, grp["p50_ns"],  width, label="p50",   color=colors[0])
    ax.bar(x,         grp["p99_ns"],  width, label="p99",   color=colors[1])
    ax.bar(x + width, grp["p999_ns"], width, label="p99.9", color=colors[2])

    ax.set_xticks(x)
    ax.set_xticklabels(grp["policy"])
    ax.set_ylabel("First-touch latency (ns)")
    ax.set_title("Prefault Policy — Page First-Touch Latency")
    ax.legend()
    ax.grid(axis="y")
    ax.set_yscale("log")
    save(fig, "prefault_latency.png")

    # Chart 2: startup time
    fig, ax = plt.subplots(figsize=(8, 4))
    ax.bar(grp["policy"], grp["startup_ms"], color=PALETTE[0])
    ax.set_ylabel("Startup time (ms)")
    ax.set_title("Prefault Policy — Region Construction Time")
    ax.grid(axis="y")
    save(fig, "prefault_startup.png")

    # Chart 3: fault counts
    fault_cols = [c for c in df.columns if "faults" in c]
    if fault_cols:
        grp2 = df.groupby("policy")[fault_cols].median().reset_index()
        fig, ax = plt.subplots(figsize=(8, 4))
        x2 = np.arange(len(grp2))
        ax.bar(x2 - 0.2, grp2["minor_faults"], 0.4, label="minor", color=PALETTE[0])
        ax.bar(x2 + 0.2, grp2["major_faults"], 0.4, label="major", color=PALETTE[2])
        ax.set_xticks(x2); ax.set_xticklabels(grp2["policy"])
        ax.set_ylabel("Page faults")
        ax.set_title("Prefault Policy — Page Fault Counts")
        ax.legend(); ax.grid(axis="y")
        save(fig, "prefault_faults.png")

# ── Thread scaling ────────────────────────────────────────────────────────────

def plot_thread_scaling():
    df = load_csv("thread_scaling_bench.csv")
    if df.empty: return

    grp = df.groupby("threads")[["throughput_mops","speedup","efficiency"]].median().reset_index()

    fig, axes = plt.subplots(1, 3, figsize=(15, 4))

    axes[0].plot(grp["threads"], grp["throughput_mops"], "o-", color=PALETTE[0])
    axes[0].set_xlabel("Threads"); axes[0].set_ylabel("Throughput (MOps/s)")
    axes[0].set_title("Throughput vs Threads"); axes[0].grid()

    axes[1].plot(grp["threads"], grp["speedup"], "o-", color=PALETTE[1], label="Measured")
    axes[1].plot(grp["threads"], grp["threads"], "--", color="#8b949e", label="Linear (ideal)")
    axes[1].set_xlabel("Threads"); axes[1].set_ylabel("Speedup")
    axes[1].set_title("Amdahl Speedup"); axes[1].legend(); axes[1].grid()

    axes[2].plot(grp["threads"], grp["efficiency"] * 100, "o-", color=PALETTE[2])
    axes[2].axhline(100, color="#8b949e", linestyle="--")
    axes[2].set_xlabel("Threads"); axes[2].set_ylabel("Efficiency (%)")
    axes[2].set_title("Parallel Efficiency"); axes[2].grid()

    fig.suptitle("Thread Scaling Benchmark", y=1.02)
    save(fig, "thread_scaling.png")

# ── False sharing ─────────────────────────────────────────────────────────────

def plot_false_sharing():
    df = load_csv("false_sharing_bench.csv")
    if df.empty: return

    grp = df.groupby(["threads","layout"])["throughput_mops"].median().reset_index()
    pivoted = grp.pivot(index="threads", columns="layout", values="throughput_mops")

    fig, ax = plt.subplots(figsize=(8, 4))
    for i, col in enumerate(pivoted.columns):
        ax.plot(pivoted.index, pivoted[col], "o-", color=PALETTE[i], label=col)
    ax.set_xlabel("Threads"); ax.set_ylabel("Throughput (MOps/s)")
    ax.set_title("False Sharing vs Padded Counters")
    ax.legend(); ax.grid()
    save(fig, "false_sharing.png")

# ── Context switch ────────────────────────────────────────────────────────────

def plot_ctx_switch():
    df = load_csv("ctx_switch_bench.csv")
    if df.empty: return

    fig, ax = plt.subplots(figsize=(7, 4))
    labels = df["config"].tolist()
    vals   = df["cost_ns"].tolist()
    bars   = ax.bar(labels, vals, color=PALETTE[:len(labels)])
    ax.bar_label(bars, fmt="%.0f ns")
    ax.set_ylabel("Context-switch cost (ns)")
    ax.set_title("Context-Switch Ping-Pong Cost")
    ax.grid(axis="y")
    save(fig, "ctx_switch_cost.png")

# ── Scheduler sweeps ──────────────────────────────────────────────────────────

def plot_sched_sweep():
    df = load_csv("sched_sweep.csv")
    if df.empty: return

    if "quantum" in df.columns:
        grp = df.groupby(["quantum","policy"])[["cpu_util","avg_response"]].median().reset_index()
        policies = grp["policy"].unique()

        fig, axes = plt.subplots(1, 2, figsize=(14, 5))
        for i, pol in enumerate(policies):
            sub = grp[grp["policy"] == pol]
            axes[0].plot(sub["quantum"], sub["cpu_util"] * 100,
                         "o-", color=PALETTE[i % len(PALETTE)], label=pol)
            axes[1].plot(sub["quantum"], sub["avg_response"],
                         "o-", color=PALETTE[i % len(PALETTE)], label=pol)

        axes[0].set_xlabel("Quantum (ticks)"); axes[0].set_ylabel("CPU Utilization (%)")
        axes[0].set_title("Quantum vs CPU Utilization"); axes[0].legend(); axes[0].grid()
        axes[1].set_xlabel("Quantum (ticks)"); axes[1].set_ylabel("Avg Response Time (ticks)")
        axes[1].set_title("Quantum vs Response Time"); axes[1].legend(); axes[1].grid()
        fig.suptitle("Scheduler Quantum Sweep")
        save(fig, "sched_quantum_sweep.png")

# ── Dispatch ─────────────────────────────────────────────────────────────────

BENCH_MAP = {
    "prefault":       plot_prefault,
    "thread_scaling": plot_thread_scaling,
    "false_sharing":  plot_false_sharing,
    "ctx_switch":     plot_ctx_switch,
    "sched_sweep":    plot_sched_sweep,
}

def main():
    parser = argparse.ArgumentParser(description="memlab chart generator")
    parser.add_argument("--bench", choices=list(BENCH_MAP.keys()),
                        help="Which benchmark to plot")
    parser.add_argument("--all", action="store_true", help="Plot all benchmarks")
    args = parser.parse_args()

    if args.all or (not args.bench):
        for fn in BENCH_MAP.values():
            fn()
    elif args.bench:
        BENCH_MAP[args.bench]()
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
