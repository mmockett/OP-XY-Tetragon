#!/usr/bin/env python3
"""Writes ../panel.svg.fragment, ../art.h and ../tests/bounds.h: CURIO's
screen, the cabinet with its drawer pulled open, in the stock isometric look.

The cabinet stands at the back with two closed drawers; the open drawer
holds a felt bed, a small drum on it and the release clamp on its stand that
drops a ball onto the drum's head. Every key's specimen shares the drawer;
the card holder on the drawer's front carries the selected specimen's
symbol. The grey is the encoder's: encoder 1's parts 323232 (the clamp, and
on the second row the damper on the head), encoder 2's 4B4B4B (the bounce
marks; the crate), encoder 3's 969696 (the drum's head and its rods; the
spot), encoder 4's CDCDCD (the ball; the grit).

 1 DROP    the clamp on its stand: higher, and the plumb line longer
 2 BOUNCE  a mark at the top of each bounce
 3 SKIN    the rods round the drum, 4 to 10
 4 BALL    the ball, five sizes
 5 DECAY   a felt damper on the head, slid toward the rim for a longer ring
 6 FLOOR   a crate on the felt, slid toward the drum for more floor
 7 SPOT    a ring on the head where the ball lands, centre to rim
 8 GRIT    grains on the head, 0 to 10

3D parts only slide or repeat: the stand is two overlapping lengths, the
upper one sliding with the clamp; the rods, marks, grains and balls are
copies placed or parked. The plumb line is the one reshaping outline: a
thin closed ribbon of four points. Every outline is one subpath of at most
64 distinct points, 1/8 px apart, with a closing edge of at least 1/4 px,
some area, and no turn sharper than SHARPEST degrees (the OP-XY strokes with
mitred joins and no mitre limit). Every 3D face carries a 0.5 px black
outline; the right face is one step darker than the top and left.

The drawing is the approved design, centred on x 240 in the stage under the
two-row header (its box's middle at y 130). Pure Python:

    python3 tools/make_panel.py
"""
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
ID = "curio"
PATHS, POINTS, PALETTE = 1, 4, 1
SHARPEST = 150.0   # degrees: the sharpest turn an outline may make

K = "#000000"
E1, E2, E3, E4 = "#323232", "#4B4B4B", "#969696", "#CDCDCD"
RAMP = {"#323232": "#262626", "#4B4B4B": "#3A3A3A", "#646464": "#4B4B4B", "#7D7D7D": "#646464",
        "#969696": "#7D7D7D", "#AFAFAF": "#969696", "#CDCDCD": "#AFAFAF"}
C30, S30 = math.cos(math.radians(30)), 0.5
OX, OY = 221.0, 117.0   # the design's origin (220, 92), moved (1, 25) to centre it


def P(x, y, z=0.0):
    """Plan point (x, y) at height z on screen: x down to the right, y down to the left."""
    return (OX + (x - y) * C30, OY + (x + y) * S30 - z)


# ---------------------------------------------------------------- the design's measures
DX, DY, DR = 70, 40, 20            # the drum: its centre and radius
BX, BY = DX - 4, DY - 4            # the plumb line and the ball
SXW, SYW = 30, 30                  # the stand
DROP0, DROP1 = 36, 74              # the clamp's height over the drawer's floor, DROP 0..100
HEAD = 24                          # the head's height
STAND = 42                         # each length of the stand
BALLS = [3.5, 4.25, 5.0, 5.75, 6.5]
PAD = (DX + 1, DY - 15, 6, 4, 1)   # the damper at DECAY 0: x, y, width, depth, height
PAD_SLIDE = (0, -4)                # plan px it slides by DECAY 100, out to the back rim
CRATE = (14, 52, 10, 10, 7)        # the crate: x at FLOOR 0, y, width, depth, height
CRATE_SLIDE = (16, 0)
SPOT = (DX + 2, DY + 4)            # the spot's ring at SPOT 0
SPOT_SLIDE = (13, 0)
RODS = range(4, 11)                # SKIN: 4 to 10 rods


def slide(sx, sy):
    """A plan move (sx, sy) on screen."""
    return ((sx - sy) * C30, (sx + sy) * S30)


def speckle(n, seed):
    """Fixed pseudo-random numbers, decided once: a control only adds or removes."""
    out, s = [], seed
    for _ in range(n):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        a = (s >> 8) / (1 << 23)
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        b = (s >> 8) / (1 << 23)
        out.append((a, b))
    return out


