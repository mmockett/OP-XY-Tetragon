#!/usr/bin/env python3
"""Writes GALILEO's screen artwork: panel.svg.fragment and art.h.

    python3 tools/make_art.py

The screen draws one object: an 18th-century demonstration inclined plane,
after the "plane with bells" instrument makers built for lecture rooms to
show Galileo's law of fall. On a moulded walnut base, a grooved board turns
on a brass hinge at its foot, an index on its underside reading its angle on
a brass quadrant; its head rests on a peg in a graduated post. Small bells
hang in brass gallows along the groove; a catch holds the ball, the rest of the
handful waits in a cup behind it; a pendulum on the post beats the time. At
the foot stands the key's instrument.

Static parts are plain artwork without IDs: nothing moves them and the device
never redraws them. Every moving part is a node, one closed outline (or a
circle) authored around its own origin, which display.cpp places, turns and
scales. The first two nodes are the reshaping outlines (unfilled, 64 points).
Draw order is stacking order: back to front.
"""
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
ID = "galileo"

# One line weight and a few greys, by role, across the whole object: the
# moving subject brightest, structure lighter, engraving dimmest. Every line
# draws 1 px wide on the screen, as stock Drumline's do: an outline's stroke
# is 1 px at the size the part is drawn, and a line drawn as a thin closed
# ribbon is RIBBON wide inside its stroke, about 1.5 px across. A part the
# display scales takes its stroke from a palette entry for its scale
# (LINE_BANDS), so it stays 1 px at any size; the rest are authored for the
# size they're drawn at (SCALE).
BRIGHT, LIGHT, MID, DIM = "#f2f2f2", "#c8c8c8", "#9a9a9a", "#666666"
BRASS = LIGHT                      # brass fittings read as the light grey
BLACK, WOOD, METAL, FELT, BALL = "#000000", "#181818", "#262626", "#3a3a3a", "#a8a8a8"
LINE = 1.0                         # every stroke, as drawn (px)
ZOOM = 1.3                         # the instrument's art is authored this much larger (see magnify())
RIBBON = 0.5                       # a ribbon's width inside its stroke, as drawn (px)
W_MOVE = W_STRUCT = W_DETAIL = W_ENGRAVE = LINE
# The palette's stroke bands for scaled parts: band b serves node scales
# around 1.5^(b - 2), between these bounds, with a stroke of 1 / 1.5^(b - 2),
# so a line draws 0.82 to 1.22 px wide.
LINE_BANDS = [1.5 ** (b - 2) for b in range(5)]
LINE_BOUNDS = [math.sqrt(LINE_BANDS[b] * LINE_BANDS[b + 1]) for b in range(4)]
# The scale the display draws a part at, where it isn't banded: about its
# middle across the models and controls that show it (tools measure it).
SCALE = {"cymbal": 0.92, "rimhoop": 0.87, "hands": 1.2, "head": 1.08, "sheen": 0.67, "collar": 0.71, "stave0": 0.65,
         "stave1": 0.65, "lathe": 0.92, "lathe2": 0.92, "seam": 1.2, "woodgrain": 0.91, "planegrain": 0.76}


def curve(points, per=4, closed=False):
    """A smooth curve through points (Catmull-Rom), `per` points a span: for
    parts that are curved in life, drawn through a few points on them."""
    n = len(points)
    out = []
    spans = n if closed else n - 1
    for i in range(spans):
        p0 = points[(i - 1) % n] if closed else points[max(i - 1, 0)]
        p1, p2 = points[i], points[(i + 1) % n]
        p3 = points[(i + 2) % n] if closed else points[min(i + 2, n - 1)]
        for k in range(per):
            t = k / per
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2 +
                                    (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in (0, 1)))
    if not closed:
        out.append(points[-1])
    return out

FOOT = (300.0, 128.0)       # the hinge pin
BEAM = 256.0                # the board's running length, head to foot
BOARD_T = 10.0
POST_X = 40.0               # the graduated post's centre (34..46)
BASE_Y = 202.0              # the base's top, where everything stands
BODY_X = 400.0              # the instrument's centre line
GALLOWS_H = 21.0            # a bell's gallows, from the running surface to its crown
PIVOT = (19.0, 53.0)        # the pendulum's pivot
INDEX = (-38.0, 15.0)       # the board's index tip, board space


def num(v):
    # A tenth of a pixel is finer than the screen shows, and keeps the artwork
    # well under its 64 KB.
    text = f"{round(v, 1):.1f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def d_of(points):
    return "M" + " L".join(f"{num(x)},{num(y)}" for x, y in points) + "Z"


def dedupe(points):
    out = []
    for p in points:
        if not out or math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) > 0.25:
            out.append(p)
    while len(out) > 2 and math.hypot(out[0][0] - out[-1][0], out[0][1] - out[-1][1]) < 0.25:
        out.pop()
    return out


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def ellipse(cx, cy, rx, ry, n=24, a0=0.0):
    return [(cx + rx * math.cos(a0 + 2 * math.pi * i / n), cy + ry * math.sin(a0 + 2 * math.pi * i / n)) for i in range(n)]


