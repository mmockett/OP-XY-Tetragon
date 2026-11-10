#!/usr/bin/env python3
"""Writes Syahi's screen artwork (panel.svg.fragment) and the node numbers
and anchors its display reads (art.h). Run it after changing anything here
and commit both.

    python3 syahi/tools/make_panel.py

The screen is a tabla in the stock isometric look: the drum standing on its
ring, its body in bands with the straps down its front and four wooden
tuning blocks wedged in them, the head on top with its nodal lines, the
syahi built up ring by ring, the resting palm's pad and the stroke's spot.
One isometric projection: 30 degree edges, no vanishing point. World units
are screen pixels: X runs down to the right, Y down to the left, Z up.

What each control moves (display.cpp):
  TUNE    the tuning blocks slide down the straps to tighten the head
  DECAY   the palm's pad on the head, smaller the longer the ring
  LOAD    the syahi: more rings and a wider black disc
  PRESS   how many nodal lines are lit
  FORM    the body's bands: four for a skin down to one for a gong
  SPOT    where the stroke lands, from the centre out
A hit lights every nodal line white; they go back one by one as it rings.

The grey follows the encoders: the bands (FORM, encoder 1 on page 2) 323232
and darker, the pad 4B4B4B, the syahi's rings 969696, the lit lines and the
spot CDCDCD. Every 3D face carries a 0.5 px black outline. The ring under
the drum never moves, so it is plain artwork; everything else is a bound
node display.cpp shows, hides, slides or (the flat pad and syahi) scales.
No outline reshapes. Each node is one closed subpath of at most 64 points;
lines are thin closed ribbons; no outline turns sharper than SHARPEST.
Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "syahi"
SHARPEST = 150.0   # degrees: the sharpest turn an outline may make (the device's mitres have no limit)

# ---------------------------------------------------------------- the view
C30, S30 = math.cos(math.radians(30)), 0.5
RX_K, RY_K = math.sqrt(1.5), math.sqrt(0.5)   # a level circle of radius r is an ellipse r*RX_K by r*RY_K
# The drawing's origin: the design's (236, 104), moved so the drum's extent over
# every setting is centred on x = 240 in the stage under the header (y 38..222).
OX, OY = 236.0 + 4.5, 104.0 + 12.5


def P(x, y, z=0.0):
    """A world point on screen."""
    return (OX + (x - y) * C30, OY + (x + y) * S30 - z)


# ---------------------------------------------------------------- the drum
CX, CY, R = 40.0, 40.0, 30.0   # the body's axis and radius
RING_R, RING_H = R + 6, 6.0    # the ring it sits on
BAND_H, BANDS = 12.0, 4        # FORM takes bands off the top: four for a skin, one for a gong
Z_BODY = RING_H                # the lowest band's foot
Z_TOP = Z_BODY + BAND_H * BANDS   # the body's top with every band: the reference for the head's nodes
HEAD_H = 2.0
Z_HEAD = Z_TOP + HEAD_H        # the head's face
STRAPS = [math.radians(40 + k * 16) for k in range(7)]
BLOCKS = [math.radians(48 + k * 26) for k in range(4)]
BLOCK, BLOCK_H = 5.0, 8.0
BLOCK_LO = 9.0                 # a block's foot with TUNE full: slid down to tighten
BLOCK_REF = Z_TOP - 10         # its foot with TUNE at 0, every band on
RULES = [("d", math.radians(20 + 45 * k)) for k in range(4)] + [("c", k) for k in (0.42, 0.72)]
SYAHI_R0, SYAHI_STEP, SYAHI_RINGS = 4.0, 2.6, 5
PAD = (CX - R * 0.6, CY + R * 0.55)
PAD_MAX, PAD_MIN = 9.0, 3.0    # the pad's radius at DECAY 0 and 100
SPOT_R = 2.2
SPOT_LO, SPOT_HI = 0.2, 0.85   # the stroke's distance out, as a share of R * 0.6 along each axis

# ---------------------------------------------------------------- greys
K = "#000000"
E1, E2, E3, E4 = "#323232", "#4B4B4B", "#969696", "#CDCDCD"
RAMP = {"#323232": "#262626", "#4B4B4B": "#3A3A3A", "#646464": "#4B4B4B", "#7D7D7D": "#646464",
        "#969696": "#7D7D7D", "#AFAFAF": "#969696", "#CDCDCD": "#AFAFAF"}
DIM = "#5A5A5A"
STRUCK = "#FFFFFF"             # a nodal line a hit has lit: white, no encoder's
RIBBON = 0.3                   # a nodal diameter's ribbon width: its stroke adds the rest
# The palette: the nodal lines take a lit, dim or struck entry; the rest is spare grey.
PALETTE = [(K, 1.0),                    # 0
           (E4, 0.9), (DIM, 0.6),       # 1, 2: a nodal circle, lit and dim
           (E4, 0.6), (DIM, 0.3),       # 3, 4: a nodal diameter (a 0.3 px ribbon), lit and dim
           (STRUCK, 0.9), (STRUCK, 0.6)]   # 5, 6: a circle and a diameter as a hit lights them
PALETTE += [(g, 1.0) for g in ["#191919", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696",
                                "#AFAFAF", "#CDCDCD", "#000000"]]
assert len(PALETTE) == 16


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def check(points):
    """The device's rules for an outline: 3..64 distinct points 1/8 px apart, a closing edge of
    at least 1/4 px, some area, and no turn sharper than SHARPEST degrees."""
    n = len(points)
    assert 3 <= n <= 64, n
    area = 0.0
    for i in range(n):
        (ax, ay), (bx, by), (cx, cy) = points[i - 1], points[i], points[(i + 1) % n]
        ux, uy, vx, vy = bx - ax, by - ay, cx - bx, cy - by
        assert abs(vx) >= 0.125 or abs(vy) >= 0.125, f"points {i} and {i + 1} merge"
        turn = math.degrees(math.atan2(abs(ux * vy - uy * vx), ux * vx + uy * vy))
        assert turn <= SHARPEST, f"turn of {turn:.1f} degrees at point {i}"
        area += ax * by - bx * ay
    (ax, ay), (bx, by) = points[-1], points[0]
    assert math.hypot(bx - ax, by - ay) >= 0.25, "closing edge"
    assert abs(area / 2) >= 0.5, f"area {area / 2:.2f}"


def poly(points):
    out = []
    for p in points:
        q = (round(p[0], 2), round(p[1], 2))
        if not out or q != out[-1]:
            out.append(q)
    if out[0] == out[-1]:
        out.pop()
    assert len(set(out)) == len(out), "repeated point"
    check(out)
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in out) + " Z"


def ellipse(x, y, z, r, n=48):
    """A level circle on screen."""
    cx, cy = P(x, y, z)
    return [(cx + r * RX_K * math.cos(2 * math.pi * i / n), cy + r * RY_K * math.sin(2 * math.pi * i / n)) for i in range(n)]


def side(x, y, z, r, h, n=40):
    """An upright cylinder's side, as the design draws it: straight up both edges from the
    foot's front arc, closed straight across at the top (the top's ellipse covers it)."""
    cx, cy = P(x, y, z)
    rx, ry = r * RX_K, r * RY_K
    arc = [(cx + rx * math.cos(math.pi - math.pi * i / n), cy + ry * math.sin(math.pi - math.pi * i / n)) for i in range(n + 1)]
    return [(cx - rx, cy - h)] + arc + [(cx + rx, cy - h)]


def ribbon(a, b, w):
    """A straight line from a to b on screen as a closed ribbon w wide."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    nx, ny = -dy / length * w / 2, dx / length * w / 2
    return [(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny), (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)]


