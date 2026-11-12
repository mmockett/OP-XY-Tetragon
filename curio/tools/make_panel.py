#!/usr/bin/env python3
"""Writes ../panel.svg.fragment and ../art.h: CURIO's sixteen specimens, each
a real object drawn in fine outline as nodes the screen moves, alone and
centred below the header.

Every part is one node: a single closed outline of at most 64 points (the
OP-XY's loader takes one subpath per path), every point distinct and at least
1/8 px from its neighbour, each line a thin closed ribbon (a line traced out
and back draws streaks on the device), the closing edge at least 1/4 px, and
every coordinate within 64 px of the node's origin. A part wider than that is
authored at half size and drawn at scale 2 (k=2). check() asserts all of it on
the numbers as written. art.h gives each node its pivot, base scale, what each
control does to it and how a hit moves it; display.cpp reads it.

Parts are drawn back to front in the order they're listed: static dark fills
(#141414, #242424) hide what stands behind them. The four reshaped outlines
are drawn first, behind everything, so nothing filled may cover them.

    python3 tools/make_panel.py
"""
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
ID = "curio"
PATHS, POINTS = 4, 64
PALETTE = 9
SHARPEST = 150.0   # degrees: the sharpest turn an outline may make
SHARP = []         # outlines that turn sharper, reported and refused

# Greys and weights, by role (few, used consistently): brightest is what the
# sound is doing; the object's body a step down; its structure, then engraving.
# Lines are thin, as stock Drumline's: an outline draws at most 1 px, a line
# (a thin closed ribbon, RIBBON wide) about 1.5 px across, its band and its
# stroke together. The dimmest greys are a step brighter than a 2 px line
# needs, so a 1 px line that falls between pixels still shows.
HERO = ("#E2E2E2", 1.0)        # the specimen itself, what moves
BODYL = ("#BDBDBD", 1.0)       # the specimen's body
DETAIL = ("#A6A6A6", 0.8)      # its details
STRUCT = ("#8A8A8A", 0.9)      # stands, frames, holders
ENGR = ("#787878", 0.6)        # engraving, grain, scales
DIM = ("#707070", 0.8)         # far things, echoes
GLINT = ("#C8C8C8", 0.8)       # one glint on glass or steel
RIBBON = 0.6                   # a line's band, px: with its stroke, 1.2 to 1.5 px across
BODY, BODY2 = "#141414", "#242424"

# Control effects: kinds. Values: px for dx/dy, Q10 (1024 = 1) for scale, tenths of a degree for rot.
DX, DY, SCALE, ROT = 1, 2, 3, 4
# Hit motions.
(KICK_DX, KICK_DY, KICK_ROT, RING_DX, RING_DY, RING_ROT, FALL, RISE, FLASH, SWING,
 STRIKE, TIP, ORBIT, GHOST) = range(1, 15)

parts = []
MODELS = ["ball", "cask", "stone", "drip", "ice", "sheet", "spark", "cork",
          "cicada", "ratchet", "pod", "match", "glass", "bell jar", "chalk", "roar"]


def P(model, name, pivot, shape, style=HERO, fill=None, fx=(), hit=None, hide=None, k=1):
    parts.append(dict(model=model, name=name, pivot=pivot, shape=shape, style=style, fill=fill,
                      fx=list(fx), hit=hit, hide=hide, k=k))


# ----------------------------------------------------------------- shapes
def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def rrect(x0, y0, x1, y1, r, n=2):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def ell(cx, cy, rx, ry, n=24, a0=0):
    return [(cx + rx * math.cos(math.radians(a0 + 360 * i / n)), cy + ry * math.sin(math.radians(a0 + 360 * i / n))) for i in range(n)]


def arc(cx, cy, rx, ry, a0, a1, n=16):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def simplify(pts, tol=0.3):
    """Douglas-Peucker: drops the points of a polyline that lie within tol px of
    the line through their neighbours (a smooth curve keeps its shape at 1:1)."""
    if len(pts) < 3:
        return list(pts)
    (x0, y0), (x1, y1) = pts[0], pts[-1]
    L = math.hypot(x1 - x0, y1 - y0)
    far, at = -1.0, 0
    for i in range(1, len(pts) - 1):
        x, y = pts[i]
        d = abs((x1 - x0) * (y0 - y) - (x0 - x) * (y1 - y0)) / L if L else math.hypot(x - x0, y - y0)
        if d > far:
            far, at = d, i
    if far <= tol:
        return [pts[0], pts[-1]]
    return simplify(pts[:at + 1], tol)[:-1] + simplify(pts[at:], tol)


def ribbon(pts, w=RIBBON):
    """A line as a thin closed band: out along one edge, back along the other.
    No band is wider than RIBBON, so every line draws about 1.5 px across
    (heavier members, a stem or the tongs, are outlines of their own)."""
    w = min(w, RIBBON)
    pts = simplify(pts)
    a, b = [], []
    n = len(pts)
    for i, (x, y) in enumerate(pts):
        x0, y0 = pts[max(i - 1, 0)]
        x1, y1 = pts[min(i + 1, n - 1)]
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L * w / 2, dx / L * w / 2
        a.append((x + nx, y + ny))
        b.append((x - nx, y - ny))
    # Started one point along an edge, so the closing edge is a long one, not the band's end.
    return a[1:] + b[::-1] + a[:1]


def curve(pts, n=4):
    """A smooth curve through an open polyline's points (Catmull-Rom), n
    steps a span: for things that are round in life. The outline's own
    simplify() drops what the eye can't place at 1:1."""
    pts = list(pts)
    q = [pts[0]] + pts + [pts[-1]]
    out = []
    for i in range(1, len(q) - 2):
        p0, p1, p2, p3 = q[i - 1], q[i], q[i + 1], q[i + 2]
        for j in range(n):
            t = j / n
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[a] + (-p0[a] + p2[a]) * t + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t2
                                    + (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * t3) for a in (0, 1)))
    out.append(pts[-1])
    return out


def loop(pts, n=4):
    """A smooth closed curve through a closed outline's points (Catmull-Rom)."""
    pts = list(pts)
    m = len(pts)
    out = []
    for i in range(m):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[(i + 1) % m], pts[(i + 2) % m]
        for j in range(n):
            t = j / n
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[a] + (-p0[a] + p2[a]) * t + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t2
                                    + (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * t3) for a in (0, 1)))
    return out


def zig(x0, y0, x1, y1, n, amp):
    pts = []
    L = math.hypot(x1 - x0, y1 - y0)
    nx, ny = -(y1 - y0) / L, (x1 - x0) / L
    for i in range(n + 1):
        t = i / n
        x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        s = amp * (1 if i % 2 else -1) * (0 if i in (0, n) else 1)
        pts.append((x + nx * s, y + ny * s))
    return pts


def mirror(half):
    """A symmetric outline from its right half, top to bottom; points on the axis aren't doubled."""
    left = [(-x, y) for x, y in reversed(half) if abs(x) > 1e-6]
    return list(half) + left


def band(a, b):
    """The closed band between two polylines running the same way."""
    return list(a) + list(b)[::-1]


def comb(x0, y0, y1, ticks, w=0.7):
    """A vertical rule from y0 down to y1 with ticks (y, length) to its right, as one outline."""
    pts = [(x0, y0), (x0, y1), (x0 + w, y1)]
    for y, L in sorted(ticks, reverse=True):
        pts += [(x0 + w, y + w / 2), (x0 + w + L, y + w / 2), (x0 + w + L, y - w / 2), (x0 + w, y - w / 2)]
    pts.append((x0 + w, y0))
    return pts


def stick(x0, y0, x1, y1, w0, w1=None):
    """A straight member from (x0, y0) to (x1, y1), w0 wide at the start and w1 at the end."""
    w1 = w0 if w1 is None else w1
    L = math.hypot(x1 - x0, y1 - y0)
    nx, ny = -(y1 - y0) / L, (x1 - x0) / L
    return [(x0 + nx * w0 / 2, y0 + ny * w0 / 2), (x1 + nx * w1 / 2, y1 + ny * w1 / 2),
            (x1 - nx * w1 / 2, y1 - ny * w1 / 2), (x0 - nx * w0 / 2, y0 - ny * w0 / 2)]


def T(pts, dx, dy):
    return [(x + dx, y + dy) for x, y in pts]


def S(pts, k):
    return [(x * k, y * k) for x, y in pts]


def R(pts, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(x * c - y * s, x * s + y * c) for x, y in pts]


def star(n, r0, r1, a0=-90):
    return [((r0 if i % 2 == 0 else r1) * math.cos(math.radians(a0 + 180 * i / n)),
             (r0 if i % 2 == 0 else r1) * math.sin(math.radians(a0 + 180 * i / n))) for i in range(2 * n)]


def C(r, cx=0.0, cy=0.0):
    return ("circle", r, cx, cy)


PX_HITS = (KICK_DX, KICK_DY, RING_DX, RING_DY, RISE, FALL)


def zoom(model, anchor, k, shift=(0, 0)):
    """Draws a specimen's parts k times larger about anchor (a point on the
    line it stands on), then moves them by shift: pivots, outlines, px moves and px hits."""
    ax, ay = anchor
    for prt in parts:
        if prt["model"] != model:
            continue
        x, y = prt["pivot"]
        prt["pivot"] = (ax + (x - ax) * k + shift[0], ay + (y - ay) * k + shift[1])
        sh = prt["shape"]
        if isinstance(sh, tuple):
            prt["shape"] = (sh[0], sh[1] * k, sh[2] * k, sh[3] * k)
            reach = max(abs(sh[2]), abs(sh[3])) * k + sh[1] * k
        else:
            prt["shape"] = S(sh, k)
            reach = max(max(abs(a), abs(b)) for a, b in prt["shape"])
        prt["fx"] = [(c, kd, round(lo * k), round(hi * k)) if kd in (DX, DY) else (c, kd, lo, hi) for c, kd, lo, hi in prt["fx"]]
        if prt["hit"] and prt["hit"][0] in PX_HITS and abs(prt["hit"][1]) > 2:
            prt["hit"] = (prt["hit"][0], round(prt["hit"][1] * k))
        grows = [(c, lo, hi) for c, kd, lo, hi in prt["fx"] if kd == SCALE and max(lo, hi) > 1024]
        if reach > 64 and grows and not isinstance(sh, tuple):
            # Too far from its pivot to draw at half size and still grow: the
            # pivot moves to the outline's middle, and a move by the same
            # control keeps it where scaling about the old pivot put it.
            assert len(grows) == 1 and not any(kd == ROT for _, kd, _, _ in prt["fx"]), prt["name"]
            c, lo, hi = grows[0]
            xs, ys = [a for a, _ in prt["shape"]], [b for _, b in prt["shape"]]
            d = (0.0, round((min(ys) + max(ys)) / 2))
            prt["shape"] = T(prt["shape"], -d[0], -d[1])
            prt["pivot"] = (prt["pivot"][0] + d[0], prt["pivot"][1] + d[1])
            prt["fx"].append((c, DY, round(d[1] * (lo / 1024 - 1)), round(d[1] * (hi / 1024 - 1))))
            reach = max(max(abs(a), abs(b)) for a, b in prt["shape"])
        if reach > 64 and prt["k"] == 1:
            prt["k"] = 2


