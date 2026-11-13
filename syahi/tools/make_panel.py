#!/usr/bin/env python3
"""Writes Syahi's screen artwork (panel.svg.fragment) and the tables its
display reads (art.h): an engraved plate of the struck object, in section on
the left and seen from above on the right.

    python3 syahi/tools/make_panel.py [--preview out.svg]

The four reshaping outlines (the head, the shell, the coupling and the
striker) are shaped by display.cpp from the keyframes here; everything else
is a fixed part the display moves, turns, scales or recolours.
"""
import argparse
import math
import pathlib

HERE = pathlib.Path(__file__).resolve().parent.parent
ID = "syahi"

# ------------------------------------------------------------------ the plate
# Everything in the section is authored in local units (px at a gain of 1) and
# drawn at SECTION_GAIN times TUNE's scale (1.0 .. 0.8), standing on the floor:
# each model's floor lies FLOOR_DEPTH below its head's plane, so the object
# fills the stage from the header to the floor line at any TUNE. The plan is
# drawn at PLAN_GAIN times its own TUNE scale (1.0 .. 0.86) round its centre.
CX = 150                   # the section's centre line
PX, PY = 380, 124          # the plan's centre
SECTION_GAIN, PLAN_GAIN, STRIKER_GAIN = 1.25, 1.06, 1.15
A_NOMINAL = 60             # the head's half-width at TUNE 0, local
R_PLAN = 70                # the plan's rim at TUNE 0, local
FLOOR = 212
MODELS = ["BOOM", "SNARE", "TOM", "TABLA", "BAR", "METAL", "BOUNCE", "WIRE"]
DRUMS = ["BOOM", "SNARE", "TOM", "BOUNCE", "WIRE"]
# Each model's depth below the head at TUNE 0 (BOOM's moves with SHELL), and its head's width.
DEPTH = {"BOOM": 70, "SNARE": 44, "TOM": 78, "TABLA": 88, "BAR": 12, "METAL": 112, "BOUNCE": 30, "WIRE": 84}
# Where the floor lies below each model's head (local): what it stands on reaches it.
FLOOR_DEPTH = {"BOOM": 106, "SNARE": 108, "TOM": 104, "TABLA": 100, "BAR": 108, "METAL": 100, "BOUNCE": 104, "WIRE": 108}
HEAD_W = {"TABLA": 0.8}
TABLA_A = A_NOMINAL * 0.8
TP = 0.9                   # the tabla's head from above, as a share of a drum's
CYMBAL = 1.1               # the cymbal from above, larger than a drum's head (it has no hoop)
BAR_Y = 19                 # the bar and its twin from above, either side of the plan's centre
KEY_Y = 57                 # the keys either side of them
NODE = 0.552               # a free bar's first nodes, as a share of its half-length

# Palette: the plate's few greys and weights (fill, stroke, width). Every line
# draws about 1 px wide on the screen, as stock Drumline's do. A palette
# entry's stroke width scales with its node: the section's parts are drawn at
# about 1 (1.25 times TUNE's 1.0 .. 0.8), the plan's at about 1, and the four
# reshaping outlines at 1.25, so the outlines take entries of their own (the
# subject, struck and path line) at 0.8 of a pixel; the near ring, drawn at
# about half, takes about 2. Lines between outlines and ribbons (the coupling,
# the parts' engraving) draw up to about 1.4 across. Greys: F0 subject, C8
# structure, 96 detail, 84 dim, 7A engraving (the faintest a line about a pixel
# wide still reads at on the device's panel).
PALETTE = [
    ("#000000", "#000000", 1.0),   # 0 black (a port, a hole)
    ("none", "#F0F0F0", 0.83),     # 1 subject: the head, the striker (paths at 1.15 to 1.25)
    ("none", "#C8C8C8", 1.0),      # 2 structure: rims, stands, the coupling's wires and spring
    ("none", "#969696", 1.0),      # 3 detail; also the nodal lines faintly lit
    ("none", "#848484", 1.0),      # 4 dim
    ("none", "#7A7A7A", 1.0),      # 5 engraving
    ("#2E2E2E", "#969696", 0.9),   # 6 paste: the syahi, felts, bolts
    ("#141414", "#969696", 1.0),   # 7 cloth and dark bodies
    ("none", "#FFFFFF", 0.85),     # 8 struck: the head and the nodal lines lit by a hit
    ("none", "#C8C8C8", 1.9),      # 9 ring, near (drawn at about half)
    ("#8C8C8C", "#D2D2D2", 0.9),   # 10 chrome: hoops, lugs, gatte, nuts
    ("#C8C8C8", "#C8C8C8", 0.6),   # 11 solid mark
    ("#0E0E0E", "#0E0E0E", 0.5),   # 12 shadow: the inside of a shell
    ("none", "#C8C8C8", 0.8),      # 13 structure on a reshaping outline (the shell, at 1.25)
    ("none", "#B8B8B8", 0.95),     # 14 nodal line, lit
    ("#202020", "#C8C8C8", 0.95),  # 15 dark body with a structure line
]
SUBJECT, LINE, MID, DIM, FAINT, PASTE, CLOTH, STRUCK, RING1, CHROME, MARK, SHADOW, PATH_LINE, NODAL2, BODY = range(1, 16)
BLACK = 0


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def ribbon(points, width=0.5):
    """A thin closed ribbon along a polyline: out along one edge, back along the other."""
    left, right = [], []
    n = len(points)
    for i, (x, y) in enumerate(points):
        a = points[max(0, i - 1)]
        b = points[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L * width / 2, dx / L * width / 2
        left.append((x + nx, y + ny))
        right.append((x - nx, y - ny))
    return left + right[::-1]


def path_d(points, close=True):
    return "M" + " L".join(f"{fmt(x)} {fmt(y)}" for x, y in points) + (" Z" if close else "")


def circle_points(r, n=24, cx=0, cy=0, start=0.0):
    return [(cx + r * math.cos(start + 2 * math.pi * i / n), cy + r * math.sin(start + 2 * math.pi * i / n)) for i in range(n)]


def catmull(points, per=6):
    """A smooth curve through the points (open), `per` samples per span."""
    out = []
    pts = [points[0]] + list(points) + [points[-1]]
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for j in range(per):
            t = j / per
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2
                                    + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in range(2)))
    out.append(points[-1])
    return out