def screen_circle(cx, cy, r, n=20):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


# ---------------------------------------------------------------- the artwork
STATIC = []    # (points, fill, stroke, width): the ring, under everything
NODES = []     # (name, points on screen, anchor on screen, fill, stroke, width)


def static(points, fill="none", stroke="none", w=0.5):
    STATIC.append((points, fill, stroke, w))


def node(name, points, anchor, fill="none", stroke="none", w=0.5):
    NODES.append((name, points, anchor, fill, stroke, w))


def centre(points):
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    return (round((min(xs) + max(xs)) / 2), round((min(ys) + max(ys)) / 2))


def build_art():
    # the ring the drum sits on: never moves
    static(side(CX, CY, 0, RING_R, RING_H, 48), "#646464", K, 0.5)
    static(ellipse(CX, CY, RING_H, RING_R, 60), "#7D7D7D", K, 0.5)
    # the body's bands, bottom up, each with its seven strap segments drawn over every band
    for k in range(BANDS):
        g = E1 if k % 2 == 0 else "#3A3A3A"
        pts = side(CX, CY, Z_BODY + BAND_H * k, R, BAND_H)
        node(f"band_{k}", pts, centre(pts), RAMP.get(g, g), K, 0.5)
    for k in range(BANDS):
        for j, a in enumerate(STRAPS):
            x, y = CX + R * math.cos(a), CY + R * math.sin(a)
            top, foot = P(x, y, Z_BODY + BAND_H * (k + 1)), P(x, y, Z_BODY + BAND_H * k)
            pts = ribbon(top, foot, 0.5)
            node(f"strap_{k}_{j}", pts, centre(pts), "none", K, 0.5)   # 0.5 wide, stroked 0.5: a 1 px line
    # TUNE's tuning blocks, wedged in the straps: left, right and top faces, drawn with TUNE at 0
    for j, a in enumerate(BLOCKS):
        x, y = CX + (R + 1) * math.cos(a), CY + (R + 1) * math.sin(a)
        x0, y0, z0, w, d, h = x - BLOCK / 2, y - BLOCK / 2, BLOCK_REF, BLOCK, BLOCK, BLOCK_H
        t = [P(x0, y0, z0 + h), P(x0 + w, y0, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0, y0 + d, z0 + h)]
        l = [P(x0, y0 + d, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0 + w, y0 + d, z0), P(x0, y0 + d, z0)]
        r = [P(x0 + w, y0, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0 + w, y0 + d, z0), P(x0 + w, y0, z0)]
        anchor = centre(t + l + r)
        node(f"block_{j}_left", l, anchor, "#7D7D7D", K, 0.5)
        node(f"block_{j}_right", r, anchor, RAMP["#7D7D7D"], K, 0.5)
        node(f"block_{j}_top", t, anchor, "#7D7D7D", K, 0.5)
    # the head, drawn on every band; everything on it shares its anchor, the head's centre
    head = P(CX, CY, Z_HEAD)
    node("head_side", side(CX, CY, Z_TOP, R, HEAD_H), head, "#646464", K, 0.5)
    node("head_top", ellipse(CX, CY, Z_HEAD, R, 60), head, "#7D7D7D", K, 0.5)
    # PRESS: the nodal lines, four diameters and two circles, lit or dim by the palette
    for i, (kind, p) in enumerate(RULES):
        if kind == "d":
            a = P(CX - R * 0.92 * math.cos(p), CY - R * 0.92 * math.sin(p), Z_HEAD)
            b = P(CX + R * 0.92 * math.cos(p), CY + R * 0.92 * math.sin(p), Z_HEAD)
            node(f"nodal_{i}", ribbon(a, b, RIBBON), head, "none", PALETTE[4][0], PALETTE[4][1])
        else:
            node(f"nodal_{i}", ellipse(CX, CY, Z_HEAD, R * p, 48), head, "none", PALETTE[2][0], PALETTE[2][1])
    # LOAD: the syahi, a black disc drawn at its widest (display.cpp scales it), and its rings
    node("syahi", ellipse(CX, CY, Z_HEAD, SYAHI_R0 + SYAHI_RINGS * SYAHI_STEP, 48), head, K, "none", 0.5)
    for k in range(SYAHI_RINGS):
        node(f"syahi_ring_{k}", ellipse(CX, CY, Z_HEAD, SYAHI_R0 + k * SYAHI_STEP, 32 if k < 2 else 40), head, "none", E3, 0.8)
    # DECAY: the resting palm's pad, drawn at its largest about its own centre
    pad = P(PAD[0], PAD[1], Z_HEAD)
    node("pad", ellipse(PAD[0], PAD[1], Z_HEAD, PAD_MAX, 36), pad, E2, K, 0.5)
    # SPOT: where the stroke lands, drawn at the head's centre (display.cpp slides it out)
    node("spot", screen_circle(head[0], head[1], SPOT_R), head, E4, K, 0.5)