# ------------------------------------------------------------ the specimens
# Each is drawn standing on a line at y 197 about x 185, then moved to the
# screen's middle (OFFSET, below). Round things are drawn with smooth curves:
# arcs and ellipses with enough points, curve() and loop() through a few.

# --- BALL: a drop test. A ball held in a retort stand's clamp over a rope-tensioned
# field drum on its stand; a crate under the drum for the floor.
m = 0
D, SB = 200, 164                     # the drum's centre, its shell's bottom rim
DROP = [(0, DY, 0, -62), (6, DX, 0, 30)]
BALLFX = DROP + [(3, SCALE, 1300, 720)]
SKIN = [(2, SCALE, 1024, 760)]
# The floor: a sounding crate under the drum, growing from a low box to a stage.
P(m, "crate", (D, 196), rect(-58, -24, 58, 0), STRUCT, BODY, fx=[(5, SCALE, 300, 1024)], hide=(5, 3, 0))
P(m, "planks", (D, 196), ribbon([(-57.5, -8), (57.5, -8), (57.5, -16), (-57.5, -16)], 0.8), ENGR, fx=[(5, SCALE, 300, 1024)], hide=(5, 3, 0))
P(m, "leg l", (D - 63, 180), [(-1, -16), (2.5, -16), (-4, 16), (-8, 16)], STRUCT, BODY2, fx=[(2, DX, 0, 16)])
P(m, "leg r", (D + 63, 180), [(1, -16), (-2.5, -16), (4, 16), (8, 16)], STRUCT, BODY2, fx=[(2, DX, 0, -16)])
# The retort stand: its foot and rod as one casting.
P(m, "stand", (60, 126), T([(-26, 0), (-24, -5), (-8, -5), (-5, -13), (-1.5, -13), (-1.5, -140), (1.5, -140), (1.5, -13), (5, -13), (8, -5), (24, -5), (26, 0)], 0, 70), STRUCT, BODY2, k=2)
P(m, "rule", (66, 90), comb(0, -44, 44, [(y, 5) for y in range(-40, 41, 20)]), ENGR)
P(m, "arm", (108, 112), rect(-92, -1.2, 92, 1.2), STRUCT, BODY2, fx=DROP, k=2)
P(m, "boss", (60, 112), [(-4, -5), (4, -5), (4, 5), (-4, 5), (-4, 1.2), (-9, 1.2), (-9, 3), (-12, 3), (-12, -3), (-9, -3), (-9, -1.2), (-4, -1.2)], STRUCT, BODY2, fx=DROP[:1])
shell = [(-64, -24), (-64, 0)] + arc(0, 0, 64, 11, 165, 15, 16) + [(64, 0), (64, -24)] + arc(0, -24, 64, 11, 15, 165, 16)
P(m, "shell", (D, SB), shell, DETAIL, fx=SKIN)
xs = [-58 + i * 116 / 12 for i in range(13)]
rope = [(x, (-19.5 if i % 2 == 0 else -4.5) + 11 * math.sqrt(1 - (x / 64.5) ** 2)) for i, x in enumerate(xs)]
P(m, "rope", (D, SB), ribbon(rope, 0.7), DETAIL, fx=SKIN)
P(m, "hoop low", (D, SB), band(arc(0, -3.5, 64, 11, 180, 0, 16), arc(0, 0, 64, 11, 180, 0, 16)), STRUCT, fx=SKIN)
P(m, "hoop top", (D, SB), band(arc(0, -24, 64, 11, 180, 0, 16), arc(0, -20.5, 64, 11, 180, 0, 16)), STRUCT, fx=SKIN)
P(m, "chain", (D - 8, 138), ribbon(zig(-34, 0, 34, 0, 10, 1.4), 0.6), DETAIL, fx=[(7, SCALE, 300, 1000), (2, DY, 0, 6)], hit=(RING_DY, 1), hide=(7, 3, 0))
for i, r in enumerate((12, 22)):
    P(m, f"ring {i}", (D, 140), ell(0, 0, r, r * 0.18, 28), DETAIL, fx=[(4, SCALE, 400, 1100), (6, DX, 0, 30), (2, DY, 0, 6)], hide=(4, 300 + i * 1600, 0))
for i, (dx, h) in enumerate(((34, 30), (58, 14), (76, 6))):
    P(m, f"ghost {i}", (D + dx, 132), C(4.5), DIM, fx=[(1, DY, 0, -h), (1, DX, -int(dx * 0.4), 0), (6, DX, 0, 30)], hide=(1, 400 + i * 900, 0))
P(m, "jaw", (D, 112), ribbon(arc(0, 12, 10.5, 10.5, 200, 340, 14), 1.3), STRUCT, fx=BALLFX)
P(m, "ball", (D, 112), C(8, 0, 12), HERO, BODY, fx=BALLFX, hit=(FALL, 1))
P(m, "seam", (D, 112), ribbon([(x, 12 + 2.6 * math.sin(math.pi * x / 6.4)) for x in [-6.4 + 12.8 * i / 12 for i in range(13)]], 0.7), DETAIL, fx=BALLFX, hit=(FALL, 1), hide=(3, 1640, 1))
P(m, "glint", (D, 112), ribbon(arc(0, 12, 5.2, 5.2, 205, 255, 7), 0.9), GLINT, fx=BALLFX, hit=(FALL, 1), hide=(3, 2050, 0))

# --- CASK: a cooper's cask on end on a stillage, cut away to show the liquid,
# four iron hoops, a brass spigot, a cooper's mallet; the cellar's vault behind.
m = 1
CX0, CY0 = 170, 134
SIZE = [(0, SCALE, 860, 1200), (0, DY, 7, -10)]


def bilge(y):
    return 34 + 8 * (1 - (y / 50) ** 2)


# The cellar's vault: one arch from floor to floor, growing up and out of
# the dark as the cellar deepens (one continuous change, not steps).
VO, VI = (124, 92), (118, 86)
vault = band([(46, 196)] + arc(170, 144, VO[0], VO[1], 180, 360, 28) + [(294, 196)],
             [(52, 196)] + arc(170, 144, VI[0], VI[1], 180, 360, 28) + [(288, 196)])
P(m, "vault", (170, 124), T(vault, -170, -124), DIM, fx=[(7, SCALE, 640, 1024), (7, DY, 26, 0)], hide=(7, 3, 0), k=2)
YS = [-50 + 6.25 * i for i in range(17)]
body = [(bilge(y), y) for y in YS] + arc(0, 50, 34, 5, 15, 165, 12)
body += [(-bilge(y), y) for y in YS[::-1]] + arc(0, -50, 34, 5, 165, 15, 12)
P(m, "cask", (CX0, CY0), body, HERO, fx=SIZE)
P(m, "head", (CX0, CY0), ell(0, -50, 34, 5, 36), DETAIL, fx=SIZE)


def stave(kk, down):
    e = 5 * math.sqrt(1 - kk * kk)
    ys = [-50 + e, -37.5, -25, -12.5, 0, 12.5, 25, 37.5, 50 + e]
    pts = [(kk * bilge(y), y) for y in ys]
    return pts if down else pts[::-1]


P(m, "staves l", (CX0, CY0), ribbon(sum((stave(kk, i % 2 == 0) for i, kk in enumerate((-0.81, -0.59, -0.31, 0))), []), 0.6), ENGR, fx=SIZE)
P(m, "staves r", (CX0, CY0), ribbon(sum((stave(kk, i % 2 == 0) for i, kk in enumerate((0.31, 0.59, 0.81))), []), 0.6), ENGR, fx=SIZE)
for i, yh in enumerate((-44, -29, 25.5, 40.5)):
    P(m, f"hoop {i}", (CX0, CY0), band(arc(0, yh, bilge(yh) + 0.7, 5, 180, 0, 14), arc(0, yh + 3.5, bilge(yh + 3.5) + 0.7, 5, 180, 0, 14)), STRUCT, BODY2,
      fx=SIZE + [(5, DY, 0, 9 if yh < 0 else -9)])
spigot = [(-6, 20), (6, 20), (6, 22), (1.5, 22), (1.5, 26), (4, 26), (4, 33), (2, 34), (2, 40), (-2, 40), (-2, 34), (-4, 33), (-4, 26), (-1.5, 26), (-1.5, 22), (-6, 22)]
P(m, "spigot", (CX0, CY0), spigot, STRUCT, BODY2, fx=SIZE)
P(m, "loose stave", (CX0 + 37, CY0 + 40), ribbon([(bilge(y) - 3 - 37, y - 40) for y in (-22, -14, -6, 2, 10, 17.5, 25, 32.5, 40)], 1.0), DETAIL,
  fx=[(0, SCALE, 860, 1200), (3, ROT, 0, 170), (0, DX, -5, 7)])
P(m, "bung", (CX0 + 14, 84), [(-5, -6), (5, -6), (6, 1), (-6, 1)], STRUCT, BODY2, fx=[(0, DY, 14, -20), (4, DY, 0, -16), (4, ROT, 0, 450)])
P(m, "stillage", (CX0, 191), rect(-62, -5, 62, 5), STRUCT, BODY)
P(m, "stillage grain", (CX0, 191), ribbon([(-58, 0), (-20, 0.8), (20, -0.6), (58, 0.4)], 0.6), ENGR)
P(m, "chock", (130, 184), [(-9, 2), (1, -5), (4, 2)], STRUCT, BODY2)
MAL = [(2, SCALE, 1250, 800), (2, DX, 14, -6)]
P(m, "mallet handle", (266, 116), [(-1.6, 7), (1.6, 7), (2.6, 58), (-2.6, 58)], STRUCT, BODY2, fx=MAL, hit=(KICK_DX, -40))
P(m, "mallet", (266, 116), [(-11, -7), (11, -7), (12.5, -4), (12.5, 4), (11, 7), (-11, 7), (-12.5, 4), (-12.5, -4)], STRUCT, BODY2, fx=MAL, hit=(KICK_DX, -40))
P(m, "felt", (266, 116), [(-12.5, -5), (-16, -4), (-16, 4), (-12.5, 5)], DETAIL, BODY2, fx=MAL, hit=(KICK_DX, -40), hide=(2, 2048, 1))

# --- STONE: a lithophone bar (reshaped: its length the pitch, its underside
# carved to the rock) on felt cords over a sounding box; a rattan mallet, a damp cloth.
m = 2
SX = 185
P(m, "box", (SX, 182), [(-64, -14), (64, -14), (64, 10), (58, 10), (58, 14), (47, 14), (47, 10), (-47, 10), (-47, 14), (-58, 14), (-58, 10), (-64, 10)], STRUCT, BODY)
P(m, "hole", (SX, 180), C(4), ENGR, BODY2)
P(m, "cord l", (SX - 32, 165), C(3.4), DETAIL, BODY2, fx=[(0, DX, 0, 15), (3, DX, 14, 0)])
P(m, "cord r", (SX + 32, 165), C(3.4), DETAIL, BODY2, fx=[(0, DX, 0, -15), (3, DX, -14, 0)])
P(m, "strata", (SX, 150), ribbon([(-50, -2), (-28, -3.5), (-6, -1.5), (20, -3), (48, -1.5), (50, 4.5), (24, 5.5), (-4, 4), (-30, 6), (-48, 4.5)], 0.6), ENGR, fx=[(0, SCALE, 1024, 530)], hit=(RING_DY, 1))
P(m, "stacked", (SX + 3, 138), [(-26, -6), (-10, -11), (14, -12), (26, -7), (28, -1), (-28, -1)], DETAIL, BODY, fx=[(5, SCALE, 300, 1024), (0, SCALE, 1024, 700)], hit=(RING_DY, 1), hide=(5, 3, 0))
cloth = [(-14, -7), (-4, -9), (10, -8), (18, -5), (20, 6), (17, 18), (21, 28), (12, 26), (6, 31), (0, 25), (-7, 29), (-12, 22), (-15, 10)]
P(m, "cloth", (SX - 50, 146), loop(cloth, 3), DETAIL, BODY2, fx=[(6, SCALE, 300, 1100), (0, DX, 0, 28)], hide=(6, 3, 0))
P(m, "chip", (SX + 55, 136), [(-2.5, -2), (2.5, -2.5), (3, 2), (-2, 2.5)], DETAIL, BODY2, fx=[(0, DX, 0, -28), (4, SCALE, 700, 1500)], hit=(RISE, -18), hide=(4, 3, 0))
for i, (dx, dy) in enumerate(((-18, -14), (22, -18), (2, -22))):
    P(m, f"dust {i}", (SX + dx, 150 + dy), C(1.8), DIM, fx=[(7, SCALE, 800, 1300)], hit=(RISE, -14), hide=(7, 3 + i * 1400, 0))
