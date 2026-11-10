#!/usr/bin/env python3
"""Writes THUNDER's artwork, panel.svg.fragment, and art.h, the node numbers,
the projection and the geometry display.cpp draws with. Run it after
changing anything here and commit both.

    python3 thunder/tools/make_panel.py

The screen is the thunder sheet in the stock look: an iron sheet hung on two
wires from a wooden gallows (a base, two posts and a beam), with the mallet
hanging at the front. One isometric projection for everything: 30 degree
edges, no vanishing point, the drawing scaled by K to sit centred under the
two-row header (y 38..222). World units: X runs down to the right, Y down to
the left, Z up; the base's back corner is the origin. The sheet hangs in the
plane y = 22.

The grey is the encoder's: the sheet is encoder 1, hollow (a light outline on
black, the one reshaping outline); the felt pads clamped to its lower edge
encoder 2 (4B4B4B boxes); the mallet's felt rings encoder 3 (969696
cylinders); the chalk cross and the chain encoder 4 (CDCDCD). Every 3D face
is outlined in 0.5 px black, the right face one step darker than the top and
left. The base, posts, beam, the mallet's cord, handle and wooden core never
move, so they are plain artwork. Everything a control or a hit moves is a
bound node drawn around its own origin, placed by display.cpp; 3D parts only
slide or are shown and hidden, never turned or scaled (the rings are drawn
where they hang and placed at (0, 0)). Lines are thin closed ribbons; no path
has more than one subpath or 64 points, and no outline turns sharper than
SHARPEST. Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "thunder"
PATHS, POINTS = 1, 64
SHARPEST = 150.0   # degrees: the sharpest turn an outline may make

# ---------------------------------------------------------------- the view
C30 = math.cos(math.radians(30))
K = 0.92                                   # the drawing's scale, so it fits the stage
# The design's isometric origin (214, 136), then centred: translate(240 130) scale(K) translate(-261 -140)
OX, OY = 240 + K * (214 - 261), 130 + K * (136 - 140)


def P(x, y, z=0.0):
    """A world point on screen."""
    return (OX + K * (x - y) * C30, OY + K * ((x + y) / 2 - z))


# ---------------------------------------------------------------- the gallows and the sheet
SHEET_Y = 22.0
BEAM_Z = 86.0                              # the beam's underside: the wires hang from it
WIRE_IN = 8.0                              # the wires, this far in from the sheet's sides
WIRE_A = 6.0                               # the wire's upper piece, down from the beam
WIRE_B = 7.0                               # its lower piece, up from the sheet's top (sag at most 7)
MALLET = (128.0, 34.0)                     # the mallet's cord and handle
CORE_Z, CORE_H, CORE_R = 38.0, 9.0, 4.0
RING_STEP = 2.4                            # each felt ring this much wider than the last
PAD = (8.0, 6.0, 10.0)                     # a felt pad: along the sheet, across it, high
LINK = (2.6, 1.6)                          # a chain link's half widths on screen (before K)
N_MARKS, N_LINKS, N_PADS = 3, 9, 4

# ---------------------------------------------------------------- greys
BLACK = "#000000"
E2, E3, E4 = "#4B4B4B", "#969696", "#CDCDCD"
RAMP = {"#323232": "#262626", "#4B4B4B": "#3A3A3A", "#646464": "#4B4B4B", "#7D7D7D": "#646464",
        "#969696": "#7D7D7D", "#AFAFAF": "#969696", "#CDCDCD": "#AFAFAF"}
EDGE = 0.5                                 # every 3D face's black outline
# The palette: 0 black (parked); 1 the sheet, encoder 4's grey at the design's 1 px (scaled);
# the rest spare.
PALETTE = [(BLACK, 1.0), (E4, 1.0 * K)] + [(BLACK, 1.0)] * 14
SHEET_SHADE = 1


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def check(points):
    """The device's rules for an outline: distinct points 1/8 px apart, a closing edge of at
    least 1/4 px, area, and no turn sharper than SHARPEST degrees (the OP-XY strokes with mitred
    joins and no mitre limit: a turn near straight back draws a long spike)."""
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


def ribbon(a, b, w):
    """A screen line from a to b as a thin closed ribbon w wide, starting mid-edge."""
    (ax, ay), (bx, by) = a, b
    L = math.hypot(bx - ax, by - ay)
    nx, ny = -(by - ay) / L * w / 2, (bx - ax) / L * w / 2
    mx, my = (ax + bx) / 2, (ay + by) / 2
    return [(mx + nx, my + ny), (bx + nx, by + ny), (bx - nx, by - ny), (ax - nx, ay - ny), (ax + nx, ay + ny)]


def line_style(w):
    """A drawn line w px wide as a ribbon and its stroke: the stroke over the ribbon makes w."""
    r = 0.25
    return r, max(0.3, w - r)


def ellipse(cx, cy, rx, ry, n, a0=0.0, a1=2 * math.pi, closed=True):
    m = n if closed else n - 1
    return [(cx + rx * math.cos(a0 + (a1 - a0) * i / m), cy + ry * math.sin(a0 + (a1 - a0) * i / m)) for i in range(n)]


def about(points, origin):
    return [(x - origin[0], y - origin[1]) for x, y in points]


def box_faces(x, y, z, w, d, h, origin=(0.0, 0.0)):
    """A box's left (y + d), right (x + w) and top faces on screen, about a screen origin."""
    def face(*q):
        return about([P(*v) for v in q], origin)
    left = face((x, y + d, z + h), (x + w, y + d, z + h), (x + w, y + d, z), (x, y + d, z))
    right = face((x + w, y, z + h), (x + w, y + d, z + h), (x + w, y + d, z), (x + w, y, z))
    top = face((x, y, z + h), (x + w, y, z + h), (x + w, y + d, z + h), (x, y + d, z + h))
    return left, right, top


