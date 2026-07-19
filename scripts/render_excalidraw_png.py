#!/usr/bin/env python3
"""Render a subset of Excalidraw JSON (rects, text, arrows) to a crisp PNG."""
import json
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

src, out = sys.argv[1], sys.argv[2]
data = json.load(open(src))
els = [e for e in data["elements"] if not e.get("isDeleted")]
by_id = {e["id"]: e for e in els}

# Bounds
xs, ys = [], []
for e in els:
    if e["type"] in ("rectangle", "text", "ellipse"):
        xs += [e["x"], e["x"] + e.get("width", 0)]
        ys += [e["y"], e["y"] + e.get("height", 0)]
    elif e["type"] in ("arrow", "line"):
        for px, py in e["points"]:
            xs.append(e["x"] + px); ys.append(e["y"] + py)
minx, maxx, miny, maxy = min(xs) - 30, max(xs) + 30, min(ys) - 30, max(ys) + 30
W, H = maxx - minx, maxy - miny

fig = plt.figure(figsize=(W / 100, H / 100), dpi=200)
ax = fig.add_axes([0, 0, 1, 1])
ax.set_xlim(minx, maxx)
ax.set_ylim(maxy, miny)  # flip y so it matches excalidraw
ax.axis("off")
fig.patch.set_facecolor(data.get("appState", {}).get("viewBackgroundColor", "#ffffff"))

FONT = "DejaVu Sans"


def draw_rect(e):
    r = 10 if e.get("roundness") else 0.5
    p = FancyBboxPatch(
        (e["x"], e["y"] + e["height"]), e["width"], e["height"],
        boxstyle=f"round,pad=0,rounding_size={r}",
        mutation_aspect=1,
        linewidth=e.get("strokeWidth", 2) * 1.1,
        edgecolor=e["strokeColor"],
        facecolor=e.get("backgroundColor", "none") if e.get("backgroundColor") not in (None, "transparent") else "none",
        zorder=2,
    )
    # FancyBboxPatch anchored at lower-left in data coords; y flipped so shift.
    p.set_boxstyle(f"round,pad=0,rounding_size={r}")
    p.set_bounds(e["x"], e["y"], e["width"], e["height"])
    ax.add_patch(p)


def draw_text(e):
    cid = e.get("containerId")
    fs = e.get("fontSize", 16) * 0.72
    color = e["strokeColor"]
    lines = e["text"]
    if cid and cid in by_id:
        c = by_id[cid]
        va = e.get("verticalAlign", "top")
        ta = e.get("textAlign", "center")
        if ta == "center":
            tx = c["x"] + c["width"] / 2; ha = "center"
        else:
            tx = c["x"] + 14; ha = "left"
        if va == "middle":
            ty = c["y"] + c["height"] / 2; mva = "center"
        else:
            ty = c["y"] + 14; mva = "top"
        ax.text(tx, ty, lines, fontsize=fs, color=color, ha=ha, va=mva,
                family=FONT, zorder=4, linespacing=1.35)
    else:
        ta = e.get("textAlign", "left")
        ha = {"left": "left", "center": "center", "right": "right"}[ta]
        tx = e["x"] + (e.get("width", 0) / 2 if ha == "center" else 0)
        weight = "bold" if fs > 18 else "normal"
        ax.text(tx, e["y"], lines, fontsize=fs, color=color, ha=ha, va="top",
                family=FONT, zorder=4, linespacing=1.35, fontweight=weight)


def draw_arrow(e):
    pts = e["points"]
    x0, y0 = e["x"] + pts[0][0], e["y"] + pts[0][1]
    x1, y1 = e["x"] + pts[-1][0], e["y"] + pts[-1][1]
    starts = e.get("startArrowhead")
    ends = e.get("endArrowhead")
    style = ("<" if starts else "") + "-" + (">" if ends else "")
    if style == "-":
        style = "-"
    arr = FancyArrowPatch(
        (x0, y0), (x1, y1),
        arrowstyle=("<|-|>" if starts and ends else ("-|>" if ends else "-")),
        mutation_scale=14, linewidth=e.get("strokeWidth", 2) * 1.1,
        color=e["strokeColor"], zorder=3, shrinkA=2, shrinkB=2,
    )
    ax.add_patch(arr)


for e in els:
    if e["type"] == "rectangle":
        draw_rect(e)
for e in els:
    if e["type"] == "arrow":
        draw_arrow(e)
for e in els:
    if e["type"] == "text":
        draw_text(e)

fig.savefig(out, dpi=200, facecolor=fig.get_facecolor())
print("wrote", out)