# Drawn half as large again as first drawn, about its foot: as large on the
# screen as the cicada. The mallet is held over the bar's right end,
# its cane raised to the right, so it strikes the bar's top.
zoom(m, (SX, 197), 1.5)
MH = (SX + 32, 80)
P(m, "mallet cane", MH, stick(5, -4.5, 50, -23, 2.6, 2.0), STRUCT, BODY2, fx=[(1, SCALE, 1400, 700)], hit=(KICK_DY, 24))
P(m, "mallet", MH, C(9), STRUCT, BODY2, fx=[(1, SCALE, 1400, 700)], hit=(KICK_DY, 24))
P(m, "yarn", MH, C(5.4), ENGR, fx=[(1, SCALE, 1400, 700)], hit=(KICK_DY, 24), hide=(1, 2048, 1))

# --- DRIP: a supply pipe along the tiled wall, its handwheel valve, a swan-neck
# spout over a pedestal basin with its plug on a chain; the drop, the bubble, the crown.
m = 3
BX = 184
BASIN = [(2, SCALE, 820, 1180), (2, DY, 10, -10)]
G = [-2 - 19.2 * i for i in range(6)]
rows, cols = [], []
for i, v in enumerate(G):
    rows += [(-2, v), (-98, v)] if i % 2 == 0 else [(-98, v), (-2, v)]
    cols += [(v, -2), (v, -98)] if i % 2 == 0 else [(v, -98), (v, -2)]
# The tiled wall rises behind as the room's echo grows.
P(m, "tiles", (362, 195), ribbon(rows, 1.0), DIM, fx=[(7, SCALE, 300, 1000)], hide=(7, 3, 0), k=2)
P(m, "joints", (362, 195), ribbon(cols, 1.0), DIM, fx=[(7, SCALE, 300, 1000)], hide=(7, 3, 0), k=2)
pipe = [(24, 67.5), (176, 67.5)] + arc(176, 78, 10.5, 10.5, -70, 0, 7)[0:] + [(186.5, 86), (181.5, 86), (181.5, 78)] + arc(176, 78, 5.5, 5.5, 0, -70, 5)[1:] + [(176, 72.5), (24, 72.5)]
pipe = [pipe[0]] + [p for p in pipe[1:]]
P(m, "pipe", (104, 77), T(pipe, -104, -77), STRUCT, BODY2, k=2)
P(m, "valve", (100, 70), C(7.5), STRUCT, BODY2)
P(m, "wheel", (100, 70), C(11.5), STRUCT)
P(m, "spokes", (100, 70), [(-1, -11), (1, -11), (1, -1), (11, -1), (11, 1), (1, 1), (1, 11), (-1, 11), (-1, 1), (-11, 1), (-11, -1), (-1, -1)], DETAIL, fx=[(3, ROT, 0, 900)])
P(m, "knob", (100, 70), C(2.4, 11.5, 0), DETAIL, BODY2, fx=[(3, ROT, 0, 900)])
# A deep basin (42 px from its rim, at 132, to its floor) on a short foot,
# so the bubble has water to rise through.
P(m, "pedestal", (BX, 186), mirror([(0, -12)] + curve([(9, -12), (7, -9.5), (5.5, -7), (5, 3), (7, 6), (13, 8.5), (16, 10)], 3) + [(16, 11)]), STRUCT, BODY2, fx=[(2, SCALE, 820, 1180), (2, DY, 2, -2)])
bowl = [(58 * math.cos(math.radians(a)), -8 + 42 * math.sin(math.radians(a))) for a in range(180, -1, -6)] + arc(0, -8, 58, 7, 15, 165, 14)
P(m, "basin", (BX, 140), bowl, BODYL, fx=BASIN)
P(m, "rim", (BX, 140), arc(0, -8, 58, 7, 180, 360, 18) + arc(0, -8, 52, 5, 0, 180, 18), DETAIL, fx=BASIN)
# The bubble rising as its note climbs, from the basin's floor: a cluster
# for no rise, a column for much, each bubble larger as it nears the top.
for i, (dx, r) in enumerate(((0, 1.6), (1.7, 1.9), (-1.3, 2.2), (1.4, 2.5), (-0.7, 2.8))):
    P(m, f"bubble {i}", (BX + dx, 169), C(r), DETAIL, fx=[(1, SCALE, 900, 1150), (1, DY, round(-1.5 * i), round(-5.8 * i)), (2, DY, 5, -5)], hit=(RISE, -4))
P(m, "ripple", (BX, 137), ell(0, 0, 18, 2.6, 30), DIM, fx=[(6, SCALE, 500, 1300), (4, DY, 6, -6), (2, DY, 10, -10)], hit=(RING_DY, 1), hide=(6, 3, 0))
P(m, "crown", (BX, 136), ribbon([(-9, 0), (-7, -6), (-4, -1.5), (-1.5, -8), (1.5, -1.5), (4, -7), (7, -1), (9, 0)], 0.6), DETAIL, fx=[(5, SCALE, 300, 1300), (4, DY, 6, -6), (2, DY, 10, -10)], hit=(KICK_DY, -6), hide=(5, 3, 0))
# The drop hangs from the spout, small and high to large and low.
P(m, "drop", (BX, 87), S(curve([(0, -6), (2, -3), (3.5, 0.5), (3, 3), (0, 5), (-3, 3), (-3.5, 0.5), (-2, -3), (0, -6)], 3)[:-1], 1.4), HERO, fx=[(0, SCALE, 600, 1500), (0, DY, 5, 13)], hit=(FALL, 2))

# --- ICE: a slab of lake ice on sawhorses, a cast-iron weight on it, icicles
# under it; the frozen lake and its far shore behind; ice tongs on their hook.
m = 4
IX = 180
SHEETFX = [(1, SCALE, 900, 1400)]
P(m, "shore", (IX, 100), ribbon(sum(([(x - 3, -3 * math.sin(x / 30)), (x, -3 * math.sin(x / 30) - (9 if i % 3 else 13)), (x + 3, -3 * math.sin(x / 30))]
                                      if i % 2 == 0 else [(x, -3 * math.sin(x / 30) - 1.5)] for i, x in enumerate(range(-120, 121, 30))), []), 1.2),
  DIM, fx=[(7, SCALE, 500, 1000), (7, DY, -10, 0)], hide=(7, 3, 0), k=2)
P(m, "hook", (318, 52), C(2.4), STRUCT, BODY2)
# The tongs hang open from the hook by their handles' bow: the two arms cross
# at a rivet, bow out and curl in to their points.
arm = [(-4, 54), (-8.5, 59.5), (-13.5, 56), (-17, 45), (-17, 30), (-11.5, 14.5)]
arm = curve(arm, 3)                         # the curls round, the arms straight through the rivet
tongs = arm + curve([(10, -11.5), (0, -16.5), (-10, -11.5)], 3) + [(-x, y) for x, y in arm[::-1]]
P(m, "tongs", (318, 71), ribbon(tongs, 1.4), STRUCT)
P(m, "rivet", (318, 71), C(2.2), STRUCT, BODY2)
P(m, "tray", (IX, 196), [(-33, -10), (-30, -10), (-29, -2.5), (29, -2.5), (30, -10), (33, -10), (31, 0), (-31, 0)], STRUCT)
P(m, "horse l", (IX - 46, 176), [(-17, 20), (-3, -20), (3, -20), (17, 20), (12, 20), (0, -13), (-12, 20)], STRUCT, BODY2)
P(m, "horse r", (IX + 46, 176), [(-17, 20), (-3, -20), (3, -20), (17, 20), (12, 20), (0, -13), (-12, 20)], STRUCT, BODY2)
P(m, "icicles", (IX, 156), [(-40, 0), (-37, 9), (-34, 0), (-12, 0), (-10, 13), (-7, 0), (14, 0), (16, 7), (18, 0), (30, 0), (33, 11), (36, 0), (36, -0.8), (-40, -0.8)], DETAIL, fx=[(4, SCALE, 400, 1200)])
P(m, "slab top", (IX, 156), [(-58, -18), (-52, -24), (64, -24), (64, -6), (58, 0), (58, -18)], DETAIL, fx=SHEETFX, hit=(KICK_DY, 2))
P(m, "slab", (IX, 156), rect(-58, -18, 58, 0), HERO, fx=SHEETFX, hit=(KICK_DY, 2))
P(m, "milk", (IX, 156), ribbon([(-52, -15), (-30, -14), (-8, -15.5), (14, -13.8), (40, -15), (52, -14)], 0.7), DETAIL, fx=SHEETFX + [(3, SCALE, 1024, 400)], hide=(3, 3900, 1))
P(m, "milk 2", (IX, 156), ribbon([(-46, -3), (-24, -4), (0, -2.5), (22, -4), (44, -2.8)], 0.7), DETAIL, fx=SHEETFX + [(3, SCALE, 1024, 500)], hide=(3, 2048, 1))
WEIGHT = [(0, SCALE, 600, 1300), (1, DY, 2, -10)]
P(m, "weight ring", (IX, 132), ribbon(arc(0, -14, 6.5, 7.5, 180, 360, 14), 1.6), STRUCT, fx=WEIGHT, hit=(KICK_DY, 3))
P(m, "weight", (IX, 132), [(-13, 0), (-11, -14), (11, -14), (13, 0)], STRUCT, BODY2, fx=WEIGHT, hit=(KICK_DY, 3))
# Frost crystals blooming on the slab's top as it gets colder, carried with
# the slab as it grows (a move by the sheet's control matching its scale).
for i, ox in enumerate((-30, 8, 42)):
    oy = -21
    P(m, f"frost {i}", (IX + ox, 156 + oy), star(6, 7, 1.3), DETAIL, fx=[(1, DX, round(ox * -0.1), round(ox * 0.4)), (1, DY, round(oy * -0.1), round(oy * 0.4)), (5, SCALE, 450, 1250)], hide=(5, 3 + i * 1300, 0))

