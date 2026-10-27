#!/usr/bin/env python3
"""Reproduces the case study (docs/case_study.md): every run, table and chart.

    python3 docs/case_study.py                 after ./comp_All.sh (uses Target/Bin)
    python3 docs/case_study.py --charts DIR    writes the charts into DIR

Needs sxe.py (Target/Bin), pandas and matplotlib. The SimpleDca parameters are those of
Target/Config/SimpleDca.ini; every engine setting is written here, so a change to the
example EngineConfig.ini does not change the numbers.
"""
import argparse
import copy
import os
import re
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "Target", "Bin"))
import sxe  # noqa: E402

import matplotlib  # noqa: E402
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pandas as pd  # noqa: E402

CONFIG = os.path.join(ROOT, "Target", "Config", "EngineConfig.ini")
STRATEGY_INI = os.path.join(ROOT, "Target", "Config", "SimpleDca.ini")

# realistic simulation: Bitget fees, real funding, liquidation, price path inside the candle
REALISTIC = {
    "STRATEGY": {"library": "SimpleDca", "config": STRATEGY_INI},
    "EXCHANGE": {"margin_mode": "isolated", "fee_taker": 0.0006, "fee_maker": 0.0002, "slippage": 0.00005},
    "SIMULATION": {"start_date": "", "end_date": "", "initial_capital": 5000,
                   "data_file": "BTCUSDT_1h_2023_2024.csv", "funding_file": "BTCUSDT_funding_2023_2024.csv",
                   "funding_rate": 0.0001, "funding_multiplier": 1,
                   "intrabar": 1, "tick_step": 0.002, "liquidation": 1},
}
NO_COSTS = {"EXCHANGE": {"fee_taker": 0, "fee_maker": 0, "slippage": 0}}
NO_FUNDING = {"SIMULATION": {"funding_file": "", "funding_rate": 0}}
NO_LIQUIDATION = {"SIMULATION": {"liquidation": 0}}
CLOSES_ONLY = {"SIMULATION": {"intrabar": 0, "tick_step": 0}}

# from the naive backtest to the realistic one, one ingredient at a time
STEPS = [
    ("Closes only, no costs", [CLOSES_ONLY, NO_COSTS, NO_FUNDING, NO_LIQUIDATION]),
    ("+ price path inside the candle", [NO_COSTS, NO_FUNDING, NO_LIQUIDATION]),
    ("+ fees and slippage", [NO_FUNDING, NO_LIQUIDATION]),
    ("+ funding", [NO_LIQUIDATION]),
    ("+ liquidation (realistic)", []),
]
PATHS = [("Low first in every candle", 2), ("High first in every candle", 3)]
LEVERAGES = [1, 2, 3, 4, 5, 6, 8]
NEIGHBOURS = [(step, tp) for step in (2, 3, 4) for tp in (1, 1.5, 2)]

SURF, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
BLUE, BLUE_LIGHT, RED = "#2a78d6", "#a9c9f0", "#e34948"


def merge(*parts):
    out = copy.deepcopy(REALISTIC)
    for part in parts:
        for section, keys in part.items():
            out.setdefault(section, {}).update(keys)
    return out


class Variants:
    """SimpleDca.ini with some parameters changed, in a temporary folder."""

    def __init__(self, folder):
        self.folder = folder
        self.text = open(STRATEGY_INI, encoding="utf-8").read()

    def ini(self, **params):
        text = self.text
        for key, value in params.items():
            text, n = re.subn(rf"^{key}\s*=.*$", f"{key} = {value}", text, flags=re.M)
            if n != 1:
                raise SystemExit(f"{STRATEGY_INI}: key {key} not found")
        path = os.path.join(self.folder, "_".join(f"{k}_{v}" for k, v in params.items()) + ".ini")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path


def run(settings, curve=False):
    """One run without files; curve=True also returns the equity every 200 steps."""
    points = []
    with sxe.Engine() as engine:
        engine.load_config(CONFIG)
        engine.update(settings)
        engine.files(False)
        engine.options(progress_every=200 if curve else 0)
        if curve:
            engine.on_progress(lambda p: points.append((p.time, p.equity, p.drawdown_pct)))
        result = engine.run()
    if curve:
        return result, pd.DataFrame(points, columns=["time", "equity", "drawdown_pct"]).set_index("time")
    return result