def resample(points, n, closed=True):
    """n points evenly spaced along the outline (by length)."""
    pts = list(points) + ([points[0]] if closed else [])
    lengths = [0.0]
    for a, b in zip(pts, pts[1:]):
        lengths.append(lengths[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = lengths[-1]
    out = []
    j = 0
    for i in range(n):
        s = total * i / (n if closed else n - 1)
        while j < len(lengths) - 2 and lengths[j + 1] < s:
            j += 1
        seg = lengths[j + 1] - lengths[j] or 1
        t = (s - lengths[j]) / seg
        out.append((pts[j][0] + (pts[j + 1][0] - pts[j][0]) * t, pts[j][1] + (pts[j + 1][1] - pts[j][1]) * t))
    return out


# ------------------------------------------------------------------ the head in section
def head_anchor(k, x):
    """FORM anchor k at x in -1..1: (top offset, thickness) in px, for a half-width of 60."""
    t, th = head_anchor46(k, x)
    return t * 1.25, 1.4 + (th - 1.4) * 1.25


def head_anchor46(k, x):
    ax = abs(x)
    edge = min(1.0, (1 - ax) * 14)
    if k == 0:   # sine: a broad pad of paste over the whole head
        pad = 4.0 * max(0.0, 1 - x * x) ** 1.4
        return -pad, 1.4 + pad
    if k == 1:   # load: the syahi's lens, built up on the head
        lens = 5.5 * max(0.0, 1 - (x / .42) ** 2) ** .6
        return -lens, 1.4 + lens
    if k == 2:   # skin
        return 0.0, 1.4
    if k == 3:   # wood: a slab
        return 0.0, 9.0 * edge + 1.4
    if k == 4:   # bar: thick ends, the arch undercut between the nodes
        return 0.0, (11.5 - 7.8 * max(0.0, 1 - (x / .64) ** 2) ** .7) * edge + 1.4
    if k == 5:   # plate: a cymbal's bell, its bow, and an edge that thins out
        bell = 5.4 * max(0.0, 1 - (x / .24) ** 2) ** .45
        bow = 8.0 * (1 - ax) ** 1.3
        th = 2.3 if ax < .24 else 1.9 - 0.8 * min(1.0, (ax - .24) / .76) ** 1.5
        return -bow - bell, th
    return -17 * max(0.0, 1 - x * x) ** .75 - 4 * max(0.0, 1 - (x / .16) ** 2) ** .5, 2.0   # bell


HEAD_N = 31
HEAD_X = [-1 + 2 * i / (HEAD_N - 1) for i in range(HEAD_N)]


def q4(v):
    return int(round(v * 16))


# ------------------------------------------------------------------ shells, in section
# Each is a closed ribbon of the cut wall, in px around the head's centre (y
# down from the head's plane), at TUNE 0; the display scales it with TUNE and
# centres it on its node. Two keyframes per model, mixed by a control.
def shell_drum(depth, a=A_NOMINAL, bulge=0.0, foot=0.0, pot=0.0, wall=4.6, lip=2.2, bottom_head=True):
    """A drum's cut wall: down the left side, across the bottom head, up the right."""
    side = 13
    def rx(s):
        barrel = 1 + bulge * math.sin(math.pi * min(1, s * 1.1)) - foot * s
        potty = max(0.92, 1.26 * math.sqrt(max(0.0, 1 - ((s - .58) / .62) ** 2)))
        return a * ((1 - pot) * barrel + pot * potty)
    outer, inner = [], []
    for i in range(side):
        s = i / (side - 1)
        y = 2 + s * depth + pot * 10 * s * s
        r = rx(s)
        outer.append((-r - wall / 2, y))
        inner.append((-r + wall / 2, y))
    base = outer[-1][1]
    k = 7
    bot_o = [(-rx(1) - wall / 2 + (2 * rx(1) + wall) * j / k, base + lip + pot * 8 * math.sin(math.pi * j / k)) for j in range(1, k)]
    bot_i = [(-rx(1) + wall / 2 + (2 * rx(1) - wall) * j / k, base - lip + pot * 8 * math.sin(math.pi * j / k)) for j in range(1, k)]
    if not bottom_head:
        bot_i = [(x, y - 2.5) for x, y in bot_i]
    pts = outer + bot_o + [(-x, y) for x, y in outer[::-1]] + [(-x, y) for x, y in inner] + bot_i[::-1] + inner[::-1]
    return pts


TUBE_TOP, TUBE_W = 15.0, 10.0


def shell_tube(length):
    """BAR: the resonator hanging under the bar's centre: a flared mouth just under
    the bar, the tube's wall drawn as a thin cut, its foot closed by a stopper."""
    w, top, t = TUBE_W, TUBE_TOP, 1.6
    bottom = top + length
    def side(sx, inset):
        pts = [(sx * (w + 3.4 - inset), top), (sx * (w + 1.4 - inset * .6), top + 1.6), (sx * (w - inset), top + 4.5)]
        pts += [(sx * (w - inset), top + 4.5 + (length - 4.5) * i / 8) for i in range(1, 9)]
        return pts
    left, right = side(-1, 0), side(1, 0)
    li, ri = side(-1, t), side(1, t)
    across = [(-w + 2 * w * j / 5, bottom + 0.8) for j in range(1, 5)]
    across_in = [(-w + t + (2 * w - 2 * t) * j / 5, bottom - 0.8) for j in range(1, 5)]
    # Round from the mouth's left lip, down, across the foot, up the right; back down inside.
    return left + across + right[::-1] + [(x, y + 0.6) for x, y in ri[:1]] + ri[1:] + across_in[::-1] + li[::-1][:-1] + [(li[0][0], li[0][1] + 0.6)]


def shell_stand():
    """METAL: the stand from the tilter down: a thin upper tube, the height clamp's
    step, a wider lower tube to the tripod's collar."""
    top, joint, foot = 14.0, 52.0, 84.0
    up = [(-1.7, top + (joint - top) * i / 6) for i in range(7)]
    low = [(-2.8, joint + 0.8 + (foot - joint - 0.8) * i / 6) for i in range(7)]
    left = up + low
    right = [(-x, y) for x, y in left[::-1]]
    return left + [(-1.0, foot + 0.6), (1.0, foot + 0.6)] + right


def shell_keyframes():
    return {
        "BOOM": (shell_drum(70), shell_drum(96)),
        "SNARE": (shell_drum(44, lip=1.8), shell_drum(44, lip=1.8)),
        "TOM": (shell_drum(78), shell_drum(78)),
        "TABLA": (shell_drum(86, a=TABLA_A, bulge=.06, foot=.04, wall=5.0), shell_drum(76, a=TABLA_A, pot=1.0, wall=5.0)),
        "BAR": (shell_tube(26), shell_tube(80)),
        "METAL": (shell_stand(), shell_stand()),
        "BOUNCE": (shell_drum(30, lip=1.6), shell_drum(30, lip=1.6)),
        "WIRE": (shell_drum(84, wall=3.8, bottom_head=False), shell_drum(84, wall=3.8, bottom_head=False)),
    }


def centred(points, mid):
    return [(x, y - mid) for x, y in points]


# ------------------------------------------------------------------ strikers
# Each striker is one closed outline in px around its origin, the contact point
# at TIP; keyframes share a parameterisation so they morph point to point.
TIP = (34.0, 30.0)
STRIKER_N = 60


def outline_from(skeleton, widths, cap=4, tail=2):
    """A tapered outline round a skeleton (list of points), widths per point,
    with a rounded end at the last point and a square-ish end at the first."""
    up, dn = [], []
    n = len(skeleton)
    for i, (x, y) in enumerate(skeleton):
        a = skeleton[max(0, i - 1)]
        b = skeleton[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L, dx / L
        w = widths[i] / 2
        up.append((x + nx * w, y + ny * w))
        dn.append((x - nx * w, y - ny * w))
    # the rounded end
    x, y = skeleton[-1]
    a = skeleton[-2]
    ux, uy = x - a[0], y - a[1]
    L = math.hypot(ux, uy) or 1
    ux, uy = ux / L, uy / L
    nx, ny = -uy, ux
    w = widths[-1] / 2
    end = []
    for j in range(1, cap):
        t = math.pi * j / cap
        end.append((x + nx * w * math.cos(t) + ux * w * math.sin(t), y + ny * w * math.cos(t) + uy * w * math.sin(t)))
    return up + end + dn[::-1]


def place_tip(points, tip):
    dx, dy = TIP[0] - tip[0], TIP[1] - tip[1]
    return [(x + dx, y + dy) for x, y in points]


def striker_brush(angle=-52, length=88):
    """A wire brush: a handle, a ferrule, then the wires fanning out to a ragged edge."""
    a = math.radians(angle)
    ux, uy = math.cos(a), math.sin(a)
    nx, ny = -uy, ux
    ferrule = 24.0
    pts = []
    # down the handle's upper side to the ferrule
    for i in range(8):
        u = length - (length - ferrule) * i / 7
        pts.append((u, 2.0 - 0.3 * i / 7))
    # the fan's upper edge out to its corner
    for i in range(1, 5):
        t = i / 4
        pts.append((ferrule * (1 - t), 1.7 + 9.5 * t))
    # the ragged edge of wire ends across the fan (the contact in its middle)
    for i in range(1, 12):
        t = i / 12
        v = 11.2 - 22.4 * t
        pts.append(((1.6 if i % 2 else -0.4) * (1 - 0.6 * abs(v) / 11), v))
    for i in range(0, 4):
        t = i / 4
        pts.append((ferrule * t, -11.2 + 9.5 * t))
    for i in range(8):
        u = ferrule + (length - ferrule) * i / 7
        pts.append((u, -1.7 - 0.3 * i / 7))
    pts = [(u * ux + v * nx, u * uy + v * ny) for u, v in pts]
    pts = resample(pts, STRIKER_N)
    # the contact: the middle of the wire ends
    tip = min(pts, key=lambda p: p[0] * ux + p[1] * uy + 0 * p[1])
    return place_tip(pts, (0.6 * ux, 0.6 * uy))


def striker_stick(tip_r, bristles=0.0, angle=-52, length=88):
    """A stick (or brush) striking down and to the right, its tip at the contact point."""
    a = math.radians(angle)
    ux, uy = math.cos(a), math.sin(a)        # from the tip up the shaft
    sk = [(ux * length * (1 - i / 9), uy * length * (1 - i / 9)) for i in range(10)]
    widths = [4.2, 4.0, 3.8, 3.4, 3.0, 2.8, 2.6, 2.4, 2.2 + tip_r, 2 * tip_r]
    pts = outline_from(sk, widths, cap=7)
    if bristles > 0:
        # A brush: the wires fan out past the ferrule as a spray of points.
        fan = []
        base = sk[-3]
        for j in range(9):
            t = -0.6 + 1.2 * j / 8
            r = 22 * bristles
            fan.append((base[0] - ux * r * math.cos(t) + uy * r * math.sin(t) * 0.9,
                        base[1] - uy * r * math.cos(t) - ux * r * math.sin(t) * 0.9))
            fan.append((base[0] - ux * r * 0.55 * math.cos(t + .07) + uy * r * .5 * math.sin(t + .07),
                        base[1] - uy * r * 0.55 * math.cos(t + .07) - ux * r * .5 * math.sin(t + .07)))
        # Mix the fan into the cap region (the points after the shaft's up side).
        k = 10
        cap = pts[k:k + 6]
        mixed = []
        for j, p in enumerate(cap):
            q = fan[int(j * (len(fan) - 1) / max(1, len(cap) - 1))]
            mixed.append((p[0] + (q[0] - p[0]) * bristles, p[1] + (q[1] - p[1]) * bristles))
        pts = pts[:k] + mixed + pts[k + 6:]
    pts = resample(pts, STRIKER_N)
    tip = max(pts, key=lambda p: p[1])
    return place_tip(pts, (0, max(p[1] for p in pts)) if False else tip)


def striker_mallet(ball, length=84, angle=-56):
    """A mallet: a shaft and a ball whose size is the softness."""
    a = math.radians(angle)
    ux, uy = math.cos(a), math.sin(a)
    cxb, cyb = ux * ball, uy * ball          # the ball's centre, its bottom at the tip
    pts = []
    sw = 1.5
    nx, ny = -uy, ux
    shaft0 = (cxb + ux * ball * 0.8, cyb + uy * ball * 0.8)
    shaft1 = (ux * length, uy * length)
    for i in range(8):
        t = i / 7
        pts.append((shaft0[0] + (shaft1[0] - shaft0[0]) * t + nx * sw, shaft0[1] + (shaft1[1] - shaft0[1]) * t + ny * sw))
    pts.append((shaft1[0] + ux * 2, shaft1[1] + uy * 2))
    for i in range(8):
        t = 1 - i / 7
        pts.append((shaft0[0] + (shaft1[0] - shaft0[0]) * t - nx * sw, shaft0[1] + (shaft1[1] - shaft0[1]) * t - ny * sw))
    a0 = math.atan2(-uy, -ux)
    gap = math.asin(min(0.9, sw / ball))
    for j in range(1, 30):
        t = (a0 + math.pi) - gap - (2 * math.pi - 2 * gap) * j / 30 + 2 * math.pi
        pts.append((cxb + ball * math.cos(t), cyb + ball * math.sin(t)))
    pts = resample(pts, STRIKER_N)
    tip = max(pts, key=lambda p: p[1])
    return place_tip(pts, tip)


def striker_beater(kind, dims=None, angle=-74, length=72):
    """A kick pedal's beater on its shaft: felt ball (0), a wooden cylinder (1),
    a plastic head with a flat face (2). With dims, any head: (along, across, squareness)."""
    ang = math.radians(angle)
    ux, uy = math.cos(ang), math.sin(ang)    # up the shaft from the tip
    nx, ny = -uy, ux
    w, h, sq = dims or [(8.5, 8.5, 2.0), (5.5, 9.0, 4.0), (3.8, 11.0, 8.0)][kind]
    # the head: a superellipse, u along the shaft (0 at the face), v across it
    head = []
    for j in range(32):
        t = math.pi / 2 + 2 * math.pi * j / 32
        c, sn = math.cos(t), math.sin(t)
        u = w + w * math.copysign(abs(c) ** (2 / sq), c)
        v = h * math.copysign(abs(sn) ** (2 / sq), sn)
        head.append((u, v))
    sw = 1.4
    # leave the head where the shaft joins its back (u = 2w), round, and up the shaft
    head = [(u, v) for u, v in head if not (u > 2 * w - 0.6 and abs(v) < sw)]
    k = max(range(len(head)), key=lambda i: head[i][0] - abs(head[i][1]))
    head = head[k:] + head[:k]
    shaft = [(2 * w + (length - 2 * w) * i / 8, -sw) for i in range(9)] + [(length - (length - 2 * w) * i / 8, sw) for i in range(9)]
    loop = shaft + head[1:] if head[0][1] < 0 else shaft + head
    pts = [(u * ux + v * nx, u * uy + v * ny) for u, v in loop]
    pts = resample(pts, STRIKER_N)
    tip = max(pts, key=lambda p: p[1])
    return place_tip(pts, tip)


def striker_hammer(steel):
    """WIRE: a hammer: a soft round head (0) to a steel head with a flat face (1)."""
    return striker_beater(0 if steel < .5 else 2) if False else _hammer(steel)


def _hammer(steel):
    length = 80
    ang = math.radians(-35)
    ux, uy = math.cos(ang), math.sin(ang)
    nx, ny = -uy, ux
    # the head lies across the handle's end: a block, rounded when soft
    hw, hh = 6 + 2 * steel, 8 - 1 * steel
    rnd = 1 - steel
    head = []
    for j in range(30):
        t = 2 * math.pi * j / 30
        c, s = math.cos(t), math.sin(t)
        p = 2 / (2 + 8 * steel)
        head.append((hw * math.copysign(abs(c) ** p, c), hh * math.copysign(abs(s) ** p, s)))
    # the head's long axis is the handle's normal; its face (along -normal... ) down
    centre = (ux * 10, uy * 10)
    def to(p):
        return (centre[0] + p[0] * ux + p[1] * nx, centre[1] + p[0] * uy + p[1] * ny)
    hd = [to(p) for p in head]
    sw = 1.6
    back = to((0, 0))
    top = (ux * length, uy * length)
    shaft = [(back[0] + (top[0] - back[0]) * i / 9 + nx * sw, back[1] + (top[1] - back[1]) * i / 9 + ny * sw) for i in range(10)]
    shaft += [(top[0] - nx * sw + (back[0] - top[0]) * i / 9, top[1] - ny * sw + (back[1] - top[1]) * i / 9) for i in range(10)]
    k = min(range(len(hd)), key=lambda i: math.hypot(hd[i][0] - (back[0] + nx * hh), hd[i][1] - (back[1] + ny * hh)))
    hd = hd[k:] + hd[:k]
    pts = shaft + hd
    pts = resample(pts, STRIKER_N)
    tip = max(pts, key=lambda p: p[1])
    return place_tip(pts, tip)


# ------------------------------------------------------------------ the hand
# The tabla's and the bounce's hand is Max's drawing of a pointing hand,
# traced: the forearm from the left, the index finger out, the thumb over the
# fist and three curled fingers stacked under it. Its lines were followed at
# the drawing's own proportions (thinned to their centres, smoothed and fitted
# within 0.6 of the drawing's pixels) and are kept here in its pixels (636 x
# 304); the forearm is cut at x 200. It is static artwork: a filled
# silhouette that hides the drum behind it, the outline in two pieces, the
# thumb's line (running on into the crease where the wrist meets the palm),
# the index's underside where it meets the thumb, and the three finger rolls.
# The display only turns it about the wrist and moves it.
HAND_TRACE = {
    "fill": [(200, 97), (216, 96.9), (229, 94.6), (307, 68.7), (352, 56.1), (373, 56.1), (402, 60), (427, 60), (505, 53.9), (578, 43), (588, 45.4), (596, 50.2), (601.1, 58), (602, 65), (601.3, 73), (597.7, 80.4), (591.6, 86.9), (584, 91), (462.5, 112), (462.5, 114.1), (470.8, 119), (475.9, 125), (478.8, 140), (473.1, 151.8), (459.8, 159.9), (469.4, 170.7), (470.8, 177), (469.8, 184), (462.9, 198), (450.9, 204.8), (452.6, 223), (449.5, 231), (444, 235.5), (428, 237), (413.9, 235.2), (344, 242.9), (321, 243), (294, 238.9), (253, 226.2), (200, 226.1)],
    "outer": [(200, 97), (216, 96.9), (229, 94.6), (307, 68.7), (330.2, 62.5), (341, 58.4), (352, 56.1), (373, 56.1), (402, 60), (427, 60), (462, 56.1), (505, 53.9), (578, 43), (588, 45.4), (596, 50.2), (601.1, 58), (602, 65), (601.3, 73), (597.7, 80.4), (591.6, 86.9), (584, 91), (577, 92.8), (561, 94.2), (537, 98.2), (462.5, 112), (462.5, 114.1), (470.8, 119), (475.9, 125), (478.6, 133), (478.8, 140), (476.6, 147), (473.1, 151.8), (468.1, 156), (459.8, 159.9), (459.8, 161.3), (466, 166.3), (469.4, 170.7), (470.8, 177), (469.8, 184), (467.1, 192), (462.9, 198), (459, 201.1), (452.2, 203.5), (450.9, 204.8), (450.7, 206.8), (452.9, 214), (452.6, 223), (449.5, 231), (444, 235.5), (439, 236.9), (428, 237), (413.9, 235.2), (398, 237.9), (378, 239.1), (344, 242.9), (321, 243), (294, 238.9), (280, 235.3), (265, 229.3), (253, 226.2), (241, 225), (200, 226.1)],
    "thumb": [(333, 63), (350, 65.4), (369, 71), (385, 77.8), (398.3, 86.5), (407.6, 97), (421.8, 128), (430.7, 141.4), (431.7, 145), (430.4, 148.7), (426.1, 152.4), (419, 154), (413, 153.6), (400, 149.7), (393.8, 146.3), (387.5, 140.2), (378.7, 123.3), (361, 115.8), (358, 115.4), (335.8, 137), (320.1, 149.8), (310.6, 156.1), (289, 167), (277, 170.6), (263, 173)],
    "under": [(460, 113), (441, 114.9), (423, 117)],
    "roll1": [(457, 160), (433, 160), (419, 162), (405, 161.9), (395, 160.4), (388, 158), (382.6, 153.2), (380.8, 150), (380.2, 147), (382, 142)],
    "roll2": [(449, 204), (445.8, 203.1), (429, 203), (416, 201.8), (403, 199.8), (393, 197.1), (384.1, 190.2), (380.4, 184), (379.3, 178), (382, 170)],
    "roll3": [(411, 235), (408.6, 233.4), (397, 229.6), (387, 225.2), (379, 219.8), (374.7, 215), (371.5, 209), (371.5, 203), (375.3, 196), (381, 191)],
}
HAND_PIVOT = (255.0, 160.0)   # the wrist, in the drawing's pixels: the hand turns about it
HAND_SCALE = 1.2              # drawn at 1.2, where the subject's stroke is 1 px
HAND_PX = 0.155 / HAND_SCALE  # the drawing's pixels to artwork px: 0.155 of them on the screen
HAND_RIBBON = 0.2             # the lines as thin closed ribbons
# BOUNCE's drumstick, held in the fist under the index (a pointer grip): from
# inside the fist out past the fingertip, in the drawing's pixels. HANDS slides
# it back through the grip by STICK_SLIDE, into the palm, until it is hidden.
STICK_FROM, STICK_TO, STICK_W, STICK_SLIDE = (440.0, 152.0), (680.0, 177.0), 13.0, 240.0


def hand_px(points):
    """To artwork px about the wrist, dropping a point within a drawing pixel of the last kept."""
    kept = []
    for i, (x, y) in enumerate(points):
        if kept and math.hypot(x - kept[-1][0], y - kept[-1][1]) < 1.0:
            if i < len(points) - 1:
                continue
            kept.pop()   # keep a line's last point where it is
        kept.append((x, y))
    return [((x - HAND_PIVOT[0]) * HAND_PX, (y - HAND_PIVOT[1]) * HAND_PX) for x, y in kept]


def stick_outline():
    """The stick: a straight shaft with a rounded tip, in the drawing's pixels."""
    (x0, y0), (x1, y1) = STICK_FROM, STICK_TO
    L = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / L, (y1 - y0) / L
    nx, ny, w = -uy, ux, STICK_W / 2
    tip = [(x1 - ux * w + nx * w * math.cos(math.pi * k / 4) + ux * w * math.sin(math.pi * k / 4),
            y1 - uy * w + ny * w * math.cos(math.pi * k / 4) + uy * w * math.sin(math.pi * k / 4)) for k in range(5)]
    return [(x0 + nx * w, y0 + ny * w), (x0 + (x1 - x0) / 2 + nx * w, y0 + (y1 - y0) / 2 + ny * w)] + tip + \
           [(x0 + (x1 - x0) / 2 - nx * w, y0 + (y1 - y0) / 2 - ny * w), (x0 - nx * w, y0 - ny * w)]


def hand_ribbon(points, width):
    """ribbon(), with an edge's point dropped where it bunches up on the inside
    of a tight turn (closer than 1/8 px as drawn to the last kept)."""
    loop = ribbon(points, width)
    n = len(points)
    edges = [loop[:n], loop[n:]]
    out = []
    for e in edges:
        kept = [e[0]]
        for q in e[1:]:
            if max(abs(q[0] - kept[-1][0]), abs(q[1] - kept[-1][1])) * HAND_SCALE >= 0.15:
                kept.append(q)
        out += kept
    return out


def crosses(points):
    """True when a closed outline crosses itself."""
    n = len(points)

    def side(a, b, c):
        v = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        return (v > 1e-12) - (v < -1e-12)
    for i in range(n):
        for j in range(i + 2, n):
            if i == 0 and j == n - 1:
                continue
            p, q, r, t = points[i], points[(i + 1) % n], points[j], points[(j + 1) % n]
            if side(p, q, r) * side(p, q, t) < 0 and side(r, t, p) * side(r, t, q) < 0:
                return True
    return False


def hand_nodes():
    """The hand's static parts, in drawing order: the stick (under the hand,
    which hides its grip), the silhouette's fill, then the lines."""
    n = []
    n.append(Node("hand_stick", ("path", hand_px(stick_outline())), 0, 0, BODY, True, ["BOUNCE"], "abs"))
    n.append(Node("hand_fill", ("path", hand_px(HAND_TRACE["fill"])), 0, 0, BLACK, True, ["TABLA", "BOUNCE"], "abs"))
    outer = HAND_TRACE["outer"]
    half = len(outer) // 2
    lines = [("hand_outer_0", outer[:half + 1]), ("hand_outer_1", outer[half:]), ("hand_thumb", HAND_TRACE["thumb"]),
             ("hand_under", HAND_TRACE["under"])] + [(f"hand_roll_{i}", HAND_TRACE[f"roll{i + 1}"]) for i in range(3)]
    for name, pts in lines:
        n.append(Node(name, ("path", hand_ribbon(hand_px(pts), HAND_RIBBON)), 0, 0, SUBJECT, models=["TABLA", "BOUNCE"], frame="abs"))
    for node in n:   # the device's rules for any path: one subpath, at most 64 points, 1/8 px apart as drawn
        pts = node.shape[1]
        assert len(pts) <= 64, (node.name, len(pts))
        assert not crosses(pts), node.name
        for i, a in enumerate(pts):
            b = pts[(i + 1) % len(pts)]
            assert max(abs(a[0] - b[0]), abs(a[1] - b[1])) * HAND_SCALE >= 0.125, (node.name, i)
    return n


def mirror(points):
    """The same striker reaching in from the upper left."""
    pts = [(2 * TIP[0] - x, y) for x, y in points]
    return pts[::-1]


def striker_keyframes():
    m = mirror
    return {
        # Each: three keyframes the model's control moves through (0, half, full).
        "beater": [m(striker_beater(0)), m(striker_beater(1)), m(striker_beater(2))],
        "stick": [m(striker_brush()), m(striker_stick(1.9)), m(striker_stick(2.4, angle=-24, length=90))],
        "mallet": [m(striker_mallet(9.0)), m(striker_mallet(6.0)), m(striker_mallet(3.4))],
        "metal": [m(striker_mallet(6.5, angle=-40)), m(striker_stick(2.6, angle=-40)), m(striker_stick(1.5, angle=-40))],
        "hammer": [m(striker_beater(0, (7.5, 7.5, 2.0), -40, 76)), m(striker_beater(0, (6.0, 9.0, 4.0), -40, 76)),
                   m(striker_beater(0, (5.0, 11.5, 9.0), -40, 76))],
    }


# ------------------------------------------------------------------ fixed parts
class Node:
    """A fixed part. frame: "sec" (x, y from the head's centre, scaled with TUNE),
    "plan" (from the plan's centre, scaled with TUNE) or "abs". back: drawn under
    the reshaping outlines (the plate's dark bodies). models: who shows it."""
    def __init__(self, name, shape, x, y, shade, fill=False, models=None, frame="sec", back=False):
        self.name, self.shape, self.x, self.y, self.shade, self.fill = name, shape, x, y, shade, fill
        self.models = models or []
        self.frame, self.back = frame, back


def circle(r):
    return ("circle", r)


def line(x0, y0, x1, y1, w=0.45):
    return ("path", ribbon([(x0, y0), (x1, y1)], w))


def poly(points):
    return ("path", points)


def rect(w, h, r=0.0):
    """A rectangle centred on its origin, its corners rounded by r."""
    if r <= 0:
        return ("path", [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)])
    pts = []
    for cx, cy, a0 in ((w / 2 - r, -h / 2 + r, -90), (w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180)):
        for j in range(4):
            t = math.radians(a0 + 90 * j / 3)
            pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return ("path", pts)


def arc(r, a0, a1, w=0.5, n=12):
    pts = [(r * math.cos(math.radians(a0 + (a1 - a0) * j / (n - 1))), r * math.sin(math.radians(a0 + (a1 - a0) * j / (n - 1)))) for j in range(n)]
    return ("path", ribbon(pts, w))


def leg(dx, dy, w):
    return ("path", ribbon([(0, 0), (dx, dy)], w))


def wing_nut():
    """A wing nut seen from the side: a boss with a wing either way, as one outline."""
    pts = []
    for j in range(9):            # the right wing, up and round
        t = math.pi * (-0.5 + j / 8)
        pts.append((2.2 + 4.2 * (1 + math.sin(t)) / 2 + 1.2 * math.cos(t), -1.0 - 4.0 * (1 - math.cos(t * 0.9)) / 2 - 2.6 * j / 8))
    pts = [(1.8, 1.6), (2.4, 0.0)] + pts[1:] + [(2.0, -3.0)]
    left = [(-x, y) for x, y in pts[::-1]]
    return ("path", pts + left)


# ------------------------------------------------------------------ hardware
# The parts' own outlines, each one closed path in local px (y down), with the
# few tells that make them the real thing at the screen's size: a hoop's flange
# and the ear its tension rod runs through, a lug's casing, a rod's square head
# on its washer, rubber feet, T-screws on the stands' clamps, a snare's
# throw-off lever, the tabla's braid and barrel-shaped gatte, the marimba rail's
# felt, a cymbal stand's tilter, cup washer and wing nut.
def arcp(cx, cy, r, a0, a1, n, ry=None):
    """n points along an arc (degrees: 0 along +x, 90 down), both ends included."""
    ry = r if ry is None else ry
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * j / (n - 1))),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * j / (n - 1)))) for j in range(n)]