# --- SHEET: a thunder sheet (reshaped) hung by two chains from a timber beam,
# clamped in a wooden batten, its turned handle at its foot for shaking; a
# far rumble sheet; a beater, soft felt to an iron rod; a second for rolls.
m = 5
SX = 172
HANGY = [(3, DY, 0, 16)]                     # the chains' length lowers everything below them
SIZEX = lambda d: [(0, DX, d, 0)]             # a smaller sheet draws its fittings in
P(m, "beam", (184, 54), rect(-118, -4.5, 118, 4.5), STRUCT, BODY, k=2)
P(m, "beam grain", (184, 54), ribbon([(-114, -1), (-60, 0.6), (-4, -1.2), (52, 0.4), (114, -0.8)], 1.0), ENGR, k=2)
for nm, x, d in (("l", SX - 60, 17), ("r", SX + 60, -17)):
    P(m, f"chain {nm}", (x, 59), ribbon(zig(0, 0, 0, 16, 6, 2.2), 0.7), STRUCT, fx=SIZEX(d) + [(3, SCALE, 600, 1600)])
P(m, "rumble", (52, 66), [(-32, 0), (-25, 0), (-25, -5), (-23, -5), (-23, 0), (23, 0), (23, -5), (25, -5), (25, 0), (32, 0), (32, 58), (-32, 58)], DIM, fx=[(4, SCALE, 500, 1024)], hit=(RING_DX, 2), hide=(4, 3, 0))
P(m, "batten", (SX, 72), rect(-82, -3.5, 82, 3.5), STRUCT, BODY2, fx=HANGY + [(0, SCALE, 740, 1024)], k=2)
for nm, x, d in (("l", SX - 60, 17), ("r", SX + 60, -17)):
    P(m, f"bolt {nm}", (x, 72), C(1.8), DETAIL, BODY, fx=HANGY + SIZEX(d))
P(m, "rattle", (SX + 78, 76), ribbon(zig(0, 0, 3, 46, 12, 2.2), 0.7), DETAIL, fx=HANGY + SIZEX(-18) + [(2, SCALE, 300, 1150)], hit=(RING_DX, 2), hide=(2, 3, 0))
P(m, "glint", (SX - 24, 112), ribbon([(-14, 14), (14, -14)], 0.8), GLINT, fx=[(5, SCALE, 300, 1400)] + HANGY)
P(m, "glint 2", (SX + 26, 136), ribbon([(-9, 9), (9, -9)], 0.8), GLINT, fx=[(5, SCALE, 300, 1400)] + HANGY, hide=(5, 1600, 0))
# The handle: a turned grip on a ferrule, bolted to the sheet's foot; it swings with the shake.
grip = mirror([(0, 0), (5, 0)] + curve([(5, 3), (3, 4.5), (3.5, 7), (5, 10), (5.5, 15), (4.5, 19), (2.5, 21), (0, 21.5)], 3))
P(m, "handle", (SX, 163.5), grip, STRUCT, BODY2, fx=HANGY + [(0, DY, -22, 0), (1, DX, 0, 12), (1, ROT, 0, -250)], hit=(RING_ROT, 30))
P(m, "beater handle", (270, 116), stick(4, 5, 40, 62, 2.4, 1.8), STRUCT, BODY2, fx=[(6, SCALE, 1500, 650)], hit=(KICK_DX, -20))
P(m, "beater", (270, 116), C(7), STRUCT, BODY2, fx=[(6, SCALE, 1500, 650)], hit=(KICK_DX, -20))
P(m, "beater felt", (270, 116), C(4), ENGR, fx=[(6, SCALE, 1500, 650)], hit=(KICK_DX, -20), hide=(6, 2400, 1))
P(m, "beater 2 handle", (268, 160), stick(4, 3, 44, 22, 2.4, 1.8), STRUCT, BODY2, fx=[(7, SCALE, 400, 1024)], hit=(RING_DX, -6), hide=(7, 3, 0))
P(m, "beater 2", (268, 160), C(6), STRUCT, BODY2, fx=[(7, SCALE, 400, 1024)], hit=(RING_DX, -6), hide=(7, 3, 0))

# --- SPARK: a Leyden jar (its foil reshaped to the body's height) with its
# lid, rod, chain and brass knob, wired to a spark gap: two brass knobs on
# glass pillars on a mahogany board, the right one sliding to the gap; a
# gold-leaf electroscope for the charge; an induction coil for the arc.
m = 6
JX, KY = 108, 74                     # the jar's axis; the knobs' height
GAPX = [(1, DX, 0, 36)]               # the right knob slides out with the gap
KNOB = [(2, SCALE, 1300, 600)]        # bigger brass knobs, a lower crack
P(m, "coil", (36, 186), [(-22, 0), (-22, -24), (-18, -24), (-18, -6), (18, -6), (18, -24), (22, -24), (22, 0)], STRUCT, BODY2, fx=[(4, SCALE, 400, 1024)], hit=(RING_DX, 1), hide=(4, 3, 0))
P(m, "windings", (36, 186), ribbon(zig(-17, -15, 17, -15, 10, 6), 0.7), DETAIL, fx=[(4, SCALE, 400, 1024)], hit=(RING_DX, 1), hide=(4, 3, 0))
P(m, "board", (192, 191), [(-126, 5), (126, 5), (126, -2), (122, -5), (-122, -5), (-126, -2)], STRUCT, BODY, k=2)
P(m, "jar", (JX, 186), mirror([(0, -82), (28, -82), (31, -78), (31, -3), (29, 0), (0, 0)]), BODYL, k=2)
P(m, "jar glint", (JX, 140), ribbon([(-24, -28), (-24, -4)], 0.9), GLINT)
P(m, "lid", (JX, 104), rrect(-34, -5, 34, 1, 1.5, 1), STRUCT, BODY2)
P(m, "chain", (JX, 110), ribbon(zig(0, 0, 0, 44, 7, 1.8), 0.6), ENGR)
P(m, "rod", (JX, 104), rect(-1.5, -26, 1.5, -5), STRUCT, BODY2)
P(m, "conductor", (JX + 41, KY), rect(-41, -1.4, 41, 1.4), STRUCT, BODY2)
P(m, "knob", (JX, KY), C(8), HERO, BODY2, hit=(RING_DY, 1))
P(m, "pillar l", (192, 133), rect(-2.5, -53, 2.5, 53), DETAIL)
P(m, "ball l", (192, KY), C(6), HERO, BODY2, fx=KNOB)
P(m, "pillar r", (268, 133), rect(-2.5, -53, 2.5, 53), DETAIL)
P(m, "clamp", (268, KY), [(-4, -5), (4, -5), (4, 5), (-4, 5)], STRUCT, BODY2)
P(m, "arm", (214, KY), rect(0, -1.4, 60, 1.4), STRUCT, BODY2, fx=GAPX)
P(m, "brush", (214, KY), [(-6, -2), (-16, -10), (-10, -3), (-18, 0), (-10, 3), (-16, 10), (-6, 2)], DETAIL, fx=GAPX + [(5, SCALE, 300, 1300)], hide=(5, 3, 0))
P(m, "ball r", (214, KY), C(6), HERO, BODY2, fx=GAPX + KNOB)
# Ozone: wisps curling up from the gap, one, two, then three, and taller.
import math as _m
# Short enough, risen and grown, to stay below the header.
wisp = curve([(3.2 * _m.sin(i * 1.3), -3.3 * i) for i in range(6)], 4)
for i, (dx, tilt) in enumerate(((0, 0), (-12, -16), (12, 16))):
    P(m, f"ozone {i}", (212 + dx, 66), ribbon(R(wisp, tilt), 0.8), DIM, fx=[(3, SCALE, 600, 1100), (1, DX, 0, 18)], hit=(RISE, -6), hide=(3, 3 + 1365 * i, 0))
SCOPE = [(7, SCALE, 1024, 640), (7, DY, 0, -6)]
P(m, "scope", (328, 186), [(-20, 0), (-20, -4), (-16, -4)] + arc(0, -44, 16, 12, 180, 360, 14) + [(16, -4), (20, -4), (20, 0)], BODYL, fx=SCOPE)
P(m, "scope rod", (328, 186), [(-1, -38), (-1, -62), (-7, -62), (-7, -66), (7, -66), (7, -62), (1, -62), (1, -38)], STRUCT, BODY2, fx=SCOPE, k=2)
for nm, sgn in (("l", 1), ("r", -1)):
    P(m, f"leaf {nm}", (328, 148), [(-0.7, 0), (0.7, 0), (1.3, 22), (-1.3, 21)], HERO,
      fx=[(0, ROT, 60 * sgn, 420 * sgn), (7, SCALE, 1024, 640), (7, DY, 0, 8)], hit=(KICK_ROT, 100 * sgn))

# --- CORK: a Bordeaux bottle, its label and punt, the capsule on its neck; the
# cork and a T corkscrew in it; the wine inside, the fizz, the spill.
m = 7
BOT = [(3, SCALE, 820, 1024), (3, DY, 7, 0)]
NECK = [(0, SCALE, 870, 1050), (3, DY, 15, 0)]
TOP = [(0, DY, 4, -5), (3, DY, 15, 0)]
P(m, "bottle", (170, 156), mirror(curve([(8, -42), (12, -38), (19, -32), (23, -25), (24, -18)], 4) + curve([(24, 36), (23.6, 38.3), (22.2, 39.6), (19, 40)], 3)), HERO, fx=BOT)
P(m, "label", (170, 156), rect(-20, -4, 20, 24), DETAIL, BODY, fx=BOT)
P(m, "cartouche", (170, 156), ell(0, 10, 12, 6.5, 30), ENGR, fx=BOT)
P(m, "glass glint", (170, 156), ribbon(curve([(-19.5, -27), (-21, -18), (-21, -8)], 4), 0.9), GLINT, fx=BOT)
for i, (dx, dy, r) in enumerate(((-8, 176, 2.5), (6, 156, 2.2), (-3, 134, 2.0))):
    P(m, f"fizz {i}", (170 + dx, dy), C(r), DETAIL, fx=[(2, SCALE, 700, 1300)], hit=(RISE, -16), hide=(2, 300 + i * 1000, 0))
P(m, "neck", (170, 114), [(-8, 0), (-7.4, -30), (7.4, -30), (8, 0)], HERO, fx=NECK)
P(m, "capsule", (170, 114), [(-7.9, -12), (-7.4, -30), (7.4, -30), (7.9, -12)], DETAIL, BODY2, fx=NECK)
# The cork sits in the neck, a little drawn; the corkscrew's worm in it, its T-bar above.
P(m, "worm", (170, 114), T(ribbon(zig(0, 9, 0, -3, 6, 2.6) + [(0, -8)], 0.7), 0, -36), STRUCT, fx=[(5, DY, -6, 0)] + NECK, hit=(FALL, -40), hide=(5, 3, 0))
P(m, "t bar", (170, 114), T(rrect(-14, -1.8, 14, 1.8, 1.5, 1), 0, -45), STRUCT, BODY2, fx=[(5, DY, -6, 0)] + NECK, hit=(FALL, -40), hide=(5, 3, 0))
P(m, "cork", (170, 114), T(rrect(-6, -12, 6, 2, 1.5, 1), 0, -28), STRUCT, BODY2, fx=NECK + [(1, DY, 0, -4)], hit=(FALL, -40))
P(m, "lip", (170, 84), rrect(-9.5, -3, 9.5, 1, 1, 1), DETAIL, BODY2, fx=TOP + [(4, SCALE, 800, 1250)])
P(m, "foam", (170, 82), ribbon([(-12, 2), (-9, -3), (-5, 0), (-1, -4), (3, -1), (7, -4), (11, 0), (10.5, 8), (9.5, 16)], 0.7), DETAIL, fx=[(6, SCALE, 300, 1200)] + TOP, hit=(KICK_DY, -4), hide=(6, 3, 0))
# Drawn 1.1 times as large as first drawn, about its foot, and moved to the
# middle: as large as the screen's height allows with the corkscrew and the
# cork's flight still below the header.
zoom(m, (170, 197), 1.1, (16, 0))

