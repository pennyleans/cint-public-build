"""Render a brief chart as deterministic SVG from a JSON specification.

The chart style of `docs/cint/STYLE.md` (Charts): one axis, thin marks, hairline
grid, text in ink tokens, a legend for two or more series, selective direct
labels, and a footer that names the status (Measured or Reported) and the
receipt the numbers come from. The output is byte-for-byte reproducible from the
specification: no timestamps, no host paths, no randomness.

Specification (JSON, UTF-8):

    {
      "title": "T1 agreement run, elapsed seconds per configuration",
      "subtitle": "20 configurations, 1,444,819 records each",
      "status": "Measured",
      "source": "results/cint/t1/receipt-*.json, 2026-10-03 UTC",
      "unit": "seconds",
      "form": "hbar",
      "categories": ["msvc /Od portable", "..."],
      "series": [{"name": "elapsed", "values": [69, 70]}],
      "emphasis": [0, 1],
      "labels": [0, 1],
      "reference": {"label": "budget", "value": 60},
      "scale": "linear",
      "decimals": 0
    }

Forms: "hbar" (horizontal bars; magnitude per category; one to four series) and
"line" (one to four series over ordered categories). "emphasis" lists category
indexes drawn in the accent color while the rest take the de-emphasis gray
(single-series charts only). "labels" lists the category indexes that get a
direct value label; "ends" labels the last point of each line. "reference"
draws one labeled hairline at a value. "scale" is "linear" or "log" (base 10,
values must be positive). Numbers are Python integers or decimal strings; the
renderer never uses binary floating point for data values (it uses
fractions.Fraction for the scale arithmetic).

Usage (from the repository root):
    python tools/cint_chart.py SPEC.json -o OUT.svg [--dark] [--table OUT.md]
"""

import argparse
import json
import math
import sys
from fractions import Fraction

LIGHT = {
    "surface": "#fcfcfb", "ink": "#0b0b0b", "ink2": "#52514e", "muted": "#898781",
    "grid": "#e1e0d9", "axis": "#c3c2b7", "deemph": "#c3c2b7",
    "series": ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"],
}
DARK = {
    "surface": "#1a1a19", "ink": "#ffffff", "ink2": "#c3c2b7", "muted": "#898781",
    "grid": "#2c2c2a", "axis": "#383835", "deemph": "#52514e",
    "series": ["#3987e5", "#d95926", "#199e70", "#c98500"],
}
FONT = "system-ui, -apple-system, 'Segoe UI', sans-serif"
W = 720
BAR = 16          # bar thickness, under the 24 px cap
GAP = 2           # surface gap between touching marks
ROW = 22          # row pitch for horizontal bars


def frac(x):
    return x if isinstance(x, Fraction) else Fraction(str(x))


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace('"', "&quot;"))


def fmt(x, decimals):
    x = frac(x)
    if decimals == 0:
        n = int(round(x))
        return format(n, ",")
    scaled = int(round(x * 10 ** decimals))
    sign = "-" if scaled < 0 else ""
    scaled = abs(scaled)
    whole, part = divmod(scaled, 10 ** decimals)
    return "%s%s.%0*d" % (sign, format(whole, ","), decimals, part)


def nice_ticks(vmax, count=5):
    """Clean linear ticks from 0 to at least vmax."""
    vmax = frac(vmax)
    if vmax <= 0:
        return [Fraction(0), Fraction(1)]
    raw = vmax / count
    mag = Fraction(10) ** int(math.floor(math.log10(raw)))
    for m in (1, 2, 5, 10):
        step = mag * m
        if step >= raw:
            break
    ticks = []
    t = Fraction(0)
    while t < vmax:
        ticks.append(t)
        t += step
    ticks.append(t)
    return ticks


def log_ticks(vmin, vmax):
    lo = int(math.floor(math.log10(frac(vmin))))
    hi = int(math.ceil(math.log10(frac(vmax))))
    return [Fraction(10) ** e for e in range(lo, hi + 1)]


class Scale:
    def __init__(self, kind, lo, hi, px0, px1):
        self.kind, self.lo, self.hi, self.px0, self.px1 = kind, frac(lo), frac(hi), px0, px1

    def __call__(self, v):
        v = frac(v)
        if self.kind == "log":
            a = math.log10(float(v)) - math.log10(float(self.lo))
            b = math.log10(float(self.hi)) - math.log10(float(self.lo))
            t = a / b if b else 0
        else:
            t = float((v - self.lo) / (self.hi - self.lo)) if self.hi != self.lo else 0
        return round(self.px0 + (self.px1 - self.px0) * t, 2)


def text(x, y, s, size, fill, anchor="start", weight="normal", extra=""):
    return ('<text x="%s" y="%s" font-family="%s" font-size="%s" fill="%s" '
            'text-anchor="%s" font-weight="%s"%s>%s</text>'
            % (x, y, FONT, size, fill, anchor, weight, extra, esc(s)))


