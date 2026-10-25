#!/usr/bin/env python3
# Draws equity and drawdown at the close of each cycle from Target/Backup/total_result.txt
# as an SVG image. Only the Python standard library is needed.
#
#   python3 docs/equity_chart.py Target/Backup/total_result.txt equity.svg
#   python3 docs/equity_chart.py Target/Backup/total_result.txt equity.svg --note "my text"
#
# If the file holds several runs, the last one is drawn. The note under the chart defaults
# to the strategy, symbol, data file, period and initial capital of that run.

import argparse
import math
import re
from datetime import datetime

FMT = '%Y-%m-%d %H:%M'
W, H = 800, 470
L, R = 64, 36
T1, B1 = 40, 250
T2, B2 = 300, 410
SURF, INK, INK2, GRID = '#fcfcfb', '#0b0b0b', '#52514e', '#e4e3df'
BLUE, RED = '#2a78d6', '#e34948'
MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']


def read_last_run(path):
    header, rows = '', []
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if line.startswith('#') and ' start ' in line:
            header, rows = line, []
        elif line[:1].isdigit():
            rows.append(line.split(';'))
    if not rows:
        raise SystemExit(f'{path}: no cycles found')
    return header, rows


def nice_step(span, ticks):
    raw = span / ticks
    mag = 10 ** math.floor(math.log10(raw))
    for m in (1, 2, 2.5, 5, 10):
        if raw <= m * mag:
            return m * mag
    return 10 * mag


def default_note(header, capital, t0, t1):
    m = re.search(r'start \w+: ([^,]+), ([^,]+), data (\S+)', header)
    parts = list(m.groups()) if m else []
    parts.append(f'{t0:%Y-%m-%d} -> {t1:%Y-%m-%d}')
    parts.append(f'initial capital {capital:,.0f} USDT')
    return ', '.join(parts)


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def main():
    ap = argparse.ArgumentParser(description='Equity and drawdown chart from total_result.txt')
    ap.add_argument('total_result')
    ap.add_argument('svg')
    ap.add_argument('--note', help='text under the chart')
    a = ap.parse_args()

    header, rows = read_last_run(a.total_result)
    capital = float(rows[0][3])
    t0 = datetime.strptime(rows[0][1], FMT)
    m = re.search(r'csv (\d{4}-\d\d-\d\d \d\d:\d\d) ->', header)
    if m:
        t0 = min(t0, datetime.strptime(m.group(1), FMT))
    pts = [(t0, capital)] + [(datetime.strptime(r[2], FMT), float(r[4])) for r in rows]
    t1 = pts[-1][0]
    dd, peak = [], 0.0
    for t, e in pts:
        peak = max(peak, e)
        dd.append((t, (e / peak - 1) * 100))

    span = max((t1 - t0).total_seconds(), 1)
    x = lambda t: L + (W - L - R) * (t - t0).total_seconds() / span

    emin, emax = min(e for _, e in pts), max(e for _, e in pts)
    step = nice_step(max(emax - emin, 1), 5)
    ylo = math.floor(emin / step) * step
    yhi = math.ceil(emax / step) * step
    pad = (yhi - ylo) * 0.08
    y1 = lambda v: B1 - (B1 - T1) * (v - ylo + pad) / (yhi - ylo + 2 * pad)

    dmin = min(d for _, d in dd)
    dstep = nice_step(max(-dmin, 1), 4)
    dlo = (math.floor(dmin / dstep) - 1) * dstep
    y2 = lambda v: T2 + (B2 - T2) * v / dlo

    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
         f'font-family="-apple-system, Segoe UI, Helvetica, Arial, sans-serif" font-size="12">',
         f'<rect width="{W}" height="{H}" fill="{SURF}" rx="6"/>',
         f'<text x="{L}" y="24" fill="{INK}" font-size="14" font-weight="600">'
         f'Equity (USDT, at the close of each cycle)</text>']
    v = ylo
    while v <= yhi + 1e-9:
        o.append(f'<line x1="{L}" x2="{W-R}" y1="{y1(v):.1f}" y2="{y1(v):.1f}" stroke="{GRID}"/>')
        o.append(f'<text x="{L-8}" y="{y1(v)+4:.1f}" fill="{INK2}" text-anchor="end">{v:,.0f}</text>')
        v += step
    o.append(f'<line x1="{L}" x2="{W-R}" y1="{y1(capital):.1f}" y2="{y1(capital):.1f}" '
             f'stroke="{INK2}" stroke-dasharray="4 3"/>')
    o.append(f'<polyline fill="none" stroke="{BLUE}" stroke-width="2" stroke-linejoin="round" points="'
             + ' '.join(f'{x(t):.1f},{y1(e):.1f}' for t, e in pts) + '"/>')
    te, ee = pts[-1]
    o.append(f'<text x="{x(te)-8:.1f}" y="{y1(ee)+22:.1f}" fill="{INK}" text-anchor="end">'
             f'{ee:,.2f} ({(ee / capital - 1) * 100:+.2f}%)</text>')

    o.append(f'<text x="{L}" y="{T2-16}" fill="{INK}" font-size="14" font-weight="600">'
             f'Drawdown from the previous peak (%)</text>')
    v = 0.0
    while v >= dlo - 1e-9:
        o.append(f'<line x1="{L}" x2="{W-R}" y1="{y2(v):.1f}" y2="{y2(v):.1f}" stroke="{GRID}"/>')
        o.append(f'<text x="{L-8}" y="{y2(v)+4:.1f}" fill="{INK2}" text-anchor="end">{v:g}</text>')
        v -= dstep
    area = ([f'{x(t0):.1f},{y2(0):.1f}'] + [f'{x(t):.1f},{y2(d):.1f}' for t, d in dd]
            + [f'{x(t1):.1f},{y2(0):.1f}'])
    o.append(f'<polygon fill="{RED}" fill-opacity="0.25" stroke="none" points="{" ".join(area)}"/>')
    o.append(f'<polyline fill="none" stroke="{RED}" stroke-width="1.5" stroke-linejoin="round" points="'
             + ' '.join(f'{x(t):.1f},{y2(d):.1f}' for t, d in dd) + '"/>')
    tm, dm = min(dd, key=lambda p: p[1])
    o.append(f'<text x="{x(tm):.1f}" y="{y2(dm)+18:.1f}" fill="{INK}" text-anchor="middle">{dm:.2f}%</text>')

    months = (t1.year - t0.year) * 12 + t1.month - t0.month
    every = 3 if months <= 36 else 6 if months <= 72 else 12
    y, mo = t0.year, t0.month
    while (y, mo) <= (t1.year, t1.month):
        t = datetime(y, mo, 1)
        if t >= t0 and (mo - 1) % every == 0:
            xx = x(t)
            o.append(f'<line x1="{xx:.1f}" x2="{xx:.1f}" y1="{B2}" y2="{B2+4}" stroke="{INK2}"/>')
            label = str(y) if every == 12 else f'{MONTHS[mo-1]} {y}'
            o.append(f'<text x="{xx:.1f}" y="{B2+18}" fill="{INK2}" text-anchor="middle">{label}</text>')
        y, mo = (y + 1, 1) if mo == 12 else (y, mo + 1)

    note = a.note if a.note is not None else default_note(header, capital, t0, t1)
    o.append(f'<text x="{L}" y="{H-10}" fill="{INK2}">{esc(note)}</text>')
    o.append('</svg>')
    with open(a.svg, 'w', encoding='utf-8') as f:
        f.write('\n'.join(o) + '\n')


if __name__ == '__main__':
    main()