# --- CICADA: pinned dorsal on a specimen plate, wings set out: big compound
# eyes, the pronotal collar, the mesonotum's W and cross, the tymbals, six
# legs, veined fore and hind wings; the abdomen reshaped (its ribs, body, tail, curl).
m = 8
Z = 1.24                     # drawn at the largest shell; SHELL scales it down
SH = [(2, SCALE, 1024, 661)]
CP = (186, 108)


def z(pts):
    return S(pts, Z)


legs = [(-10, -13.5), (-26, -19.5), (-29.2, -34), (-24.5, -42.5), (-23.5, -41.5), (-27, -34), (-25, -16.5), (-10, -10.5),
        (-10, -1.5), (-30, 2.5), (-39, 20), (-37.5, 20.6), (-29, 6), (-10, 1.5),
        (-10, 9), (-26, 18.5), (-31, 40), (-29.5, 40.4), (-24.5, 21), (-10, 12)]
P(m, "legs l", CP, z(legs), DETAIL, BODY, fx=SH)
P(m, "legs r", CP, z([(-x, y) for x, y in legs]), DETAIL, BODY, fx=SH)
fore = [(0, -3), (14, -8), (34, -10.5), (56, -11), (76, -9), (90, -5), (97, 0), (99.5, 4.5), (97.5, 9), (91, 12.5), (74, 15.5), (55, 17.5), (38, 18.5), (24, 16.5), (13, 12.5), (5, 7.5), (1, 3)]
veins = [(5, -1), (34, -5.5), (62, -7.5), (66, -10.6), (69, -7), (78, -5.5), (82, -8.6), (85, -3.5), (90, 0.5), (95, -0.6), (91.5, 5),
         (87, 9), (90, 12.6), (82, 11), (72, 12.5), (72, 16.4), (62, 13.5), (40, 12.5), (18, 8), (6, 3), (36, 4), (64, 5), (86, 3)]
hind = [(1, 6), (18, 7), (38, 10), (54, 15), (59, 20), (54, 24.5), (38, 25.5), (22, 23), (9, 17), (2, 11)]
for side in (1, -1):
    sx = (lambda pts: [(side * x, y) for x, y in pts])
    nm = "r" if side > 0 else "l"
    WFX = [(6, ROT, 0, -250 * side), (2, SCALE, 1024, 661), (2, DX, 0, round(-18 * Z * 0.339 * side))]
    WP = (CP[0] + side * 18 * Z, CP[1])
    P(m, f"hindwing {nm}", WP, z(sx(R(loop(hind, 3), 6))), DETAIL, fx=WFX, hit=(KICK_ROT, 60 * side), k=2)
    P(m, f"wing {nm}", WP, z(sx(loop(fore, 3))), HERO, fx=WFX, hit=(KICK_ROT, 60 * side), k=2)
    P(m, f"veins {nm}", WP, z(sx(ribbon(veins, 0.9))), DETAIL, fx=WFX, hit=(KICK_ROT, 60 * side), k=2)
for side in (1, -1):
    P(m, f"tymbal {side}", CP, z(ell(13 * side, 26, 4, 5.5, 20)), DETAIL, BODY, fx=SH)
P(m, "mesonotum", CP, z([(-19, -5)] + curve([(19, -5), (18, 6), (12, 16), (4, 21), (0, 22), (-4, 21), (-12, 16), (-18, 6), (-19, -5)], 3)[:-1]), HERO, BODY, fx=SH)
P(m, "w mark", CP, z(ribbon([(-11, -2), (-7, 8), (-2, 1), (2, 1), (7, 8), (11, -2)], 0.7)), DETAIL, fx=SH)
P(m, "cross", CP, z([(-4, 12.5), (0, 14), (4, 12.5), (1.2, 15.5), (2.6, 18.5), (0, 17), (-2.6, 18.5), (-1.2, 15.5)]), DETAIL, fx=SH)
P(m, "pronotum", CP, z([(-16, -21), (16, -21), (22, -8), (19, -5), (-19, -5), (-22, -8)]), HERO, BODY, fx=SH)
P(m, "collar", CP, z(ribbon([(-17, -10), (-8, -12.5), (0, -13), (8, -12.5), (17, -10)], 0.6)), ENGR, fx=SH)
P(m, "head", CP, z(curve([(-20, -20), (-18, -25), (-10, -29), (0, -30.5), (10, -29), (18, -25), (20, -20)], 3) + [(10, -21.5), (0, -22), (-10, -21.5)]), HERO, BODY, fx=SH)
P(m, "antennae", (CP[0], CP[1] - 29 * Z), z([(-1, 0), (-10, -9), (-9, -10), (0, -1.6), (9, -10), (10, -9), (1, 0)]), DETAIL, fx=SH + [(1, SCALE, 450, 1600), (2, DY, 0, round(29 * Z * 0.339))])
for side in (1, -1):
    P(m, f"eye {side}", CP, C(4.5 * Z, 21.5 * Z * side, -23 * Z), HERO, BODY, fx=SH)
P(m, "pin", CP, C(2.2 * Z, 0, 2 * Z), HERO, "#C8C8C8", fx=SH)
swarm = [(0, -5), (2, -3), (13, -6), (15, -3), (3, 1), (2, 6), (0, 8), (-2, 6), (-3, 1), (-15, -3), (-13, -6), (-2, -3)]
for i, (x, y) in enumerate(((58, 172), (322, 166), (300, 62))):
    P(m, f"swarm {i}", (x, y), swarm, DIM, fx=[(7, SCALE, 600, 1100)], hit=(RING_DX, 1), hide=(7, 400 + i * 1300, 0))

# --- RATCHET: a cranked rattle: the toothed wheel (reshaped: its teeth) and its
# crank, a wooden frame on a turned handle in a block, the slat with a sliding
# clamp, a clock-gong spring, a weight for the slowing, a sounding box.
m = 9
RC = (128, 124)
P(m, "box", (232, 196), rect(-50, -30, 50, 0), STRUCT, BODY, fx=[(6, SCALE, 300, 1024)], hide=(6, 3, 0))
P(m, "box hole", (232, 182), C(5), ENGR, fx=[(6, SCALE, 300, 1024), (6, DY, 10, 0)], hide=(6, 3, 0))
P(m, "handle", RC, mirror([(0, 4), (3, 4)] + curve([(3, 24), (3.6, 28), (5.6, 32), (6, 40), (5, 50), (4, 56), (5, 60), (6, 62)], 3) + [(18, 62), (18, 72), (0, 72)]), STRUCT, BODY2, k=2)
frame = [(300, 98), (102, 98), (100, 100), (100, 148), (102, 150), (300, 150), (300, 145), (106, 145), (105, 144), (105, 104), (106, 103), (300, 103)]
P(m, "frame", (200, 124), T(frame, -200, -124), STRUCT, BODY2, k=2)
P(m, "end block", (296, 124), rect(-8, -21, 6, 21), STRUCT, BODY)
P(m, "slat", (218, 112), [(72, -3.6), (-72, -1.4), (-73, 1), (72, 3.6)], DETAIL, BODY2, hit=(RING_DY, 1), k=2)
P(m, "grain", (282, 112), ribbon([(6, -1.3), (-60, -0.9), (-122, -0.4), (-123, 0.6), (-60, 1.1), (6, 1.5)], 1.0), DETAIL, fx=[(3, SCALE, 300, 1000)], hit=(RING_DY, 1), k=2)
P(m, "wear", (150, 111), ribbon([(-3, 1), (0, -1.6), (3, 1), (6, -1.6)], 0.6), DETAIL, fx=[(7, SCALE, 300, 1500)], hide=(7, 3, 0))
P(m, "clamp", (280, 111), [(-4, -6), (-1, -6), (-1, -11), (-4, -11), (-4, -13), (4, -13), (4, -11), (1, -11), (1, -6), (4, -6), (4, 6), (-4, 6)], STRUCT, BODY2, fx=[(2, DX, 0, -96)])
P(m, "gong", (296, 124), ribbon([(10 * t / 20 * math.cos(t * 0.6), 10 * t / 20 * math.sin(t * 0.6)) for t in range(3, 21)] + [(16, 2)], 0.6), DETAIL, fx=[(5, SCALE, 300, 1024)], hide=(5, 3, 0))
P(m, "weight", (296, 146), [(-0.6, 0), (0.6, 0)] + arc(0, 20, 5, 6, -83, 263, 22), STRUCT, BODY2, fx=[(4, SCALE, 300, 1300)], hide=(4, 3, 0))
P(m, "hub", RC, C(4), STRUCT, BODY2)
P(m, "crank", RC, [(-2.2, 0), (-1.6, -26), (1.6, -26), (2.2, 0)], STRUCT, BODY2, fx=[(1, ROT, -300, -1500)], hit=(SWING, 0))
P(m, "crank knob", RC, C(3.2, 0, -28), STRUCT, BODY2, fx=[(1, ROT, -300, -1500)], hit=(SWING, 0))

# --- POD: a dried poppy head on its stem in a specimen clip: the capsule and its
# crown of rays, the pores under it, a cut window with the seeds inside, a dry leaf.
m = 10
PB = (180, 150)
PS = [(3, SCALE, 1050, 1450)]
TILT = [(5, DX, 0, 12), (5, DY, 0, 2), (5, ROT, 0, 200)]      # the pod nods on its stem, about the clip
POD = PS + TILT
P(m, "clip base", (180, 196), [(-16, 0), (-14, -4), (-4, -6), (4, -6), (14, -4), (16, 0)], STRUCT, BODY2)
P(m, "stem", (180, 186), ribbon([(0, 0), (0.4, -11), (1.2, -23), (0, -35)], 1.8), STRUCT, fx=[(5, ROT, 0, 200)])
P(m, "clip", (180, 186), rect(-3, -6, 3, 4), STRUCT, BODY2)
P(m, "leaf", (181, 181), curve([(0, 0), (6, -2.5), (14, -1), (20, 4), (23, 11)], 3) + curve([(23, 11), (17, 8.5), (10, 8), (4, 4), (0, 0)], 3)[1:-1], DETAIL, BODY2, fx=[(6, ROT, 0, -450), (6, SCALE, 600, 1250), (4, ROT, 0, 350)], hit=(RING_ROT, 60))
cap = [(0, -58), (9, -58), (12, -55.5), (18, -47), (22, -35), (22.6, -24), (20, -14), (14, -6), (8, -2), (4, 0)]
P(m, "capsule", PB, mirror(cap[:1] + curve(cap[1:], 3)), HERO, fx=POD, hit=(RING_DX, 2))


def capw(y):
    pts = sorted(cap, key=lambda p: p[1])
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if y0 <= y <= y1:
            return x0 + (x1 - x0) * (y - y0) / (y1 - y0 or 1)
    return 0


