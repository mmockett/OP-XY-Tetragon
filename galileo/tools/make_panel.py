#!/usr/bin/env python3
"""Writes GALILEO's screen artwork: panel.svg.fragment and art.h.

    python3 galileo/tools/make_panel.py

The screen is Galileo's inclined plane on a bench, in the stock isometric
look: a grooved board on trestles, its head on a stack of risers, bells hung
over the groove, the handful of balls waiting at the head, and a snare at the
foot with the bounce from the board onto its head dotted between them.
Isometric (30 degree edges, no vanishing point), every face of every 3D part
outlined in 0.5 px black, top and left faces the base grey and the right face
one step darker.

The grey is the encoder's. Page 1 on the drum: its head 323232 (TUNE, the
drum one of SIZES copies, smaller as it rises), its wires 4B4B4B (one to six),
its ring 969696 (DECAY, the one reshaping outline) and a tone tab CDCDCD
sliding up its shell. Page 2 on the plane: the risers 323232 (TILT, one to
four), the bells 4B4B4B (FRETS, none to nine at Galileo's spacing), the
bounce's dots 969696 (BOUNCE) and the balls CDCDCD (BALLS, one to six).

World units are screen pixels: X runs down to the right, Y down to the left,
Z up. Static parts are plain artwork without ids. Every other part is a node,
one closed outline (at most 64 points, one subpath): either drawn where it
stands (the boards, the risers, the drums and their wires: shown or parked)
or drawn around its own anchor, which display.cpp places (the bells, the
balls, the tab and the dots: they only slide). Lines are thin closed ribbons.
Node 0 is the ring, the one reshaping outline (unfilled, 64 points, rebuilt
by display.cpp), drawn over the head. Draw order is stacking order: back to
front. Pure Python.
"""
import math
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
ID = "galileo"
SHARPEST = 150.0   # degrees: the sharpest turn an outline may make

# ---------------------------------------------------------------- the view
C30, S30 = math.cos(math.radians(30)), 0.5
# The design's origin (170, 96), fitted to the stage under the two-row header:
# its extent over every setting centred on x 240, y 130 (y 38..222).
OX, OY = 170.0 - 5.5, 96.0 - 1.0


def P(x, y, z=0.0):
    return (OX + (x - y) * C30, OY + (x + y) * S30 - z)


# ---------------------------------------------------------------- greys
K = "#000000"
E1, E2, E3, E4 = "#323232", "#4B4B4B", "#969696", "#CDCDCD"   # encoders 1..4
RAMP = {"#323232": "#262626", "#4B4B4B": "#3A3A3A", "#646464": "#4B4B4B", "#7D7D7D": "#646464",
        "#969696": "#7D7D7D", "#AFAFAF": "#969696", "#CDCDCD": "#AFAFAF"}
GLINT = "#E6E6E6"

# ---------------------------------------------------------------- geometry (plan px)
RISER_Z0, RISER_H = 5.0, 7.0           # the risers: 14 x 18 x 7 boxes from the bench's top
BOARD_T, BOARD_W = 4.0, 18.0           # the board's thickness and width
FOOT_Z = 13.0                          # the board's underside at its foot, on the trestle
GROOVE_Y = 17.0                        # the groove's line across the board
HEAD_X, LIP_X = 4.0, 130.0             # the board's head and foot (the lip)
BELL_X0, BELL_X1, BELL_SPAN = 14.0, 116.0, 0.88   # bells at x0 + (x1 - x0) span (k / n)^2
BELL_R, BELL_H, BELL_DROP, HANGER = 3.0, 4.0, 16.0, 10.0
BELLS = 9
BALL_R = 2.6 * 1.15                    # the balls' radius on screen
BALL_X0, BALL_STEP, BALLS = 12.0, 6.0, 6
DRUM_X, DRUM_Y = 166.0, 17.0           # the snare's centre
SHELL_Z, SHELL_H, RIM_H = 5.0, 14.0, 2.0
HEAD_Z = SHELL_Z + SHELL_H + RIM_H     # 21: the head
SIZES = 7                              # the drum's copies, TUNE 0 .. 127: radius 26 .. 18
R_BIG, R_SMALL = 26.0, 18.0
WIRES = 6
TAB_W, TAB_D, TAB_H = 3.0, 4.0, 6.0    # the tone tab, slid up the shell: its foot from z 5 to 11
TAB_Z0, TAB_Z1 = 5.0, 11.0
DOTS = 8
ARC_X0, ARC_X1 = 132.0, DRUM_X - 4.0   # the bounce: from the board's foot onto the head
ARC_LO, ARC_HI = 6.0, 30.0             # its height, BOUNCE 0 .. 127
LAND_X = ARC_X1