def cyl_parts(x, y, z, r, h, n=24):
    """An upright cylinder (the design's: r in plan, h on screen before K): its side and its top."""
    cx, cy = P(x, y, z)
    rx, ry, hh = K * r * 1.2247, K * r * 0.7071, K * h
    side = [(cx - rx, cy - hh)] + ellipse(cx, cy, rx, ry, n, math.pi, 0.0, False) + [(cx + rx, cy - hh)]
    top = ellipse(cx, cy - hh, rx, ry, 2 * n)
    return side, top


# ---------------------------------------------------------------- the static scene
STATIC = {"back": [], "front": [], "core": []}


def static(layer, points, fill="none", stroke="none", w=EDGE):
    STATIC[layer].append((poly(points), fill, stroke, w))


def box(layer, x, y, z, w, d, h, g):
    left, right, top = box_faces(x, y, z, w, d, h)
    static(layer, left, g, BLACK)
    static(layer, right, RAMP[g], BLACK)
    static(layer, top, g, BLACK)


def scene():
    box("back", 0, 0, 0, 150, 40, 6, "#646464")                    # the base
    for x in (6, 136):
        box("back", x, 18, 6, 8, 8, 80, "#7D7D7D")                 # the posts
    box("back", 0, 17, 86, 150, 10, 8, "#7D7D7D")                  # the beam
    mx, my = MALLET
    r, s = line_style(0.6 * K)
    static("front", ribbon(P(mx, my, 86), P(mx, my, 74), r), "none", "#AFAFAF", s)   # the mallet's cord
    box("front", mx - 1.5, my - 1.5, 48, 3, 3, 26, "#7D7D7D")     # its handle
    side, top = cyl_parts(mx, my, CORE_Z, CORE_R, CORE_H)          # its wooden core, over the rings
    static("core", side, RAMP["#646464"], BLACK)
    static("core", top, "#646464", BLACK)


# ---------------------------------------------------------------- bound parts
NODES = []   # (name, points, fill, stroke, width)


def node(name, points, fill="none", stroke_=E4, w=1.0):
    NODES.append((name, points, fill, stroke_, w))


def parts():
    # the wires: an upper piece down from the beam and a lower one up from the sheet's top,
    # overlapping however far the sheet sags; each about its lower end
    r, s = line_style(0.6 * K)
    for side in "lr":
        o = P(0, SHEET_Y, BEAM_Z - WIRE_A)
        node(f"wire_{side}_a", about(ribbon(P(0, SHEET_Y, BEAM_Z), o, r), o), "none", "#AFAFAF", s)
        o = P(0, SHEET_Y, 0)
        node(f"wire_{side}_b", about(ribbon(P(0, SHEET_Y, WIRE_B), o, r), o), "none", "#AFAFAF", s)
    # the hammered marks, hairlines on the sheet, about their left end
    r, s = line_style(0.7 * K)
    o = P(0, SHEET_Y, 0)
    for k in range(N_MARKS):
        node(f"mark_{k}", about(ribbon(P(-4, SHEET_Y, 0), P(4, SHEET_Y, -1), r), o), "none", "#4A4A4A", s)
    # SPOT's chalk cross on the sheet, two strokes about its middle
    r, s = line_style(1.2 * K)
    node("cross_a", about(ribbon(P(-3, SHEET_Y, -3), P(3, SHEET_Y, 3), r), o), "none", E4, s)
    node("cross_b", about(ribbon(P(-3, SHEET_Y, 3), P(3, SHEET_Y, -3), r), o), "none", E4, s)
    # CHAIN's links lying on the sheet, flat ovals on screen
    for k in range(N_LINKS):
        node(f"link_{k}", ellipse(0, 0, K * LINK[0], K * LINK[1], 20), "none", E4, 0.9 * K)
    # DAMP's felt pads, boxes about their back lower corner
    for k in range(N_PADS):
        left, right, top = box_faces(0, 0, 0, *PAD, P(0, 0, 0))
        node(f"pad_{k}_left", left, E2, BLACK, EDGE)
        node(f"pad_{k}_right", right, RAMP[E2], BLACK, EDGE)
        node(f"pad_{k}_top", top, E2, BLACK, EDGE)
    # BEATER's felt rings round the mallet's core, largest first; drawn where they hang
    for k in (3, 2, 1):
        side, top = cyl_parts(*MALLET, CORE_Z, CORE_R + k * RING_STEP, CORE_H)
        node(f"ring_{k}_side", side, RAMP[E3], BLACK, EDGE)
        node(f"ring_{k}_top", top, E3, BLACK, EDGE)