ribs = [(-0.72 * capw(y), y) for y in (-52, -40, -28, -16, -7)] + [(0.72 * capw(y), y) for y in (-7, -16, -28, -40, -52)]
P(m, "ribs", PB, ribbon(ribs, 0.6), ENGR, fx=POD, hit=(RING_DX, 2))
P(m, "window", PB, ell(0, -28, 11, 15, 28), DETAIL, BODY, fx=POD, hit=(RING_DX, 2))
for i, (dx, dy) in enumerate(((-4, -20), (4, -24), (-1, -30), (5, -33), (-5, -36), (1, -40))):
    P(m, f"seed {i}", PB, C(2.4, dx, dy), DETAIL, BODY2, fx=PS + [(5, DX, 0, 12), (5, ROT, 0, 200), (1, DY, 0, -5)], hit=(RING_DX, 2), hide=(0, 200 + i * 700, 0))
crown = [((20 if i % 2 == 0 else 15.5) * math.cos(math.radians(180 * i / 12)), -58.5 + (5 if i % 2 == 0 else 3.8) * math.sin(math.radians(180 * i / 12))) for i in range(24)]
P(m, "crown", PB, crown, BODYL, BODY, fx=POD, hit=(RING_DX, 2))
P(m, "boss", PB, ell(0, -59.5, 5, 1.8, 18), DETAIL, fx=POD, hit=(RING_DX, 2))
P(m, "wrinkle", PB, ribbon(curve([(-16, -44), (-12, -34), (-16, -24), (-13, -14)], 4), 0.6), ENGR, fx=POD, hit=(RING_DX, 2), hide=(4, 3, 0))
P(m, "wrinkle 2", PB, ribbon(curve([(16, -46), (13, -36), (17, -26), (14, -16)], 4), 0.6), ENGR, fx=POD, hit=(RING_DX, 2), hide=(4, 2048, 0))
for i, x in enumerate((214, 236, 252)):
    P(m, f"spilt {i}", (x, 194), C(2.4), DETAIL, BODY2, fx=[(7, DX, -12 - 6 * i, 14 + 6 * i)], hide=(7, 3 + i * 1400, 0))

# --- MATCH: a matchbox in three-quarter view, its tray drawn out with the
# matches in it, the label and star on its top, the striker along its side;
# a match drawn along the striker: its head, its flame and the flame's
# halo, sparks of phosphorus, the char down its stick, smoke.
m = 11
MB = (196, 162)                          # the sleeve's middle
# The tray, drawn 50 px out of the sleeve's left end (at 84): a thin-walled
# open box a little lower than the sleeve, its rim's inner edge showing the
# walls' thickness, three rows of matches lying in it lengthwise, their heads
# to the left, stepping back into the tray; the sticks run on under the sleeve.
TR = (70, 170)
P(m, "tray", TR, [(-36, 14), (-36, -13), (-18, -25), (26, -25), (26, 14)], STRUCT, BODY2)
P(m, "tray inside", TR, [(-33.5, -11.5), (-18.8, -21.5), (26, -21.5), (26, -11.5)], DETAIL, BODY)
rows = [(-29 + 5.4 * i, -14 - 3.4 * i) for i in range(3)]
sticks = []
for i, (x0, y0) in enumerate(rows):
    sticks += [(x0 + 2, y0), (25, y0)] if i % 2 == 0 else [(25, y0), (x0 + 2, y0)]
P(m, "tray sticks", TR, ribbon(sticks, 0.7), DETAIL)
for i, (x0, y0) in reversed(list(enumerate(rows))):
    P(m, f"tray head {i}", TR, ell(x0, y0, 3.3, 2.1, 16), HERO, "#BDBDBD")
box = [(-112, 24), (-112, -10), (-90, -24), (110, -24), (110, 10), (88, 24)]
P(m, "sleeve", MB, box, STRUCT, BODY, k=2)
P(m, "edges", MB, ribbon([(-112, -10), (88, -10), (110, -24)], 1.0) , STRUCT, k=2)
P(m, "end", MB, ribbon([(88, -10), (88, 24)], 1.0), STRUCT, k=2)
P(m, "label", MB, [(-78, -12.5), (62, -12.5), (80, -22), (-60, -22)], ENGR, k=2)
P(m, "star", (MB[0] + 2, MB[1] - 17), S(star(5, 1, 0.42), 6.5), DETAIL)
P(m, "striker", MB, rect(-102, -4, 78, 18), DETAIL, BODY2, k=2)
P(m, "grit", MB, ribbon(zig(-98, 7, 74, 7, 18, 5), 0.8), ENGR, fx=[(0, SCALE, 750, 1024)], k=2)
MT = [(3, DX, 36, -56)]                   # drawn: the match runs along the striker
mh = (186, 171)                           # the head, on the striker
SCR = [(0, ROT, 80, -180)]                 # scrape: pressed flatter and harder
P(m, "match", mh, stick(6, -2.6, 96, -56, 4.0, 3.4), BODYL, BODY2, fx=MT + SCR, hit=(KICK_DX, 44), k=2)
P(m, "char", mh, stick(6, -3.2, 38, -23, 4.2), DETAIL, BODY, fx=MT + SCR + [(5, SCALE, 400, 1000)], hit=(KICK_DX, 44), hide=(5, 3, 0), k=2)
P(m, "head", mh, ell(0, 0, 7, 5.6, 24), HERO, BODY2, fx=MT + SCR + [(2, SCALE, 560, 1700)], hit=(KICK_DX, 44))
flame = curve([(0, -30), (4, -21), (8, -11), (9, -3), (7, 3), (0, 6.5), (-7, 3), (-9, -3), (-8, -11), (-4, -21), (0, -30)], 3)[:-1]
P(m, "halo", (186, 165), S(flame, 1.55), DIM, fx=MT + [(1, SCALE, 600, 1400), (6, SCALE, 500, 1400)], hit=(KICK_DX, 44), hide=(6, 3, 0))
P(m, "flame", (186, 165), S(flame, 1.1), HERO, fx=MT + [(1, SCALE, 600, 1400)], hit=(KICK_DX, 44))
P(m, "core", (186, 165), S(curve([(0, -13), (3.4, -4), (3, 1.5), (0, 3.4), (-3, 1.5), (-3.4, -4), (0, -13)], 3)[:-1], 1.1), DETAIL, fx=MT + [(1, SCALE, 600, 1400)], hit=(KICK_DX, 44))
for i, (x, y, d) in enumerate(((168, 160, (-6, -1.6)), (206, 156, (2.4, -6)), (176, 146, (-3, -5)))):
    P(m, f"spark {i}", (x, y), ribbon([(0, 0), d], 1.0), DETAIL, fx=MT + [(4, SCALE, 500, 1500)], hit=(KICK_DX, 44), hide=(4, 3 + 1365 * i, 0))
P(m, "smoke", (186, 118), ribbon(curve([(0, 0), (6, -8), (0, -16), (-6, -24), (0, -32)], 4), 0.8), DIM, fx=MT + [(7, SCALE, 400, 1300)], hit=(RISE, -12), hide=(7, 3, 0))
P(m, "smoke 2", (197, 108), ribbon(curve([(0, 0), (-5, -7), (0, -14), (5, -21)], 4), 0.8), DIM, fx=MT + [(7, SCALE, 400, 1300)], hit=(RISE, -10), hide=(7, 2000, 0))

# --- GLASS: a cut-glass goblet (its bowl and water reshaped) and a pendulum
# striker on a brass stand, pulled back by the strike; a wet finger on the rim;
# a second glass for the chink.
m = 12
RING = [(2, DY, 10, -10)]
P(m, "second", (312, 168), mirror(curve([(12, -30), (12.5, -20), (10, -10), (5, -4), (1.5, -2)], 3) + curve([(1.5, 22), (5, 24.6), (9, 26), (10, 28)], 2)), DETAIL, fx=[(7, DX, 30, 0)], hit=(RING_DX, 1), hide=(7, 3, 0))
# The stand: foot, column and arm as one brass casting.
P(m, "column", (240, 134), [(-30, -61.5), (19.6, -61.5), (19.6, 53), (23, 53), (24, 56), (36, 58), (38, 62), (-2, 62), (0, 58), (12, 56), (13, 53), (16.4, 53), (16.4, -58.5), (-30, -58.5)], STRUCT, BODY2)
P(m, "finial", (258, 70), C(3), STRUCT, BODY2)
PEND = [(3, DX, -6, 6), (1, ROT, -30, -400)]
P(m, "rod", (212, 74), rect(-0.7, 0, 0.7, 48), STRUCT, BODY2, fx=PEND, hit=(STRIKE, 160))
P(m, "spoon", (212, 74), ell(0, 54, 3.4, 6.5, 22), HERO, BODY2, fx=PEND, hit=(STRIKE, 160), hide=(1, 2048, 1))
P(m, "rod knob", (212, 74), C(4, 0, 51), HERO, BODY2, fx=PEND, hit=(STRIKE, 160), hide=(1, 2048, 0))
P(m, "carriage", (212, 74), C(2.6), DETAIL, BODY2, fx=PEND[:1])
P(m, "stem", (165, 196), T(mirror([(2.2, -26)] + curve([(2.2, -16), (4.5, -13), (5.5, -10), (4.5, -7), (2, -5)], 3) + curve([(1.8, 12), (3.5, 17), (9, 21), (18, 24.5), (23, 26.5)], 3) + [(23, 28)]), 0, -28), HERO, fx=[(2, SCALE, 850, 1150)])
P(m, "foot rim", (165, 196), ribbon(arc(0, -1.5, 22, 2.2, 160, 20, 12), 0.6), ENGR, fx=[(2, SCALE, 850, 1150)])
# The crack runs down from the rim, further as crack rises: the bowl's height at 100.
P(m, "crack", (178, 105), ribbon([(0, 0), (3, 6), (0, 12), (2, 18), (-2, 24), (-1, 30)], 0.6), DETAIL, fx=[(6, SCALE, 400, 1100), (2, DY, 0, -16)], hide=(6, 3, 0))
P(m, "finger", (143, 103), [(-40, -4)] + curve([(-6, -5), (-1, -3.5), (1, -1), (0.5, 1.5), (-2, 3), (-7, 3.5)], 3) + [(-40, 3.5)], BODYL, BODY2, fx=[(4, DX, -30, 0), (2, DY, 0, -16), (3, DX, 0, -20)], hit=(RING_DX, 2), hide=(4, 3, 0))