def flip_x(points):
    return [(-x, y) for x, y in points[::-1]]


def flip_y(points):
    return [(x, -y) for x, y in points[::-1]]


def hoop(head=False):
    """A drum's hoop cut through at the rim, the right side, about its node (the
    head's edge at x -1.5): the flanged wall over the head's collar and, along
    its top, the ear the tension rod passes through (the rod 4 px out). With
    head, the rod's square head sits on the ear (the bottom hoop, flipped)."""
    pts = arcp(-1.7, -2.7, 0.8, 180, 270, 3)
    if head:
        pts += [(2.9, -3.5), (2.9, -5.7), (3.3, -6.1), (4.7, -6.1), (5.1, -5.7), (5.1, -3.5)]
    pts += arcp(5.6, -2.75, 0.75, 270, 450, 4)
    pts += [(1.0, -2.0), (0.4, -1.4)]
    pts += arcp(-0.4, 2.7, 0.8, 0, 90, 3) + arcp(-1.7, 2.7, 0.8, 90, 180, 3)
    return pts


def rod():
    """A tension rod: the shaft down into the lug, its washer on the hoop's ear
    and the square head a drum key turns."""
    right = [(0.45, 11.6), (0.45, -3.5), (1.5, -3.5), (1.5, -4.1), (1.1, -4.1), (1.1, -6.0), (0.7, -6.4)]
    return right + flip_x(right)