# ---------------------------------------------------------------- outlines
def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def check(points, name, reach=None):
    """The device's rules for an outline: distinct points 1/8 px apart, a closing edge of at
    least 1/4 px, area, and no turn sharper than SHARPEST degrees."""
    n = len(points)
    assert 3 <= n <= 64, (name, n)
    area = 0.0
    for i in range(n):
        (ax, ay), (bx, by), (cx, cy) = points[i - 1], points[i], points[(i + 1) % n]
        ux, uy, vx, vy = bx - ax, by - ay, cx - bx, cy - by
        assert math.hypot(vx, vy) >= 0.125, (name, "points merge at", i)
        turn = math.degrees(math.atan2(abs(ux * vy - uy * vx), ux * vx + uy * vy))
        assert turn <= SHARPEST, (name, f"turn of {turn:.1f} degrees at point {i}")
        area += ax * by - bx * ay
        if reach is not None:
            assert abs(bx) <= reach and abs(by) <= reach, (name, "reaches", bx, by)
    (ax, ay), (bx, by) = points[-1], points[0]
    assert math.hypot(bx - ax, by - ay) >= 0.25, (name, "closing edge")
    assert abs(area / 2) >= 0.25, (name, f"area {area / 2:.2f}")


def tidy(points):
    out = []
    for x, y in points:
        q = (round(x, 2), round(y, 2))
        if not out or q != out[-1]:
            out.append(q)
    if out[0] == out[-1]:
        out.pop()
    assert len(set(out)) == len(out), "repeated point"
    return out


def d_of(points):
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in points) + " Z"


def ellipse(cx, cy, rx, ry, n, a0=0.0, a1=2 * math.pi, closed=True):
    m = n if closed else n - 1
    return [(cx + rx * math.cos(a0 + (a1 - a0) * i / m), cy + ry * math.sin(a0 + (a1 - a0) * i / m)) for i in range(n)]


def rrect(x0, y0, x1, y1, r, n=3):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def box_faces(x0, y0, z0, w, d, h, g):
    """A box's left (y = y0 + d), right (x = x0 + w) and top faces, back to front."""
    t = [P(x0, y0, z0 + h), P(x0 + w, y0, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0, y0 + d, z0 + h)]
    l = [P(x0, y0 + d, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0 + w, y0 + d, z0), P(x0, y0 + d, z0)]
    r = [P(x0 + w, y0, z0 + h), P(x0 + w, y0 + d, z0 + h), P(x0 + w, y0 + d, z0), P(x0 + w, y0, z0)]
    return [("left", l, g), ("right", r, RAMP.get(g, g)), ("top", t, g)]