# --- BELL JAR: an electric bell under a glass bell jar on the air pump's plate:
# the gong (reshaped), its post, the electromagnet, the trembler's hammer, a
# felt damper; the pump's barrel and T handle, its pipe and cock; a Leclanché cell.
m = 13
JJ = [(6, SCALE, 900, 1140), (6, DY, 6, -8)]
P(m, "cell", (62, 176), [(-12, -20), (12, -20), (12, 17), (10, 20), (-10, 20), (-12, 17)], DETAIL)
P(m, "terminal", (62, 148), C(2.4), STRUCT, BODY2)
P(m, "wire", (100, 160), ribbon(curve([(-36, -12), (-24, -22), (-10, -14), (0, 6), (14, 26), (30, 28)], 4), 0.7), ENGR)
P(m, "post", (165, 184), rect(-1.5, -38, 1.5, 0), STRUCT, BODY2)
P(m, "magnet", (140, 182), [(-9, 0), (-9, -22), (-3, -22), (-3, -6), (3, -6), (3, -22), (9, -22), (9, 0)], STRUCT, BODY2, fx=[(0, SCALE, 600, 1200)])
P(m, "windings", (140, 182), ribbon(zig(-6, -20, -6, -8, 6, 2.4) + [(6, -6)] + zig(6, -8, 6, -20, 6, 2.4)[1:], 0.6), ENGR, fx=[(0, SCALE, 600, 1200)])
P(m, "hammer", (128, 176), stick(0, 0, 15, -31, 1.6, 1.2), STRUCT, BODY2, fx=[(3, SCALE, 860, 1100)], hit=(KICK_ROT, 120))
P(m, "hammer ball", (128, 176), C(3.4, 15.5, -33), HERO, BODY2, fx=[(3, SCALE, 860, 1100)], hit=(KICK_ROT, 120))
P(m, "damper post", (205, 184), rect(-1.4, -34, 1.4, 0), STRUCT, BODY2)
P(m, "damper", (190, 152), rrect(-2.5, -4, 4, 4, 1.5, 1), DETAIL, BODY2, fx=[(4, DX, 0, 12)])
P(m, "jar", (165, 128), mirror(curve([(0, -56), (14, -55), (29, -51), (40, -43), (47, -32), (50, -20), (51, -8)], 3) + [(51, 48), (54, 52), (54, 56)]), BODYL, fx=JJ)
P(m, "jar glint", (165, 128), ribbon(arc(0, -8, 44, 44, 205, 235, 10), 0.8), GLINT, fx=JJ)
P(m, "jar knob", (165, 67), C(5), BODYL, BODY2, fx=[(6, DY, 11, -16)])
P(m, "plate", (165, 196), [(-62, -11), (62, -11), (62, -5), (56, -5), (56, 0), (48, 0), (48, -5), (-48, -5), (-48, 0), (-56, 0), (-56, -5), (-62, -5)], STRUCT, BODY2, fx=[(7, SCALE, 850, 1150)])
P(m, "pipe", (258, 188), ribbon([(-24, 0), (24, 0), (27, -2)], 1.6), STRUCT)
P(m, "cock", (252, 188), C(3), STRUCT, BODY2)
P(m, "pump rod", (285, 118), [(-1.2, 30), (-1.2, -29.8), (-15, -29.8), (-15, -34.2), (15, -34.2), (15, -29.8), (1.2, -29.8), (1.2, 30)], STRUCT, BODY2, fx=[(2, DY, 20, -26)])
P(m, "barrel", (285, 152), rrect(-8, -34, 8, 34, 2, 1), STRUCT, BODY2)

# --- CHALK: a school slate in its pegged frame with ruled lines and its sponge
# on a string, on a chalk ledge; a brass chalk holder writing the stroke (reshaped).
m = 14
SL = [(2, SCALE, 1024, 760)]
SLP = (175, 188)
# The frame: outer and inner edges as one outline, the seam a mitred corner.
P(m, "frame", SLP, [(-115, -128), (115, -128), (115, 0), (-115, 0), (-115, -127.4), (-107, -119.4), (-107, -8), (107, -8), (107, -120), (-107, -120)], STRUCT, fx=SL, k=2)
P(m, "rules", SLP, ribbon(sum(([(-106.5, y), (106.5, y)] if i % 2 == 0 else [(106.5, y), (-106.5, y)] for i, y in enumerate(range(-106, -19, 17))), []), 1.0), ENGR, fx=SL, k=2)
for i, (x, y) in enumerate(((-111, -124), (111, -124), (-111, -4), (111, -4))):
    P(m, f"peg {i}", SLP, C(1.8, x, y), DETAIL, BODY2, fx=SL, k=2)
P(m, "string", SLP, ribbon(curve([(-102, -124), (-110, -116), (-115, -100), (-116, -82)], 4), 1.2), DETAIL, fx=SL, k=2)
P(m, "sponge", SLP, T(loop([(-126, -82), (-120, -80), (-117, -74), (-118, -66), (-122, -62), (-128, -61), (-134, -64), (-136, -71), (-133, -78)], 3), 10, 0), DETAIL, BODY2, fx=SL, k=2)
P(m, "ledge", (175, 193), [(-100, -3), (-96, -6), (-92, -3), (92, -3), (96, -6), (100, -3), (100, 3), (-100, 3)], STRUCT, BODY, k=2)
for i, (dx, dy) in enumerate(((-36, 8), (-8, -6), (22, 10))):
    P(m, f"grit {i}", (175 + dx, 150 + dy), C(1.8), DETAIL, fx=[(5, SCALE, 700, 1400)], hide=(5, 3 + i * 1300, 0))
P(m, "dust", (212, 185), C(4), DIM, fx=[(7, SCALE, 400, 1400)], hit=(RISE, -12), hide=(7, 3, 0))
P(m, "dust 2", (226, 187), C(3), DIM, fx=[(7, SCALE, 400, 1400)], hit=(RISE, -16), hide=(7, 2000, 0))
PRESS = [(1, ROT, -250, 150), (0, SCALE, 1300, 750)]
P(m, "chalk", (175, 150), [(-2.2, -11), (2.2, -11), (2.2, -2), (0.6, 0.4), (-1.6, -0.6)], HERO, BODY2, fx=PRESS, hit=(TIP, 0))
P(m, "holder", (175, 150), [(-2.8, -40), (2.8, -40), (3.2, -14), (2.6, -11), (-2.6, -11), (-3.2, -14)], STRUCT, BODY2, fx=PRESS, hit=(TIP, 0))
P(m, "holder ring", (175, 150), rect(-3.8, -21, 3.8, -17), DETAIL, BODY2, fx=PRESS, hit=(TIP, 0))

# --- ROAR: a bull-roarer: a long carved blade (its hole, its incised band and
# border) whirled on its cord from a stick lashed to a turned post; the
# orbit it sweeps (reshaped), and the cord (reshaped).
m = 15
P(m, "post", (56, 146), mirror([(0, -50), (3, -50)] + curve([(3, -46), (5, -44), (4, -32), (3, -10), (4.5, 0), (6, 22), (6.5, 40), (8, 44), (16, 46), (18, 50)], 3) + [(0, 50)]), STRUCT, BODY2)
P(m, "stick", (56, 96), stick(-4, 4, 40, -20, 2.8, 2.2), STRUCT, BODY2)
P(m, "lashing", (56, 96), ribbon(zig(-3, 3, 4, -1, 4, 2.4), 0.8), DETAIL)
P(m, "knot", (96, 76), C(2), DETAIL, BODY2)
blade = loop([(-42, 0), (-37, -5.5), (-26, -9.5), (-10, -11.5), (10, -11.5), (26, -9.5), (37, -5.5), (42, 0), (37, 5.5), (26, 9.5), (10, 11.5), (-10, 11.5), (-26, 9.5), (-37, 5.5)], 3)
SLATFX = [(1, SCALE, 1300, 700), (5, ROT, 0, 300)]
for i in range(2):
    P(m, f"ghost {i}", (216, 124), blade, DIM, fx=SLATFX, hit=(GHOST, i + 1), hide=(0, 300 + i * 1500, 0))
P(m, "blade", (216, 124), blade, HERO, BODY, fx=SLATFX, hit=(ORBIT, 0))
P(m, "border", (216, 124), S(blade[1:7] + blade[8:14], 0.0) if False else ribbon(arc(0, 0, 33, 7.5, 200, 340, 16), 0.7), ENGR, fx=SLATFX, hit=(ORBIT, 0))
P(m, "notches", (216, 124), ribbon(zig(-24, 1.5, 30, 1.5, 9, 4), 0.8), DETAIL, fx=SLATFX + [(6, SCALE, 350, 1100)], hit=(ORBIT, 0), hide=(6, 3, 0))
P(m, "hole", (216, 124), C(2.2, -35, 0), DETAIL, BODY2, fx=SLATFX, hit=(ORBIT, 0))
P(m, "wind", (250, 72), ribbon([(x, -2.5 * (1 - (x / 34) ** 2)) for x in range(-34, 35, 4)], 0.8), DIM, fx=[(7, SCALE, 300, 1300)], hit=(RING_DX, 3), hide=(7, 3, 0))
P(m, "wind 2", (238, 176), ribbon([(x, 2.5 * (1 - (x / 28) ** 2)) for x in range(-28, 29, 4)], 0.8), DIM, fx=[(7, SCALE, 300, 1300)], hit=(RING_DX, -3), hide=(7, 1600, 0))

# ------------------------------------------------------------- centring
# Each specimen stands alone on the black, its picture centred in the screen
# below the header: every part moves by its specimen's (dx, dy), px, and the
# display moves its reshaped outlines by the same (art.h's ox, oy). Measured
# by tests/clear.cpp, which prints each specimen's extent at its defaults.
# Each specimen's box at rest is centred on (240, 130), as far as its every
# sweep stays between y 46 (clear of the two-row header) and 214 and between
# x 6 and 474.
OFFSET = [(100, 9), (44, 7), (55, 5), (47, 3), (14, 7), (49, 6), (33, 5), (54, 3),
          (54, 21), (41, -17), (60, -6), (70, -16), (35, -2), (65, 1), (70, -6), (60, -1)]
for prt in parts:
    dx, dy = OFFSET[prt["model"]]
    prt["pivot"] = (prt["pivot"][0] + dx, prt["pivot"][1] + dy)


def kit_defaults():
    """Each specimen's design defaults, 0..127, from kit.h."""
    rows = re.findall(r'\{"[a-z ]+", \d, \{[^}]*\}, \{([^}]*)\}', (ROOT / "kit.h").read_text())
    assert len(rows) == 16
    return [[int(v) for v in r.split(",")] for r in rows]


def drawn_scale(prt, d):
    """The scale a part's controls give it at the defaults d (1 = as authored)."""
    k = 1.0
    for c, kd, lo, hi in prt["fx"]:
        if kd == SCALE:
            k *= (lo + (hi - lo) * d[c] / 127) / 1024
    return k


def check(pts, name):
    assert 3 <= len(pts) <= 64, (name, len(pts))
    n = len(pts)
    for x, y in pts:
        assert abs(x) <= 64 and abs(y) <= 64, (name, "reaches", x, y)
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        d = math.hypot(a[0] - b[0], a[1] - b[1])
        assert d >= 0.125, (name, i, d)
        if i == n - 1:
            assert d >= 0.25, (name, "closing", d)
    # No turn sharper than SHARPEST: the OP-XY strokes with mitred joins and no
    # mitre limit, so a turn near straight back draws a long spike.
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        v1 = (p1[0] - p0[0], p1[1] - p0[1])
        v2 = (p2[0] - p1[0], p2[1] - p1[1])
        turn = math.degrees(math.atan2(abs(v1[0] * v2[1] - v1[1] * v2[0]), v1[0] * v2[0] + v1[1] * v2[1]))
        if turn > SHARPEST:
            assert name != "snap", "snapping sharpens a turn"
            SHARP.append((name, i, round(turn)))
    if len(set((round(x * 1024), round(y * 1024)) for x, y in pts)) != n:
        raise AssertionError((name, "repeats a point"))


def fmt(v):
    s = f"{v:.1f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def rounded(pts, k=1):
    return [(float(fmt(x / k)), float(fmt(y / k))) for x, y in pts]


SNAP = 0.25   # px on screen