def lug_casing():
    """A lug, side on (the right side): flat against the shell, its casing
    swelling out between the two screws that hold it."""
    front = [(0.7 + 1.55 * math.cos(math.pi * y / 13.6), y) for y in (-6.0, -4.5, -2.5, 0.0, 2.5, 4.5, 6.0)]
    return [(-2.25, -5.8), (-1.55, -6.5), (0.25, -6.5)] + front + [(0.25, 6.5), (-1.55, 6.5), (-2.25, 5.8)]


def foot(w, h):
    """A rubber foot: flat on the floor, domed over."""
    dome = [((w / 2) * math.cos(math.radians(t)), (h / 2 - 0.45) - (h - 0.45) * math.sin(math.radians(t)) ** 0.7)
            for t in (0, 25, 55, 90, 125, 155, 180)]
    return [(-w / 2 + 0.45, h / 2), (w / 2 - 0.45, h / 2)] + dome


def spur_foot():
    """A spur's rubber foot, a bell standing on the floor."""
    right = [(2.3, 1.6), (2.0, 0.9), (1.35, 0.15), (1.1, -1.15), (0.6, -1.6)]
    return [(-x, y) for x, y in right[::-1]] + right


def clamp_screw(w, h, r):
    """A stand's clamp, w by h, its corners rounded by r, with a T-screw out of
    its right side: the stem and the grip across it."""
    x = w / 2
    screw = [(x, -0.6), (x + 1.3, -0.6), (x + 1.3, -1.6), (x + 1.7, -2.0), (x + 2.1, -2.0), (x + 2.5, -1.6),
             (x + 2.5, 1.6), (x + 2.1, 2.0), (x + 1.7, 2.0), (x + 1.3, 1.6), (x + 1.3, 0.6), (x, 0.6)]
    return (arcp(x - r, -h / 2 + r, r, 270, 360, 3) + screw + arcp(x - r, h / 2 - r, r, 0, 90, 3)
            + arcp(-x + r, h / 2 - r, r, 90, 180, 3) + arcp(-x + r, -h / 2 + r, r, 180, 270, 3))