def cyl(x, y, z, r, h, g, n=48):
    """An upright cylinder (side in the ramp's darker grey, top in g): its side as the hull of
    its two ellipses' near halves, then its top."""
    cx, cy = P(x, y, z)
    rx, ry = r * 1.2247, r * 0.7071
    side = [(cx - rx, cy - h)] + ellipse(cx, cy, rx, ry, n // 2 + 1, math.pi, 0.0, closed=False) + [(cx + rx, cy - h)]
    return [("side", side, RAMP.get(g, g)), ("top", ellipse(cx, cy - h, rx, ry, n), g)]


# ---------------------------------------------------------------- the artwork
out = [f'<g id="{ID}_panel">', '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
nodes = {}         # name -> index
order = []         # node names by index (after the plumb line)
bounds = {}        # index -> (left, top, right, bottom) about its origin


def static(points, fill, stroke=K, w=0.5, name="static"):
    pts = tidy(points)
    check(pts, name)
    out.append(f'<path d="{d_of(pts)}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')


def static_circle(cx, cy, r, fill, stroke=K, w=0.5):
    out.append(f'<circle cx="{fmt(cx)}" cy="{fmt(cy)}" r="{fmt(r)}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')


def node(name, points, origin, fill, stroke=K, w=0.5):
    """A node: its outline given on screen and written about its origin, in the artwork's order."""
    ox, oy = origin
    pts = tidy([(x - ox, y - oy) for x, y in points])
    check(pts, name, reach=64)
    i = len(order) + PATHS
    order.append(name)
    nodes[name] = i
    bounds[i] = (min(x for x, _ in pts), min(y for _, y in pts), max(x for x, _ in pts), max(y for _, y in pts))
    out.append(f'<path id="{ID}_node_{i}" transform="matrix(1 0 0 1 {fmt(ox)} {fmt(oy)})" d="{d_of(pts)}" '
               f'fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')


def node_circle(name, cx, cy, r, origin, fill, stroke=K, w=0.5):
    """A round node: (cx, cy) its centre from its origin."""
    i = len(order) + PATHS
    order.append(name)
    nodes[name] = i
    bounds[i] = (cx - r, cy - r, cx + r, cy + r)
    out.append(f'<circle id="{ID}_node_{i}" transform="matrix(1 0 0 1 {fmt(origin[0])} {fmt(origin[1])})" '
               f'cx="{fmt(cx)}" cy="{fmt(cy)}" r="{fmt(r)}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')


def box_node(name, x0, y0, z0, w, d, h, g, origin):
    for face, pts, fill in box_faces(x0, y0, z0, w, d, h, g):
        node(f"{name}_{face}", pts, origin, fill)


def box_static(x0, y0, z0, w, d, h, g, faces=("left", "right", "top")):
    for face, pts, fill in box_faces(x0, y0, z0, w, d, h, g):
        if face in faces:
            static(pts, fill, name=f"box {face}")


def symbol(kind):
    """Each specimen's symbol, about 6 px, one outline about (0, 0)."""
    def e(rx, ry, n=16, rot=0.0):
        return [(x * math.cos(rot) - y * math.sin(rot), x * math.sin(rot) + y * math.cos(rot)) for x, y in ellipse(0, 0, rx, ry, n)]
    if kind == "cask":       # a barrel: bulging sides, flat ends
        right = [(1.5 + 0.6 * math.cos(math.pi * (i / 6 - 0.5)), -2.6 + 5.2 * i / 6) for i in range(7)]
        return right + [(-x, y) for x, y in reversed(right)]
    if kind == "stone":      # a bar
        return rrect(-3.4, -1.2, 3.4, 1.2, 0.5, 2)
    if kind == "drip":       # a drop
        return [(0, -2.8)] + [(1.5 * math.cos(math.radians(d)), 0.9 + 1.5 * math.sin(math.radians(d))) for d in range(-30, 211, 20)]
    if kind == "ice":        # a crystal
        return [(2.4 * math.cos(math.radians(60 * i + 30)), 2.4 * math.sin(math.radians(60 * i + 30))) for i in range(6)]
    if kind == "sheet":      # a hung sheet
        return [(-1.8, -2.6), (1.8, -2.6), (2.0, 2.6), (-1.6, 2.6)]
    if kind == "spark":      # a four-pointed spark
        return [((2.8 if i % 2 == 0 else 1.25) * math.cos(math.pi * i / 4 - math.pi / 2),
                 (2.8 if i % 2 == 0 else 1.25) * math.sin(math.pi * i / 4 - math.pi / 2)) for i in range(8)]
    if kind == "cork":       # a cork, wider at its top
        return [(-1.6, -2.2), (1.6, -2.2), (1.1, 2.2), (-1.1, 2.2)]
    if kind == "cicada":     # wings folded over the body
        return [(0, -2.8), (1.4, -1.6), (2.0, 1.6), (0, 2.8), (-2.0, 1.6), (-1.4, -1.6)]
    if kind == "ratchet":    # a cog
        pts = []
        for i in range(8):
            a = 2 * math.pi * i / 8
            for da, r in ((-0.17, 2.6), (0.17, 2.6), (0.24, 1.8), (0.55, 1.8)):
                pts.append((r * math.cos(a + da), r * math.sin(a + da)))
        return pts
    if kind == "pod":        # a seed pod
        return e(1.5, 2.6, 16)
    if kind == "match":      # a stick with its head
        head = [(0.8 * math.cos(math.radians(d)), -1.9 + 0.8 * math.sin(math.radians(d))) for d in list(range(64, -245, -31)) + [-244]]
        return [(0.35, 2.8)] + head + [(-0.35, 2.8)]
    if kind == "glass":      # a goblet
        return [(-2.0, -2.8), (2.0, -2.8), (1.6, -0.9), (0.35, 0.2), (0.35, 2.0), (1.6, 2.8), (-1.6, 2.8),
                (-0.35, 2.0), (-0.35, 0.2), (-1.6, -0.9)]
    if kind == "bell jar":   # a bell
        dome = [(1.4 * math.cos(math.radians(d)), -1.0 + 1.6 * math.sin(math.radians(d))) for d in range(-180, 1, 30)]
        return dome + [(1.6, 1.6), (2.4, 2.4), (-2.4, 2.4), (-1.6, 1.6)]
    if kind == "chalk":      # a stick of chalk, at a slant
        return [(x * math.cos(0.6) - y * math.sin(0.6), x * math.sin(0.6) + y * math.cos(0.6))
                for x, y in rrect(-0.8, -3.0, 0.8, 3.0, 0.3, 1)]
    if kind == "roar":       # a slat
        return e(3.2, 1.0, 18, -0.35)
    raise KeyError(kind)


MODELS = ["ball", "cask", "stone", "drip", "ice", "sheet", "spark", "cork",
          "cicada", "ratchet", "pod", "match", "glass", "bell jar", "chalk", "roar"]
slides = {}

# the cabinet behind, two closed drawers in it
box_static(0, -6, 0, 120, 6, 70, "#646464")
for z in (48, 26):
    box_static(6, -9, z, 108, 3, 18, "#7D7D7D")
    static_circle(*P(60, -9, z + 9), 2, "#AFAFAF")
# the open drawer: its floor, far side and the felt bed
box_static(4, 0, 0, 112, 80, 4, "#646464")
box_static(4, 0, 4, 4, 80, 12, "#7D7D7D")
box_static(10, 6, 4, 100, 68, 2, "#323232")
# FLOOR: the crate on the felt, sliding toward the drum
cx0, cy0, cw, cd, ch = CRATE
crate_o = P(cx0 + cw / 2, cy0 + cd / 2, 6 + ch / 2)
box_node("crate", cx0, cy0, 6, cw, cd, ch, E2, crate_o)
slides["crate"] = slide(*CRATE_SLIDE)
# the drum: its shell, then its head (encoder 3)
for part, pts, fill in cyl(DX, DY, 6, DR, 16, "#7D7D7D"):
    static(pts, fill, name=f"shell {part}")
for part, pts, fill in cyl(DX, DY, 22, DR + 1, 2, E3, n=56):
    static(pts, fill, name=f"head {part}")
# DECAY: the damper on the head
px0, py0, pw, pd, ph = PAD
pad_o = P(px0 + pw / 2, py0 + pd / 2, HEAD + ph / 2)
box_node("pad", px0, py0, HEAD, pw, pd, ph, E1, pad_o)
slides["pad"] = slide(*PAD_SLIDE)
# SPOT: a ring on the head
spot_o = P(SPOT[0], SPOT[1], HEAD)
node("spot", ellipse(spot_o[0], spot_o[1], 1.8 * 1.2247, 1.8 * 0.7071, 20), spot_o, "none", "#3A3A3A", 0.6)
slides["spot"] = slide(*SPOT_SLIDE)
# SKIN: the rods, ten copies about their tops; their tops for each count
rod_tops = []
for n in RODS:
    row = []
    for k in range(10):
        a = math.pi * (0.1 + 0.8 * k / max(1, n - 1))
        row.append(P(DX + DR * math.cos(a), DY + DR * math.sin(a), 22) if k < n else (0.0, 0.0))
    rod_tops.append(row)
for k in range(10):
    tx, ty = rod_tops[-1][k]
    node(f"rod_{k}", [(tx - 0.15, ty), (tx - 0.15, ty + 14), (tx + 0.15, ty + 14), (tx + 0.15, ty)], (tx, ty), "#AFAFAF", "#AFAFAF", 0.5)
# GRIT: grains on the head
grains = [P(DX + (a - 0.5) * 24, DY + (b - 0.5) * 24, HEAD) for a, b in speckle(10, 9)]
for k, g in enumerate(grains):
    node_circle(f"grit_{k}", 0, 0, 0.8, g, E4, "none", 0.5)
# DROP: the clamp's base, its stand (a fixed lower length, the upper one sliding) and the clamp
box_static(SXW - 3, SYW - 3, 6, 6, 6, 2, "#7D7D7D")
box_static(SXW - 1, SYW - 1, 8, 2, 2, STAND, "#7D7D7D", faces=("left", "right"))
# the upper length at DROP 0: its top at the drop plus 14
stand_o = P(SXW, SYW, DROP0 + 14 - STAND / 2)
box_node("stand", SXW - 1, SYW - 1, DROP0 + 14 - STAND, 2, 2, STAND, "#7D7D7D", stand_o)
arm_o = P(SXW + 13, SYW, DROP0 + 2)
box_node("arm", SXW, SYW - 2, DROP0, 26, 4, 4, E1, arm_o)
# the plumb line (node 0, reshaping): its foot on the head
plumb_o = P(BX, BY, HEAD)
L0 = DROP0 - HEAD
plumb_pts = [(0.125, 0), (-0.125, 0), (-0.125, -L0), (0.125, -L0)]
check(plumb_pts, "plumb")
out.append(f'<path id="{ID}_node_0" transform="matrix(1 0 0 1 {fmt(plumb_o[0])} {fmt(plumb_o[1])})" d="{d_of(plumb_pts)}" '
           f'fill="none" stroke="#3A3A3A" stroke-width="0.4"/>')
# BOUNCE: a mark at each bounce's top, five copies; their places at the head's height
marks = [P(BX + 6 * (k + 1), BY, HEAD) for k in range(5)]
for k, (mx, my) in enumerate(marks):
    node(f"mark_{k}", [(mx + x, my + y) for x, y in rrect(-2.5, -1, 2.5, 1, 1, 3)], (mx, my), E2, "none", 0.5)
# BALL: five sizes, each a ball and its glint about the ball's foot, at rest over the head
for k, br in enumerate(BALLS):
    rr = br * 1.15
    foot = (plumb_o[0], plumb_o[1] - 0.55 * L0)
    node_circle(f"ball_{k}", 0, -rr, rr, foot, E4, K, 0.5)
    node(f"glint_{k}", [(foot[0] + x, foot[1] + y) for x, y in ellipse(-rr * 0.35, -rr * 1.35, rr * 0.28, rr * 0.18, 12)],
         foot, "#E6E6E6", "none", 0.5)
# the drawer's front, its card holder with the specimen's symbol, and its knob
box_static(4, 80, 0, 112, 4, 18, "#7D7D7D")
hx, hy = P(60, 84, 11)
static(rrect(hx - 9, hy - 5, hx + 9, hy + 3, 1.5, 3), "#AFAFAF")
sym_o = (hx, hy - 1)
for m in MODELS:
    if m == "ball":
        node_circle("symbol_ball", 0, 0, 2.2, sym_o, "none", "#3A3A3A", 0.9)
    else:
        node(f"symbol_{m.replace(' ', '_')}", [(sym_o[0] + x, sym_o[1] + y) for x, y in symbol(m)], sym_o, "none", "#3A3A3A", 0.9)
static_circle(*P(96, 84, 9), 2.6, "#AFAFAF")

# the header's eight bar nodes and sixteen text slots
header = len(order) + PATHS
greys = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
for i, g in enumerate(greys):
    out.append(f'<path id="{ID}_node_{header + i}" transform="matrix(1 0 0 1 {60 * i} 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="{g}" stroke="{g}" stroke-width="0.5"/>')
node_count = header + 8
xs = [5, 125, 245, 365]
for i in range(16):
    col = i % 4
    if i < 4:
        x, y, size, fill = xs[col], 10, 10, "#CBCBCB" if col < 2 else "#000000"
    elif i < 8:
        x, y, size, fill = xs[col] + 60, 17.4, 20, "#CBCBCB" if col < 2 else "#000000"
    elif i < 12:
        x, y, size, fill = xs[col], 33, 10, "#5A5A5A"
    else:
        x, y, size, fill = xs[col] + 60, 33, 13, "#707070"
    out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {x} {y})" fill="{fill}" font-family="UniversNextPro-Light" font-size="{size}px">-</text>')
out.append(f'<circle id="{ID}_palette_0" cx="640" cy="0" r="1" fill="#000000" stroke="#000000" stroke-width="0.5"/>')
out.append("</g>")


def main():
    svg = "\n".join(out) + "\n"
    assert not re.search(r"(d|r|cx|cy)=\"[^\"]*\d[eE]", svg), "exponent in the artwork"
    assert len(svg) <= 65536, f"artwork is {len(svg)} bytes, over 64 KB"
    (ROOT / "panel.svg.fragment").write_text(svg)

    def q(v):
        return round(v * 1024)

    def arr(vals):
        return "{" + ", ".join(str(v) for v in vals) + "}"

    enum = ",\n".join(f"    {name} = {i}" for i, name in enumerate(order, PATHS))
    rods_x = ",\n    ".join(arr(q(p[0]) for p in row) for row in rod_tops)
    rods_y = ",\n    ".join(arr(q(p[1]) for p in row) for row in rod_tops)
    s = slides
    text = f"""#pragma once
// Written by tools/make_panel.py: edit the drawing there, then run it.
// The cabinet's open drawer: its nodes, and where each moving part stands on
// screen, Q10 px (1024 a pixel).
#include <opxy/types.hpp>

namespace curio {{
namespace art {{
using opxy::i32;
const unsigned node_count = {node_count}, header_node = {header};
enum Node : unsigned {{
    plumb = 0,
{enum}
}};
// Sixteen symbols for the card holder, one a specimen, in the kit's order.
const unsigned symbol_first = symbol_ball;
// The plumb line's foot on the head, where the ball lands; the drop over the
// head at DROP 0, and how much more at 100. The ball rests 0.55 of the way up.
const i32 plumb_x = {q(plumb_o[0])}, plumb_y = {q(plumb_o[1])};
const i32 drop_lo = {q(DROP0 - HEAD)}, drop_span = {q(DROP1 - DROP0)};
// The clamp and the stand's upper length at DROP 0: they rise with the drop.
const i32 arm_x = {q(arm_o[0])}, arm_y = {q(arm_o[1])};
const i32 stand_x = {q(stand_o[0])}, stand_y = {q(stand_o[1])};
// The damper (DECAY), the crate (FLOOR) and the spot (SPOT) at 0, and how far each slides by 100.
const i32 pad_x = {q(pad_o[0])}, pad_y = {q(pad_o[1])}, pad_dx = {q(s['pad'][0])}, pad_dy = {q(s['pad'][1])};
const i32 crate_x = {q(crate_o[0])}, crate_y = {q(crate_o[1])}, crate_dx = {q(s['crate'][0])}, crate_dy = {q(s['crate'][1])};
const i32 spot_x = {q(spot_o[0])}, spot_y = {q(spot_o[1])}, spot_dx = {q(s['spot'][0])}, spot_dy = {q(s['spot'][1])};
// The rods' tops round the head, for 4 to 10 rods.
const i32 rod_x[7][10] = {{
    {rods_x}}};
const i32 rod_y[7][10] = {{
    {rods_y}}};
// The grains on the head; the bounce marks' places at the head's height (each
// raised by its bounce's height).
const i32 grit_x[10] = {arr(q(p[0]) for p in grains)};
const i32 grit_y[10] = {arr(q(p[1]) for p in grains)};
const i32 mark_x[5] = {arr(q(p[0]) for p in marks)};
const i32 mark_y[5] = {arr(q(p[1]) for p in marks)};
// The symbol on the card holder.
const i32 symbol_x = {q(sym_o[0])}, symbol_y = {q(sym_o[1])};
}}
}}
"""
    (ROOT / "art.h").write_text(text)
    rows_b = []
    for i in range(header):
        if i < PATHS:
            rows_b.append("    {0, 0, 0, 0},   // the plumb line: its points are in the frame")
        else:
            rows_b.append("    {" + ", ".join(f"{v:.2f}f" for v in bounds[i]) + f"}},   // {i} {order[i - PATHS]}")
    (ROOT / "tests" / "bounds.h").write_text(f"""#pragma once
// Written by tools/make_panel.py: each node's outline as authored, its reach
// about the node's origin in px: left, top, right, bottom.
namespace curio {{
namespace bounds {{
const float node[{header}][4] = {{
{chr(10).join(rows_b)}
}};
}}
}}
""")
    d = (ROOT / "definition.json").read_text()
    d = re.sub(r'"nodes": \d+', f'"nodes": {node_count}', d)
    d = re.sub(r'"palette": \d+', f'"palette": {PALETTE}', d)
    d = re.sub(r'"paths": \d+', f'"paths": {PATHS}', d)
    d = re.sub(r'"points": \d+', f'"points": {POINTS}', d)
    (ROOT / "definition.json").write_text(d)
    print(f"{node_count} nodes ({len(order)} parts, the plumb line, 8 header bars), {len(svg)} bytes of artwork")


if __name__ == "__main__":
    main()