def arc(cx, cy, rx, ry, a0, a1, n):
    return [(cx + rx * math.cos(a0 + (a1 - a0) * i / n), cy + ry * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


def ribbon(points, w=RIBBON):
    """A line as a thin closed ribbon: out along one edge, back along the other."""
    left, right = [], []
    for i, (x, y) in enumerate(points):
        a = points[max(0, i - 1)]
        b = points[min(len(points) - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L * w / 2, dx / L * w / 2
        left.append((x + nx, y + ny))
        right.append((x - nx, y - ny))
    return left + right[::-1]


def comb(spine, teeth, w=RIBBON):
    """Many engraved ticks as one outline: a thin spine (points along it, in
    order) and ticks standing off it, each (spine point index, tip). The spine
    is traced out along one side with each tick visited, and back along the
    other: 4 points a tick, so up to 15 ticks an outline."""
    out = []
    for i, (x, y) in enumerate(spine):
        a = spine[max(0, i - 1)]
        b = spine[min(len(spine) - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy) or 1
        tx, ty = dx / L, dy / L
        tips = [t for k, t in teeth if k == i]
        if not tips:
            out.append((x, y))
            continue
        for tip in tips:
            h = w / 2
            out += [(x - tx * h, y - ty * h), (tip[0] - tx * h, tip[1] - ty * h), (tip[0] + tx * h, tip[1] + ty * h), (x + tx * h, y + ty * h)]
    # back along the spine's other side
    back = []
    for i, (x, y) in enumerate(spine):
        a = spine[max(0, i - 1)]
        b = spine[min(len(spine) - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy) or 1
        back.append((x + dy / L * w, y - dx / L * w))
    # a straight run of the spine needs only its ends
    keep = [back[0]]
    for i in range(1, len(back) - 1):
        (ax, ay), (bx, by), (cx, cy) = keep[-1], back[i], back[i + 1]
        if abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) > 0.05:
            keep.append(back[i])
    keep.append(back[-1])
    return out + keep[::-1]


def rib(name):
    """A ribbon's width in the instrument's art, to draw RIBBON wide at its part's usual scale."""
    return RIBBON / (ZOOM * SCALE[name])


STATIC = []        # (layer, svg)
NODES = []         # (name, svg element without id/transform, origin, layer)
NAMES = {}


def paint(stroke, width, fill):
    return f'fill="{fill}" stroke="{stroke}" stroke-width="{width}"'


def static(points, stroke=MID, width=W_STRUCT, fill="none", layer="back"):
    pts = dedupe(points)
    assert 3 <= len(pts) <= 64, len(pts)
    STATIC.append((layer, f'<path d="{d_of(pts)}" {paint(stroke, width, fill)}/>'))


def node(name, points, origin=(640, 320), stroke=MID, width=W_STRUCT, fill="none", layer="body"):
    pts = dedupe(points)
    assert 3 <= len(pts) <= 64, (name, len(pts))
    NAMES[name] = len(NODES)
    NODES.append((name, f'<path d="{d_of(pts)}" {paint(stroke, width, fill)}/>', origin, layer))


def circle_node(name, r, origin=(640, 320), stroke=MID, width=W_STRUCT, fill="none", layer="body"):
    NAMES[name] = len(NODES)
    NODES.append((name, f'<circle cx="0" cy="0" r="{num(r)}" {paint(stroke, width, fill)}/>', origin, layer))


def path_node(name, stroke, width, layer):
    pts = [(-32 + i, (i % 2) * 0.5) for i in range(32)] + [(31 - i, 3 + (i % 2) * 0.5) for i in range(32)]
    NAMES[name] = len(NODES)
    NODES.append((name, f'<path d="{d_of(pts)}" fill="none" stroke="{stroke}" stroke-width="{width}"/>', (640, 320), layer))


# ------------------------------------------------------------- the paths --
path_node("head", BRIGHT, W_MOVE, "head")        # a drum's batter head, dented by each strike
path_node("teeth", LIGHT, W_STRUCT, "front")     # a ratchet's gear; a cowbell's mouth; a cabasa's front chain

# ------------------------------------------------- the base and the post --
# A moulded walnut base the length of the screen, on two scrolled feet.
base = [(2, BASE_Y + 2), (6, BASE_Y), (474, BASE_Y), (478, BASE_Y + 2), (478, BASE_Y + 9)]
base += arc(462, BASE_Y + 9, 10, 7, 0, math.pi, 8)
base += arc(18, BASE_Y + 9, 10, 7, 0, math.pi, 8)
base += [(2, BASE_Y + 9)]
static(base, MID, W_STRUCT, WOOD)
static(ribbon([(8, BASE_Y + 3.4), (472, BASE_Y + 3.4)]), DIM, W_ENGRAVE, DIM)
# The graduated post: a square column on a plinth under a brass cap, its slot, its scale.
post = [(POST_X - 10, BASE_Y), (POST_X - 9, BASE_Y - 5), (POST_X - 6, BASE_Y - 5), (POST_X - 6, 48), (POST_X - 8, 48), (POST_X - 8, 45),
        (POST_X - 4, 41), (POST_X + 4, 41), (POST_X + 8, 45), (POST_X + 8, 48), (POST_X + 6, 48), (POST_X + 6, BASE_Y - 5),
        (POST_X + 9, BASE_Y - 5), (POST_X + 10, BASE_Y)]
static(post, MID, W_STRUCT, WOOD)
static(rect(POST_X - 1.3, 56, POST_X + 1.3, BASE_Y - 12), DIM, W_DETAIL, BLACK)
ticks = [(i, y) for i, y in enumerate(range(58, int(BASE_Y) - 8, 5))]
for half in (ticks[:14], ticks[14:]):
    spine = [(POST_X + 6.3, y) for _, y in half]
    teeth = [(k, (POST_X + (12 if i % 5 == 0 else 9.5), y)) for k, (i, y) in enumerate(half)]
    static(comb(spine, teeth), MID, W_ENGRAVE, MID)
# The pendulum's bracket and the arc it reads against.
static([(POST_X - 6, PIVOT[1] - 2), (PIVOT[0] - 2, PIVOT[1] - 2), (PIVOT[0] - 2, PIVOT[1] + 2), (POST_X - 6, PIVOT[1] + 2)], BRASS, W_DETAIL, METAL)
spine, teeth = [], []
for k in range(-3, 4):
    a = math.radians(90 + 6 * k)
    for sub in ((0, 1) if k < 3 else (0,)):
        b = math.radians(90 + 6 * k + 3 * sub)
        spine.append((PIVOT[0] + 44.5 * math.cos(b), PIVOT[1] + 44.5 * math.sin(b)))
        if sub == 0:
            r1 = 47.5 if k % 3 else 50
            teeth.append((len(spine) - 1, (PIVOT[0] + r1 * math.cos(a), PIVOT[1] + r1 * math.sin(a))))
static(comb(spine[::-1], [(len(spine) - 1 - i, t) for i, t in teeth]), DIM, W_ENGRAVE, DIM)

# --------------------------------------------------- the hinge and its quadrant --
static([(FOOT[0] - 15, BASE_Y), (FOOT[0] - 12, FOOT[1] + 13), (FOOT[0] - 14, FOOT[1] + 13), (FOOT[0] - 14, FOOT[1] + 10),
        (FOOT[0] + 14, FOOT[1] + 10), (FOOT[0] + 14, FOOT[1] + 13), (FOOT[0] + 12, FOOT[1] + 13), (FOOT[0] + 15, BASE_Y)], MID, W_STRUCT, WOOD)
# the quadrant: a brass sector behind the board's foot, ticked every 2 degrees
# of tilt (3 to 17), where the board's index points
index_r = math.hypot(*INDEX)
index_a = math.degrees(math.atan2(INDEX[1], INDEX[0]))   # board space, at no tilt
sweep = [1 + 18 * i / 10 for i in range(11)]
outer = [(FOOT[0] + (index_r + 5) * math.cos(math.radians(index_a + t)), FOOT[1] + (index_r + 5) * math.sin(math.radians(index_a + t))) for t in sweep]
inner = [(FOOT[0] + (index_r - 6) * math.cos(math.radians(index_a + t)), FOOT[1] + (index_r - 6) * math.sin(math.radians(index_a + t))) for t in sweep]
static(outer + inner[::-1], BRASS, W_DETAIL, METAL)
spine, teeth = [], []
for t in range(3, 18, 1):
    a = math.radians(index_a + t)
    r = index_r + 3.5
    spine.append((FOOT[0] + r * math.cos(a), FOOT[1] + r * math.sin(a)))
    if t % 2:
        long = (t - 3) % 4 == 0
        r0 = index_r - (4.5 if long else 2)
        teeth.append((len(spine) - 1, (FOOT[0] + r0 * math.cos(a), FOOT[1] + r0 * math.sin(a))))
static(comb(spine, teeth), LIGHT, W_ENGRAVE, LIGHT)

# --------------------------------------------- the board and its fittings --
# Board space: x along the board from the foot (0) to the head (-BEAM), y down
# from the running surface. display.cpp turns these about the hinge.
edge = [(-BEAM - 2, 0), (3, 0), (3, BOARD_T)]
for i in range(9, 0, -1):          # the engraved scale: nicks in the lower edge every tenth
    x = -BEAM * i / 10
    edge += [(x + 0.8, BOARD_T), (x + 0.8, BOARD_T - 2.6), (x - 0.8, BOARD_T - 2.6), (x - 0.8, BOARD_T)]
edge += [(-BEAM - 2, BOARD_T)]
node("board", edge, FOOT, LIGHT, W_STRUCT, WOOD, "board")
# the grain along its side, and the brass lining of the groove
pts = [(-BEAM + 6 + i * (BEAM - 14) / 14, 5.4 + 0.7 * math.sin(i * 0.9 + 1.7)) for i in range(15)]
node("grain", ribbon(pts), FOOT, DIM, W_ENGRAVE, DIM, "board")
node("lining", ribbon([(-BEAM + 1, -0.6), (1, -0.6)]), FOOT, BRASS, W_DETAIL, METAL, "board")
node("ferrule_head", rect(-BEAM - 4, -1.5, -BEAM + 3, BOARD_T + 1.5), FOOT, BRASS, W_DETAIL, METAL, "board")
node("ferrule_foot", rect(-5, -1.5, 4, BOARD_T + 1.5), FOOT, BRASS, W_DETAIL, METAL, "board")
node("index", [(-30, BOARD_T), (-26, BOARD_T), (INDEX[0] + 0.5, INDEX[1] + 4), (INDEX[0] - 0.5, INDEX[1] + 4)],
     FOOT, BRASS, W_DETAIL, METAL, "board")
# the cup holding the handful, and the catch: a post, and its lever across the groove
cx = -BEAM * (1 - 0.06)
node("cup", [(cx - 12, 0), (cx - 12, -7), (cx - 10.5, -7), (cx - 10.5, -1.5), (cx + 3.5, -1.5), (cx + 3.5, -7), (cx + 5, -7), (cx + 5, 0)],
     FOOT, BRASS, W_DETAIL, METAL, "board")
gx = -BEAM * (1 - 0.105)
node("catch_post", rect(gx - 1, -13, gx + 1, 0), FOOT, BRASS, W_DETAIL, METAL, "board")
node("catcher", [(0, -1.1), (11, -0.6), (12, 0.8), (0, 1.1), (-1.6, 0)], (640, 320), BRASS, W_DETAIL, METAL, "front")
# the bells' gallows, seen from the side: a slender post on the board's edge
# and its arm over the groove, the bell hung from the arm's end
gallows = [(4.0, 0), (4.0, -GALLOWS_H + 0.8), (3.4, -GALLOWS_H), (-0.6, -GALLOWS_H), (-0.6, -GALLOWS_H + 0.6), (3.4, -GALLOWS_H + 0.6),
           (3.4, 0)]
for i in range(16):
    node(f"gallows{i}", gallows, (640, 320), BRASS, W_DETAIL, METAL, "gallows")
# a bell: its crown loop, shoulder, waist and flared lip, hung from the gallows
bell = [(-0.5, 0), (0.5, 0), (0.5, 1.3), (1.6, 1.6), (2.7, 2.9), (3.0, 5.0), (3.4, 6.8), (4.9, 8.6), (4.7, 9.4),
        (-4.7, 9.4), (-4.9, 8.6), (-3.4, 6.8), (-3.0, 5.0), (-2.7, 2.9), (-1.6, 1.6), (-0.5, 1.3)]
for i in range(16):
    node(f"bell{i}", bell, (640, 320), LIGHT, W_DETAIL, METAL, "bells")

# Galileo's dotted half-parabola from the lip, and the hops on the body: the
# ball's path as a stroboscope would catch it, a dot for each equal moment;
# display.cpp lights the dots the ball has passed and sizes them by its speed.
# Under the ball and the shot, which pass over them.
for i in range(7):
    circle_node(f"fall{i}", 1.7, (640, 320), DIM, 0.9, DIM, "front")
for i in range(24):
    circle_node(f"hop{i}", 1.7, (640, 320), DIM, 0.9, DIM, "front")

# The handful in the cup, the shot in flight, and the ball with its glint.
for i in range(8):
    circle_node(f"heap{i}", 2.2, (640, 320), MID, W_DETAIL, METAL, "board")
for i in range(5):
    circle_node(f"shot{i}", 3.0, (640, 320), LIGHT, W_DETAIL, "#7a7a7a", "front")
circle_node("ball", 5.4, (640, 320), BRIGHT, W_MOVE, BALL, "front")
node("glint", ribbon(arc(0, 0, 3.0, 3.0, math.radians(200), math.radians(260), 4), 1.1), (640, 320), "#ffffff", 0.5, "#ffffff", "front")

# The pendulum: rod and lens bob, with the cord that ties it back on FREE; the
# peg in the post and its thumbscrew.
pend = [(0.3, 0), (0.3, 37.5)] + arc(0, 42, 4.6, 4.6, -math.pi / 2 + 0.065, 3 * math.pi / 2 - 0.065, 20) + [(-0.3, 37.5), (-0.3, 0)]
node("pendulum", pend, PIVOT, LIGHT, W_DETAIL, METAL, "front")
node("tie", [(0, -0.3), (12, -0.3), (12, 0.3), (0, 0.3)], (640, 320), LIGHT, W_DETAIL, LIGHT, "front")
node("peg", [(-6, 0), (16, 0), (17.5, 1.8), (16, 3.6), (-6, 3.6)], (640, 320), BRASS, W_DETAIL, METAL, "board")
circle_node("screw", 2.8, (640, 320), BRASS, W_DETAIL, METAL, "front")

# ----------------------------------------------------------- the drums --
# Authored at their drawn size around the batter head's centre; display.cpp
# scales them a little with tune. The head itself is the "head" path, over its hoop.
RX, RY, DEPTH = 60.0, 10.5, 40.0
front = [(RX * math.cos(a), RY * math.sin(a)) for a in [math.pi * i / 24 for i in range(25)]]          # the head's near half, right to left
bottom = [(RX * math.cos(a), DEPTH + RY * math.sin(a)) for a in [math.pi - math.pi * i / 24 for i in range(25)]]
shell = [(-RX, 0), (-RX, DEPTH)] + bottom[1:-1] + [(RX, DEPTH), (RX, 0)] + front[1:-1]
node("shell", shell, (640, 320), MID, W_STRUCT, WOOD)
# the sheen: one tell of a lacquered shell
node("sheen", [(-RX * 0.62, RY * 0.79 + 2), (-RX * 0.5, RY * 0.87 + 2), (-RX * 0.5, DEPTH + RY * 0.87 - 2), (-RX * 0.62, DEPTH + RY * 0.79 - 2)],
     (640, 320), DIM, W_ENGRAVE, METAL)
node("hoop_low", [(RX * math.cos(a), DEPTH - 2.8 + RY * math.sin(a)) for a in [math.pi - math.pi * i / 24 for i in range(25)]]
     + [(RX * 1.02 * math.cos(a), DEPTH + 0.8 + RY * 1.04 * math.sin(a)) for a in [math.pi * i / 24 for i in range(25)]], (640, 320), MID, W_DETAIL, METAL)
# the bass drum's deeper shell and its lower hoop
BASS = 62.0
bass_bottom = [(RX * math.cos(a), BASS + RY * math.sin(a)) for a in [math.pi - math.pi * i / 24 for i in range(25)]]
node("bass_shell", [(-RX, 0), (-RX, BASS)] + bass_bottom[1:-1] + [(RX, BASS), (RX, 0)] + front[1:-1], (640, 320), MID, W_STRUCT, WOOD)
node("bass_hoop", [(RX * math.cos(a), BASS - 2.8 + RY * math.sin(a)) for a in [math.pi - math.pi * i / 24 for i in range(25)]]
     + [(RX * 1.02 * math.cos(a), BASS + 0.8 + RY * 1.04 * math.sin(a)) for a in [math.pi * i / 24 for i in range(25)]], (640, 320), MID, W_DETAIL, METAL)
# the six tension rods and lugs, one outline hung from the hoop's lower rim
band_lo = lambda a: (RX * math.cos(a), (RY + 1.2) * math.sin(a) + 1.6)
band_hi = lambda a: (RX * math.cos(a), (RY + 1.2) * math.sin(a) + 0.4)


def lugs(rod, lug):
    pts = [band_lo(0.0)]
    for i in range(6):
        a = math.pi * (5.5 - i) / 6          # right to left
        lx, top = band_lo(a)
        w = 2.0 * math.sin(a) + 0.8
        mid = top + rod
        pts += [(lx + 0.4, top), (lx + 0.4, mid), (lx + w, mid), (lx + w, mid + lug), (lx - w, mid + lug), (lx - w, mid), (lx - 0.4, mid), (lx - 0.4, top)]
    pts.append(band_lo(math.pi))
    return pts + [band_hi(math.pi * (6 - k) / 6) for k in range(0, 7)]


node("lugs", lugs(DEPTH * 0.3, 9), (640, 320), LIGHT, W_DETAIL, METAL, "drum")


def deep(name, depth):
    """A shell `depth` deep under the head, and its lower hoop."""
    bot = [(RX * math.cos(a), depth + RY * math.sin(a)) for a in [math.pi - math.pi * i / 24 for i in range(25)]]
    node(f"{name}_shell", [(-RX, 0), (-RX, depth)] + bot[1:-1] + [(RX, depth), (RX, 0)] + front[1:-1], (640, 320), MID, W_STRUCT, WOOD)
    node(f"{name}_hoop", [(RX * math.cos(a), depth - 2.8 + RY * math.sin(a)) for a in [math.pi - math.pi * i / 24 for i in range(25)]]
         + [(RX * 1.02 * math.cos(a), depth + 0.8 + RY * 1.04 * math.sin(a)) for a in [math.pi * i / 24 for i in range(25)]], (640, 320), MID, W_DETAIL, METAL)


# The snare's shallow shell, its short lugs; a rack tom's deep one.
SNARE_D, TOM_D = 22.0, 50.0
deep("snare", SNARE_D)
node("snare_lugs", lugs(3.5, 7), (640, 320), LIGHT, W_DETAIL, METAL, "drum")
deep("tom", TOM_D)
# A rack tom's mount: a bracket on the shell's side, the L-arm out of it and
# down to the clamp on a stand's post, the post to the floor. Its origin is the
# clamp; the bracket sits up and to the left, on the shell.
node("mount", [(-18, -32), (-5, -32), (-5, -26), (-1.6, -26), (-1.6, -2), (3.2, -2), (3.2, 3), (1.8, 3), (1.8, 40),
               (-1.8, 40), (-1.8, 3), (-3.2, 3), (-3.2, -2), (-1.9, -2), (-1.9, -20), (-5, -20), (-5, -14), (-18, -14)],
     (640, 320), LIGHT, W_DETAIL, METAL)
# A rim-click's heavy counter-hoop round the head, its claws hooked under it.
claws = []
for i in range(5):
    a = math.pi * (0.1 + 0.8 * i / 4)
    claws.append(a)
hoop_out = []
for k in range(25):
    a = math.pi * k / 24
    hoop_out.append((RX * 1.07 * math.cos(a), (RY + 3.6) * math.sin(a) + 1.0))
hoop_in = [(RX * 1.0 * math.cos(math.pi * k / 24), (RY + 0.6) * math.sin(math.pi * k / 24) - 0.8) for k in range(25)]
node("rimhoop", hoop_out + hoop_in[::-1], (640, 320), BRIGHT, W_STRUCT, METAL, "drum")
node("hoop", ellipse(0, 0, RX + 2.4, RY + 1.3, 60), (640, 320), MID, W_STRUCT, METAL, "drum")
node("collar", ribbon(ellipse(0, 0, RX * 0.9, RY * 0.9, 40)[:21], rib("collar")), (640, 320), DIM, W_ENGRAVE, DIM, "over")
# the felt laid on a head (decay): wider as decay falls
node("felt", [(0, -3.2), (34, -2.0), (36, 0.4), (34, 3.2), (0, 3.6), (-1.5, 0)], (640, 320), LIGHT, W_DETAIL, FELT, "over")
# the snare's wires under the bottom head, and its strainer
wires = [(-36, 0), (36, 0), (36, 3.6)] + [(36 - 3.6 * i - 1.8, 2.0 if i % 2 else 3.6) for i in range(20)] + [(-36, 3.6)]
node("wires", wires, (640, 320), LIGHT, W_DETAIL, METAL)
# the strainer on the shell's side, its throw-off lever (up: the wires on)
node("strainer", [(-3.5, -6), (4, -6), (5.5, -4), (5.5, 6), (4, 8), (-3.5, 8)], (640, 320), BRASS, W_DETAIL, METAL)
node("lever", [(-1.2, 0), (-1, -14), (0, -16), (1, -14), (1.2, 0), (2.4, 1.6), (0, 3.2), (-2.4, 1.6)], (640, 320), LIGHT, W_DETAIL, METAL)
# a stick (snare tone, rim wood): a tapered shaft and its bead
node("stick", [(0, -1.4), (60, -0.9), (63, -1.4), (66, 0), (63, 1.4), (60, 0.9), (0, 1.4)], (640, 320), LIGHT, W_DETAIL, METAL, "over")
# a muffling ring on a tom's head, its near half
node("ring", ribbon(ellipse(0, 0, 28, 4.4, 40)[:21], 1.0), (640, 320), MID, W_DETAIL, FELT, "over")
# the kick's spurs and its pedal: footboard, beater shaft and head
node("spur_l", [(0, 0), (1.8, 0), (-8, 34), (-1.4, 34), (-1.4, 35.6), (-10, 35.6)], (640, 320), MID, W_STRUCT, METAL)
node("spur_r", [(0, 0), (-1.8, 0), (8, 34), (1.4, 34), (1.4, 35.6), (10, 35.6)], (640, 320), MID, W_STRUCT, METAL)
node("footboard", [(-20, 0), (18, -7), (19, -5), (-19, 2)], (640, 320), MID, W_DETAIL, METAL)
node("shaft", rect(-0.4, -52, 0.4, 0), (640, 320), MID, W_DETAIL, METAL)
node("beater", ellipse(0, 0, 6.5, 6.5, 28), (640, 320), LIGHT, W_STRUCT, FELT, "over")
# a snare stand: a column, three basket arms, a tripod with its feet
node("column", [(-1.4, 0), (1.4, 0), (1.4, 60), (2.4, 60), (2.4, 64), (-2.4, 64), (-2.4, 60), (-1.4, 60)], (640, 320), MID, W_DETAIL, METAL)
node("basket", [(-34, -4), (-31, -4), (0, 6), (31, -4), (34, -4), (1, 8.5), (-1, 8.5)], (640, 320), MID, W_DETAIL, METAL)
node("tripod", [(-34, 0), (-35, -2), (0, -18), (35, -2), (34, 0), (31, 0), (0, -14.5), (-31, 0)], (640, 320), MID, W_STRUCT, METAL)
# a floor tom's legs
node("legs", [(-58, 0), (-54, 0), (-47, -46), (-45, -46), (45, -46), (47, -46), (54, 0), (58, 0), (48, -43), (-48, -43)], (640, 320), MID, W_DETAIL, METAL)
# a conga: a tall staved barrel under the head, on a low ring stand; the slapping hand and the muting palm
side = curve([(-RX, 0), (-RX * 1.07, 18), (-RX * 1.1, 40), (-RX * 1.03, 72), (-RX * 0.84, 100)], 3)
barrel = side + [(RX * 0.84 * math.cos(a), 100 + RY * 0.84 * math.sin(a)) for a in [math.pi - math.pi * i / 14 for i in range(1, 14)]] + \
         [(-x, y) for x, y in side[::-1]] + front[1:-1]
node("barrel", barrel, (640, 320), MID, W_STRUCT, WOOD)
for k, x in enumerate((-0.5, 0.5)):
    pts = [(RX * x * (1 + 0.12 * math.sin(math.pi * t / 6)), RY * 0.9 + 1 + t * 15.2) for t in range(7)]
    node(f"stave{k}", ribbon(curve(pts, 2), rib(f"stave{k}")), (640, 320), DIM, W_ENGRAVE, DIM)
node("ringstand", [(-46, 0), (46, 0), (40, 10), (36, 10), (40, 3), (-40, 3), (-36, 10), (-40, 10)], (640, 320), MID, W_DETAIL, METAL)
hand = [(-18, 1), (-13.5, -2.9), (-4.5, -4.1), (2.2, -3.6), (8, -5), (13.5, -5.2), (18, -3.4), (19, -0.7), (17, 1.6), (10, 2.5), (4.5, 3.2), (-11, 3.4)]
node("slap", curve(hand, 3, closed=True), (640, 320), LIGHT, W_STRUCT, FELT, "over")
node("palm", curve([(-13.5, 0.4), (-9, -3.2), (6.8, -3.6), (13.5, -1.6), (14.6, 1.4), (9, 2.5), (-10, 2.5)], 4, closed=True), (640, 320), MID, W_STRUCT, FELT, "over")
# the fill's rack: a bar on a post with two clamped toms
node("rack", [(-2, 0), (2, 0), (2, -46), (44, -46), (44, -43), (2, -43), (2, -40), (-2, -40)], (640, 320), MID, W_DETAIL, METAL)
for k in (1, 2):
    node(f"rack_shell{k}", shell, (640, 320), MID, W_STRUCT, WOOD)
    node(f"rack_head{k}", ellipse(0, 0, RX + 1.8, RY + 1, 48), (640, 320), LIGHT, W_STRUCT, METAL)

# ----------------------------------------------------------- the metals --
# A cymbal in three-quarter view: the plate, two lathe rings, its bell.
plate = ellipse(0, 0, 58, 8.5, 60)
node("cymbal_low", plate, (640, 320), MID, W_STRUCT, WOOD)
node("cymbal", plate, (640, 320), BRIGHT, W_MOVE, METAL)
node("lathe", ribbon(ellipse(0, 0, 40, 5.9, 36)[:19], rib("lathe")), (640, 320), DIM, W_ENGRAVE, DIM)
node("lathe2", ribbon(ellipse(0, 0, 24, 3.5, 28)[:15], rib("lathe2")), (640, 320), DIM, W_ENGRAVE, DIM)
node("dome", arc(0, 0, 11.5, 7.5, math.pi, 2 * math.pi, 22)[1:-1] + [(11.5, 0.4), (-11.5, 0.4)], (640, 320), LIGHT, W_DETAIL, METAL)
node("clutch", [(-4, -4), (4, -4), (4, -1), (2, 0), (-2, 0), (-4, -1)], (640, 320), BRASS, W_DETAIL, METAL)
node("felts", [(-5, -2), (5, -2), (5, 2), (-5, 2)], (640, 320), MID, W_DETAIL, FELT)
node("wingnut", [(-6, -2), (-2, -1.4), (-1.5, -3.5), (1.5, -3.5), (2, -1.4), (6, -2), (6, 0), (-6, 0)], (640, 320), MID, W_DETAIL, METAL)
for i in range(4):
    circle_node(f"rivet{i}", 2.0, (640, 320), LIGHT, W_DETAIL, METAL)
node("pedal", [(-15, 0), (15, -7.5), (16, -5.5), (-14, 2)], (640, 320), MID, W_DETAIL, METAL)
node("rod", rect(-1.1, 0, 1.1, 60), (640, 320), MID, W_DETAIL, METAL)
# tubular chimes: a frame (posts and a top beam), five tubes each hung on its cord, the damper bar, the hammer
node("frame", [(-52, 0), (-49, 0), (-49, -112), (49, -112), (49, 0), (52, 0), (52, -116), (-52, -116)], (640, 320), MID, W_DETAIL, METAL)
for i in range(5):
    tube = [(-0.3, -9), (0.3, -9), (0.3, -1.8), (3.6, -1.8), (4.6, 0), (4.6, 3), (3.9, 3), (3.9, 66), (-3.9, 66), (-3.9, 3),
            (-4.6, 3), (-4.6, 0), (-3.6, -1.8), (-0.3, -1.8)]
    node(f"tube{i}", tube, (640, 320), LIGHT, W_DETAIL, METAL)
node("damper_bar", [(-42, -2.4), (42, -2.4), (42, 2.4), (-42, 2.4)], (640, 320), LIGHT, W_DETAIL, FELT)
node("hammer_shaft", rect(-0.4, -48, 0.4, 0), (640, 320), MID, W_DETAIL, METAL)
node("hammer", [(-6.5, -3.6), (6.5, -3.6), (7.6, 0), (6.5, 3.6), (-6.5, 3.6), (-7.6, 0)], (640, 320), LIGHT, W_STRUCT, FELT)

# Alpine cowbells on a wooden yoke: each a forged body with its riveted lip
# band and a leather strap through its crown; the clapper swings in the open
# mouth (the "teeth" path is the first bell's mouth, flared by metal). The
# second bell grows with pair; a cloth tape round the first damps it (decay).
node("yoke", [(-44, -3), (-40, -5), (40, -5), (44, -3), (44, 3), (40, 5), (-40, 5), (-44, 3)], (640, 320), MID, W_STRUCT, WOOD)
# the body's side, crown to mouth: a rounded shoulder and a flare, in one curve
cowside = curve([(9, 0), (12.2, 0.9), (14, 3.6), (14.6, 9), (15.1, 18), (16.2, 30), (18, 38)], 3)
cowbody = [(-9, 0)] + cowside + [(-x, y) for x, y in cowside[::-1]][:-1]
# a leather loop through the crown, round over the top
strap = [(-4.5, 1)] + arc(0, -6.5, 4.5, 4.5, math.pi, 2 * math.pi, 8) + [(4.5, 1), (3, 1)] + arc(0, -6.5, 3, 3, 2 * math.pi, math.pi, 8) + [(-3, 1)]
node("strap", strap,
     (640, 320), MID, W_DETAIL, FELT)
node("clapper", [(-0.35, 0), (0.35, 0), (0.35, 34)] + arc(0, 38, 4.4, 4.4, -math.pi / 2 + 0.08, 3 * math.pi / 2 - 0.08, 16)[1:-1] + [(-0.35, 34)],
     (640, 320), LIGHT, W_DETAIL, METAL)
node("cowbell", cowbody, (640, 320), LIGHT, W_STRUCT, METAL)
node("lipband", ribbon(curve([(-16.6, 31), (0, 31.6), (16.6, 31)], 4), 1.0), (640, 320), MID, W_DETAIL, METAL)
node("seam", ribbon(curve([(0, 3), (0.4, 18), (0, 29)], 3), rib("seam")), (640, 320), DIM, W_ENGRAVE, DIM)
node("clapper2", [(-0.35, 0), (0.35, 0), (0.35, 34)] + arc(0, 38, 4.4, 4.4, -math.pi / 2 + 0.08, 3 * math.pi / 2 - 0.08, 16)[1:-1] + [(-0.35, 34)],
     (640, 320), LIGHT, W_DETAIL, METAL)
node("cowbell2", cowbody, (640, 320), MID, W_STRUCT, METAL)
node("mouth2", ellipse(0, 0, 18, 2.6, 30), (640, 320), MID, W_DETAIL, BLACK)
node("strap2", strap,
     (640, 320), MID, W_DETAIL, FELT)
node("tape", [(-8, 0), (8, 0), (8.4, 5), (-8.4, 5)], (640, 320), MID, W_DETAIL, FELT)

# A bracket clock: a case with a bell top, carrying handle and feet; the
# silvered dial with its chapter ring and hands; through the glazed door
# below the dial, the pendulum swinging tick and tock; the bell on top.
# the bell top, an inverted bell's ogee, in one curve over the case's shoulders
top = curve([(-34, -54), (-31, -58), (-26, -60.5), (-23.5, -66), (-16, -72), (-6, -74.2), (6, -74.2), (16, -72), (23.5, -66),
             (26, -60.5), (31, -58), (34, -54)], 3)
case = [(-34, 0), (-36, 3), (-30, 3), (-30, 6), (-34, 6)] + top + [(34, 6), (30, 6), (30, 3), (36, 3), (34, 0)]
node("clockcase", case, (640, 320), MID, W_STRUCT, WOOD)
node("door", [(-15, -20), (15, -20), (15, -2), (-15, -2)], (640, 320), MID, W_DETAIL, BLACK)
node("bob", [(-0.35, 0), (0.35, 0), (0.35, 22)] + arc(0, 25, 4.2, 3.2, -math.pi / 2 + 0.09, 3 * math.pi / 2 - 0.09, 16)[1:-1] + [(-0.35, 22)],
     (640, 320), LIGHT, W_DETAIL, METAL)
# the dial: a silvered disc, its chapter ring's inner edge notched at each
# hour (one outline, both edges traced the same way round, so it fills whole
# and hides the pendulum's rod behind it)
ring = [(16 * math.cos(-math.pi / 2 - 2 * math.pi * i / 26), 16 * math.sin(-math.pi / 2 - 2 * math.pi * i / 26)) for i in range(27)]
inner = []
for h in range(12):
    a = -math.pi / 2 - 2 * math.pi * h / 12
    for da, r in ((0.09, 12), (0, 9.6 if h % 3 else 8.2), (-0.09, 12)):
        inner.append((r * math.cos(a + da), r * math.sin(a + da)))
inner.append((12 * math.cos(-math.pi / 2 + 0.2), 12 * math.sin(-math.pi / 2 + 0.2)))
node("dial", ring + inner, (640, 320), LIGHT, W_DETAIL, METAL)
node("hands", [(-0.4, 1.6), (-0.4, -11.5), (0, -12.6), (0.4, -11.5), (0.4, -0.4), (8.2, -0.4), (9, 0), (8.2, 0.4), (0.4, 0.4), (0.4, 1.6)],
     (640, 320), BRIGHT, W_DETAIL, BRIGHT)
node("gong", arc(0, 0, 13, 9, math.pi, 2 * math.pi, 22)[1:-1] + [(13, 0), (13, 1.6), (-13, 1.6), (-13, 0)], (640, 320), BRASS, W_STRUCT, METAL)
node("finial", [(-1.2, 0), (-1.2, -3), (-2.6, -4.5), (0, -7), (2.6, -4.5), (1.2, -3), (1.2, 0)], (640, 320), BRASS, W_DETAIL, METAL)

# ---------------------------------------------------------- the woods --
# a woodblock on a clamp: the block, its grain and slit, the holder; a mallet (shared)
# a block of hardwood, its edges rounded
block = (arc(-43, -11.5, 3.5, 3.5, math.pi, 1.5 * math.pi, 4) + arc(43, -11.5, 3.5, 3.5, 1.5 * math.pi, 2 * math.pi, 4) +
         arc(44.5, 11.5, 3.5, 3.5, 0, 0.5 * math.pi, 4) + arc(-44.5, 11.5, 3.5, 3.5, 0.5 * math.pi, math.pi, 4))
node("woodblock", block, (640, 320), MID, W_STRUCT, WOOD)
node("woodgrain", ribbon(curve([(-39, -6), (-12, -7), (16, -5.2), (39, -7)], 4), rib("woodgrain")), (640, 320), DIM, W_ENGRAVE, DIM)
node("slot", [(-34, -2), (34, -2), (35.6, 0), (34, 2), (-34, 2), (-35.6, 0)], (640, 320), MID, W_DETAIL, BLACK)
node("holder", [(-6, 0), (6, 0), (6, 6), (2, 6), (2, 40), (-2, 40), (-2, 6), (-6, 6)], (640, 320), MID, W_DETAIL, METAL)
node("mallet_shaft", rect(-0.4, -48, 0.4, 0), (640, 320), MID, W_DETAIL, METAL)
node("mallet", ellipse(0, 0, 5.5, 5.5, 28), (640, 320), LIGHT, W_STRUCT, FELT)

# A slapstick, the orchestra's clapper board: two maple boards hinged at one
# end, the lower held on a stand by its handle, the upper with its own handle,
# sprung open; it slaps shut on the ball. A sound box under it (room).
board_l = [(0, 0), (118, 0), (121, 2), (121, 9), (0, 9)]
node("chamber", [(-54, 0), (54, 0), (54, 36), (-54, 36)], (640, 320), MID, W_STRUCT, WOOD)
node("soundhole", ellipse(0, 0, 9, 5, 24), (640, 320), MID, W_DETAIL, BLACK)
node("plank_low", [(0, 0), (-118, 0), (-121, 2), (-121, 9), (0, 9)], (640, 320), LIGHT, W_STRUCT, WOOD)
node("plank_up", [(0, 0), (-118, 0), (-121, -2), (-121, -9), (0, -9)], (640, 320), LIGHT, W_STRUCT, WOOD)
for k in ("knob", "knob2"):
    # a turned knob: a stem and a ball on it
    a0 = math.atan2(4.4, -2.4)
    node(k, [(-2.4, 0), (-2.4, -9.1)] + arc(0, -13.5, 5, 5, a0, 2 * math.pi + math.pi - a0, 14)[1:-1] + [(2.4, -9.1), (2.4, 0)],
         (640, 320), MID, W_DETAIL, WOOD)
node("hinge", [(-5, -9), (5, -9), (7, -7), (7, 9), (-5, 9)], (640, 320), BRASS, W_DETAIL, METAL)
node("planegrain", ribbon(curve([(-112, 4.5), (-80, 3.8), (-40, 5), (-6, 4.2)], 4), rib("planegrain")), (640, 320), DIM, W_ENGRAVE, DIM)

# A cabasa lying on its cradle: a steel cylinder wrapped in loops of bead
# chain, its open end towards us, a turned handle to the right. The front
# chain is the "teeth" path (its beads sized by grit); shell strings more
# chains along it.
CR, CL, CE = 24.0, 60.0, 7.0            # radius, length, the ends' half-depth
node("cylinder", [(0, -CR), (CL, -CR)] + [(CL + CE * math.cos(a), CR * math.sin(a)) for a in [-math.pi / 2 + math.pi * i / 16 for i in range(1, 16)]] +
     [(CL, CR), (0, CR)], (640, 320), MID, W_STRUCT, METAL)
node("cap", ellipse(0, 0, CE, CR, 32), (640, 320), LIGHT, W_DETAIL, WOOD)


def chain(beads=9, bump=3.4):
    """A loop of bead chain's near half round the cylinder: round beads on a
    string, bulging out from it (display.cpp draws the front one the same way)."""
    def at(a, d):
        x, y = -CE * 1.08 * math.cos(a), CR * 1.08 * math.sin(a)
        nx, ny = -CR * math.cos(a), CE * math.sin(a)
        L = math.hypot(nx, ny)
        return (x + nx / L * d, y + ny / L * d)
    out = []
    for j in range(beads):
        for f, d in ((0.0, 0.4), (0.3, bump), (0.7, bump)):
            out.append(at(-math.pi / 2 + math.pi * (j + f) / beads, d))
    out.append(at(math.pi / 2, 0.4))
    back = [at(-math.pi / 2 + math.pi * j / 6, -0.5) for j in range(7)]
    return out + back[::-1]


for i in range(4):
    node(f"chain{i}", chain(), (640, 320), LIGHT, W_STRUCT, METAL)
# the turned handle: its profile in one curve, mirrored
grip_top = curve([(0, -5), (8, -6), (11, -4), (30, -4.5), (36, -6), (40, -3)], 2)
node("grip", grip_top + [(x, -y) for x, y in grip_top[::-1]],
     (640, 320), MID, W_DETAIL, WOOD)
node("cradle", [(-12, 0), (-10, 0), (0, 8), (10, 0), (12, 0), (2, 11), (-2, 11)], (640, 320), MID, W_DETAIL, METAL)

# A watchman's ratchet: a cogwheel (the "teeth" path) on a turned handle;
# the frame's two rails turn on the axle and hold at their end block the
# tongue, a springy slat (the "head" path) whose tip rides the teeth.
node("handle", [(-3.5, 0), (3.5, 0), (4.5, 30), (5.5, 34), (4.5, 50), (-4.5, 50), (-5.5, 34), (-4.5, 30)], (640, 320), MID, W_DETAIL, WOOD)
node("frame_r", [(-7, -10), (84, -10), (88, -7), (88, 9), (84, 12), (-7, 12)] + arc(-7, 1, 3.5, 11, 0.5 * math.pi, 1.5 * math.pi, 8)[1:-1], (640, 320), MID, W_STRUCT, WOOD)
node("slot_r", [(9, -5), (70, -5), (70, 7), (9, 7)], (640, 320), MID, W_DETAIL, BLACK)
circle_node("hub", 5.0, (640, 320), BRASS, W_DETAIL, METAL)

HEADER_FIRST = len(NODES)

PALETTE = [
    (METAL, BRIGHT, LINE),           # 0 a ringing bell
    (METAL, LIGHT, LINE),            # 1 a still bell
    (BALL, BRIGHT, LINE),            # 2 the ball
    ("#8a8a8a", BLACK, 0.8),         # 3 a dot of the ball's path still ahead of it, ringed in black over the body's lines
    (BRIGHT, BLACK, 0.8),            # 4 a dot the ball has passed, larger
]
# 5.. the scaled parts' strokes: each grey that has them, a band of widths each (LINE_BANDS)
LINE_TONES = [LIGHT, MID]
LINE_PALETTE = len(PALETTE)
for tone in LINE_TONES:
    PALETTE += [(BLACK, tone, LINE / b) for b in LINE_BANDS]


def line_tone(i):
    """The scaled part's grey (1 + its index in LINE_TONES), or 0 for a part
    drawn at a fixed size or painted by the display itself: the instrument's
    parts (and the ratchet's gear, a reshaping outline it scales)."""
    name, el, _, _ = NODES[i]
    if i < NAMES["shell"] and name != "teeth":
        return 0
    stroke = re.search(r'stroke="([^"]+)"', el).group(1)
    return LINE_TONES.index(stroke) + 1 if stroke in LINE_TONES else 0


def stroke_width(el, width):
    return re.sub(r'stroke-width="[^"]*"', f'stroke-width="{width:.2f}"', el)


def header_svg():
    out = ['<path d="M0,0 L480,0 L480,38 L0,38Z" fill="#000000"/>']
    shades = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i in range(8):
        out.append(f'<path id="{ID}_node_{HEADER_FIRST + i}" transform="matrix(1 0 0 1 {60 * i} 0)" d="M0,0 L60,0 L60,20 L0,20Z" '
                   f'fill="{shades[i]}" stroke="{shades[i]}" stroke-width="0.5"/>')
    texts = [(5 + 120 * i, 10, "#CBCBCB" if i < 2 else "#000000", 10) for i in range(4)]
    texts += [(65 + 120 * i, 17.4, "#CBCBCB" if i < 2 else "#000000", 20) for i in range(4)]
    texts += [(5 + 120 * i, 33, "#5A5A5A", 10) for i in range(4)]
    texts += [(65 + 120 * i, 33, "#707070", 13) for i in range(4)]
    for t, (x, y, fill, size) in enumerate(texts):
        out.append(f'<text id="{ID}_text_{t}" transform="matrix(1 0 0 1 {num(x)} {num(y)})" fill="{fill}" '
                   f'font-family="UniversNextPro-Light" font-size="{size}px">-</text>')
    return out


# The instrument at the foot is drawn ZOOM times the size display.cpp lays it
# out at (its zoom about the foot of the plane): its nodes' outlines are
# authored that much larger, at the same line weights. The two reshaping
# outlines are magnified by their node's scale instead (their points stay
# within the 63 px a path allows), so their lines are authored that much
# thinner, to draw at the same weights.


def magnify(el):
    def scaled(m):
        return f'{m.group(1)}="' + re.sub(NUMBER, lambda n: num(float(n.group(0)) * ZOOM), m.group(2)) + '"'
    return re.sub(r'\b(d|r)="([^"]*)"', scaled, el)


def thinned(el):
    return re.sub(r'stroke-width="([^"]*)"', lambda m: f'stroke-width="{num(float(m.group(1)) / ZOOM)}"', el)


NUMBER = r"-?\d+(?:\.\d+)?"


def main():
    lines = [f'<g id="{ID}_panel">']
    lines += header_svg()
    lines.append('<path d="M0,38 L480,38 L480,220 L0,220Z" fill="#000000" stroke="none"/>')

    def emit(i):
        name, el, origin, layer = NODES[i]
        tag = "<path " if el.startswith("<path ") else "<circle "
        # every line 1 px as drawn: a scaled part's stroke comes from its band
        # (authored here at its unit scale), the rest's at their usual scale
        if not line_tone(i):
            el = stroke_width(el, LINE / SCALE.get(name, 1.0) * (ZOOM if name == "head" else 1))
        if i >= NAMES["shell"]:
            el = magnify(el)
        elif name in ("head", "teeth"):
            el = thinned(el)
        # A node display.cpp places every frame needs no authored place (MD KIT's
        # art does the same); only those standing where they are drawn carry one.
        place = "" if origin == (640, 320) else f'transform="matrix(1 0 0 1 {num(origin[0])} {num(origin[1])})" '
        lines.append(el.replace(tag, f'{tag}id="{ID}_node_{i}" {place}', 1))

    # Back to front: the base, post and gantry; the instrument (behind the
    # board's foot); its hoop and lugs, its head and what lies on it; the board
    # and its fittings with the hinge over them; the gallows and bells; in front
    # the catch, the ball and shot, the dots, the pendulum and the damper.
    for layer in ["back", "body", "drum", "head", "over", "board", "gallows", "bells", "front"]:
        for l, svg in STATIC:
            if l == layer:
                lines.append(svg)
        for i, (name, el, origin, nl) in enumerate(NODES):
            if nl == layer:
                emit(i)
        if layer == "board":
            lines.append(f'<path d="{d_of([(FOOT[0] - 6, FOOT[1] + 12), (FOOT[0] - 5, FOOT[1] - 3), (FOOT[0], FOOT[1] - 6), (FOOT[0] + 5, FOOT[1] - 3), (FOOT[0] + 6, FOOT[1] + 12)])}" {paint(BRASS, W_DETAIL, METAL)}/>')
            lines.append(f'<circle cx="{num(FOOT[0])}" cy="{num(FOOT[1])}" r="2.4" {paint(LIGHT, W_DETAIL, WOOD)}/>')
    for i, (fill, stroke, width) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" transform="matrix(1 0 0 1 640 320)" cx="0" cy="0" r="1" '
                     f'fill="{fill}" stroke="{stroke}" stroke-width="{width:.2f}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    (ROOT / "panel.svg.fragment").write_text(text)

    h = ["#pragma once",
         "// Generated by tools/make_art.py: the screen's nodes, in panel.svg.fragment's order.",
         "#include <opxy/types.hpp>", "", "namespace galileo {", "namespace art {", "using opxy::u32;", "using opxy::i32;",
         f"const u32 node_count = {len(NODES)};   // the artwork's nodes; the header's eight bars follow",
         "enum Node : u32 {"]
    for i, (name, *_rest) in enumerate(NODES):
        h.append(f"    {name} = {i},")
    h.append("};")
    h.append("// Each node's grey for its line's width, 1 + its index among the scaled parts' greys, 0 for none:")
    h.append(f"// palette entry line_palette + {len(LINE_BANDS)} (tone - 1) + its band (line_bounds, node scales in Q10).")
    tones = [str(line_tone(i)) for i in range(len(NODES))]
    h.append("const unsigned char line_tone[node_count] = {")
    for k in range(0, len(tones), 32):
        h.append("    " + ", ".join(tones[k:k + 32]) + ",")
    h.append("};")
    h.append(f"const u32 line_palette = {LINE_PALETTE}, line_bands = {len(LINE_BANDS)};")
    h.append("const i32 line_bounds[" + str(len(LINE_BOUNDS)) + "] = {" + ", ".join(str(int(round(b * 1024))) for b in LINE_BOUNDS) + "};")
    consts = {"foot_x": FOOT[0], "foot_y": FOOT[1], "beam": BEAM, "board_t": BOARD_T, "post_x": POST_X, "base_y": BASE_Y,
              "body_x": BODY_X, "gallows_h": GALLOWS_H, "pivot_x": PIVOT[0], "pivot_y": PIVOT[1],
              "drum_rx": RX, "drum_ry": RY, "drum_depth": DEPTH, "bass_depth": BASS, "snare_depth": SNARE_D, "tom_depth": TOM_D}
    h.append("// The plane's geometry (pixels, Q10).")
    for k, v in consts.items():
        h.append(f"const i32 {k} = {int(round(v * 1024))};")
    h += ["}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(h))
    elements = text.count("<path") + text.count("<circle")
    print(f"{len(NODES)} art nodes, {len(STATIC) + 2} static parts, {elements} elements, {len(text.encode())} bytes; "
          f"header bars from node {HEADER_FIRST}")


if __name__ == "__main__":
    main()