def lever():
    """The snare's throw-off: a lever on its pivot (the node's origin), tapering
    up to a thumb tab."""
    right = [(1.6, -0.2), (1.25, -4.5), (0.9, -8.3), (1.6, -9.3)]
    return right + arcp(0, -10.0, 1.6, 0, -180, 7)[1:-1] + [(-x, y) for x, y in right[::-1]] + arcp(0, -0.2, 1.6, 180, 0, 7)[1:-1]


def butt_end():
    """The snare's butt end: a plate on the shell, its fine-tuning knob under it."""
    right = [(1.2, -4.0), (1.8, -3.4), (1.8, 2.0), (1.2, 2.6), (0.5, 2.6), (0.5, 3.6), (1.2, 3.6), (1.7, 4.1), (1.7, 5.5), (1.2, 6.0)]
    return right + [(-x, y) for x, y in right[::-1]]


def barrel(w, h):
    """A gatta: a wooden block turned as a little barrel, its ends seen from just above."""
    right = [(w / 2 - 1.0, -h / 2 + 0.1), (w / 2 - 0.55, -h / 2 + 0.75), (w / 2 - 0.1, -h / 4.4), (w / 2 + 0.1, 0),
             (w / 2 - 0.1, h / 4.4), (w / 2 - 0.55, h / 2 - 0.75), (w / 2 - 1.0, h / 2 - 0.1)]
    return right + [(1.0, h / 2), (-1.0, h / 2)] + [(-x, y) for x, y in right[::-1]] + [(-1.0, -h / 2), (1.0, -h / 2)]


def braid(r, lobes=5, depth=0.24, n=30):
    """The gajra cut through: a braid of leather, its strands' ridges round it."""
    return [((r + depth * math.cos(lobes * t)) * math.cos(t), (r + depth * math.cos(lobes * t)) * math.sin(t))
            for t in (2 * math.pi * j / n for j in range(n))]


def cushion_ring(w, h):
    """The tabla's ring cushion side on: a stuffed ring, round at either end."""
    r = h / 2
    return arcp(w / 2 - r, 0, r, -90, 90, 7) + arcp(-w / 2 + r, 0, r, 90, 270, 7)


def pin():
    """A marimba frame's post: the pin the bar's cord loops over, its head over the cord's groove."""
    right = [(1.0, 5.5), (1.0, -3.0), (0.65, -3.0), (0.65, -3.9), (1.4, -3.9), (1.4, -4.9), (0.9, -5.5)]
    return right + [(-x, y) for x, y in right[::-1]]


def felt_rail(w, h):
    """The frame's rail under the bars' nodes, cut across, the felt strip along its top."""
    x, y = w / 2, h / 2
    right = [(3.4, -y), (3.8, -y + 0.4), (3.8, -y + 1.35), (x - 0.6, -y + 1.35), (x, -y + 1.95), (x, y - 0.6), (x - 0.6, y)]
    return right + [(-a, b) for a, b in right[::-1]]


def cup_washer():
    """The cup washer the cymbal's lower felt sits in, cut through: dished up at its rim."""
    right = [(1.8, 0.1), (4.1, -0.3), (5.3, -1.2), (6.0, -1.0), (4.6, 0.4), (2.0, 0.9)]
    return right + [(-x, y) for x, y in right[::-1]]


def tilter():
    """A cymbal stand's tilter: the round ratchet housing it locks at, on the
    block that clamps the stand's top tube (spanning 10 px down, about its node)."""
    cx, cy, r = 0.0, -1.4, 3.6
    block = [(2.6, 1.2), (2.6, 4.4), (2.0, 5.0), (-2.0, 5.0), (-2.6, 4.4), (-2.6, 1.2)]
    a0 = math.degrees(math.atan2(1.2 - cy, -2.6 - cx))
    return block + arcp(cx, cy, r, a0 + 8, 540 - a0 - 8, 13)


def plan_wing_nut():
    """A wing nut from above: the boss and its two wings, flared at their tips."""
    pts = arcp(0, 0, 2.9, 196, 344, 6)
    pts += [(4.4, -0.85)] + arcp(6.2, 0, 1.3, 270, 450, 6) + [(4.4, 0.85)]
    pts += arcp(0, 0, 2.9, 16, 164, 6)
    pts += [(-4.4, 0.85)] + arcp(-6.2, 0, 1.3, 90, 270, 6) + [(-4.4, -0.85)]
    return pts


def ear():
    """A drum's hoop from above at a tension rod: the ear, round at its outer end
    (local -y, outward), reaching in over the hoop (+y)."""
    return arcp(0, 0.6, 2.1, 180, 360, 7) + [(2.1, 4.7), (1.6, 5.2), (-1.6, 5.2), (-2.1, 4.7)]


def tripod(n, prefix, top, floor, spread, models, w=2.4):
    """A stand's tripod: a collar, two legs splayed either way, a third leg behind
    (back), braces from the tube to the legs, and rubber feet."""
    n.append(Node(f"{prefix}_back", leg(7, floor - top - 1.5, w * .9), 0, top + 1.5, BODY, True, models, back=True))
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        n.append(Node(f"{prefix}_leg_{side}", leg(sx * spread, floor - top - 1.5, w), 0, top + 1.5, BODY, True, models))
        n.append(Node(f"{prefix}_brace_{side}", leg(sx * (spread * .52 - 2.6), (floor - top) * .52 - 9, 1.3), sx * 2.6, top + 9, BODY, True, models))
        n.append(Node(f"{prefix}_foot_{side}", poly(foot(6, 2.6)), sx * (spread + 1), floor - 1.3, PASTE, True, models))
    n.append(Node(f"{prefix}_collar", poly(clamp_screw(8, 6, 1.6)), 0, top, CHROME, True, models))