def months(first, last):
    y, m = map(int, first.split("-"))
    while f"{y}-{m:02d}" <= last:
        yield f"{y}-{m:02d}"
        m += 1
        if m > 12:
            y, m = y + 1, 1


def add_months(month, n):
    y, m = map(int, month.split("-"))
    m += n
    return f"{y + (m - 1) // 12}-{(m - 1) % 12 + 1:02d}"


def row(name, r):
    return {"run": name, "return_pct": r.return_pct, "max_drawdown_pct": r.max_drawdown_pct,
            "final_equity": r.final_equity, "cycles": r.cycles, "liquidations": r.liquidations,
            "fees": r.fees, "funding": r.funding}


def table(rows):
    df = pd.DataFrame(rows).set_index("run")
    return df.round({"return_pct": 1, "max_drawdown_pct": 1, "final_equity": 0, "fees": 0, "funding": 0})


# ---------- charts ----------

def style(ax):
    ax.set_facecolor(SURF)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=INK2, labelsize=9, length=0)
    ax.grid(color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def save(fig, path):
    fig.savefig(path, format="svg", facecolor=SURF, metadata={"Date": None})
    plt.close(fig)
    print(f"chart: {os.path.relpath(path, ROOT)}")


def chart_steps(steps, path):
    names = [name for name, _ in steps][::-1]
    values = [r.final_equity for _, r in steps][::-1]
    fig, ax = plt.subplots(figsize=(8, 3.2), dpi=100)
    fig.patch.set_facecolor(SURF)
    style(ax)
    ax.grid(axis="y", visible=False)
    bars = ax.barh(names, values, height=0.6, color=[BLUE] + [BLUE_LIGHT] * (len(values) - 1))
    for bar, value in zip(bars, values):
        ax.text(bar.get_width() + max(values) * 0.01, bar.get_y() + bar.get_height() / 2,
                f"{value:,.0f}", va="center", fontsize=9, color=INK)
    ax.set_xlim(0, max(values) * 1.15)
    ax.xaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v / 1000:.0f}k"))
    ax.tick_params(axis="y", labelsize=10, colors=INK)
    ax.set_title("Final equity (USDT) from 5,000 USDT, BTCUSDT 2023-2024",
                 loc="left", fontsize=11, color=INK, pad=10)
    fig.tight_layout()
    save(fig, path)


def chart_equity(curve, max_drawdown_pct, path):
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 5.2), dpi=100, sharex=True,
                                   gridspec_kw={"height_ratios": [2.2, 1]})
    fig.patch.set_facecolor(SURF)
    for ax in (ax1, ax2):
        style(ax)
    ax1.plot(curve.index, curve.equity, color=BLUE, linewidth=1.6)
    ax1.set_yscale("log")
    ax1.yaxis.set_major_locator(matplotlib.ticker.FixedLocator([5000, 10000, 20000, 50000]))
    ax1.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v / 1000:g}k"))
    ax1.yaxis.set_minor_formatter(matplotlib.ticker.NullFormatter())
    ax1.set_title("Equity of the realistic simulation (USDT, log scale)", loc="left", fontsize=11, color=INK, pad=8)

    ax2.fill_between(curve.index, curve.drawdown_pct, 0, color=RED, alpha=0.25, linewidth=0)
    ax2.plot(curve.index, curve.drawdown_pct, color=RED, linewidth=1.2)
    worst = curve.drawdown_pct.idxmin()
    # the curve is sampled every 200 steps: the label shows the exact maximum of the result
    ax2.annotate(f"{max_drawdown_pct:.1f}%  ({worst:%d %b %Y})", xy=(worst, curve.drawdown_pct.min()),
                 xytext=(-10, 4), textcoords="offset points", ha="right", fontsize=9, color=INK)
    ax2.set_title("Drawdown from the highest equity (%)", loc="left", fontsize=11, color=INK, pad=8)
    ax2.set_ylim(min(-75, curve.drawdown_pct.min() * 1.1), 0)
    ax2.xaxis.set_major_formatter(matplotlib.dates.DateFormatter("%b %Y"))
    fig.tight_layout()
    save(fig, path)


