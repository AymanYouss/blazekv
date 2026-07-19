#!/usr/bin/env python3
"""Render the BlazeKV vs Redis benchmark comparison chart from results.json.

Produces a two-panel figure: throughput (ops/sec) and tail latency (p99) for
GET and SET, with the measured speedup annotated. Styled for a portfolio /
README headline rather than a default matplotlib look.
"""
import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager  # noqa: F401

HERE = os.path.dirname(os.path.abspath(__file__))

# Palette
BG = "#0F1117"
PANEL = "#151823"
GRID = "#242838"
TEXT = "#E6E8EE"
MUTED = "#8A90A2"
BLAZE = "#F5A623"
BLAZE2 = "#FF8A3D"
REDIS = "#8892A6"


def fmt_ops(v):
    if v >= 1_000_000:
        return f"{v/1_000_000:.2f}M"
    if v >= 1_000:
        return f"{v/1_000:.0f}K"
    return f"{v:.0f}"


def load(path):
    with open(path) as f:
        return json.load(f)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "results", "results.json")
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "results", "blazekv_vs_redis.png")
    data = load(path)
    r = data["results"]
    cfg = data.get("config", {})

    ops = {
        "GET": (r["redis"]["GET"]["ops_sec"], r["blazekv"]["GET"]["ops_sec"]),
        "SET": (r["redis"]["SET"]["ops_sec"], r["blazekv"]["SET"]["ops_sec"]),
    }
    p99 = {
        "GET": (r["redis"]["GET"]["p99_ms"], r["blazekv"]["GET"]["p99_ms"]),
        "SET": (r["redis"]["SET"]["p99_ms"], r["blazekv"]["SET"]["p99_ms"]),
    }

    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "figure.facecolor": BG,
        "axes.facecolor": PANEL,
        "savefig.facecolor": BG,
        "text.color": TEXT,
        "axes.labelcolor": TEXT,
        "xtick.color": MUTED,
        "ytick.color": MUTED,
        "axes.edgecolor": GRID,
    })

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5.6), dpi=200)
    fig.subplots_adjust(left=0.07, right=0.97, top=0.80, bottom=0.12, wspace=0.22)

    labels = ["GET", "SET"]
    x = range(len(labels))
    w = 0.36

    # ---- Throughput panel ----
    redis_ops = [ops[l][0] for l in labels]
    blaze_ops = [ops[l][1] for l in labels]
    b1 = ax1.bar([i - w / 2 for i in x], redis_ops, w, label="Redis 7.4", color=REDIS)
    b2 = ax1.bar([i + w / 2 for i in x], blaze_ops, w, label="BlazeKV", color=BLAZE)
    ax1.set_title("Throughput  (ops/sec, higher is better)", color=TEXT, fontsize=13,
                  fontweight="bold", loc="left", pad=12)
    ax1.set_xticks(list(x))
    ax1.set_xticklabels(labels, fontsize=12)
    ax1.grid(axis="y", color=GRID, linewidth=0.8, alpha=0.6)
    ax1.set_axisbelow(True)
    for spine in ["top", "right"]:
        ax1.spines[spine].set_visible(False)
    for bars in (b1, b2):
        for rect in bars:
            h = rect.get_height()
            ax1.text(rect.get_x() + rect.get_width() / 2, h, fmt_ops(h), ha="center",
                     va="bottom", color=TEXT, fontsize=10, fontweight="bold")
    for i, l in enumerate(labels):
        if ops[l][0] > 0:
            speed = ops[l][1] / ops[l][0]
            ax1.text(i, max(redis_ops[i], blaze_ops[i]) * 1.14, f"{speed:.2f}x",
                     ha="center", color=BLAZE2, fontsize=12, fontweight="bold")
    ax1.set_ylim(0, max(max(redis_ops), max(blaze_ops)) * 1.28)
    ax1.legend(facecolor=PANEL, edgecolor=GRID, labelcolor=TEXT, loc="upper right")

    # ---- p99 latency panel ----
    redis_p = [p99[l][0] for l in labels]
    blaze_p = [p99[l][1] for l in labels]
    ax2.bar([i - w / 2 for i in x], redis_p, w, label="Redis 7.4", color=REDIS)
    ax2.bar([i + w / 2 for i in x], blaze_p, w, label="BlazeKV", color=BLAZE)
    ax2.set_title("p99 latency  (ms, lower is better)", color=TEXT, fontsize=13,
                  fontweight="bold", loc="left", pad=12)
    ax2.set_xticks(list(x))
    ax2.set_xticklabels(labels, fontsize=12)
    ax2.grid(axis="y", color=GRID, linewidth=0.8, alpha=0.6)
    ax2.set_axisbelow(True)
    for spine in ["top", "right"]:
        ax2.spines[spine].set_visible(False)
    ymax = max(max(redis_p), max(blaze_p), 0.001)
    for i in x:
        ax2.text(i - w / 2, redis_p[i], f"{redis_p[i]:.2f}", ha="center", va="bottom",
                 color=TEXT, fontsize=10)
        ax2.text(i + w / 2, blaze_p[i], f"{blaze_p[i]:.2f}", ha="center", va="bottom",
                 color=TEXT, fontsize=10)
    ax2.set_ylim(0, ymax * 1.28)
    ax2.legend(facecolor=PANEL, edgecolor=GRID, labelcolor=TEXT, loc="upper right")

    tool = cfg.get("tool", "redis-benchmark")
    sub = (f"single node, identical hardware  |  {cfg.get('clients','?')} clients, "
           f"pipeline {cfg.get('pipeline','?')}, {cfg.get('datasize','?')}B values  |  {tool}")
    fig.suptitle("BlazeKV vs Redis", color=TEXT, fontsize=22, fontweight="bold", x=0.07,
                 ha="left", y=0.955)
    fig.text(0.07, 0.87, sub, color=MUTED, fontsize=11, ha="left")

    os.makedirs(os.path.dirname(out), exist_ok=True)
    fig.savefig(out, dpi=200)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