def fixed_parts():
    n = []
    A, R = A_NOMINAL, R_PLAN
    F = FLOOR_DEPTH
    # ---- the plate: the floor, in two halves (a path reaches 64 px either way).
    n.append(Node("floor_l", line(-62, 0, 62, 0, 0.5), CX - 60, FLOOR, DIM, frame="abs"))
    n.append(Node("floor_r", line(-62, 0, 62, 0, 0.5), CX + 60, FLOOR, DIM, frame="abs"))
    # ---- a drum's construction, behind the outlines: the inside of the shell,
    # the far wall's lug bolts.
    n.append(Node("inside_a", rect(110, 40), 0, 24, SHADOW, True, DRUMS, back=True))
    n.append(Node("inside_b", rect(110, 52), 0, 46, SHADOW, True, ["BOOM", "TOM", "WIRE"], back=True))
    for row in range(2):
        for i, x in enumerate((-34, 0, 34)):
            n.append(Node(f"bolt_{row}{i}", circle(1.9), x, 12, PASTE, True, DRUMS, back=True))
    # BAR, behind: the cords under the bars from above, the posts the cords run
    # over between the bars, the trestle's far legs and its stretcher.
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        n.append(Node(f"plan_rail_{side}", rect(10, 124, 2), sx * NODE * 62, 0, BODY, True, ["BAR"], "plan", True))
        n.append(Node(f"cord_{side}", line(0, -62, 0, 62, 0.6), sx * NODE * 62, 0, MID, models=["BAR"], frame="plan", back=True))
        n.append(Node(f"post_{side}", poly(pin()), sx * NODE * A, 15.5, CHROME, True, ["BAR"], back=True))
        h = (F["BAR"] - 28) / 2   # centred on its middle: a path reaches 64 px either way
        n.append(Node(f"trestle_{side}", ("path", ribbon([(-sx * 7.5, -h), (sx * 7.5, h)], 3.0)), sx * (NODE * A + 7.5), 28 + h, BODY, True, ["BAR"], back=True))
        n.append(Node(f"bar_foot_{side}", poly(foot(7, 2.6)), sx * (NODE * A + 15), F["BAR"] - 1.3, PASTE, True, ["BAR"], back=True))
    n.append(Node("stretcher", line(-44, 0, 44, 0, 2.4), 0, F["BAR"] - 9, BODY, True, ["BAR"], back=True))
    # The plan's dark bodies: a drum's hoop and head, the snare's muffling ring,
    # a tabla's braid and head, the cymbal, the bar.
    n.append(Node("plan_hoop", circle(R + 4.5), 0, 0, BODY, True, DRUMS, "plan", True))
    n.append(Node("plan_muffle", circle(R), 0, 0, CHROME, True, ["SNARE"], "plan", True))
    n.append(Node("plan_head", circle(R), 0, 0, CLOTH, True, DRUMS + ["TABLA"], "plan", True))
    n.append(Node("plan_rim", circle(R * TP + 4.5), 0, 0, BODY, True, ["TABLA"], "plan", True))
    n.append(Node("plan_tabla_head", circle(R * TP), 0, 0, CLOTH, True, ["TABLA"], "plan", True))
    for i, (r, shade) in enumerate(((R * TP * .44, PASTE), (R * TP * .33, CLOTH), (R * TP * .21, BODY))):
        n.append(Node(f"syahi_{i}", circle(r), 0, 0, shade, True, ["TABLA"], "plan", True))
    n.append(Node("plan_cymbal", circle(R + 4.5), 0, 0, CLOTH, True, ["METAL"], "plan", True))
    n.append(Node("plan_bar", rect(124, 28, 3), 0, -BAR_Y, BODY, True, ["BAR"], "plan", True))
    n.append(Node("plan_twin", rect(118, 26, 3), 0, BAR_Y, BODY, True, ["BAR"], "plan", True))
    # The keys either side on the frame: the next one up the keyboard, shorter,
    # and the next one down, longer.
    n.append(Node("plan_key_hi", rect(108, 24, 3), 0, -KEY_Y, BODY, True, ["BAR"], "plan", True))
    n.append(Node("plan_key_lo", rect(128, 26, 3), 0, KEY_Y + 1, BODY, True, ["BAR"], "plan", True))
    # ---- in front of the outlines
    # DECAY: a folded cloth lying on the head, and its fold.
    # Wide and low, so a short DECAY lays it over half the head.
    cloth = [(-22, 0), (-20.5, -2.8), (-14, -5.6), (-3, -6.6), (11, -5.4), (19, -2.4), (22, 0), (8, 0.8), (-8, 0.8)]
    n.append(Node("cloth", poly(catmull(cloth + [cloth[0]], 3)[:-1]), 0, 0, CLOTH, True))
    n.append(Node("fold", line(-11, -3.2, 12, -3.0, 0.5), 0, 0, DIM))
    # A drum's hoops, lugs and rods.
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        side_of = (lambda pts: pts) if sx > 0 else flip_x
        n.append(Node(f"hoop_t{side}", poly(side_of(hoop())), sx * (A + 1.5), 0, CHROME, True, DRUMS))
        n.append(Node(f"hoop_b{side}", poly(side_of(flip_y(hoop(head=True)))), sx * (A + 1.5), 48, CHROME, True, DRUMS))
        n.append(Node(f"lug_{side}", poly(side_of(lug_casing())), sx * (A + 5.5), 17, CHROME, True, DRUMS))
        n.append(Node(f"lug2_{side}", poly(side_of(lug_casing())), sx * (A + 5.5), 60, CHROME, True, ["BOOM", "TOM", "WIRE"]))
        n.append(Node(f"rod_{side}", poly(rod()), sx * (A + 5.5), 0, MID, models=DRUMS))
    # BOOM: spurs down to the floor (the display stretches them as SHELL deepens
    # the drum), the port in the shell's side and the air it pushes, a click pad.
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        n.append(Node(f"spur_{side}", leg(sx * 14, 40, 2.4), sx * (A + 3), 66, BODY, True, ["BOOM"]))
        n.append(Node(f"spur_tip_{side}", poly(spur_foot()), 0, 0, PASTE, True, ["BOOM"]))
    n.append(Node("port", circle(4.6), -(A + 1), 50, BLACK, True, ["BOOM"]))
    for i in range(3):
        n.append(Node(f"air_{i}", arc(12 + 10 * i, 125, 235, 0.7, 14), -(A + 1), 50, [LINE, MID, DIM][i], models=["BOOM"]))
    n.append(Node("pad", rect(10, 1.8), 0, -1, PASTE, True, ["BOOM"]))
    n.append(Node("plan_pad", circle(6), 0, 0, PASTE, True, ["BOOM"], "plan"))
    # SNARE: the strainer, the butt plate, the muffling ring lying on the head; its
    # stand: the basket's arms under the bottom hoop, the post, a tripod.
    # The strainer's lever turns about its foot, below the lug: SNAP throws it
    # from hanging out (the wires loose) up against the shell (drawn tight).
    n.append(Node("strainer", poly(lever()), A + 5.5, 37, CHROME, True, ["SNARE"]))
    n.append(Node("butt", poly(butt_end()), -(A + 6), 30, CHROME, True, ["SNARE"]))
    n.append(Node("oring_l", circle(2.6), 0, -1.5, MARK, True, ["SNARE"]))
    n.append(Node("oring_r", circle(2.6), 0, -1.5, MARK, True, ["SNARE"]))
    sb = DEPTH["SNARE"] + 4
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        n.append(Node(f"basket_{side}", leg(sx * 40, -4.5, 1.8), 0, sb + 8, CHROME, True, ["SNARE"]))
        n.append(Node(f"basket_cup_{side}", rect(5, 4, 1.4), sx * 40, sb + 2.5, PASTE, True, ["SNARE"]))
    n.append(Node("snare_post", leg(0, 26, 2.8), 0, sb + 10, BODY, True, ["SNARE"]))
    n.append(Node("snare_tilt", rect(7, 6, 1.8), 0, sb + 9, CHROME, True, ["SNARE"]))
    n.append(Node("snare_knob", circle(2.2), 5.5, sb + 10, CHROME, True, ["SNARE"]))
    tripod(n, "snare", sb + 36, F["SNARE"], 30, ["SNARE"])
    # TOM: a floor tom's legs down to the floor, in their brackets; the second head
    # seen through the first.
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        n.append(Node(f"leg_{side}", leg(sx * 12, F["TOM"] - 58, 2.2), sx * (A + 6), 58, BODY, True, ["TOM"]))
        n.append(Node(f"leg_foot_{side}", poly(foot(5, 2.6)), sx * (A + 18), F["TOM"] - 1.3, PASTE, True, ["TOM"]))
    n.append(Node("plan_second", circle(R * 0.94), 0, 0, DIM, models=["TOM"], frame="plan"))
    # BOUNCE: a practice drum on a tripod stand.
    n.append(Node("post", leg(0, 34, 2.8), 0, DEPTH["BOUNCE"] + 4, BODY, True, ["BOUNCE"]))
    n.append(Node("pad_tilt", poly(clamp_screw(7, 6, 1.8)), 0, DEPTH["BOUNCE"] + 6, CHROME, True, ["BOUNCE"]))
    tripod(n, "bounce", DEPTH["BOUNCE"] + 40, F["BOUNCE"], 32, ["BOUNCE"])
    # WIRE: the thunder drum stands in a frame: two legs splayed from brackets
    # under the lower lugs down to the floor, on rubber feet; the spring's clamp at its end.
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        n.append(Node(f"frame_bracket_{side}", poly(clamp_screw(6, 5, 1.5) if sx > 0 else flip_x(clamp_screw(6, 5, 1.5))), sx * (A + 6), 70, CHROME, True, ["WIRE"]))
        n.append(Node(f"frame_leg_{side}", leg(sx * 13, F["WIRE"] - 72, 2.4), sx * (A + 6), 72, BODY, True, ["WIRE"]))
        n.append(Node(f"frame_foot_{side}", poly(foot(7, 2.6)), sx * (A + 20), F["WIRE"] - 1.3, PASTE, True, ["WIRE"]))
    n.append(Node("clamp", rect(12, 5, 1.5), 0, 0, CHROME, True, ["WIRE"]))
    n.append(Node("plan_boss", circle(6), 0, 0, CHROME, True, ["WIRE"], "plan"))
    n.append(Node("plan_coil", circle(14), 0, 0, MID, models=["WIRE"], frame="plan"))
    # TABLA: the cushion it sits in on the floor, the braid at the rim, the gatte under the straps.
    n.append(Node("cushion", poly(cushion_ring(124, 12)), 0, F["TABLA"] - 6, BODY, True, ["TABLA"]))
    n.append(Node("braid_l", poly(braid(3.4)), -(TABLA_A + 2), 1, PASTE, True, ["TABLA"]))
    n.append(Node("braid_r", poly(braid(3.4)), TABLA_A + 2, 1, PASTE, True, ["TABLA"]))
    for i in range(5):
        n.append(Node(f"gatte_{i}", poly(barrel(5.5, 12)), 0, 0, CHROME, True, ["TABLA"]))
    n.append(Node("kinar", circle(R * TP * 0.86), 0, 0, DIM, models=["TABLA"], frame="plan"))
    for i in range(16):
        t = math.radians(360 * i / 16 + 11.25)
        n.append(Node(f"loop_{i}", circle(2.5), (R * TP + 2.4) * math.cos(t), (R * TP + 2.4) * math.sin(t), LINE, models=["TABLA"], frame="plan"))
        r0, r1 = R * TP + 6, R * TP + 15
        n.append(Node(f"strap_{i}", ("path", ribbon([(0, 0), ((r1 - r0) * math.cos(t), (r1 - r0) * math.sin(t))], 0.6)),
                      r0 * math.cos(t), r0 * math.sin(t), MID, models=["TABLA"], frame="plan"))
    # BAR, in front: the rails under its nodes, the cord through each node's hole,
    # the bracket holding the resonator, its stopper; the wood's grain.
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        n.append(Node(f"rail_{side}", poly(felt_rail(11, 7.5)), sx * NODE * A, 24.5, BODY, True, ["BAR"]))
        n.append(Node(f"hole_{side}", circle(2.1), 0, 0, MID, models=["BAR"]))
        n.append(Node(f"cord_end_{side}", circle(1.0), 0, 0, MARK, True, ["BAR"]))
    n.append(Node("bracket", line(-NODE * A + 5, 0, NODE * A - 5, 0, 1.8), 0, 25, CHROME, True, ["BAR"]))
    for i in range(3):
        n.append(Node(f"grain_{i}", line(-46, 0, 46, 0, 0.45), 0, 0, FAINT, models=["BAR"]))
        n.append(Node(f"plan_grain_{i}", line(-54, 0, 54, 0, 0.45), 0, -BAR_Y - 7 + 7 * i, FAINT, models=["BAR"], frame="plan"))
    for i in range(2):
        n.append(Node(f"plan_grain_hi_{i}", line(-46, 0, 46, 0, 0.45), 0, -KEY_Y - 4 + 8 * i, FAINT, models=["BAR"], frame="plan"))
        n.append(Node(f"plan_grain_lo_{i}", line(-56, 0, 56, 0, 0.45), 0, KEY_Y + 1 - 4 + 8 * i, FAINT, models=["BAR"], frame="plan"))
    # The hole through each key at its nodes, where the cord runs through it.
    for j, y in enumerate((-KEY_Y, -BAR_Y, BAR_Y, KEY_Y + 1)):
        for side in ("l", "r"):
            sx = -1 if side == "l" else 1
            n.append(Node(f"plan_hole_{j}{side}", circle(2.4), sx * NODE * 62, y, MID, models=["BAR"], frame="plan"))
    n.append(Node("cap", rect(2 * TUBE_W + 1.5, 4.5, 1.6), 0, 0, BODY, True, ["BAR"]))
    # From above: the posts between the bars and past them, where the cords tie.
    for i, (x, y) in enumerate(((-1, 0), (1, 0), (-1, -38), (1, -38), (-1, 38), (1, 38))):
        n.append(Node(f"plan_post_{i}", circle(2.2), x * NODE * 62, y, CHROME, True, ["BAR"], "plan"))
    # The mallet's yarn, wound round its ball (it rides with the striker).
    n.append(Node("yarn", ("path", ribbon([(7.4 * math.cos(math.pi * j / 10), 3.0 * math.sin(math.pi * j / 10)) for j in range(11)], 0.5)), 0, 0, MID, models=["TOM", "BAR"], frame="abs"))
    # METAL: the stand's top: the rod, the tilter and its key, the cup washer, the
    # felts either side of the bell and the sleeve through it, the wing nut; the
    # height clamp, the collar and the tripod.
    n.append(Node("rod_up", line(0, -8, 0, 18, 1.3), 0, 0, MID, models=["METAL"]))
    n.append(Node("rod_low", line(0, -10, 0, 2, 1.3), 0, 0, MID, models=["METAL"]))
    n.append(Node("sleeve", rect(3.0, 9, 0.8), 0, 0, PASTE, True, ["METAL"]))
    n.append(Node("felt_top", rect(10, 3.6, 1.5), 0, 0, PASTE, True, ["METAL"]))
    n.append(Node("felt_bottom", rect(10, 3.6, 1.5), 0, 0, PASTE, True, ["METAL"]))
    n.append(Node("washer", poly(cup_washer()), 0, 0, CHROME, True, ["METAL"]))
    n.append(Node("nut", wing_nut(), 0, 0, CHROME, True, ["METAL"]))
    n.append(Node("tilter", poly(tilter()), 0, 9, CHROME, True, ["METAL"]))
    n.append(Node("tilt_key", circle(2.3), 5.6, 8, CHROME, True, ["METAL"]))
    n.append(Node("height_clamp", rect(8, 6, 1.6), 0, 52, CHROME, True, ["METAL"]))
    n.append(Node("height_key", circle(2.3), 6, 52, CHROME, True, ["METAL"]))
    tripod(n, "metal", 86, F["METAL"], 40, ["METAL"])
    # From above: the edge's band, the lathe's lines over the bow, the grooves
    # WASH cuts between them, the bell, the sleeve and the nut, rivets.
    C = R * CYMBAL
    n.append(Node("edge", circle(C * 0.93), 0, 0, DIM, models=["METAL"], frame="plan"))
    for i in range(6):
        n.append(Node(f"lathe_{i}", circle(C * (0.32 + 0.1 * i)), 0, 0, FAINT, models=["METAL"], frame="plan"))
    for i in range(4):
        n.append(Node(f"groove_{i}", circle(C * (0.37 + 0.1 * i)), 0, 0, MID, models=["METAL"], frame="plan"))
    n.append(Node("plan_cup", circle(C * 0.24), 0, 0, PASTE, True, ["METAL"], "plan"))
    n.append(Node("plan_felt", circle(5.5), 0, 0, PASTE, True, ["METAL"], "plan"))
    n.append(Node("plan_nut", poly(plan_wing_nut()), 0, 0, CHROME, True, ["METAL"], "plan"))
    n.append(Node("plan_hole", circle(2.6), 0, 0, CHROME, True, ["METAL"], "plan"))
    for i in range(6):
        n.append(Node(f"rivet_{i}", circle(2.0), 0, 0, MARK, True, ["METAL"], "plan"))
    # A drum's lugs from above.
    for i in range(8):
        n.append(Node(f"plan_lug_{i}", poly(ear()), 0, 0, CHROME, True, DRUMS, "plan"))
    n.append(Node("collar", circle(R * 0.9), 0, 0, FAINT, models=DRUMS, frame="plan"))
    # BOUNCE: the stick's marks from above, one per strike.
    for i in range(8):
        n.append(Node(f"bounce_{i}", circle(2.6), 0, 0, MID, models=["BOUNCE"], frame="plan"))
    # ---- what a hit lights: the nodal lines, the strike mark and its rings. A
    # bar's are short lines across it: its first mode's two nodes, its second's three.
    for i in range(3):
        n.append(Node(f"nodal_circle_{i}", circle(R * (.36 + .2 * i)), 0, 0, MID, frame="plan"))
    for i in range(4):
        n.append(Node(f"diameter_{i}", line(-60, 0, 60, 0, 0.4), 0, 0, MID, frame="plan"))
    for i, x in enumerate((-NODE, NODE, -0.736, 0.0, 0.736)):
        n.append(Node(f"bar_node_{i}", line(0, -16, 0, 16, 0.4), x * 62, -BAR_Y, MID, frame="plan"))
    n.append(Node("mark", circle(3.6), 0, 0, MARK, True, frame="plan"))
    n.append(Node("mark_ring", circle(6.5), 0, 0, MID, frame="plan"))
    n.append(Node("ring_near", circle(12), 0, 0, RING1, frame="plan"))
    n.append(Node("ring_far", circle(20), 0, 0, DIM, frame="plan"))
    # Last, over everything in the section: the hand.
    n += hand_nodes()
    return n


