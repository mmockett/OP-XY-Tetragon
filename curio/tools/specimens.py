"""CURIO's sixteen specimens as dead-front silhouettes, for tools/make_panel.py: one flat solid each
(the ball's CDCDCD, encoder 4's grey), detail cut in black. Drawn from the objects themselves.

Each specimen is a list of layers (name, points, fill, stroke, width) in px, its foot at (0, 0)
and its top near y = -H (H = 28): the first layer the solid, later layers black cuts or small
solid parts. Every outline is one closed subpath of at most 64 points, no turn sharper than
SHARPEST, points 1/8 px apart. Pure Python.
"""
import math

H = 28.0
SOLID, CUT = "#CDCDCD", "#000000"
SHARPEST = 150.0


def arc(cx, cy, rx, ry, a0, a1, n):
    """Points from angle a0 to a1 (degrees, y down) inclusive."""
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def circle(cx, cy, r, n=20):
    return arc(cx, cy, r, r, 0, 360, n)[:-1]


def ellipse(cx, cy, rx, ry, n=24, rot=0.0):
    c, s = math.cos(rot), math.sin(rot)
    return [(cx + rx * math.cos(2 * math.pi * i / n) * c - ry * math.sin(2 * math.pi * i / n) * s,
             cy + rx * math.cos(2 * math.pi * i / n) * s + ry * math.sin(2 * math.pi * i / n) * c) for i in range(n)]


def rrect(x0, y0, x1, y1, r, n=3):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        pts += arc(cx, cy, r, r, a0, a0 + 90, n)
    return pts


def ribbon(a, b, w):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l = math.hypot(dx, dy)
    nx, ny = -dy / l * w / 2, dx / l * w / 2
    return [(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny), (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)]