def blunt(pts, k=1, cut=0.4):
    """Each corner sharper than SHARPEST cut across, a quarter pixel or so back
    along both its edges, so the device's mitred join draws no spike: the one
    sharp turn becomes two gentle ones."""
    for _ in range(4):
        out, n, changed = [], len(pts), False
        for i in range(n):
            p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
            v1 = (p1[0] - p0[0], p1[1] - p0[1])
            v2 = (p2[0] - p1[0], p2[1] - p1[1])
            L1, L2 = math.hypot(*v1), math.hypot(*v2)
            turn = math.degrees(math.atan2(abs(v1[0] * v2[1] - v1[1] * v2[0]), v1[0] * v2[0] + v1[1] * v2[1]))
            # the cut's two points must stand 1/8 px apart however narrow the tip
            need = 0.17 * k / max(1e-6, 2 * math.sin(math.radians(180 - turn) / 2))
            c = min(max(cut * k, need), 0.48 * L1, 0.48 * L2, L1 - 0.16 * k, L2 - 0.16 * k)
            if turn > SHARPEST - 1 and c >= need:
                out.append((p1[0] - v1[0] / L1 * c, p1[1] - v1[1] / L1 * c))
                out.append((p1[0] + v2[0] / L2 * c, p1[1] + v2[1] / L2 * c))
                changed = True
            else:
                out.append(p1)
        pts = out
        if not changed:
            break
    return pts


def snapped(pts, k=1):
    """Coordinates within a quarter pixel (on screen) of a whole one are written
    whole, saving the artwork's bytes; a part whose outline that would spoil
    (points too close, a turn straight back) keeps its tenths."""
    def one(v):
        w = round(v / k)
        return float(w) if abs(v / k - w) * k <= SNAP else float(fmt(v / k))
    pts2 = [(one(x), one(y)) for x, y in pts]
    try:
        check(pts2, "snap")
        return pts2
    except AssertionError:
        return rounded(pts, k)


def d_of(pts):
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in pts) + " Z"


def main():
    out = [f'<g id="{ID}_panel">']
    # The header block (the SDK's two-page header: its bar nodes come last, see below).
    out.append('<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>')
    # Nodes: the four reshaping outlines first (placeholders, unfilled, every point distinct).
    nodes = []
    bounds = []   # each node's outline's reach about its origin, px, as authored (before its node's scale)
    for p in range(PATHS):
        pts = [((i % 8 if (i // 8) % 2 == 0 else 7 - i % 8), i // 8) for i in range(POINTS)]
        out.append(f'<path id="{ID}_node_{p}" d="{d_of(pts)}" fill="none" stroke="{HERO[0]}" stroke-width="{fmt(HERO[1])}"/>')
        nodes.append(None)
        bounds.append(None)
    defaults = kit_defaults()
    for prt in parts:
        i = len(nodes)
        st, w = prt["style"]
        kk = prt["k"]
        fill = prt["fill"] or "none"
        # A node's stroke scales with it: a part a control draws larger than
        # authored at the specimen's defaults is authored that much thinner,
        # so it draws its style's width there.
        grown = drawn_scale(prt, defaults[prt["model"]])
        sw = fmt(w / kk / (grown if grown > 1.05 else 1))
        name = MODELS[prt["model"]] + " " + prt["name"]
        top = max([hi for c, kd, lo, hi in prt["fx"] if kd == SCALE] + [lo for c, kd, lo, hi in prt["fx"] if kd == SCALE] + [1024])
        assert kk * top <= 2048 or kk == 1, (name, "scales past 2")
        if isinstance(prt["shape"], tuple):
            _, r, cx, cy = prt["shape"]
            assert abs(cx / kk) + r / kk <= 64 and abs(cy / kk) + r / kk <= 64, name
            out.append(f'<circle id="{ID}_node_{i}" cx="{fmt(cx / kk)}" cy="{fmt(cy / kk)}" r="{fmt(r / kk)}" fill="{fill}" stroke="{st}" stroke-width="{sw}"/>')
            bounds.append(((cx - r) / kk, (cy - r) / kk, (cx + r) / kk, (cy + r) / kk))
        else:
            shape = prt["shape"]
            if len(shape) >= 12:   # a long outline: drop points the eye can't place (a ribbon's are dropped already)
                shape = simplify(shape + [shape[0]], 0.25)[:-1]
            pts = snapped(blunt(shape, kk), kk)
            check(pts, name)
            out.append(f'<path id="{ID}_node_{i}" d="{d_of(pts)}" fill="{fill}" stroke="{st}" stroke-width="{sw}"/>')
            bounds.append((min(x for x, _ in pts), min(y for _, y in pts), max(x for x, _ in pts), max(y for _, y in pts)))
        nodes.append(prt)
    header = len(nodes)
    greys = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i, g in enumerate(greys):
        out.append(f'<path id="{ID}_node_{header + i}" transform="matrix(1 0 0 1 {60 * i} 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="{g}" stroke="{g}" stroke-width="0.5"/>')
    node_count = header + 8
    assert node_count <= 512
    # Text slots: the header's 16.
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
    # The palette, lent to the reshaped outlines: three spares, a hit's flash,
    # the hero's grey, structure, engraving, then the hero's and structure's
    # for an outline drawn at half size and shown at twice it (a palette
    # entry's width scales with its node, so these are half the width they
    # draw). Each outline draws 1 px; a line is a half-pixel band, 1.5 px across.
    palette = [("#000000", "#000000", 1.0), ("#2A2A2A", "#555555", 0.6), ("#D8D8D8", "#E8E8E8", 0.6),
               ("#141414", "#FFFFFF", 1.2), ("#141414", "#E2E2E2", 1.0), ("#141414", "#8A8A8A", 1.0),
               ("#141414", "#868686", 0.8), ("#141414", "#E2E2E2", 0.5), ("#141414", "#8A8A8A", 0.5)]
    assert len(palette) == PALETTE
    for i, (f, s, w) in enumerate(palette):
        out.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{f}" stroke="{s}" stroke-width="{fmt(w)}"/>')
    out.append("</g>")
    svg = "\n".join(out) + "\n"
    assert not re.search(r"(d|r|cx|cy)=\"[^\"]*\d[eE]", svg), "exponent in the artwork"
    assert len(svg) <= 65536, f"artwork is {len(svg)} bytes, over 64 KB"
    (ROOT / "panel.svg.fragment").write_text(svg)

    # art.h
    by_model = {mm: [] for mm in range(16)}
    for i, prt in enumerate(nodes):
        if isinstance(prt, dict):
            by_model[prt["model"]].append(i)
    rows = []
    for i, prt in enumerate(nodes):
        if not isinstance(prt, dict):
            continue
        assert len(prt["fx"]) <= 4, prt["name"]
        fx = (prt["fx"] + [(0, 0, 0, 0)] * 4)[:4]
        hk, ha = prt["hit"] or (0, 0)
        hc, ht, hb = prt["hide"] or (255, 0, 0)
        x, y = prt["pivot"]
        eff = ", ".join(f"{{{c}, {kd}, {lo}, {hi}}}" for c, kd, lo, hi in fx)
        rows.append(f"    {{{i}, {round(x * 4)}, {round(y * 4)}, {1024 * prt['k']}, {{{eff}}}, {hk}, {ha}, {hc}, {ht}, {hb}}},   // {MODELS[prt['model']]}: {prt['name']}")
    firsts, counts = [], []
    kk = 0
    for mm in range(16):
        firsts.append(kk)
        counts.append(len(by_model[mm]))
        kk += len(by_model[mm])
    text = f"""#pragma once
// Written by tools/make_panel.py: edit the drawing there, then run it.
#include <opxy/types.hpp>

namespace curio {{
namespace art {{
using opxy::i16;
using opxy::u8;
using opxy::u16;
const unsigned node_count = {node_count}, header_node = {header};
enum Effect : u8 {{ none = 0, dx = 1, dy = 2, scale = 3, rot = 4 }};
// A hit's motion: kicks and rings in px or tenths of a degree; the ball's, the
// drop's and the cork's fall; rising; a flash; the ratchet's wheel turning a
// tooth a tooth; a pendulum swinging to an angle; the chalk at its stroke's tip;
// the bull-roarer's slat on its orbit and its trailing ghosts.
enum Hit : u8 {{ still = 0, kick_dx, kick_dy, kick_rot, ring_dx, ring_dy, ring_rot, fall, rise, flash, swing,
                strike, tip, orbit, ghost }};
// node; pivot x, y (quarter pixels); base scale (Q10: 2048 for a part drawn at
// half size); four control effects {{control, kind, value at 0, at 127}} (px,
// Q10 scale or tenths of a degree); a hit's motion and its size; hidden while a
// control is below (or, last 1, at or above) a Q12 value.
struct Effect4 {{ u8 c, k; i16 lo, hi; }};
struct Part {{ u16 node; i16 x, y, base; Effect4 fx[4]; u8 hit; i16 size; u8 hide_c; i16 hide_at; u8 hide_above; }};
const Part parts[{len(rows)}] = {{
{chr(10).join(rows)}
}};
// Each specimen's parts: first entry and count.
const u16 first[16] = {{{", ".join(map(str, firsts))}}};
const u8 count[16] = {{{", ".join(map(str, counts))}}};
// Each specimen's move to the screen's middle, px: its parts' pivots carry it;
// the display adds it to its reshaped outlines.
const i16 ox[16] = {{{", ".join(str(d[0]) for d in OFFSET)}}};
const i16 oy[16] = {{{", ".join(str(d[1]) for d in OFFSET)}}};
}}
}}
"""
    (ROOT / "art.h").write_text(text)
    # tests/bounds.h: each node's reach, for the test that keeps the specimens out of the header.
    rows_b = []
    for i, b in enumerate(bounds):
        if b is None:
            rows_b.append("    {0, 0, 0, 0},   // a reshaped outline: its points are in the frame")
        else:
            rows_b.append("    {" + ", ".join(f"{v:.2f}f" for v in b) + f"}},   // {i}")
    (ROOT / "tests" / "bounds.h").write_text(f"""#pragma once
// Written by tools/make_panel.py: each node's outline as authored, its reach
// about the node's origin in px before the node's scale: left, top, right, bottom.
namespace curio {{
namespace bounds {{
const float node[{len(bounds)}][4] = {{
{chr(10).join(rows_b)}
}};
}}
}}
""")
    # The display's node and palette counts in definition.json follow the artwork.
    d = (ROOT / "definition.json").read_text()
    d = re.sub(r'"nodes": \d+', f'"nodes": {node_count}', d)
    d = re.sub(r'"palette": \d+', f'"palette": {PALETTE}', d)
    (ROOT / "definition.json").write_text(d)
    per = [0] * 16
    for line in out:
        mm = re.search(r'_node_(\d+)"', line)
        if mm and int(mm.group(1)) < len(nodes) and isinstance(nodes[int(mm.group(1))], dict):
            per[nodes[int(mm.group(1))]["model"]] += len(line) + 1
    print(f"{node_count} nodes ({len(rows)} specimen parts), {len(svg)} bytes of artwork")
    print("bytes by specimen:", ", ".join(f"{MODELS[i]} {per[i]} ({counts[i]})" for i in range(16)))
    assert not SHARP, f"outlines turning sharper than {SHARPEST} degrees: {SHARP}"


if __name__ == "__main__":
    main()