# ------------------------------------------------------------------ writers
def restart(points):
    """The same loop started where its closing edge is longest on an axis, as
    view.hpp's outline() does: the device draws a short closing edge (a ribbon's
    end) as a sliver along the next."""
    n = len(points)
    def span(a, b):
        return max(abs(a[0] - b[0]), abs(a[1] - b[1]))
    best = max(range(n), key=lambda i: (span(points[i - 1], points[i]), -i))
    return points[best:] + points[:best]


def svg_shape(node, index):
    kind, data = node.shape
    if kind == "path":
        data = restart(data)
    p = PALETTE[node.shade]
    fill = p[0] if node.fill else "none"
    stroke, width = p[1], p[2]
    tid = f'id="{ID}_node_{index}"'   # the display places every node each frame
    if kind == "circle":
        return f'<circle {tid} cx="0" cy="0" r="{fmt(data)}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(width)}"/>'
    return f'<path {tid} d="{path_d(data)}" fill="{fill}" stroke="{stroke}" stroke-width="{fmt(width)}"/>'


SHARPEST = 150.0   # degrees: the sharpest turn a fixed part's outline may make
SHARP = []         # the parts that turn sharper, reported and refused


def turns(points):
    """Each point's turn in degrees (0 straight on, 180 straight back)."""
    out, n = [], len(points)
    for i in range(n):
        p0, p1, p2 = points[i - 1], points[i], points[(i + 1) % n]
        v1, v2 = (p1[0] - p0[0], p1[1] - p0[1]), (p2[0] - p1[0], p2[1] - p1[1])
        out.append(math.degrees(math.atan2(abs(v1[0] * v2[1] - v1[1] * v2[0]), v1[0] * v2[0] + v1[1] * v2[1])))
    return out


def check_outline(points, what, sharp=True):
    """The device's rules for a path, as written (to the artwork's decimals): 3 to
    64 points, each an eighth of a pixel from the next, the closing edge a quarter,
    within 64 px of the node; and (sharp) no turn past SHARPEST, which the device
    strokes with a mitre and no limit: a spike."""
    points = [(float(fmt(x)), float(fmt(y))) for x, y in points]
    assert 3 <= len(points) <= 64, f"{what}: {len(points)} points"
    for i, a in enumerate(points):
        b = points[(i + 1) % len(points)]
        assert abs(a[0] - b[0]) >= 0.125 or abs(a[1] - b[1]) >= 0.125, f"{what}: points {i} too close"
        assert all(abs(v) <= 64 for v in a), f"{what}: beyond 64 px"
    a, b = points[-1], points[0]
    assert max(abs(a[0] - b[0]), abs(a[1] - b[1])) >= 0.25, f"{what}: closing edge under a quarter pixel"
    if sharp:
        SHARP.extend((what, i, round(t)) for i, t in enumerate(turns(points)) if t > SHARPEST)


def header_block():
    out = ["<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
           '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    greys = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i, g in enumerate(greys):
        out.append(f'<path id="{ID}_node_{HEADER_FIRST + i}" transform="matrix(1 0 0 1 {60 * i} 0)" d="M0,0 L60,0 L60,20 L0,20 Z" fill="{g}" stroke="{g}" stroke-width="0.5"/>')
    texts = [(5, 10, "#CBCBCB", 10), (125, 10, "#CBCBCB", 10), (245, 10, "#000000", 10), (365, 10, "#000000", 10),
             (65, 17.4, "#CBCBCB", 20), (185, 17.4, "#CBCBCB", 20), (305, 17.4, "#000000", 20), (425, 17.4, "#000000", 20),
             (5, 33, "#5A5A5A", 10), (125, 33, "#5A5A5A", 10), (245, 33, "#5A5A5A", 10), (365, 33, "#5A5A5A", 10),
             (65, 33, "#707070", 13), (185, 33, "#707070", 13), (305, 33, "#707070", 13), (425, 33, "#707070", 13)]
    for i, (x, y, c, s) in enumerate(texts):
        out.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {x} {y})" fill="{c}" font-family="UniversNextPro-Light" font-size="{s}px">-</text>')
    return out


PATHS = 4
HEADER_FIRST = None