def radius(size):
    return R_BIG + (R_SMALL - R_BIG) * size / (SIZES - 1)


def zh_of(risers):
    return RISER_Z0 + risers * RISER_H


def fmt(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def check(points):
    """The device's rules for an outline: distinct points 1/8 px apart, a closing edge of at
    least 1/4 px, area, and no turn sharper than SHARPEST degrees (the OP-XY strokes with
    mitred joins and no mitre limit: a turn near straight back draws a long spike)."""
    n = len(points)
    assert 3 <= n <= 64, n
    area = 0.0
    for i in range(n):
        (ax, ay), (bx, by), (cx, cy) = points[i - 1], points[i], points[(i + 1) % n]
        ux, uy, vx, vy = bx - ax, by - ay, cx - bx, cy - by
        assert abs(vx) >= 0.125 or abs(vy) >= 0.125, f"points {i} and {(i + 1) % n} merge"
        turn = math.degrees(math.atan2(abs(ux * vy - uy * vx), ux * vx + uy * vy))
        assert turn <= SHARPEST, f"turn of {turn:.1f} degrees at point {i}"
        area += ax * by - bx * ay
    (ax, ay), (bx, by) = points[-1], points[0]
    assert math.hypot(bx - ax, by - ay) >= 0.25, "closing edge"
    assert abs(area / 2) >= 0.1, f"area {area / 2:.3f}"


def d_of(points, origin=(0.0, 0.0)):
    out = []
    for x, y in points:
        q = (round(x - origin[0], 2), round(y - origin[1], 2))
        if not out or q != out[-1]:
            out.append(q)
    if out[0] == out[-1]:
        out.pop()
    assert len(set(out)) == len(out), "repeated point"
    check(out)
    return "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in out) + " Z"


# ---------------------------------------------------------------- primitives (screen polygons)
def box(x, y, z, w, d, h):
    """An isometric box's left (y = y + d), right (x = x + w) and top faces."""
    t = [P(x, y, z + h), P(x + w, y, z + h), P(x + w, y + d, z + h), P(x, y + d, z + h)]
    l = [P(x, y + d, z + h), P(x + w, y + d, z + h), P(x + w, y + d, z), P(x, y + d, z)]
    r = [P(x + w, y, z + h), P(x + w, y + d, z + h), P(x + w, y + d, z), P(x + w, y, z)]
    return l, r, t


def ellipse(x, y, z, r, n):
    cx, cy = P(x, y, z)
    rx, ry = r * 1.2247, r * 0.7071
    return [(cx + rx * math.cos(2 * math.pi * i / n), cy + ry * math.sin(2 * math.pi * i / n)) for i in range(n)]


def cyl_side(x, y, z, r, h, n=30):
    """An upright cylinder's side: the left edge down, the front half of its foot, the right edge up."""
    cx, cy = P(x, y, z)
    rx, ry = r * 1.2247, r * 0.7071
    arc = [(cx + rx * math.cos(math.pi - math.pi * i / n), cy + ry * math.sin(math.pi - math.pi * i / n)) for i in range(n + 1)]
    return [(cx - rx, cy - h)] + arc + [(cx + rx, cy - h)]


def circle(cx, cy, r, n):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def ribbon(a, b, w):
    """A drawn line from a to b (screen) as a thin closed ribbon w wide, starting mid-edge."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    nx, ny = -dy / L * w / 2, dx / L * w / 2
    m = ((a[0] + b[0]) / 2 + nx, (a[1] + b[1]) / 2 + ny)
    return [m, (b[0] + nx, b[1] + ny), (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny), (a[0] + nx, a[1] + ny)]


RIBBON = 0.2


def line_paint(colour, w):
    """A line of width w as a ribbon RIBBON wide stroked to w."""
    return "none", colour, max(0.3, w - RIBBON)


def slab(zh):
    """The board from the risers (its underside at zh) down to the trestle: left side, end, top."""
    a, b, w, t = (HEAD_X, 8.0), (LIP_X, 8.0), BOARD_W, BOARD_T
    (ax, ay), (bx, by) = a, b
    z0, z1 = zh, FOOT_Z
    top = [P(ax, ay, z0 + t), P(bx, by, z1 + t), P(bx, by + w, z1 + t), P(ax, ay + w, z0 + t)]
    left = [P(ax, ay + w, z0 + t), P(bx, by + w, z1 + t), P(bx, by + w, z1), P(ax, ay + w, z0)]
    end = [P(bx, by, z1 + t), P(bx, by + w, z1 + t), P(bx, by + w, z1), P(bx, by, z1)]
    return left, end, top


def zat(x, zh):
    """The groove's surface at x on a board whose head rests at zh."""
    return zh + BOARD_T + (FOOT_Z + BOARD_T - zh - BOARD_T) * (x - HEAD_X) / (LIP_X - HEAD_X)


# ---------------------------------------------------------------- the artwork
ITEMS = []    # ("static", d, fill, stroke, w) or ("node", name, d, fill, stroke, w, home)


def static(points, fill, stroke=K, w=0.5):
    ITEMS.append(("static", d_of(points), fill, stroke, w))


def node(name, points, fill, stroke=K, w=0.5, home=None, anchor=None):
    """A node drawn where it stands (home: its origin, the points' own place) or around an
    anchor display.cpp places (parked until then)."""
    origin = home if home is not None else anchor
    ITEMS.append(("node", name, d_of(points, origin), fill, stroke, w, home))


def faces(prefix, parts, g, home=None, anchor=None, names=("left", "right", "top")):
    l, r, t = parts
    for nm, pts, col in zip(names, (l, r, t), (g, RAMP.get(g, g), g)):
        if prefix is None:
            static(pts, col)
        else:
            node(f"{prefix}_{nm}", pts, col, home=home, anchor=anchor)


def build_art():
    # the bench
    faces(None, box(0, 0, 0, 210, 34, 5), "#646464")
    # E1: the risers under the board's head; the first always there, three more by TILT
    faces(None, box(4, 8, RISER_Z0, 14, 18, RISER_H), E1)
    for k in range(1, 4):
        home = P(4, 8, RISER_Z0 + k * RISER_H)
        faces(f"riser{k}", box(4, 8, RISER_Z0 + k * RISER_H, 14, 18, RISER_H), E1, home=home)
    # the foot trestle
    faces(None, box(120, 10, 5, 6, 14, 8), "#7D7D7D")
    # the board for one to four risers, one copy shown, its groove a hairline
    for k in range(4):
        zh = zh_of(k + 1)
        home = P(HEAD_X, 8.0, zh)
        left, end, top = slab(zh)
        node(f"board{k}_left", left, "#7D7D7D", home=home)
        node(f"board{k}_end", end, RAMP["#7D7D7D"], home=home)
        node(f"board{k}_top", top, "#7D7D7D", home=home)
        fill, stroke, w = line_paint("#3A3A3A", 1.0)
        node(f"board{k}_groove", ribbon(P(6, GROOVE_Y, zh + 4), P(128, GROOVE_Y, FOOT_Z + BOARD_T), RIBBON), fill, stroke, w, home=home)
    # the posts at the board's head and foot
    faces(None, box(10, 2, 5, 3, 3, 52), "#7D7D7D")
    faces(None, box(118, 2, 5, 3, 3, 30), "#7D7D7D")
    # E2: the bells, each a hanger over a cylinder, around the bell's foot (anchor (0, 0, 0))
    anchor = P(0, 0, 0)
    for j in range(BELLS):
        fill, stroke, w = line_paint("#AFAFAF", 0.5)
        node(f"bell{j}_hanger", ribbon(P(0, 0, BELL_H + HANGER), P(0, 0, BELL_H), RIBBON), fill, stroke, w, anchor=anchor)
        node(f"bell{j}_side", cyl_side(0, 0, 0, BELL_R, BELL_H, 16), RAMP[E2], anchor=anchor)
        node(f"bell{j}_top", ellipse(0, 0, BELL_H, BELL_R, 24), E2, anchor=anchor)
    # the snare at the foot, SIZES copies: shell, rim, head (E1), its wires (E2)
    for s in range(SIZES):
        r = radius(s)
        home = P(DRUM_X, DRUM_Y, HEAD_Z)
        node(f"drum{s}_shell", cyl_side(DRUM_X, DRUM_Y, SHELL_Z, r, SHELL_H, 40), RAMP["#7D7D7D"], home=home)
        node(f"drum{s}_rim", cyl_side(DRUM_X, DRUM_Y, SHELL_Z + SHELL_H, r, RIM_H, 40), RAMP[E1], home=home)
        node(f"drum{s}_head", ellipse(DRUM_X, DRUM_Y, HEAD_Z, r, 56), E1, home=home)
        for k in range(WIRES):
            y = DRUM_Y - r * 0.5 + k * (r / 5)
            fill, stroke, w = line_paint(E2, 0.8)
            node(f"drum{s}_wire{k}", ribbon(P(DRUM_X - r * 0.6, y, HEAD_Z + 0.2), P(DRUM_X + r * 0.6, y, HEAD_Z + 0.2), RIBBON),
                 fill, stroke, w, home=home)
    # E3: the ring on the head, the reshaping outline (node 0, drawn here): a placeholder
    # ellipse around its centre until display.cpp sets its 64 points
    ITEMS.append(("path", "ring"))
    # E4: the tone tab, around its foot's back corner
    faces("tab", box(0, 0, 0, TAB_W, TAB_D, TAB_H), E4, anchor=anchor)
    # E3: the bounce's dots, around their centres
    for k in range(DOTS):
        node(f"dot{k}", circle(0, 0, 1.2, 12), E3, "none", 0, anchor=(0.0, 0.0))
    # E4: the balls, around where they touch the board; slot 5 drawn first, slot 0 last
    for k in range(BALLS - 1, -1, -1):
        rr = BALL_R
        node(f"ball{k}", circle(0, -rr, rr, 20), E4, anchor=(0.0, 0.0))
        node(f"glint{k}", [(-rr * 0.35 + rr * 0.28 * math.cos(2 * math.pi * i / 10), -rr * 1.35 + rr * 0.18 * math.sin(2 * math.pi * i / 10)) for i in range(10)], GLINT, "none", 0, anchor=(0.0, 0.0))


def q10(v):
    return int(round(v * 1024))


def main():
    build_art()
    # the reshaping outline is node 0 wherever it is drawn; the rest in drawing order
    names = [i[1] for i in ITEMS if i[0] == "path"] + [i[1] for i in ITEMS if i[0] == "node"]
    index = {n: i for i, n in enumerate(names)}
    count = len(names)
    lines = [f'<g id="{ID}_panel">', "<!-- SDK two-page header: keep this block before machine artwork below y=38. -->",
             '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    shades = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    for i in range(8):
        lines.append(f'<path id="{ID}_node_{count + i}" transform="matrix(1 0 0 1 {60 * i} 0)" d="M0,0 L60,0 L60,20 L0,20 Z" '
                     f'fill="{shades[i]}" stroke="{shades[i]}" stroke-width="0.5"/>')
    texts = [(5 + 120 * i, 10, "#CBCBCB" if i < 2 else "#000000", 10) for i in range(4)]
    texts += [(65 + 120 * i, 17.4, "#CBCBCB" if i < 2 else "#000000", 20) for i in range(4)]
    texts += [(5 + 120 * i, 33, "#5A5A5A", 10) for i in range(4)]
    texts += [(65 + 120 * i, 33, "#707070", 13) for i in range(4)]
    for t, (x, y, fill, size) in enumerate(texts):
        lines.append(f'<text id="{ID}_text_{t}" transform="matrix(1 0 0 1 {fmt(x)} {fmt(y)})" fill="{fill}" '
                     f'font-family="UniversNextPro-Light" font-size="{size}px">-</text>')
    homes = {}
    for item in ITEMS:
        if item[0] == "static":
            _k, d, fill, stroke, w = item
            sw = f' stroke="{stroke}" stroke-width="{fmt(w)}"' if stroke != "none" else ' stroke="none"'
            lines.append(f'<path d="{d}" fill="{fill}"{sw}/>')
        elif item[0] == "path":
            pts = ellipse(0, 0, 0, 8, 64)
            o = P(0, 0, 0)
            lines.append(f'<path id="{ID}_node_{index[item[1]]}" transform="matrix(1 0 0 1 640 320)" d="{d_of(pts, o)}" '
                         f'fill="none" stroke="{E3}" stroke-width="0.8"/>')
            homes[item[1]] = None
        else:
            _k, name, d, fill, stroke, w, home = item
            homes[name] = home
            sw = f' stroke="{stroke}" stroke-width="{fmt(w)}"' if stroke != "none" else ' stroke="none"'
            at = f"{fmt(home[0])} {fmt(home[1])}" if home is not None else "640 320"
            lines.append(f'<path id="{ID}_node_{index[name]}" transform="matrix(1 0 0 1 {at})" d="{d}" fill="{fill}"{sw}/>')
    palette = [(K, K, 0.5), ("none", E3, 0.8)]
    for i, (fill, stroke, w) in enumerate(palette):
        lines.append(f'<circle id="{ID}_palette_{i}" transform="matrix(1 0 0 1 640 320)" cx="0" cy="0" r="1" '
                     f'fill="{fill if fill != "none" else K}" stroke="{stroke}" stroke-width="{fmt(w)}"/>')
    lines.append("</g>")
    text = "\n".join(lines) + "\n"
    assert len(text.encode()) < 64 * 1024, len(text.encode())
    (ROOT / "panel.svg.fragment").write_text(text)

    first = lambda n: index[n]
    h = ["#pragma once",
         "// Generated by tools/make_panel.py: the screen's nodes, in panel.svg.fragment's order, and the",
         "// plane's geometry. Plan positions are pixels (Q10): X down to the right, Y down to the left, Z up.",
         "#include <opxy/types.hpp>", "", "namespace galileo {", "namespace art {", "using opxy::u32;", "using opxy::i32;",
         f"const u32 node_count = {count};   // the artwork's nodes; the header's eight bars follow",
         f"const u32 palette = {len(palette)};",
         "// screen (Q10) = (ox + (x - y) cos 30, oy + (x + y) / 2 - z)",
         f"const i32 ox = {q10(OX)}, oy = {q10(OY)}, cos30 = {q10(C30)};",
         "enum Node : u32 {"]
    for i, n in enumerate(names):
        h.append(f"    {n} = {i},")
    h.append("};")
    h += [f"// node groups: a board copy's four, a riser's three, a bell's three, a drum copy's {3 + WIRES}, a ball's two",
          f"const u32 board_nodes = 4, riser_nodes = 3, bell_nodes = 3, drum_nodes = {3 + WIRES}, ball_nodes = 2;",
          f"const u32 bells = {BELLS}, balls = {BALLS}, sizes = {SIZES}, wires = {WIRES}, dots = {DOTS};",
          "// where each node drawn in place stands (Q10), the rest 0: display.cpp shows it there or parks it",
          "const i32 home[node_count][2] = {"]
    for n in names:
        hm = homes.get(n)
        h.append(f"    {{{q10(hm[0])}, {q10(hm[1])}}},   // {n}" if hm is not None else f"    {{0, 0}},   // {n}")
    h.append("};")
    consts = {
        "riser_z0": RISER_Z0, "riser_h": RISER_H, "board_t": BOARD_T, "foot_z": FOOT_Z, "groove_y": GROOVE_Y,
        "head_x": HEAD_X, "lip_x": LIP_X, "bell_x0": BELL_X0, "bell_x1": BELL_X1, "bell_drop": BELL_DROP,
        "ball_x0": BALL_X0, "ball_step": BALL_STEP, "drum_x": DRUM_X, "drum_y": DRUM_Y, "head_z": HEAD_Z,
        "tab_w": TAB_W, "tab_d": TAB_D, "tab_z0": TAB_Z0, "tab_z1": TAB_Z1, "arc_x0": ARC_X0, "arc_x1": ARC_X1,
        "arc_lo": ARC_LO, "arc_hi": ARC_HI, "land_x": LAND_X, "r_big": R_BIG, "r_small": R_SMALL}
    h.append("// the plane's geometry (px, Q10)")
    for k, v in consts.items():
        h.append(f"const i32 {k} = {q10(v)};")
    h.append(f"const i32 bell_span = {q10(BELL_SPAN)};   // Q10: the last bell at bell_x0 + (bell_x1 - bell_x0) bell_span")
    h += ["}", "}", ""]
    (ROOT / "art.h").write_text("\n".join(h))
    n_static = sum(1 for i in ITEMS if i[0] == "static")
    print(f"wrote panel.svg.fragment ({len(text.encode())} bytes) and art.h: {count} nodes + 8 header bars, {n_static} static paths")


if __name__ == "__main__":
    main()