def q10(v):
    return int(round(v * 1024))


def header_block():
    lines = ["<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
             '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    greys = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i in range(8):
        lines.append(f'<path id="{ID}_node_{i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
                     f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{greys[i]}" stroke="{greys[i]}" stroke-width="0.5"/>')
    xs = [5, 125, 245, 365, 65, 185, 305, 425] * 2
    ys = [10] * 4 + [17.4] * 4 + [33] * 8
    cols = (["#CBCBCB"] * 2 + ["#000000"] * 2) * 2 + ["#5A5A5A"] * 4 + ["#707070"] * 4
    sizes = [10] * 4 + [20] * 4 + [10] * 4 + [13] * 4
    for i in range(16):
        lines.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {fmt(xs[i])} {fmt(ys[i])})" fill="{cols[i]}" '
                     f'font-family="UniversNextPro-Light" font-size="{sizes[i]}px">-</text>')
    return lines


def build():
    build_art()
    header = 8
    lines = [f'<g id="{ID}_panel">'] + header_block()
    lines.append('<path d="M0,38 L480,38 L480,220 L0,220 Z" fill="#000000" stroke="none"/>')
    for pts, fill, stroke, w in STATIC:
        lines.append(f'<path d="{poly(pts)}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}" stroke-linejoin="round"/>')
    for k, (name, pts, (ax, ay), fill, stroke, w) in enumerate(NODES):
        rel = [(x - ax, y - ay) for x, y in pts]
        lines.append(f'<path id="{ID}_node_{header + k}" transform="matrix(1 0 0 1 640 320)" d="{poly(rel)}" '
                     f'fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}" stroke-linejoin="round"/>')
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    assert len(text) < 64 * 1024, len(text)
    (ROOT / "panel.svg.fragment").write_text(text)

    count = header + len(NODES)
    names = [n[0] for n in NODES]
    pad, head = P(PAD[0], PAD[1], Z_HEAD), P(CX, CY, Z_HEAD)
    h = ["#pragma once",
         "// Generated by tools/make_panel.py: the artwork's node numbers, where each node",
         "// is drawn (its anchor on screen, Q10 px) and the drum's measures.",
         "#include <opxy/types.hpp>", "", "namespace syahi {", "namespace art {",
         "using opxy::i32;", "using opxy::u32;",
         "const u32 header = 0;                 // the two-page header's eight bar nodes",
         f"const u32 nodes = {count};",
         f"const u32 first = {header};                  // the drum's first node",
         "// the palette: a nodal circle lit and dim, a nodal diameter lit and dim, each as a hit lights it",
         "const u32 circle_lit = 1, circle_dim = 2, diameter_lit = 3, diameter_dim = 4, circle_struck = 5, diameter_struck = 6;",
         "// the drum, in px: its bands and the body's top with every band on",
         f"const i32 bands = {BANDS}, band_h = {int(BAND_H)}, z_body = {int(Z_BODY)}, z_top = {int(Z_TOP)};",
         "// a tuning block's foot with TUNE at 0 (this far below the body's top), with TUNE full, and as drawn, px",
         f"const i32 block_drop = 10, block_lo = {int(BLOCK_LO)}, block_ref = {int(BLOCK_REF)};",
         "// the syahi's smallest ring and each ring's step, px * 10",
         f"const i32 syahi_r0 = {int(SYAHI_R0 * 10)}, syahi_step = {int(SYAHI_STEP * 10)}, syahi_rings = {SYAHI_RINGS};",
         "// the pad's radius at DECAY 0 and 100, px * 10",
         f"const i32 pad_max = {int(PAD_MAX * 10)}, pad_min = {int(PAD_MIN * 10)};",
         "// the spot's slide right per unit of its distance out (Q10 px), and that distance at SPOT 0 and 100 (Q15)",
         f"const i32 spot_run = {q10(2 * R * 0.6 * C30)}, spot_lo = {int(round(SPOT_LO * 32768))}, spot_hi = {int(round(SPOT_HI * 32768))};",
         f"const i32 head_x = {q10(head[0])}, head_y = {q10(head[1])}, pad_x = {q10(pad[0])}, pad_y = {q10(pad[1])};",
         "// each drum node's anchor on screen as drawn, Q10 px",
         f"const i32 at_x[{len(NODES)}] = {{" + ", ".join(str(q10(n[2][0])) for n in NODES) + "};",
         f"const i32 at_y[{len(NODES)}] = {{" + ", ".join(str(q10(n[2][1])) for n in NODES) + "};",
         "enum Node : u32 {"]
    h += [f"    {name} = {header + k}," for k, name in enumerate(names)]
    h += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(h))
    print(f"wrote panel.svg.fragment ({len(text)} bytes) and art.h: {count} nodes, {len(STATIC)} static paths")


if __name__ == "__main__":
    build()