def polyribbon(line, w):
    """A polyline as a closed ribbon w wide, mitred at its bends."""
    def normal(a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        l = math.hypot(dx, dy)
        return -dy / l, dx / l
    left, right = [], []
    for i, (x, y) in enumerate(line):
        ns = [normal(line[i - 1], line[i])] if i > 0 else []
        ns += [normal(line[i], line[i + 1])] if i + 1 < len(line) else []
        nx, ny = sum(n[0] for n in ns) / len(ns), sum(n[1] for n in ns) / len(ns)
        k = (w / 2) / max(0.5, nx * ns[0][0] + ny * ns[0][1]) / max(1e-9, math.hypot(nx, ny)) * math.hypot(nx, ny)
        nl = math.hypot(nx, ny)
        nx, ny = nx / nl, ny / nl
        k = (w / 2) / max(0.5, nx * ns[0][0] + ny * ns[0][1])
        left.append((x + nx * k, y + ny * k))
        right.append((x - nx * k, y - ny * k))
    return left + list(reversed(right))


def mirror(right):
    """A symmetric outline from its right half, top to bottom (x >= 0)."""
    return right + [(-x, y) for x, y in reversed(right) if abs(x) > 0.06]


def solid(name, pts):
    return (name, pts, SOLID, "none", 0.5)


def cut(name, pts):
    return (name, pts, CUT, "none", 0.5)


def ball():
    r = 10.0
    return [solid("ball", circle(0, -r, r, 32)),
            ("glint", ellipse(-r * 0.38, -r * 1.42, r * 0.26, r * 0.16, 12), "#E6E6E6", "none", 0.5)]


def cask():
    # a barrel standing: flat ends 15 wide, bulging to 20, 24 tall; two hoops and the bung cut in
    right = [(7.5, -24.0)] + [(7.5 + 2.5 * math.sin(math.pi * i / 8), -24.0 + 24.0 * i / 8) for i in range(1, 8)] + [(7.5, 0.0)]
    return [solid("cask", mirror(right)),
            cut("hoop_top", ribbon((-8.9, -19.5), (8.9, -19.5), 1.4)),
            cut("hoop_foot", ribbon((-8.9, -4.5), (8.9, -4.5), 1.4)),
            cut("bung", circle(0, -12, 1.6, 10))]


def stone():
    # a ringing stone slab hung from its hole: a broad rounded slab, its hole and a chip
    slab = rrect(-13, -20, 13, 0, 4, 3)
    return [solid("stone", slab),
            cut("hole", circle(-7, -14, 2.0, 12)),
            cut("chip", ribbon((3, -6), (10, -9), 1.4))]


def drip():
    # a drop: a round foot rising to a point (the tip's angle 40 degrees, inside the device's limit)
    r = 8.0
    pts = [(0.0, -27.0)] + arc(0, -r, r, r, -20, 200, 22)
    return [solid("drop", pts),
            cut("shine", ellipse(-3.2, -9.5, 1.3, 2.6, 12))]


def ice():
    # a shard of lake ice: a slanted slab with a jagged top, a crack cut through it
    pts = [(-10, 0), (11, 0), (12, -14), (7, -19), (4, -15), (-1, -26), (-5, -18), (-10, -21)]
    return [solid("ice", pts),
            cut("crack", polyribbon([(-1, -23), (0.6, -15), (-2.4, -8.5), (0.2, -1)], 1.4))]


def sheet():
    # a thunder sheet hung from its two holes, rippled along its foot
    right = [(10.0, -27.0), (10.0, -3.0)]
    foot = [(10.0 - 20.0 * i / 8, -2.0 + 1.6 * math.sin(math.pi * i / 2)) for i in range(9)]
    pts = [(-10.0, -27.0), (10.0, -27.0), (10.0, -2.0)] + foot[1:-1] + [(-10.0, -2.0)]
    return [solid("sheet", pts),
            cut("hole_l", circle(-6, -23.5, 1.3, 10)),
            cut("hole_r", circle(6, -23.5, 1.3, 10)),
            cut("fold", ribbon((-10, -9), (10, -9), 1.2))]


def spark():
    # a Leyden jar: the jar, its foil band cut in, the rod and its knob
    jar = rrect(-8, -17, 8, 0, 2.5, 3)
    return [solid("jar", jar),
            cut("foil", ribbon((-8.5, -9), (8.5, -9), 1.4)),
            solid("rod", ribbon((0, -16.5), (0, -24), 1.6)),
            solid("knob", circle(0, -25, 3.0, 16))]


def cork():
    # a cork, wider at its top, with a corkscrew's T handle driven into it
    body = [(-6.5, -14), (6.5, -14), (5.0, 0), (-5.0, 0)]
    return [solid("cork", body),
            cut("grain", ribbon((-3.5, -7), (3.5, -7), 1.2)),
            solid("screw", ribbon((0, -14), (0, -23), 1.6)),
            solid("handle", rrect(-8, -26.5, 8, -23, 1.5, 2))]


def cicada():
    # a cicada from above: its head with the eyes bulging, the wings folded to two tips
    right = [(0.0, -27.5), (2.4, -27.3), (4.2, -28.0), (6.2, -27.1), (6.6, -25.2), (5.4, -23.9), (7.4, -23.0),
             (9.4, -19.0), (10.0, -12.0), (9.2, -5.0), (7.2, -0.8), (5.4, 0.0), (3.6, -0.8), (1.6, -3.4), (0.0, -4.4)]
    return [solid("cicada", mirror(right)),
            cut("collar", ribbon((-6.0, -23.2), (6.0, -23.2), 1.2)),
            cut("seam", ribbon((0, -22.6), (0, -4.6), 1.2)),
            cut("vein_l", ribbon((-2.6, -19.5), (-6.4, -5.5), 1.0)),
            cut("vein_r", ribbon((2.6, -19.5), (6.4, -5.5), 1.0))]


def ratchet():
    # a watchman's rattle: the toothed wheel on its handle
    teeth = []
    for i in range(10):
        a = 2 * math.pi * i / 10
        for da, r in ((-0.14, 10.0), (0.14, 10.0), (0.2, 7.6), (0.49, 7.6)):
            teeth.append((r * math.cos(a + da), -17.0 + r * math.sin(a + da)))
    return [solid("wheel", teeth),
            cut("hub", circle(0, -17, 2.6, 12)),
            solid("handle", rrect(-1.8, -9, 1.8, 0, 0.9, 2))]


def pod():
    # a dried seed pod on its stalk, its seam cut along it
    return [solid("pod", ellipse(0, -11.5, 6.5, 11.5, 28)),
            cut("seam", polyribbon([(0, -21), (1.2, -11.5), (0, -2)], 1.2)),
            solid("stalk", ribbon((0, -22.5), (2.5, -27.5), 1.6))]


def match():
    # a match: the stick, its head and the flame over it
    return [solid("stick", rrect(-1.4, -15, 1.4, 0, 0.6, 1)),
            solid("head", ellipse(0, -16, 2.6, 3.4, 16)),
            solid("flame", [(0.0, -28.5)] + arc(0, -22.5, 3.4, 3.2, -10, 190, 14))]


def glass():
    # a wine glass: the bowl, stem and foot in one outline, its water line cut in
    right = [(7.5, -27.0), (7.4, -22.0), (6.5, -18.0), (4.0, -15.0), (1.0, -13.5), (0.9, -2.5), (5.5, -1.2), (6.0, 0.0)]
    return [solid("glass", mirror(right)),
            cut("water", ribbon((-7.0, -21.5), (7.0, -21.5), 1.3))]


def bell_jar():
    # a bell in its jar: the bell solid, the jar's dome a fine outline round it
    bell = [(0.0, -19.0), (3.0, -18.0), (4.2, -14.5), (5.0, -8.0), (7.5, -5.0), (-7.5, -5.0), (-5.0, -8.0), (-4.2, -14.5), (-3.0, -18.0)]
    dome = arc(0, -16, 10.5, 11.0, 180, 360, 14) + [(10.5, -1.5), (12.5, -1.5), (12.5, 0.0), (-12.5, 0.0), (-12.5, -1.5), (-10.5, -1.5)]
    knob = circle(0, -28.5, 1.6, 10)
    return [("dome", dome, "none", SOLID, 0.6),
            solid("bell", bell),
            solid("clapper", circle(0, -4.2, 1.3, 10)),
            solid("knob", knob)]


def chalk():
    # chalk on a slate: the slate's frame, its face cut black, a squeal of chalk on it
    return [solid("slate", rrect(-13, -22, 13, 0, 1.2, 2)),
            cut("face", [(-11, -20), (11, -20), (11, -2), (-11, -2)]),
            solid("squeal", polyribbon([(-8, -7), (-4, -15), (0, -7), (4, -15), (8, -7)], 1.4))]


def roar():
    # a bull-roarer: the slat on its cord, the cord's hole and a groove cut in
    return [solid("slat", arc(0, -11, 6.0, 11.5, -90, 270, 30)[:-1]),
            cut("hole", circle(0, -19, 1.3, 10)),
            cut("groove", ribbon((0, -15), (0, -3), 1.2)),
            solid("cord", ribbon((0, -22.5), (3, -29), 0.9))]


MODELS = ["ball", "cask", "stone", "drip", "ice", "sheet", "spark", "cork",
          "cicada", "ratchet", "pod", "match", "glass", "bell jar", "chalk", "roar"]
DRAW = {"ball": ball, "cask": cask, "stone": stone, "drip": drip, "ice": ice, "sheet": sheet, "spark": spark,
        "cork": cork, "cicada": cicada, "ratchet": ratchet, "pod": pod, "match": match, "glass": glass,
        "bell jar": bell_jar, "chalk": chalk, "roar": roar}


def check(points, name):
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
    assert abs(area / 2) >= 0.25, (name, "area")


if __name__ == "__main__":
    for m in MODELS:
        for name, pts, *_ in DRAW[m]():
            check(pts, f"{m} {name}")
    print("all sixteen specimens pass")