# the drawing order, back to front
ORDER = ["back", "wire_*", "path:0", "mark_*", "cross_*", "link_*", "pad_*", "front", "ring_*", "core"]


def q10(v):
    return int(round(v * 1024))


def build():
    scene()
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
    index = {name: PATHS + n_header + k for k, (name, *_rest) in enumerate(NODES)}
    by_name = {n[0]: n for n in NODES}
    emitted = set()

    def emit(name):
        _n, points, fill, stroke_, w = by_name[name]
        emitted.add(name)
        lines.append(f'<path id="{ID}_node_{index[name]}" transform="matrix(1 0 0 1 640 320)" d="{poly(points)}" '
                     f'fill="{fill}" stroke="{stroke_}" stroke-width="{fmt(w)}"/>')
    for item in ORDER:
        if item in STATIC:
            for d, fill, stroke_, w in STATIC[item]:
                lines.append(f'<path d="{d}" fill="{fill}" stroke="{stroke_}" stroke-width="{fmt(w)}"/>')
        elif item.startswith("path:"):
            p = int(item[5:])
            # the reshaping outline, unfilled, authored as distinct points round an ellipse
            pts = [(30 * math.cos(2 * math.pi * i / POINTS), 20 * math.sin(2 * math.pi * i / POINTS)) for i in range(POINTS)]
            col, w = PALETTE[SHEET_SHADE]
            lines.append(f'<path id="{ID}_node_{p}" d="{poly(pts)}" fill="none" stroke="{col}" stroke-width="{fmt(w)}"/>')
        elif item.endswith("*"):
            for name, *_r in NODES:
                if name.startswith(item[:-1]):
                    emit(name)
        else:
            emit(item)
    assert emitted == set(by_name), set(by_name) - emitted
    for i, (col, w) in enumerate(PALETTE):
        lines.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{col}" stroke="{col}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    assert len(text) < 64 * 1024, len(text)
    (ROOT / "panel.svg.fragment").write_text(text)

    names = sorted(index.items(), key=lambda kv: kv[1])
    head = ["#pragma once",
            "// Generated by tools/make_panel.py: the artwork's node numbers, the projection and the sheet's geometry.",
            "#include <opxy/types.hpp>", "", "namespace thunder {", "namespace art {",
            "using opxy::i32;",
            f"const opxy::u32 header_node = {first_header};",
            f"const opxy::u32 node_count = {bound};",
            "// screen (Q10 px) = (ox + (x - y) kc, oy + (x + y) kh - z kz) for world x y z in Q10",
            f"const i32 ox = {q10(OX)}, oy = {q10(OY)};",
            f"const i32 kc = {q10(K * C30)}, kh = {q10(K / 2)}, kz = {q10(K)};",
            "// the sheet's plane, the beam's underside and the wires, Q10 world units",
            f"const i32 sheet_y = {q10(SHEET_Y)}, beam_z = {q10(BEAM_Z)};",
            f"const i32 wire_in = {q10(WIRE_IN)}, wire_a = {q10(WIRE_A)};",
            "// the parts' counts, and the sheet's palette entry",
            f"const opxy::u32 marks = {N_MARKS}, links = {N_LINKS}, pads = {N_PADS};",
            f"const opxy::u32 sheet_shade = {SHEET_SHADE};",
            "enum Node : opxy::u32 {"]
    head += [f"    {name} = {idx}," for name, idx in names]
    head += ["};", "}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(head))
    n_static = sum(len(v) for v in STATIC.values())
    print(f"wrote panel.svg.fragment ({len(text)} bytes) and art.h: {bound} nodes, {n_static} static paths")


if __name__ == "__main__":
    build()