def chart_leverage(rows, path):
    lev = [f"{x}x" for x in LEVERAGES]
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8, 3.4), dpi=100)
    fig.patch.set_facecolor(SURF)
    for ax in (ax1, ax2):
        style(ax)
        ax.grid(axis="x", visible=False)
        ax.axhline(0, color=INK2, linewidth=0.8)
    returns = [r["return_pct"] for r in rows]
    drawdowns = [r["max_drawdown_pct"] for r in rows]
    ax1.bar(lev, returns, width=0.6, color=BLUE)
    for i, r in enumerate(rows):
        label = f"{r['return_pct']:+,.0f}%"
        if r["liquidations"]:
            label += f"\n{r['liquidations']} liq."
        # a loss is labelled above the zero line, clear of the axis labels
        ax1.text(i, max(r["return_pct"], 0) + 60, label, ha="center", va="bottom", fontsize=8, color=INK)
    ax1.set_ylim(min(min(returns), 0) - 150, max(returns) * 1.25)
    ax1.set_title("Return (%)", loc="left", fontsize=11, color=INK, pad=8)
    ax2.bar(lev, drawdowns, width=0.6, color=RED)
    for i, d in enumerate(drawdowns):
        ax2.text(i, d - 2, f"{d:.0f}%", ha="center", va="top", fontsize=8, color=INK)
    ax2.set_ylim(-118, 0)
    ax2.set_title("Max drawdown (%)", loc="left", fontsize=11, color=INK, pad=8)
    for ax in (ax1, ax2):
        ax.set_xlabel("leverage", fontsize=9, color=INK2)
    fig.tight_layout()
    save(fig, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--charts", default=os.path.join(ROOT, "docs", "case_study"),
                        help="folder of the SVG charts (default docs/case_study)")
    args = parser.parse_args()
    os.makedirs(args.charts, exist_ok=True)
    plt.rcParams["svg.hashsalt"] = "sxe-case-study"
    pd.set_option("display.width", 120)

    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(4) as pool:
        variants = Variants(tmp)

        # 1. from the naive backtest to the realistic one
        jobs = [pool.submit(run, merge(*parts)) for _, parts in STEPS]
        realistic = pool.submit(run, merge(), True)
        paths = [pool.submit(run, merge({"SIMULATION": {"intrabar": n}})) for _, n in PATHS]
        steps = [(name, job.result()) for (name, _), job in zip(STEPS, jobs)]
        print("\n1. From the naive backtest to the realistic one")
        print(table([row(name, r) for name, r in steps] +
                    [row(f"realistic, {name.lower()}", job.result()) for (name, _), job in zip(PATHS, paths)]).to_string())

        # 2. leverage
        jobs = [pool.submit(run, merge({"STRATEGY": {"config": variants.ini(leverage=x)}})) for x in LEVERAGES]
        lev_rows = [row(f"leverage {x}", job.result()) for x, job in zip(LEVERAGES, jobs)]
        print("\n2. Leverage")
        print(table(lev_rows).to_string())

        # 3. every 12-month window
        starts = list(months("2023-01", "2024-01"))
        jobs = [pool.submit(run, merge({"SIMULATION": {"start_date": s, "end_date": add_months(s, 11)}}))
                for s in starts]
        print("\n3. Every 12-month window")
        print(table([row(f"{s} to {add_months(s, 11)}", job.result()) for s, job in zip(starts, jobs)]).to_string())

        # 4. neighbouring parameters
        jobs = [pool.submit(run, merge({"STRATEGY": {"config": variants.ini(step_perc=s, take_profit_perc=t)}}))
                for s, t in NEIGHBOURS]
        print("\n4. Neighbouring parameters")
        print(table([row(f"step {s}%, take profit {t}%", job.result())
                     for (s, t), job in zip(NEIGHBOURS, jobs)]).to_string())

        print()
        r, curve = realistic.result()
        chart_steps(steps, os.path.join(args.charts, "steps.svg"))
        chart_equity(curve, r.max_drawdown_pct, os.path.join(args.charts, "equity.svg"))
        chart_leverage(lev_rows, os.path.join(args.charts, "leverage.svg"))


if __name__ == "__main__":
    main()