def render(spec, theme):
    form = spec.get("form", "hbar")
    cats = spec["categories"]
    series = spec["series"]
    if not 1 <= len(series) <= 4:
        raise SystemExit("one to four series")
    decimals = int(spec.get("decimals", 0))
    scale_kind = spec.get("scale", "linear")
    labels = spec.get("labels", [])
    emphasis = spec.get("emphasis")
    reference = spec.get("reference")
    values = [[frac(v) for v in s["values"]] for s in series]
    for s in values:
        if len(s) != len(cats):
            raise SystemExit("every series needs one value per category")
    allv = [v for s in values for v in s]
    if reference:
        allv.append(frac(reference["value"]))
    vmax = max(allv)
    vmin = min(allv)
    if scale_kind == "log":
        if vmin <= 0:
            raise SystemExit("log scale needs positive values")
        ticks = log_ticks(vmin, vmax)
        lo, hi = ticks[0], ticks[-1]
    else:
        ticks = nice_ticks(vmax)
        lo, hi = Fraction(0), ticks[-1]

    title_h = 24 + (18 if spec.get("subtitle") else 0)
    legend_h = 20 if len(series) > 1 else 0
    left = 8 + max(len(c) for c in cats) * 7 if form == "hbar" else 56
    left = min(left, 260)
    right = 24
    top = 16 + title_h + legend_h
    footer_h = 34
    out = []

    if form == "hbar":
        n = len(cats)
        group = len(series) * (BAR + GAP) - GAP
        row = max(ROW, group + 8)
        plot_h = n * row
        H = top + plot_h + 28 + footer_h
        xs = Scale(scale_kind, lo, hi, left, W - right)
        out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" role="img" aria-label="%s">'
                   % (W, H, W, H, esc(spec["title"])))
        out.append('<rect width="%d" height="%d" fill="%s"/>' % (W, H, theme["surface"]))
        out.append(text(16, 24, spec["title"], 15, theme["ink"], weight="600"))
        if spec.get("subtitle"):
            out.append(text(16, 42, spec["subtitle"], 12, theme["ink2"]))
        if len(series) > 1:
            x = 16
            for i, s in enumerate(series):
                out.append('<rect x="%d" y="%d" width="10" height="10" rx="2" fill="%s"/>' % (x, top - 14, theme["series"][i]))
                out.append(text(x + 14, top - 5, s["name"], 12, theme["ink2"]))
                x += 14 + len(s["name"]) * 7 + 16
        # grid and ticks
        for t in ticks:
            x = xs(t)
            out.append('<line x1="%s" y1="%d" x2="%s" y2="%d" stroke="%s" stroke-width="1"/>' % (x, top, x, top + plot_h, theme["grid"]))
            out.append(text(x, top + plot_h + 16, fmt(t, decimals), 11, theme["muted"], anchor="middle",
                            extra=' style="font-variant-numeric: tabular-nums"'))
        out.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="1"/>' % (left, top, left, top + plot_h, theme["axis"]))
        out.append(text(W - right, top + plot_h + 16 + 12, spec.get("unit", ""), 11, theme["muted"], anchor="end"))
        for ci, c in enumerate(cats):
            y0 = top + ci * row + (row - group) / 2
            out.append(text(left - 8, y0 + group / 2 + 4, c, 12, theme["ink2"], anchor="end"))
            for si, s in enumerate(values):
                v = s[ci]
                y = y0 + si * (BAR + GAP)
                x1 = xs(v)
                x0 = xs(lo) if scale_kind == "linear" else left
                w = max(x1 - x0, 1)
                color = theme["series"][si]
                if emphasis is not None and len(series) == 1:
                    color = theme["series"][0] if ci in emphasis else theme["deemph"]
                r = 4 if w >= 8 else 0
                path = ('M%s,%s H%s a%d,%d 0 0 1 %d,%d V%s a%d,%d 0 0 1 -%d,%d H%s Z'
                        % (x0, y, x1 - r, r, r, r, r, y + BAR - r, r, r, r, r, x0))
                out.append('<path d="%s" fill="%s"><title>%s, %s: %s %s</title></path>'
                           % (path, color, esc(c), esc(series[si]["name"]), fmt(v, decimals), esc(spec.get("unit", ""))))
                if ci in labels:
                    out.append(text(x1 + 6, y + BAR - 4, fmt(v, decimals), 11, theme["ink"],
                                    extra=' style="font-variant-numeric: tabular-nums"'))
        if reference:
            x = xs(reference["value"])
            out.append('<line x1="%s" y1="%d" x2="%s" y2="%d" stroke="%s" stroke-width="1"/>' % (x, top - 4, x, top + plot_h, theme["ink2"]))
            out.append(text(x + 4, top - 6 + (0 if legend_h else 0), reference["label"], 11, theme["ink2"]))
        fy = top + plot_h + 28 + 14
    else:  # line
        n = len(cats)
        plot_h = 220
        H = top + plot_h + 40 + footer_h
        xs = Scale("linear", 0, max(n - 1, 1), left, W - right - 48)
        ys = Scale(scale_kind, lo, hi, top + plot_h, top)
        out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" role="img" aria-label="%s">'
                   % (W, H, W, H, esc(spec["title"])))
        out.append('<rect width="%d" height="%d" fill="%s"/>' % (W, H, theme["surface"]))
        out.append(text(16, 24, spec["title"], 15, theme["ink"], weight="600"))
        if spec.get("subtitle"):
            out.append(text(16, 42, spec["subtitle"], 12, theme["ink2"]))
        if len(series) > 1:
            x = 16
            for i, s in enumerate(series):
                out.append('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="2" stroke-linecap="round"/>' % (x, top - 9, x + 12, top - 9, theme["series"][i]))
                out.append(text(x + 16, top - 5, s["name"], 12, theme["ink2"]))
                x += 16 + len(s["name"]) * 7 + 16
        for t in ticks:
            y = ys(t)
            out.append('<line x1="%d" y1="%s" x2="%d" y2="%s" stroke="%s" stroke-width="1"/>' % (left, y, W - right - 48, y, theme["grid"]))
            out.append(text(left - 6, y + 4, fmt(t, decimals), 11, theme["muted"], anchor="end",
                            extra=' style="font-variant-numeric: tabular-nums"'))
        out.append('<line x1="%d" y1="%s" x2="%d" y2="%s" stroke="%s" stroke-width="1"/>' % (left, ys(lo), W - right - 48, ys(lo), theme["axis"]))
        out.append(text(left - 6, top - 8, spec.get("unit", ""), 11, theme["muted"], anchor="end"))
        for ci, c in enumerate(cats):
            out.append(text(xs(ci), top + plot_h + 16, c, 11, theme["muted"], anchor="middle"))
        for si, s in enumerate(values):
            pts = " ".join("%s,%s" % (xs(i), ys(v)) for i, v in enumerate(s))
            out.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>' % (pts, theme["series"][si]))
            for i, v in enumerate(s):
                out.append('<circle cx="%s" cy="%s" r="4" fill="%s" stroke="%s" stroke-width="2"><title>%s, %s: %s %s</title></circle>'
                           % (xs(i), ys(v), theme["series"][si], theme["surface"], esc(cats[i]), esc(series[si]["name"]), fmt(v, decimals), esc(spec.get("unit", ""))))
            if labels == "ends":
                out.append(text(xs(n - 1) + 8, ys(s[-1]) + 4, fmt(s[-1], decimals), 11, theme["ink"],
                                extra=' style="font-variant-numeric: tabular-nums"'))
        if reference:
            y = ys(reference["value"])
            out.append('<line x1="%d" y1="%s" x2="%d" y2="%s" stroke="%s" stroke-width="1"/>' % (left, y, W - right - 48, y, theme["ink2"]))
            out.append(text(W - right - 44, y + 4, reference["label"], 11, theme["ink2"]))
        fy = top + plot_h + 40 + 14
    status = spec.get("status", "Reported")
    out.append(text(16, fy, "%s. Source: %s" % (status, spec.get("source", "")), 11, theme["muted"]))
    out.append(text(16, fy + 14, "Regenerate: python tools/cint_chart.py %s" % spec.get("spec_path", "SPEC.json"), 11, theme["muted"]))
    out.append("</svg>")
    return "\n".join(out) + "\n"


def table(spec):
    decimals = int(spec.get("decimals", 0))
    head = "| Category | " + " | ".join(s["name"] for s in spec["series"]) + " |"
    sep = "| --- |" + " ---: |" * len(spec["series"])
    rows = [head, sep]
    for i, c in enumerate(spec["categories"]):
        rows.append("| %s | %s |" % (c, " | ".join(fmt(s["values"][i], decimals) for s in spec["series"])))
    unit = spec.get("unit", "")
    rows.append("")
    rows.append("%s. Source: %s. Values in %s." % (spec.get("status", "Reported"), spec.get("source", ""), unit))
    return "\n".join(rows) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("spec")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--dark", action="store_true")
    ap.add_argument("--table")
    ns = ap.parse_args(argv)
    with open(ns.spec, "rb") as fh:
        spec = json.loads(fh.read().decode("utf-8"))
    spec.setdefault("spec_path", ns.spec.replace("\\", "/"))
    svg = render(spec, DARK if ns.dark else LIGHT)
    with open(ns.out, "wb") as fh:
        fh.write(svg.encode("utf-8"))
    if ns.table:
        with open(ns.table, "wb") as fh:
            fh.write(table(spec).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
