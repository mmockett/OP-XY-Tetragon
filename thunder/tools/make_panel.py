#!/usr/bin/env python3
"""Writes THUNDER's artwork, panel.svg.fragment, and art.h, the node numbers
display.cpp places. Run it after changing anything here and commit both.

    python3 thunder/tools/make_panel.py

The screen is a theatre thunder sheet hung in its gallows frame, drawn in
fine outline. The frame, the batten and its chains never move, so they are
plain artwork. Everything a control or a hit moves is a bound node, drawn
here around its own pivot (0, 0); display.cpp places it. Parts that must meet
share a pivot and a transform. Lines are thin closed ribbons; no path has
more than one subpath or 64 points. Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "thunder"
PATHS, POINTS = 2, 64

# greys: the object's line, its secondary parts, its fine detail
INK, MID, DIM, FAINT = "#E6E6E6", "#9C9C9C", "#666666", "#3C3C3C"
RIG = "#8C8C8C"    # the frame's one grey, bright enough to hold at 1 px
WOOD = "#181818"   # the oak's dark body, static: GALILEO's wood, so the kits match
IRON = "#262626"   # the frame's iron fittings, filled as GALILEO's metal

# The palette: 0 black (parked); 1..8 a ramp of greys at the parts' line
# weight, 1 px; 9 the outline's grey at half weight (it is drawn at scale 2,
# which doubles a stroke: 1 px drawn); 10..12 dimmer greys at that weight,
# spare; 13 a heavier ink; 14, 15 spare.
# Lines are 1 px wide as drawn, ribbons about 1.5 px across: a part placed
# well over unit scale is authored larger and placed smaller (LINK_K).
LINE = 1.0
LINK_K = 1.3   # display.cpp: link_k
BEAD_K = 1.15  # display.cpp: bead_k (a small bead still draws 0.7 px)
RAMP = ["#2E2E2E", "#444444", "#5C5C5C", "#767676", "#909090", "#ACACAC", "#C8C8C8", "#E6E6E6"]
PALETTE = [("#000000", 1.0)] + [(g, LINE) for g in RAMP] + [
    ("#E6E6E6", LINE / 2), ("#ACACAC", LINE / 2), ("#767676", LINE / 2), ("#4A4A4A", LINE / 2), ("#E6E6E6", 1.6), ("#FFFFFF", 1.0),
    ("#000000", 1.0)]


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def poly(points):
    out = []
    for p in points:
        q = (round(p[0], 2), round(p[1], 2))
        if not out or q != out[-1]:
            out.append(q)
    if out[0] == out[-1]:
        out.pop()
    assert 3 <= len(out) <= 64, len(out)
    assert len(set(out)) == len(out), "repeated point"
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in out) + " Z"


def ribbon(points, w=0.5):
    """A closed ribbon along a polyline: how a line is drawn."""
    n, left, right = len(points), [], []
    for i in range(n):
        a, b = points[max(0, i - 1)], points[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy) or 1
        left.append((points[i][0] - dy / length * w / 2, points[i][1] + dx / length * w / 2))
        right.append((points[i][0] + dy / length * w / 2, points[i][1] - dx / length * w / 2))
    # start mid-way along one side, so the closing edge is a long one
    loop = left + right[::-1]
    k = max(1, n // 2)
    return loop[k:] + loop[:k]


def line(x0, y0, x1, y1, w=0.5, steps=1):
    return ribbon([(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps) for i in range(steps + 1)], w)


def rect(x0, y0, x1, y1, r=0.0, steps=3):
    if r <= 0:
        return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def ellipse(cx, cy, rx, ry, n=20, rot=0.0):
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    out = []
    for i in range(n):
        a = 2 * math.pi * i / n
        x, y = rx * math.cos(a), ry * math.sin(a)
        out.append((cx + x * c - y * s, cy + x * s + y * c))
    return out


def arc(cx, cy, r, a0, a1, n=12, w=0.5):
    return ribbon([(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
                   for i in range(n + 1)], w)


def rotate(points, deg, ox=0.0, oy=0.0):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(ox + (x - ox) * c - (y - oy) * s, oy + (x - ox) * s + (y - oy) * c) for x, y in points]


def shift(points, dx, dy):
    return [(x + dx, y + dy) for x, y in points]


# ---------------------------------------------------------------- the scene
# Static artwork: (d, stroke, width, fill)
STATIC = []


def static(points, col=MID, w=1.15, fill="none"):
    STATIC.append((poly(points), col, w, fill))


def wavy(x0, y0, x1, y1, amp, waves, n=24, w=0.4):
    """A wood-grain line: a long ribbon with a slight wave."""
    n = min(n, 31)
    pts = []
    for i in range(n + 1):
        t = i / n
        x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
        d = amp * math.sin(2 * math.pi * waves * t + 0.7)
        # across the line's direction
        L = math.hypot(x1 - x0, y1 - y0)
        pts.append((x - (y1 - y0) / L * d, y + (x1 - x0) / L * d))
    return ribbon(pts, w)


def frame():
    # A quiet gallows: a slim oak beam across the top and two posts at the
    # screen's edges, cropped by it; the batten the sheet is clipped to, hung
    # from the beam by two iron straps; eye bolts on the posts for the stays
    # and a cleat on the left post for the chain. One grey, one weight.
    static([(0, 41), (480, 41), (480, 47), (0, 47)], RIG, LINE, WOOD)
    for x0 in (2, 470):
        static([(x0, 47), (x0 + 8, 47), (x0 + 8, 222), (x0, 222)], RIG, LINE, WOOD)
    static(rect(70, 51, 410, 58, 1.2, 2), RIG, LINE, WOOD)
    for x in (130, 350):
        static(rect(x - 2, 46.5, x + 2, 51.5, 0.5, 1), RIG, LINE, IRON)
    for x in (10 + 4.5, 470 - 4.5):
        static(ellipse(x, 200, 3.2, 3.2, 12), RIG, LINE, IRON)
    static([(10, 94), (16, 92.5), (18, 94), (14.5, 96), (14.5, 98), (18, 100), (16, 101.5), (10, 100)], RIG, LINE, IRON)


# ---------------------------------------------------------------- bound parts
NODES = []   # (name, kind, data, fill)


def node(name, points=None, circle=None, fill="none"):
    NODES.append((name, points, circle, fill))


def scaled(points, k):
    return [(x * k, y * k) for x, y in points]


def parts():
    # ---------------- the sheet's own detail (placed from its size; still while it rings)
    # C-clips holding its top hem to the batten, each with its bolt
    for side in ("l", "r", "m"):
        node(f"clamp_{side}", [(-6, -7), (6, -7), (6, 6), (3, 6), (3, -3), (-3, -3), (-3, 6), (-6, 6)])
        node(f"clamp_bolt_{side}", circle=(0, 1.5, 1.7))
    # rivets along its hem and one down each side: few, and rings large
    # enough to read on the device (5 px across)
    for k in range(7):
        node(f"rivet_{k}", circle=(0, 0, 2.6))
    # ---------------- the damper: a felt pad on a lever pivoted on a bracket
    # hung from the batten; a counterweight on the lever's short end. Pivot
    # at the bracket's pin; the lever points along +x and turns down.
    node("damper_bracket", [(-5, -9), (5, -9), (5, 2), (3, 4), (-3, 4), (-5, 2)])
    node("damper_arm", [(-5, -1.8), (0, -2.2), (32, -1.2), (32, 1.2), (0, 2.2), (-5, 1.8)])
    node("damper_pivot", circle=(0, 0, 2.6))
    # the pad, centred on the lever's end: a backing plate and the felt
    node("damper_plate", rect(-7.5, -12, 7.5, -9.5, 0.6, 1))
    node("damper_pad", rect(-7, -9.5, 7, 11, 3.5, 2))
    node("damper_felt_a", line(-4, -4.5, 4, -3.8, 0.4))
    node("damper_felt_b", line(-4, 0.5, 4, 1.2, 0.4))
    node("damper_felt_c", line(-4, 5.5, 4, 6.2, 0.4))
    # ---------------- the stays: wire rope from a grommet at each lower corner
    # out to an eye bolt on its post, each through a turnbuckle (an open
    # frame the wire's screws run into). Along +x, 100 long: display.cpp
    # stretches it to its length, 77 to 123 px, so its line stays near 1 px.
    for side in ("l", "r"):
        node(f"stay_{side}", line(0, 0, 100, 0, 0.5, 1))
        node(f"grommet_{side}", circle=(0, 0, 2.8))
        node(f"buckle_{side}", [(-10, -1.4), (-6.5, -3.4), (6.5, -3.4), (10, -1.4), (10, 1.4), (6.5, 3.4), (-6.5, 3.4), (-10, 1.4)])
    # ---------------- the rigging sandbag: canvas, a seam, a tied neck, on a
    # rope from a snap hook under the sheet (pivot: the hook's eye)
    node("bag_hook", [(-1.4, 0), (1.4, 0), (2.2, 2), (1.2, 4), (-1.2, 4), (-2.2, 2)])
    node("bag_rope", ribbon([(0, 4), (0.4, 5.5), (0, 7)], 0.9))
    node("bag_neck", rect(-3, 7, 3, 9.5, 0.8, 1))
    node("bag", [(-2.5, 9.5), (2.5, 9.5), (6.5, 12), (9.5, 15.5), (10.5, 19.5), (9.5, 23.5), (6, 25.8), (0, 26.5), (-6, 25.8),
                 (-9.5, 23.5), (-10.5, 19.5), (-9.5, 15.5), (-6.5, 12)])
    node("bag_seam", line(0, 12.5, 0.5, 25, 0.5, 4))
    # ---------------- the chain: twelve links, alternately flat and edge-on, along +x,
    # authored LINK_K larger than they were and placed that much smaller, so
    # their lines draw 1 px where the links are drawn large
    for k in range(12):
        if k % 2 == 0:
            node(f"link_{k}", scaled(ellipse(0, 0, 4.8, 2.8, 16), LINK_K))
        else:
            node(f"link_{k}", scaled(line(-4.4, 0, 4.4, 0, 2.6), LINK_K))

    # ================ the implements (display.cpp parks those a model doesn't use)
    # ---------------- the mallet (BOOM, SLAP, TOM, CRASH, CLANK): pivot at the
    # head's centre, the handle down and right; a tapered cane, a turned
    # collar, a cord-wrapped grip
    K = 1.0
    d = (math.cos(math.radians(47)), math.sin(math.radians(47)))
    n_ = (-d[1], d[0])
    def along(t, w):
        return (K * (d[0] * t + n_[0] * w), K * (d[1] * t + n_[1] * w))
    node("mallet_handle", [along(13, -1.3), along(40, -1.6), along(56, -1.9), along(56, 1.9), along(40, 1.6), along(13, 1.3)])
    node("mallet_grip", [along(56, -2.7), along(66, -3.0), along(76, -2.8), along(78, 0), along(76, 2.8), along(66, 3.0), along(56, 2.7)])
    node("mallet_collar", [along(9, -2.8), along(13, -2.4), along(13, 2.4), along(9, 2.8)])
    node("mallet_cord_a", [along(61, -3.1), along(63.5, -3.1), along(63.5, 3.1), along(61, 3.1)])
    node("mallet_cord_b", [along(69, -3.1), along(71.5, -3.1), along(71.5, 3.1), along(69, 3.1)])
    node("mallet_felt", ellipse(0, 0, 11 * K, 10.4 * K, 28, -43))
    node("mallet_wrap_a", arc(0, 0, 7.5 * K, 150, 300, 10, 0.4))
    node("mallet_wrap_b", arc(0, 0, 7.5 * K, -30, 120, 10, 0.4))
    node("mallet_wood", rotate(rect(-6.5 * K, -9.5 * K, 6.5 * K, 9.5 * K, 3, 2), -43))
    node("mallet_rod", rotate(rect(-2.8 * K, -7 * K, 2.8 * K, 8 * K, 1.2, 1), -43))
    node("mallet_ferrule", rotate(rect(-3.8 * K, 9 * K, 3.8 * K, 12.5 * K, 0.6, 1), -43))
    # TOM's roll: each strike it bounces, a short stroke fanned round the spot
    for k in range(7):
        a0 = 195 + 22 * k
        r0, r1 = 15, 23
        node(f"roll_{k}", line(r0 * math.cos(math.radians(a0)), r0 * math.sin(math.radians(a0)),
                               r1 * math.cos(math.radians(a0)), r1 * math.sin(math.radians(a0)), 1.3, 1))
    # CRASH's swell, the bloom spreading from the strike; and the sheet's own
    # ring (METAL, SHEET) spreading over its face
    for k, r in enumerate((15, 22, 29)):
        node(f"swell_{k}", ellipse(0, 0, r, r * 0.62, 36))

    # ---------------- CLANK: the frame's iron shackle hung from the beam by
    # its pin; the bar's arms bend up into a stirrup (pivots at the bar's ends)
    H = 1.35
    node("shackle_bar", scaled(rect(-10, -2.8, 10, 2.8, 2.4, 2), H))
    node("shackle_arm_l", scaled(rect(-14, -2.8, 0, 2.8, 2.4, 2), H))
    node("shackle_arm_r", scaled(rect(0, -2.8, 14, 2.8, 2.4, 2), H))
    node("shackle_pin", scaled(rect(-2.2, -20, 2.2, -2.8, 1.1, 1), H))
    node("shackle_eye", circle=(0, -23 * H, 3.2 * H))
    node("shackle_nut", scaled(rotate(ellipse(0, 0, 4.6, 4.6, 6), 30), H))
    node("shackle_strap", scaled(rect(-1.6, -32, 1.6, -26, 0.6, 1), H))

    # ---------------- FLICK: a strip of spring steel clamped in a turned
    # wooden grip; its tip on the sheet (pivot), the grip up and right
    Fk = 1.4
    node("flick_grip", scaled(rotate([(30, -3), (40, -4.5), (58, -4), (64, -2.5), (64, 2.5), (58, 4), (40, 4.5), (30, 3)], -35), Fk))
    node("flick_ferrule", scaled(rotate(rect(25, -3.4, 30, 3.4, 0.6, 1), -35), Fk))
    node("flick_strip", scaled(ribbon([(0, 0), (8, -4), (16, -9), (23, -14), (27, -17)], 1.4), Fk))
    node("flick_pad", scaled(ellipse(0, 0, 5.5, 5.5, 16), Fk))
    node("flick_coin", scaled(ellipse(0, 0, 7.5, 2.2, 18, -32), Fk))
    node("flick_scuff", ribbon([(0, 0), (-12, 2), (-24, 3.4), (-36, 4), (-46, 3.4)], 1.0))

    # ---------------- POP: a G-clamp on the sheet's lower edge, its screw
    # pressing a dent; a T-bar with knobs, threads on the screw
    G = 1.25
    node("pop_frame", scaled(ribbon([(0, 3), (0, 7), (4, 10), (18, 10), (22, 7), (22, -30), (18, -34), (-3, -34)], 3.6), G))
    node("pop_screw", scaled(line(0, -50, 0, -27, 2.6, 1), G))
    node("pop_thread_a", scaled(line(-2.4, -40, 2.4, -41.6, 0.5, 1), G))
    node("pop_thread_b", scaled(line(-2.4, -45, 2.4, -46.6, 0.5, 1), G))
    node("pop_handle", scaled(line(-11, 0, 11, 0, 2.2, 1), G))
    node("pop_knob_l", circle=(-11 * G, 0, 2.6))
    node("pop_knob_r", circle=(11 * G, 0, 2.6))
    node("pop_pad", scaled(rect(-5.5, -27, 5.5, -23, 1, 1), G))
    for k in range(7):
        node(f"dent_{k}", ellipse(0, 0, 5.4, 3.4, 16))

    # ---------------- GRIT: a grocer's scoop pouring beads (pivot: the handle's end)
    node("scoop_handle", [(0, -2.2), (-6, -2.8), (-18, -2.6), (-24, -1.8), (-24, 1.8), (-18, 2.6), (-6, 2.8), (0, 2.2)])
    node("scoop_ring", circle=(-4, 0, 1.6))
    node("scoop_bowl", [(-26, -5), (-32, -9), (-50, -10), (-62, -8), (-66, -3), (-66, 4), (-61, 10), (-48, 12), (-33, 11), (-26, 6)])
    node("scoop_lip", ribbon([(-64, -6), (-67, 0), (-64, 7)], 0.6))
    for k in range(10):
        node(f"bead_{k}", circle=(0, 0, 1.9 * BEAD_K))

    # ---------------- HOP: a dropped bearing and the arcs it bounces in (pivot:
    # each arc's landing, the arc rising to its left)
    node("ball", circle=(0, 0, 8.5))
    node("ball_band", arc(0, 0, 5.4, 200, 340, 10, 0.6))
    node("ball_shine", arc(0, 0, 5.0, 205, 255, 5, 0.7))
    for k in range(3):
        node(f"arc_{k}", ribbon([(-40 * (1 - i / 12), -36 * 4 * (i / 12) * (1 - i / 12)) for i in range(13)], 0.6))

    # ---------------- ZAP: a tapping stylus with a wooden handle, and a contact
    # pickup clipped to the far corner, its cable coiling away
    node("stylus", ribbon([(0, -3), (10, -15), (17, -24)], 1.6))
    node("stylus_handle", [(15, -21), (19, -25), (33, -42), (35, -46), (32, -48), (29, -45), (16, -28), (13, -24)])
    node("stylus_tip", circle=(0, 0, 3.0))
    node("pickup", circle=(0, 0, 10))
    node("pickup_core", circle=(0, 0, 4.6))
    node("pickup_clip", [(-4, -10), (4, -10), (5, -16), (-5, -16)])
    node("pickup_cable", ribbon([(9, -4), (14, -8), (18, -6), (22, -11), (26, -9), (30, -14), (36, -18), (44, -22)], 0.6))
    for k in range(5):
        node(f"wave_{k}", arc(0, 0, 17 + 8 * k, 180, 270, 10, 0.5))

    # ---------------- BOW: a cello bow along the sheet's lower edge; pivot
    # where its hair meets the edge, the frog to the left
    B = 1.0
    node("bow_hair", scaled(line(-72, 0, 72, 0, 0.7, 2), B))
    node("bow_stick", scaled(ribbon([(-74, 6), (-40, 8.5), (0, 9.6), (40, 8.4), (70, 4.6), (75, 1.2)], 1.8), B))
    node("bow_frog", scaled([(-76, 0.6), (-64, 0.6), (-62, 6), (-63, 9), (-77, 9), (-78, 6)], B))
    node("bow_eye", circle=(-70 * B, 4.6 * B, 2.0))
    node("bow_head", scaled([(70, 4.6), (75, -0.8), (79, 0.4), (78, 6), (74, 7.5)], B))
    node("bow_button", scaled(rect(-84, 5, -78, 9, 0.8, 1), B))

    # ---------------- SHAKE: the shaking handle, a turned grip bolted through a
    # plate at the sheet's lower left corner (pivot); a crease where it cracks
    node("handle_grip", scaled([(-3, -2.5), (-10, 0.5), (-12, -0.5), (-15, 1.5), (-26, 7), (-31, 12), (-32, 16), (-29, 17),
                                (-24, 14), (-13, 7), (-11, 7.5), (-9, 5.5), (-2, 4)], 1.5))
    node("handle_plate", rect(-4, -5, 6, 5, 1.2, 1))
    node("handle_bolt", circle=(0, 0, 2.2))
    node("crease", ribbon([(0, 0), (7, -6), (11, -14), (18, -18), (23, -26), (30, -30)], 0.6))


def build():
    frame()
    parts()
    n_header = 8
    bound = len(NODES) + PATHS + n_header
    lines = [f'<g id="{ID}_panel">', "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
             '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    first_header = PATHS
    greys = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i in range(8):
        lines.append(f'<path id="{ID}_node_{first_header + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
                     f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{greys[i]}" stroke="{greys[i]}" stroke-width="0.5"/>')
    for i in range(16):
        x = [5, 125, 245, 365, 65, 185, 305, 425, 5, 125, 245, 365, 65, 185, 305, 425][i]
        y = [10, 10, 10, 10, 17.4, 17.4, 17.4, 17.4, 33, 33, 33, 33, 33, 33, 33, 33][i]
        col = (["#CBCBCB"] * 2 + ["#000000"] * 2) * 2 + ["#5A5A5A"] * 4 + ["#707070"] * 4
        size = [10] * 4 + [20] * 4 + [10] * 4 + [13] * 4
        lines.append(f'<text id="{ID}_text_{i}" transform="matrix(1 0 0 1 {x} {y})" fill="{col[i]}" '
                     f'font-family="UniversNextPro-Light" font-size="{size[i]}px">-</text>')
    for d, col, w, fill in STATIC:
        lines.append(f'<path d="{d}" fill="{fill}" stroke="{col}" stroke-width="{w}"/>')
    # the reshaping outlines, unfilled, authored as distinct points round a circle
    for p in range(PATHS):
        pts = [(30 * math.cos(2 * math.pi * i / POINTS), 20 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
        lines.append(f'<path id="{ID}_node_{p}" d="{poly(pts)}" fill="none" stroke="{INK}" stroke-width="{fmt(LINE / 2)}"/>')
    names = []
    for k, (name, points, circle, fill) in enumerate(NODES):
        idx = PATHS + n_header + k
        names.append((name, idx))
        if circle:
            cx, cy, r = circle
            lines.append(f'<circle id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" cx="{fmt(cx)}" cy="{fmt(cy)}" '
                         f'r="{fmt(r)}" fill="{fill}" stroke="{MID}" stroke-width="{fmt(LINE)}"/>')
        else:
            lines.append(f'<path id="{ID}_node_{idx}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                         f'fill="{fill}" stroke="{MID}" stroke-width="{fmt(LINE)}"/>')
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{w}"/>')
    lines.append("</g>")
    (ROOT / "panel.svg.fragment").write_text("\n".join(lines) + "\n")
    head = ["#pragma once", "// Generated by tools/make_panel.py: the artwork's node numbers.", "#include <opxy/types.hpp>", "",
            "namespace thunder {", "namespace art {",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};", "enum Node : opxy::u32 {"]
    head += [f"    {name} = {idx}," for name, idx in names]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    print(f"wrote panel.svg.fragment and art.h: {bound} nodes, {len(STATIC)} static paths")


if __name__ == "__main__":
    build()