def main():
    global HEADER_FIRST
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview")
    args = ap.parse_args()
    parts = fixed_parts()
    first = PATHS
    HEADER_FIRST = first + len(parts)
    node_count = HEADER_FIRST + 8
    # ---- svg
    svg = [f'<g id="{ID}_panel">'] + header_block()
    svg.append('<path d="M0 38 L480 38 L480 220 L0 220 Z" fill="#000000" stroke="none"/>')
    # The four reshaping outlines, authored hollow, 64 distinct points each.
    # Artwork order is stacking order: the dark bodies first, then the four
    # reshaping outlines, then everything drawn over them.
    for i, node in enumerate(parts):
        if node.shape[0] == "path":
            check_outline(restart(node.shape[1]), node.name)
        if node.back:
            svg.append(svg_shape(node, first + i))
    for p in range(PATHS):
        pts = [(-32 + i, 0.0) for i in range(32)] + [(31 - i, 1.0) for i in range(32)]
        svg.append(f'<path id="{ID}_node_{p}" d="{path_d(pts)}" fill="none" '
                   f'stroke="{PALETTE[SUBJECT][1]}" stroke-width="{PALETTE[SUBJECT][2]}"/>')
    for i, node in enumerate(parts):
        if not node.back:
            svg.append(svg_shape(node, first + i))
    for i, (fill, stroke, width) in enumerate(PALETTE):
        svg.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{fill if fill != "none" else "#000000"}" stroke="{stroke}" stroke-width="{fmt(width)}"/>')
    svg.append("</g>")
    assert not SHARP, f"turns sharper than {SHARPEST:g} degrees: {SHARP}"
    (HERE / "panel.svg.fragment").write_text("\n".join(svg) + "\n")
    # ---- art.h
    h = ["#pragma once",
         "// Generated by tools/make_panel.py: edit the generator, then regenerate.",
         "#include <opxy/types.hpp>",
         "",
         "namespace syahi {",
         "namespace art {",
         "using opxy::i16;",
         "using opxy::u8;",
         "using opxy::u32;",
         f"const int cx = {CX}, px = {PX}, py = {PY}, floor_y = {FLOOR}, plan_r = {R_PLAN}, nominal = {A_NOMINAL};",
         "// The section's, the plan's and the striker's gains, Q10.",
         f"const int section_gain = {round(SECTION_GAIN * 1024)}, plan_gain = {round(PLAN_GAIN * 1024)}, striker_gain = {round(STRIKER_GAIN * 1024)};",
         "// Where the floor lies below each model's head, px at a gain of 1.",
         "const int floor_depth[8] = {" + ", ".join(str(FLOOR_DEPTH[m]) for m in MODELS) + "};",
         f"const int bar_y = {BAR_Y}, cymbal_q10 = {round(CYMBAL * 1024)};",
         "// Each model's depth below the head at TUNE 0, px.",
         "const int depth[8] = {" + ", ".join(str(DEPTH[m]) for m in MODELS) + "};",
         f"const u32 paths = {PATHS}, header = {HEADER_FIRST}, nodes = {node_count};",
         "enum Palette : u32 { black, subject, line, mid, dim, faint, paste, cloth_ink, struck, ring1, chrome, mark_ink, shadow, path_line, nodal2, body };",
         "enum Part : u32 {"]
    h.append("    " + ", ".join(f"{p.name} = {first + i}" for i, p in enumerate(parts)))
    h += ["};",
          "// Each part's resting place (px * 16, from the head's centre, the plan's centre or the screen's",
          "// corner, as frame says), shade, whether it is filled, and the models that show it (a bit each; 0: all).",
          "enum Frame_ : u8 { screen_frame, section_frame, plan_frame };",
          "struct Fixed { i16 x, y; u8 shade, fill, frame, models; };",
          f"const u32 fixed_first = {first}, fixed_count = {len(parts)};",
          "const Fixed fixed[fixed_count] = {"]
    for p in parts:
        mask = sum(1 << MODELS.index(m) for m in p.models)
        frame = {"abs": 0, "sec": 1, "plan": 2}[p.frame]
        h.append(f"    {{{q4(p.x)}, {q4(p.y)}, {p.shade}, {1 if p.fill else 0}, {frame}, {mask}}},   // {p.name}")
    h.append("};")
    # Head anchors.
    h.append(f"// The head in section at each FORM anchor: (top offset, thickness) at {HEAD_N} points across, px * 16.")
    h.append(f"const u32 head_points = {HEAD_N};")
    h.append(f"const i16 head[7][{HEAD_N}][2] = {{")
    for k in range(7):
        h.append("    {" + ", ".join(f"{{{q4(t)}, {q4(th)}}}" for t, th in (head_anchor(k, x) for x in HEAD_X)) + "},")
    h.append("};")
    lens = [max(0.0, 1 - (x / .42) ** 2) ** .6 for x in HEAD_X]
    h.append("// The syahi's lens (LOAD), the centre and edge mode shapes (a hit's flex), Q12.")
    h.append(f"const i16 lens[{HEAD_N}] = {{" + ", ".join(str(int(round(v * 4096))) for v in lens) + "};")
    h.append(f"const i16 mode_centre[{HEAD_N}] = {{" + ", ".join(str(int(round(math.cos(x * math.pi / 2) * 4096))) for x in HEAD_X) + "};")
    h.append(f"const i16 mode_edge[{HEAD_N}] = {{" + ", ".join(str(int(round(math.sin(x * math.pi) * 4096))) for x in HEAD_X) + "};")
    h.append(f"const i16 sag[{HEAD_N}] = {{" + ", ".join(str(int(round((1 - x * x) * 4096))) for x in HEAD_X) + "};")
    h.append("// A free bar's first mode: the ends and the middle swing opposite, still at its nodes.")
    h.append(f"const i16 mode_bar[{HEAD_N}] = {{" + ", ".join(str(int(round(math.cos(x * math.pi / 2 / NODE) * 4096))) for x in HEAD_X) + "};")
    h.append(f"const i16 mode_bar2[{HEAD_N}] = {{" + ", ".join(str(int(round(math.sin(x * math.pi / 0.736) * 4096))) for x in HEAD_X) + "};")
    # Shells.
    shells = shell_keyframes()
    from_models = ["BOOM", "SNARE", "TOM", "TABLA", "BAR", "METAL", "BOUNCE", "WIRE"]
    count = max(len(shells[m][k]) for m in from_models for k in range(2))
    h.append(f"// Each model's shell (its cut wall, tube or stand), two keyframes, px * 16, at TUNE 0.")
    h.append("// shell_points: how many each uses.")
    h.append(f"const u32 shell_max = {count};")
    h.append("const u8 shell_points[8] = {" + ", ".join(str(len(shells[m][0])) for m in from_models) + "};")
    mids = {}
    for m in from_models:
        ys = [y for kf in shells[m] for _, y in kf]
        mids[m] = round((min(ys) + max(ys)) / 2)
        shells[m] = tuple(centred(kf, mids[m]) for kf in shells[m])
        for kf in shells[m]:
            check_outline(kf, m + " shell", sharp=False)   # view.hpp's outline() blunts these
    h.append("// Where each shell's node sits below the head's centre, px at TUNE 0 (its points are centred on it).")
    h.append("const int shell_mid[8] = {" + ", ".join(str(mids[m]) for m in from_models) + "};")
    h.append(f"const i16 shell[8][2][{count}][2] = {{")
    for m in from_models:
        a, b = shells[m]
        assert len(a) == len(b), m
        rows = []
        for kf in (a, b):
            kf = kf + [kf[-1]] * (count - len(kf))
            rows.append("{" + ", ".join(f"{{{q4(x)}, {q4(y)}}}" for x, y in kf) + "}")
        h.append("    {" + ", ".join(rows) + f"}},   // {m}")
    h.append("};")
    strikers = striker_keyframes()
    order = ["beater", "stick", "mallet", "metal", "hammer", "hand", "bounce"]   # the hands are static nodes
    h.append(f"// The strikers: three keyframes each, {STRIKER_N} points, px * 16, the contact point at ({TIP[0]:g}, {TIP[1]:g});")
    h.append("// the two hands (the last two) have their own table below.")
    h.append("enum Striker : u32 { " + ", ".join(f"{s}_striker" for s in order) + " };")
    h.append(f"const int tip_x = {int(TIP[0])}, tip_y = {int(TIP[1])};")
    h.append(f"const u32 striker_points = {STRIKER_N};")
    h.append(f"const i16 striker[{len(order) - 2}][3][{STRIKER_N}][2] = {{")
    for s in order[:-2]:
        rows = []
        for i, kf in enumerate(strikers[s]):
            assert len(kf) == STRIKER_N, (s, len(kf))
            for x, y in kf:
                assert abs(x) < 64 and abs(y) < 64, (s, i, x, y)
            rows.append("{" + ", ".join(f"{{{q4(x)}, {q4(y)}}}" for x, y in kf) + "}")
        h.append("    {" + ", ".join(rows) + f"}},   // {s}")
    h.append("};")
    hull = hand_px(HAND_TRACE["fill"])
    stick = hand_px(stick_outline())
    tip = max(stick, key=lambda q: q[0])
    (x0, y0), (x1, y1) = STICK_FROM, STICK_TO
    L = math.hypot(x1 - x0, y1 - y0)
    slide = (-(x1 - x0) / L * STICK_SLIDE * HAND_PX, -(y1 - y0) / L * STICK_SLIDE * HAND_PX)
    h.append("// The hand (static nodes hand_stick .. hand_roll_2, about the wrist): drawn at hand_scale (Q10);")
    h.append("// its silhouette's points for fitting it under the header and finding what touches first,")
    h.append("// the stick's tip and the slide that takes the stick back into the fist (px * 16).")
    h.append(f"const int hand_scale = {round(HAND_SCALE * 1024)};")
    h.append(f"const u32 hand_hull_points = {len(hull)};")
    h.append("const i16 hand_hull[" + str(len(hull)) + "][2] = {" + ", ".join(f"{{{q4(x)}, {q4(y)}}}" for x, y in hull) + "};")
    h.append(f"const i16 stick_tip[2] = {{{q4(tip[0])}, {q4(tip[1])}}}, stick_slide[2] = {{{q4(slide[0])}, {q4(slide[1])}}};")
    h.append("// sin(i degrees) for 0..90, Q10: the screen's own turns and wobble, kept local so the module")
    h.append("// links no shared copy of the SDK's.")
    h.append("const i16 quarter[91] = {" + ", ".join(str(int(round(1024 * math.sin(math.radians(i))))) for i in range(91)) + "};")
    h += ["}", "}", ""]
    (HERE / "art.h").write_text("\n".join(h))
    print(f"{len(parts)} fixed parts, {node_count} nodes; panel {len(chr(10).join(svg))} bytes")


if __name__ == "__main__":
    main()
