#include "display.h"
#include "view.hpp"
#include "models.hpp"
#include "human.hpp"
#include "kit.h"
#include "pads.hpp"

// Syahi's screen: an engraved plate. On the left the selected key's object in
// section (its head shaped by FORM, its shell, the part that couples to it and
// whatever strikes it); on the right its head from above, with the nodal lines
// of the mode the stroke excites and the strike mark. Every control moves a
// part; a hit flexes the head in its mode shape, sends two rings out from the
// strike mark and lights the nodal lines, for as long as the sound rings.
namespace syahi {
namespace screen {
using namespace opxy::draw;
using opxy::i16;
const i32 U = opxy::draw::unit;   // Q10 pixel

// ---- small helpers
inline i32 dclamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 mix(i32 a, i32 b, i32 t15) { return a + (((b - a) * (t15 >> 3)) >> 12); }   // t in Q15
inline i32 q15of(u32 raw) { return static_cast<i32>(raw > 127 ? 32767 : raw * 258u); }   // 0..127 -> Q15
template <class F> inline void put(F& frame, u32 node, i32 x, i32 y, i32 scale, u32 shade) {
    const bool filled = node >= art::fixed_first && node < art::fixed_first + art::fixed_count && art::fixed[node - art::fixed_first].fill;
    place(frame, node, x, y, scale, shade, filled ? fill_and_stroke : stroke);
}
// TUNE's scale on a length in Q10 px.
struct Look;
template <class F> inline void rest(F& frame, u32 node, i32 dx = 0, i32 dy = 0, i32 scale = U, u32 shade = 0xffu) {
    const art::Fixed& f = art::fixed[node - art::fixed_first];
    put(frame, node, f.x * 64 + dx, f.y * 64 + dy, scale, shade == 0xffu ? f.shade : shade);
}
// sin of tenths of a degree, Q10, and a clockwise turn by tenths: the screen's own.
static inline i32 sin10(i32 tenths) {
    tenths %= 3600;
    if (tenths < 0) tenths += 3600;
    const i32 sign = tenths >= 1800 ? -1 : 1;
    tenths %= 1800;
    if (tenths > 900) tenths = 1800 - tenths;
    const i32 degree = tenths / 10, frac = tenths % 10;
    const i32 low = art::quarter[degree], high = art::quarter[degree < 90 ? degree + 1 : 90];
    return sign * (low + (high - low) * frac / 10);
}
static inline Turn turn_by(i32 tenths) { const Turn t = {sin10(tenths + 900), sin10(tenths)}; return t; }
// A part's turn by tenths of a degree, clockwise.
template <class F> inline void tilt(F& frame, u32 node, i32 tenths) { if (tenths) turn(frame, node, turn_by(tenths)); }

// e^(-x) for x in Q16, Q15.
inline i32 decay15(i32 x) { return x >= 16 * 65536 ? 0 : exp2q(-multiply(x, q31(0.5 / 0.6931471805599453)) * 2) >> 16; }

// ---- what the controls make of the object
struct Look {
    u32 model;
    u32 raw[8];
    i32 c[8];          // Q15 0..1
    i32 a;             // the head's half-width, Q10 px
    i32 s;             // the section's scale: its gain times TUNE's (1.0 .. 0.8), Q10
    i32 sp;            // the plan's: its gain times TUNE's (1.0 .. 0.86), Q10
    i32 y0;            // the head's plane on screen, Q10 px: the object stands on the floor
    i32 depth;         // the shell's depth below the head, Q10 px
    i32 spot;          // Q15, after HUMAN's nudge
    i32 flex;          // the head's displacement at its antinode, Q10 px (signed)
    i32 lit;           // 0..3: how brightly the hit lights the nodal lines
    Spec spec;
};

inline i32 sc(const Look& k, i32 v) { return (v >> 2) * k.s >> 8; }
inline i32 scp(const Look& k, i32 v) { return (v >> 2) * k.sp >> 8; }
// The reshaping outlines are drawn at the section's gain: their points (Q10
// screen px from the node) go back to path units, which reach 64 px either way.
inline void fit(Point* p, u32 n) {
    for (u32 i = 0; i < n; ++i) {
        p[i].x = p[i].x * 1024 / art::section_gain;
        p[i].y = p[i].y * 1024 / art::section_gain;
    }
}
// A fixed part where the artwork puts it in its frame (the section from the
// head's centre, the plan from its centre, both scaled with TUNE), moved by
// (dx, dy) px at TUNE 0 (Q10), scaled by mul (Q10).
template <class F> inline void part(F& frame, const Look& k, u32 node, i32 dx = 0, i32 dy = 0, i32 mul = U, u32 shade = 0xffu) {
    const art::Fixed& f = art::fixed[node - art::fixed_first];
    i32 x = f.x * 64 + dx, y = f.y * 64 + dy, scale = mul;
    if (f.frame == art::section_frame) { x = art::cx * U + sc(k, x); y = k.y0 + sc(k, y); scale = (mul * k.s) >> 10; }
    else if (f.frame == art::plan_frame) { x = art::px * U + scp(k, x); y = art::py * U + scp(k, y); scale = (mul * k.sp) >> 10; }
    put(frame, node, x, y, scale, shade == 0xffu ? f.shade : shade);
}

// Which control drives each model's striker, shell and their keyframes.
const u8 striker_kind[8] = {art::beater_striker, art::stick_striker, art::mallet_striker, art::hand_striker,
                            art::mallet_striker, art::metal_striker, art::bounce_striker, art::hammer_striker};
const u8 striker_slot[8] = {3, 3, 6, 6, 3, 3, 6, 7};
const opxy::i8 shell_slot[8] = {6, -1, -1, 7, 6, -1, -1, -1};

// ---- the head in section: top and thickness at 31 points, Q10 px from the head's plane.
struct Head {
    i32 x[art::head_points], top[art::head_points], thick[art::head_points];
};
inline void head_shape(const Look& k, i32 flex, Head& h) {
    const u32 position = k.raw[4] * 3096u;   // FORM over the six spans, Q16
    u32 i0 = position >> 16;
    if (i0 > 5) i0 = 5;
    const i32 f = static_cast<i32>(position - (i0 << 16)) >> 1;   // Q15
    const bool tabla_head = k.model == tabla, bar_model = k.model == bar, metal_model = k.model == metal;
    for (u32 i = 0; i < art::head_points; ++i) {
        const i32 xn = (static_cast<i32>(i) * 2 - 30) * 1092;          // -1..1 in Q15 (32768 / 30)
        h.x[i] = (k.a * (static_cast<i32>(i) * 2 - 30)) / 30;
        i32 top = mix(art::head[i0][i][0], art::head[i0 + 1][i][0], f) * 64;   // px*16 -> Q10
        i32 th = mix(art::head[i0][i][1], art::head[i0 + 1][i][1], f) * 64;
        if (tabla_head) {
            // LOAD: the syahi's lens builds on whatever FORM made.
            const i32 lens = (((k.c[2] >> 3) * art::lens[i]) >> 12) * 6656 >> 12;   // up to 6.5 px, built up on the head
            th += lens;
            top -= lens;
            // PRESS: the heel of the palm dents the head where it rests.
            const i32 heel = -k.spot / 2 - 4000;                               // Q15 position of the heel
            const i32 d = (xn - heel) / 12;                                    // Q15 / 0.37
            if (d > -2731 && d < 2731) {
                const i32 u = 32767 - ((d * d) >> 11) * 3;                     // 1 - (d / 0.37)^2 in Q15
                if (u > 0) top += ((k.c[3] >> 4) * ((u >> 4) * (u >> 4) >> 14) >> 9) * 9 / 2;
            }
        }
        if (k.model == boom || k.model == tom) top += (k.c[2] >> 4) * art::sag[i] / 4096 * 7 / 4;   // BEND: a slack head sags
        if (metal_model) {
            // SWELL: a gong's turned-down rim.
            const i32 out = (xn < 0 ? -xn : xn) - 24576;
            if (out > 0) top += (k.c[6] >> 5) * ((out >> 3) * (out >> 3) >> 16) / 4 * 7;
        }
        if (bar_model) th = th * (40 - (k.c[7] >> 10)) / 32;                  // ALLOY: wood thick, bronze thin
        // A hit flexes the head in the stroke's mode: the centre's (0,1) or the edge's (1,1).
        if (flex && bar_model) top += (flex * ((art::mode_bar[i] * (32767 - k.spot) + art::mode_bar2[i] * (k.spot * 4 / 5)) >> 15)) >> 12;
        else if (flex) top += (flex * ((art::mode_centre[i] * (32767 - k.spot) + art::mode_edge[i] * (k.spot * 4 / 5)) >> 15)) >> 12;
        h.top[i] = sc(k, top);
        h.thick[i] = sc(k, th);
    }
}
// One of the head's rows (top or thickness) at x (Q10 px from the centre).
inline i32 along(const Head& h, const i32* v, i32 x) {
    if (x <= h.x[0]) return v[0];
    for (u32 i = 1; i < art::head_points; ++i)
        if (x <= h.x[i]) {
            const i32 span = h.x[i] - h.x[i - 1];
            return v[i - 1] + (span ? (v[i] - v[i - 1]) * ((x - h.x[i - 1]) >> 4) / (span >> 4 ? span >> 4 : 1) : 0);
        }
    return v[art::head_points - 1];
}
// The head's resting top at x (Q10 px from the centre).
inline i32 top_at(const Head& h, i32 x) {
    if (x <= h.x[0]) return h.top[0];
    for (u32 i = 1; i < art::head_points; ++i)
        if (x <= h.x[i]) {
            const i32 span = h.x[i] - h.x[i - 1];
            return h.top[i - 1] + (span ? (h.top[i] - h.top[i - 1]) * ((x - h.x[i - 1]) >> 4) / (span >> 4 ? span >> 4 : 1) : 0);
        }
    return h.top[art::head_points - 1];
}

template <class F>
inline void draw_head(F& frame, const Look& k, const Head& h, u32 key) {
    Point p[64];
    u32 n = 0;
    for (u32 i = 0; i < art::head_points; ++i) p[n++] = Point{h.x[i], h.top[i]};
    for (u32 i = art::head_points; i-- > 0;) p[n++] = Point{h.x[i], h.top[i] + h.thick[i]};
    fit(p, n);
    if (frame.paths[0].key != key) view::outline(frame.paths[0], p, n, key);
    place(frame, 0, art::cx * U, k.y0, art::section_gain, k.lit >= 2 ? art::struck : art::subject, stroke);
}

// The shell: two keyframes mixed by the model's control, x scaled by the head.
template <class F>
inline void draw_shell(F& frame, const Look& k, u32 key) {
    const u32 m = k.model;
    const i32 t = shell_slot[m] >= 0 ? k.c[shell_slot[m]] : 0;
    const u32 count = art::shell_points[m];
    Point p[64];
    for (u32 i = 0; i < count && i < 64; ++i) {
        const i32 x = sc(k, mix(art::shell[m][0][i][0], art::shell[m][1][i][0], t) * 64);
        const i32 y = sc(k, mix(art::shell[m][0][i][1], art::shell[m][1][i][1], t) * 64);
        p[i] = Point{x, y};
    }
    fit(p, count);
    for (u32 i = 0; i < count && i < 64; ++i) p[i] = Point{dclamp(p[i].x, -63 * U, 63 * U), dclamp(p[i].y, -63 * U, 63 * U)};
    if (frame.paths[1].key != key) view::outline(frame.paths[1], p, count, key);
    place(frame, 1, art::cx * U, k.y0 + sc(k, art::shell_mid[m] * U), art::section_gain, art::path_line, stroke);
}

// The striker: three keyframes along its control, placed with its contact
// point at (x, y) (Q10 screen px), turned by tenths of a degree about it, drawn
// at the striker's gain. A mallet's yarn rides on its ball.
template <class F>
inline void draw_striker(F& frame, const Look& k, i32 x, i32 y, i32 tenths, u32 key) {
    const u32 kind = striker_kind[k.model];
    const i32 c = k.c[striker_slot[k.model]] * 2;   // 0..2 in Q15
    const u32 k0 = c >= 32768 ? 1 : 0;
    const i32 t = dclamp(c - static_cast<i32>(k0) * 32768, 0, 32767);
    Point p[64];
    for (u32 i = 0; i < art::striker_points; ++i)
        p[i] = Point{mix(art::striker[kind][k0][i][0], art::striker[kind][k0 + 1][i][0], t) * 64,
                     mix(art::striker[kind][k0][i][1], art::striker[kind][k0 + 1][i][1], t) * 64};
    // Turned about the contact point: the node's origin moves so the tip stays put.
    // Lean it over (or back) by the least that keeps it clear of the header above
    // y 40 and on the screen at the left. Where no lean clears it from the left (a
    // strike out at the rim, a rimshot), the player reaches from the right: the
    // striker drawn mirrored, over the head, rather than swung down below it.
    const i32 G = art::striker_gain;
    Turn r = Turn{U, 0};
    i32 rx = 0, ry = 0, used = 0;
    bool mirrored = false;
    i32 chosen_miss = 0x7fffffff;
    for (i32 side = 0; side < 2; ++side) {
        const i32 sx = side ? -1 : 1;
        const i32 tx = sx * art::tip_x * G, ty = art::tip_y * G;
        const i32 base = sx * tenths;
        i32 best = -1, best_miss = 0x7fffffff;
        for (i32 step = 0; step <= 36; ++step) {
            const i32 lean = (step + 1) / 2 * 50 * (step & 1 ? 1 : -1);
            const i32 u = base - lean;
            const Turn t = u ? turn_by(u) : Turn{U, 0};
            const i32 qx0 = (tx * t.cos - ty * t.sin) / U, qy0 = (tx * t.sin + ty * t.cos) / U;
            i32 top = 0x7fffffff, left = 0x7fffffff;
            for (u32 i = 0; i < art::striker_points; ++i) {
                const i32 px = sx * p[i].x;
                const i32 py = (px * t.sin + p[i].y * t.cos) / U * G / U;
                const i32 qx = (px * t.cos - p[i].y * t.sin) / U * G / U;
                top = py < top ? py : top;
                left = qx < left ? qx : left;
            }
            const i32 above = 46 * U - (y - qy0 + top), off = 2 * U - (x - qx0 + left);   // 8 px clear of the header
            const i32 miss = (above > 0 ? above : 0) + (off > 0 ? off : 0);
            // A striker whose far end rises less than 12 px over its contact has
            // swung down beside or under the head: count the shortfall as a miss.
            const i32 rise = qy0 - top;
            const i32 low = rise < 12 * U ? 12 * U - rise : 0;
            if (miss + low < best_miss) { best_miss = miss + low; best = step; }
            if (!best_miss) break;
        }
        if (best_miss < chosen_miss) {
            chosen_miss = best_miss;
            mirrored = side != 0;
            const i32 lean = (best + 1) / 2 * 50 * (best & 1 ? 1 : -1);
            used = base - lean;
            r = used ? turn_by(used) : Turn{U, 0};
            rx = (tx * r.cos - ty * r.sin) / U;
            ry = (tx * r.sin + ty * r.cos) / U;
        }
        if (!chosen_miss) break;
    }
    if (mirrored) {
        for (u32 i = 0; i < art::striker_points; ++i) p[i].x = -p[i].x;
        key ^= 0x6a09e667u;
    }
    if (frame.paths[3].key != key) view::outline(frame.paths[3], p, art::striker_points, key);
    place(frame, 3, x - rx, y - ry, G, art::subject, stroke);
    if (used) turn(frame, 3, r);
    // The yarn wound round a mallet's ball, across its shaft: the ball's centre
    // lies its radius above the contact point, along the turned striker.
    if (kind == art::mallet_striker) {
        const i32 ball = k0 ? mix(6 * U, 3400 * U / 1000, t) : mix(9 * U, 6 * U, t);   // Q10 px, as authored
        if (ball > 4 * U) {
            const i32 b = ball * G / U;
            put(frame, art::yarn, x + b * r.sin / U, y - b * r.cos / U, (ball / 9) * G / U, art::mid);
            tilt(frame, art::yarn, mirrored ? used + 340 : used - 340);
        } else hide(frame, art::yarn);
    }
}

// ---- the hand (TABLA, BOUNCE): Max's drawing of a pointing hand, static
// artwork turned about the wrist and moved, never reshaped. It points down onto
// the head at 50 degrees, the index's tip (or, low enough, BOUNCE's stick) the
// first thing to touch it. At rest the wrist is cocked back by `swing` (tenths
// of a degree); `raise` (Q15) runs from 0 at a hit to 1 at rest. PRESS lays it
// flatter. Where the cocked hand would reach the header, it cocks less, then
// lies flatter, judged at rest so it never changes as it strikes. `slide`
// (Q15) takes BOUNCE's stick back through the fist.
template <class F>
inline void draw_hand(F& frame, i32 x, i32 y, i32 press, i32 swing, i32 slide, i32 raise, bool stick) {
    const i32 S = art::hand_scale;
    // An artwork point (px * 16 about the wrist), scaled and turned: Q10 px.
    auto at = [](i32 hx, i32 hy, const Turn& t, i32& ox, i32& oy) {
        const i32 px = hx * 64 * art::hand_scale / U, py = hy * 64 * art::hand_scale / U;
        ox = (px * (t.cos >> 2) - py * (t.sin >> 2)) >> 8;
        oy = (px * (t.sin >> 2) + py * (t.cos >> 2)) >> 8;
    };
    const i32 sx = art::stick_slide[0] * (slide >> 5) >> 10, sy = art::stick_slide[1] * (slide >> 5) >> 10;   // px * 16
    i32 phi = 500 - press, theta = 30, cx = 0, cy = 0;
    for (i32 d = 0; d <= 300; d += 20) {
        phi = 500 - press - d;
        const Turn a = turn_by(phi);
        // What touches the head first: the lowest point on the way down.
        i32 lx = 0, ly = -0x7fffffff, top = 0x7fffffff, ox, oy;
        for (u32 i = 0; i < art::hand_hull_points; ++i) {
            at(art::hand_hull[i][0], art::hand_hull[i][1], a, ox, oy);
            if (oy > ly) { ly = oy; lx = ox; }
            top = oy < top ? oy : top;
        }
        if (stick) {
            at(art::stick_tip[0] + sx, art::stick_tip[1] + sy, a, ox, oy);
            if (oy > ly) { ly = oy; lx = ox; }
        }
        cx = x - lx;
        cy = y - ly;
        bool clear = false;
        for (theta = swing; theta >= 30 && cy + top >= 41 * U; theta -= 30) {
            const Turn b = turn_by(phi - theta);
            i32 high = 0x7fffffff;
            for (u32 i = 0; i < art::hand_hull_points; ++i) {
                at(art::hand_hull[i][0], art::hand_hull[i][1], b, ox, oy);
                high = oy < high ? oy : high;
            }
            if (cy + high >= 41 * U) { clear = true; break; }
        }
        if (clear) break;
        theta = 30;
    }
    const Turn r = turn_by(phi - theta * (raise >> 5) / 1024);
    for (u32 n = art::hand_fill; n <= art::hand_roll_2; ++n) {
        put(frame, n, cx, cy, S, art::fixed[n - art::fixed_first].shade);
        turn(frame, n, r);
    }
    if (stick) {
        i32 ox, oy;
        at(sx, sy, r, ox, oy);
        put(frame, art::hand_stick, cx + ox, cy + oy, S, art::fixed[art::hand_stick - art::fixed_first].shade);
        turn(frame, art::hand_stick, r);
    } else hide(frame, art::hand_stick);
}

// ---- the coupling outline (path 2): wires, lacing, strap, cord, chain, bounces, spring.
template <class F>
inline void draw_coupling(F& frame, const Look& k, const Head& h, i32 buzz, i32 age, u32 key) {
    Point p[32];
    u32 n = 0;
    const i32 a = k.a;
    i32 ox = art::cx * U, oy = k.y0;
    u32 shade = art::mid;
    switch (k.model) {
    case snare: {
        // The snare wires under the bottom head, out to the strainer and the
        // butt: WIRES their swing (up to 8 px), and below half the wires let go
        // of the head and hang clear, 8 px at their middle at none; SNAP their
        // tightness (flatter, finer); a hit their buzz.
        const i32 amp = sc(k, ((k.c[2] >> 5) * 8 * U >> 10) * (48 - (k.c[6] >> 11)) / 48) + buzz;
        const i32 slack = k.c[2] < 16384 ? sc(k, ((16384 - k.c[2]) >> 4) * 8 * U >> 10) : 0;
        const i32 y = k.depth + sc(k, 3 * U);
        const u32 zigs = 16 + static_cast<u32>(k.c[6] >> 11);
        const i32 z = static_cast<i32>(zigs);
        const i32 w = a + sc(k, 2 * U);   // the path reaches 64 px either way
        for (u32 i = 0; i <= zigs && n < 32; ++i) {
            const i32 j = static_cast<i32>(i);
            const i32 sag = slack * ((4 * j * (z - j) << 10) / (z * z)) >> 10;   // a parabola, 0 at the ends
            p[n++] = Point{-w + 2 * w * j / z, y + sag + ((i & 1) && i < zigs ? amp + U / 2 : 0)};
        }
        shade = art::path_line;
        break;
    }
    case tom: {
        // The lacing over the shell, hoop to hoop, through a waist: SQUEEZE pulls
        // the waist in (the talking drum under the arm) or lets it bow out.
        const i32 squeeze = k.c[3] - 16384;                          // -1..1 in Q14
        const i32 waist = 16384 - (squeeze >> 1) * 3 / 4;            // 0.62 .. 1.38 of the width, Q14
        const i32 top = sc(k, 4 * U), bottom = k.depth - sc(k, 4 * U), mid = (top + bottom) / 2;
        for (u32 i = 0; i <= 12 && n < 31; ++i) {
            const i32 x = -a + 2 * a * static_cast<i32>(i) / 12;
            if (i) {
                const i32 xm = (x - a / 12) * waist >> 14;           // the cord's middle, drawn in or out
                p[n++] = Point{dclamp(xm, -a, a), mid};
            }
            p[n++] = Point{x, (i & 1) ? bottom : top};
        }
        shade = art::mid;
        break;
    }
    case tabla: {
        // The straps: a zigzag across the shell from the braid to the base,
        // following its belly (GOURD), over the gatte TUNE hammers down.
        const i32 belly = mix(4, 27, k.c[7]);   // % of the head at mid-height
        const i32 top = sc(k, 2 * U), bottom = k.depth - sc(k, 2 * U);
        for (u32 i = 0; i <= 12 && n < 31; ++i) {
            const i32 x = -a + 2 * a * static_cast<i32>(i) / 12;
            if (i) {
                // Half way down, the strap lies on the shell's belly.
                const i32 xm = (x - a / 12) * (100 + belly) / 100;
                p[n++] = Point{dclamp(xm, -63 * U, 63 * U), (top + bottom) / 2};
            }
            p[n++] = Point{x, (i & 1) ? bottom : top};
        }
        shade = art::mid;
        break;
    }
    case metal: {
        // The sizzle chain, draped from the cup over the edge: SIZZLE lets more of it hang.
        const i32 hang = sc(k, (k.c[7] >> 4) * 24 * U / 2048);
        for (u32 i = 0; i <= 10; ++i) {
            const i32 x = -a * static_cast<i32>(i) / 10 - a / 8;
            const i32 lie = top_at(h, x) - 3 * U / 2;
            p[n++] = Point{x, lie};
        }
        p[n++] = Point{-a - 3 * U, top_at(h, -a) + hang / 3};
        p[n++] = Point{-a - 4 * U, top_at(h, -a) + hang * 2 / 3};
        p[n++] = Point{-a - 4 * U + U / 2, top_at(h, -a) + hang + U};
        shade = art::mid;
        break;
    }
    case bounce: {
        // The stick's path as it bounces: COUNT arcs (up to eight), GAP their reach, SHRINK how they close up.
        const u32 count = dclamp(static_cast<i32>(1 + ((k.raw[2] * 23u + 63u) / 127u)), 1, 24);
        const u32 arcs = count - 1 > 7 ? 7 : count - 1;
        if (!arcs) {   // one strike: no bounce to draw
            hide(frame, 2);
            p[0] = Point{-8 * U, 0};
            p[1] = Point{8 * U, U};
            if (frame.paths[2].key != 1) view::polyline(frame.paths[2], p, 2, 1);
            return;
        }
        const i32 x0 = -(a * 85 / 100) * k.spot / 32768;
        i32 x = x0, span = sc(k, (7 + (k.c[3] >> 11)) * U), height = sc(k, (10 + (k.c[3] >> 11)) * U);
        const i32 ratio = 19660 + (k.c[7] * 7 >> 3);   // each gap's ratio to the last, 0.6 .. 1.47, Q15
        p[n++] = Point{x, top_at(h, x) - U};
        for (u32 b = 0; b < arcs && n + 3 < 32; ++b) {
            if (x + span > x0 + 110 * U) span = x0 + 110 * U - x;   // the path reaches 64 px either way
            if (span < 2 * U) break;
            for (u32 j = 1; j <= 3; ++j) {
                const i32 xx = x + span * static_cast<i32>(j) / 3;
                const i32 rise = j == 3 ? 0 : height * (j == 1 ? 8 : 8) / 9;
                p[n++] = Point{xx, top_at(h, xx) - U - rise};
            }
            x += span;
            span = (span >> 15) * ratio + ((span & 32767) * ratio >> 15);   // span * ratio >> 15, which can pass 2^31
            height = height * (ratio < 32768 ? ratio : 32768) >> 15;
        }
        if (n < 2) p[n++] = Point{x0 + U, top_at(h, x0) - U};
        shade = art::dim;
        break;
    }
    case wire: {
        // The spring hanging from the head through the tube and out under it:
        // SPRING its coils, SWEEP their width, RATE its stretch.
        const u32 coils = 6 + static_cast<u32>(k.c[6] >> 12);
        const i32 width = sc(k, 3 * U + (k.c[2] >> 5) * 7 * U / 1024);
        const i32 length = k.depth * 6 / 10 + sc(k, (k.c[3] >> 5) * 44 * U / 1024);
        for (u32 i = 0; i <= coils * 2 && n < 32; ++i) {
            const i32 y = sc(k, 6 * U) + (length + buzz) * static_cast<i32>(i) / static_cast<i32>(coils * 2);
            p[n++] = Point{(i & 1) ? width : -width, y};
        }
        shade = art::path_line;
        break;
    }
    default:
        for (u32 i = 0; i < 2; ++i) p[n++] = Point{i ? U : -U, 0};
        hide(frame, 2);
        if (frame.paths[2].key != 1) view::polyline(frame.paths[2], p, 2, 1);
        return;
    }
    (void)age;
    // Centre the points on the node (path points reach 64 px either way).
    i32 low = p[0].y, high = p[0].y;
    for (u32 i = 1; i < n; ++i) { low = p[i].y < low ? p[i].y : low; high = p[i].y > high ? p[i].y : high; }
    i32 left = p[0].x, right = p[0].x;
    for (u32 i = 1; i < n; ++i) { left = p[i].x < left ? p[i].x : left; right = p[i].x > right ? p[i].x : right; }
    const i32 mid = ((low + high) / 2) & ~(U - 1), centre = ((left + right) / 2) & ~(U - 1);
    for (u32 i = 0; i < n; ++i) { p[i].y -= mid; p[i].x -= centre; }
    oy += mid;
    ox += centre;
    fit(p, n);
    if (frame.paths[2].key != key) view::polyline(frame.paths[2], p, n, key);
    place(frame, 2, ox, oy, art::section_gain, shade, stroke);
}

// ---- the header's captions
inline void copy_text(char* out, const char* s) { opxy::draw::copy(out, header_capacity, s); }
inline u32 put_number(char* out, u32 n, i32 v) {
    char d[6];
    u32 k = 0;
    if (v < 0) { out[n++] = '-'; v = -v; }
    do { d[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && k < 6);
    while (k) out[n++] = d[--k];
    return n;
}
// A pitch in log2 Hz (Q16) as a note name: "G#3".
inline void note_text(char* out, i32 log2_hz) {
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const i32 semis = (((log2_hz - lg(440)) * 12) + 32768) >> 16;
    const i32 midi = dclamp(69 + semis, 0, 127);
    u32 n = 0;
    for (const char* s = names[midi % 12]; *s; ++s) out[n++] = *s;
    n = put_number(out, n, midi / 12 - 1);
    out[n] = 0;
}
// A time in log2 s (Q16): "35ms", "0.47s", "2.6s".
inline void time_text(char* out, i32 log2_s) {
    const i32 ms = static_cast<i32>(exp2i(dclamp(log2_s + lg(1000), 0, 20 * 65536)));
    u32 n = 0;
    if (ms < 100) { n = put_number(out, n, ms); out[n++] = 'm'; out[n++] = 's'; }
    else if (ms < 995) {
        const i32 cs = (ms + 5) / 10;
        out[n++] = '0';
        out[n++] = '.';
        out[n++] = static_cast<char>('0' + cs / 10);
        out[n++] = static_cast<char>('0' + cs % 10);
        out[n++] = 's';
    } else {
        const i32 ds = (ms + 50) / 100;
        n = put_number(out, n, ds / 10);
        out[n++] = '.';
        out[n++] = static_cast<char>('0' + ds % 10);
        out[n++] = 's';
    }
    out[n] = 0;
}
inline void amount_text(char* out, u32 raw) {
    u32 n = put_number(out, 0, static_cast<i32>((raw * 100u + 63u) / 127u));
    out[n] = 0;
}
// Each control's caption: real units where the control has one, else 0..100.
inline void value_text(char* out, const Look& k, u32 slot) {
    const u32 raw = k.raw[slot];
    const u32 m = k.model;
    if (slot == 0) {
        i32 pitch = k.spec.pitch;
        if (m == tabla) pitch += log2u(static_cast<u32>(65536 - multiply(k.spec.load, q16(0.18)))) - 16 * octave;
        note_text(out, pitch);
    } else if (slot == 1) time_text(out, k.spec.decay);
    else if (slot == 4) copy_text(out, form::name(raw));
    else if (m == tom && slot == 3) {
        const i32 v = (static_cast<i32>(raw) - 64) * 100 / (raw < 64 ? 64 : 63);   // -100 .. +100
        u32 n = 0;
        if (v > 0) out[n++] = '+';
        n = put_number(out, n, v);
        out[n] = 0;
    } else if (m == bounce && slot == 2) { u32 n = put_number(out, 0, static_cast<i32>(k.spec.count)); out[n] = 0; }
    else if (m == bounce && slot == 3) time_text(out, k.spec.gap);
    else if (m == wire && slot == 3) time_text(out, k.spec.bend_time);
    else if (m == bar && slot == 2) {
        // BEAT in hertz: "0Hz" .. "8.0Hz"
        const i32 tenths = (static_cast<i32>(raw) * 80 + 63) / 127;
        u32 n = put_number(out, 0, tenths / 10);
        if (tenths) { out[n++] = '.'; out[n++] = static_cast<char>('0' + tenths % 10); }
        out[n++] = 'H'; out[n++] = 'z'; out[n] = 0;
    } else amount_text(out, raw);
}

// e^(-t / time * nepers), Q15, for t and time in Q10 s.
inline i32 fall(i32 t, i32 time, i32 nepers_q8) {
    if (time < 1) time = 1;
    const i32 limit = (16 << 16) / nepers_q8;   // past e^-16 it is gone
    const i32 ratio = t / time;
    if (ratio > limit) return 0;
    return decay15((((t % time) << 8) / time + (ratio << 8)) * nepers_q8);
}

}

// No frame changes more than this many nodes. The outlines, the header's bars
// and what a hit moves always go; the fixed parts a turned control moves take
// what is left, in order, and the rest follow in the next frames.
const u32 node_budget = 26;
inline bool moved(const opxy::draw::Node& a, const opxy::draw::Node& b) {
    return a.x != b.x || a.y != b.y || a.scale != b.scale || a.cos != b.cos || a.sin != b.sin || a.shade != b.shade || a.paint != b.paint;
}
inline bool always(u32 n) {
    return n < Frame::path_count || n >= art::header ||
           (n >= art::nodal_circle_0 && n <= art::ring_far) || n == art::pad || n == art::plan_pad || n == art::yarn ||
           (n >= art::hand_stick && n <= art::hand_roll_2);
}
inline void hold_to_budget(DisplayState& state, Frame& frame, const u32* keys_before) {
    u32 used = 0;
    for (u32 n = 0; n < Frame::node_count; ++n)
        if (always(n) && (moved(frame.nodes[n], state.before[n]) || (n < Frame::path_count && frame.paths[n].key != keys_before[n]))) ++used;
    for (u32 n = Frame::path_count; n < art::header; ++n) {
        if (always(n) || !moved(frame.nodes[n], state.before[n])) continue;
        if (used < node_budget) ++used;
        else frame.nodes[n] = state.before[n];   // next frame
    }
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
    state.age = 1 << 20;
    state.shown = 0;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    using namespace opxy::draw;
    using namespace screen;
    // Hits: the note serial is the selected key's; a track change is never a hit.
    const bool baseline = !state.initialized || state.track != input.track;
    const bool hit = !baseline && input.note_serial != state.serial;
    const i32 step = dclamp(input.elapsed, 0, 1024);
    if (baseline) state.age = 1 << 20;
    else if (hit) state.age = 0;
    else state.age = dclamp(state.age + step, 0, 1 << 20);
    state.initialized = 1;
    state.track = input.track;
    state.serial = input.note_serial;
    Look k;
    k.model = input.model < kit::models ? input.model : 0;
    // A reset or a new model sets every element at once; otherwise this frame's
    // changes are held to a budget (below).
    const bool steady = state.shown == k.model + 1;
    if (!steady) {
        state.shown = k.model + 1;
        for (u32 p = 0; p < Frame::path_count; ++p) frame.paths[p].key = 0;
    }
    for (u32 n = 0; n < Frame::node_count; ++n) state.before[n] = frame.nodes[n];
    u32 keys_before[Frame::path_count];
    for (u32 p = 0; p < Frame::path_count; ++p) keys_before[p] = frame.paths[p].key;
    for (u32 i = 0; i < 8; ++i) {
        k.raw[i] = static_cast<u32>(input.slots[i] < 0 ? 0 : input.slots[i]) >> 24;
        k.c[i] = q15of(k.raw[i]);
    }
    i32 fractions[8];
    for (u32 i = 0; i < 8; ++i) fractions[i] = fraction(k.raw[i]);
    spec(k.model, fractions, k.spec);
    // HUMAN nudges this hit's strike point, as the engine does, from the key's note serial.
    const Variation v = vary(input.note_serial, input.pad[0]);
    k.spot = dclamp(q15of(static_cast<u32>(dclamp(static_cast<i32>(k.raw[5]) + v.spot, 0, 127))), 0, 32767);
    // TUNE scales the whole object, 1.0 to 0.8 in section (standing on the floor)
    // and 1.0 to 0.86 from above: a smaller drum is a higher note.
    k.s = (art::section_gain * (U - ((k.c[0] >> 5) * 205 >> 10))) >> 10;
    k.sp = (art::plan_gain * (U - ((k.c[0] >> 5) * 143 >> 10))) >> 10;
    k.a = sc(k, (k.model == tabla ? art::nominal * 4 / 5 : art::nominal) * U);
    {
        // A head domed up (FORM toward the bell) shrinks the section, standing on
        // the floor, so the dome and whatever strikes it stay clear of the header:
        // the dome's top at TUNE 0 no higher than y 72.
        Head domed;
        head_shape(k, 0, domed);
        i32 top = 0;
        for (u32 i = 0; i < art::head_points; ++i) top = domed.top[i] < top ? domed.top[i] : top;
        const i32 dome = -top * 1024 / k.s;                                         // local Q10 at TUNE 0
        const i32 tall = (art::floor_depth[k.model] * U + dome) * art::section_gain >> 10;   // Q10 px at TUNE 0
        const i32 room = (art::floor_y - 72) * U;
        if (tall > room) {
            const i32 fit = static_cast<i32>((static_cast<u32>(room) << 10) / static_cast<u32>(tall));   // Q10
            k.s = (k.s * fit) >> 10;
            k.a = sc(k, (k.model == tabla ? art::nominal * 4 / 5 : art::nominal) * U);
        }
    }
    k.y0 = art::floor_y * U - sc(k, art::floor_depth[k.model] * U);
    i32 depth = art::depth[k.model];
    if (k.model == boom) depth += (k.c[6] >> 5) * 26 / 1024;   // SHELL deepens the kick
    if (k.model == tabla) depth = 88;
    k.depth = sc(k, depth * U);
    // The head rings as the sound does: 60 dB over DECAY (to -24 dB it shows). A kit's
    // voice plays until its own sound ends: the OP-XY applies no amp envelope to it
    // and a note let go changes nothing, so neither does the screen.
    const i32 per_second = static_cast<i32>(exp2i(dclamp(lg(6.9078) - k.spec.decay + 16 * 65536, 0, 26 * 65536)));   // Q16
    const i32 ring = input.reduced_motion || state.age >= (1 << 20) ? 0 : fall(state.age, 1024, per_second >> 8);   // Q15
    // A visible wobble at 9 Hz under that ring, in eighths of a pixel.
    const i32 wobble = static_cast<i32>(static_cast<u32>(state.age) * 3164u / 1000u);   // 9 Hz in tenths of a degree (3240 a second)
    i32 flex = (((ring * (sin10(wobble + 900) << 5)) >> 15) * 8) >> 5;             // up to 8 px, Q10
    flex &= ~(U / 8 - 1);                                                          // in eighths of a pixel
    if (ring < 2400) flex = 0;   // still: nothing moves once it has rung down
    k.flex = flex;
    k.lit = ring > 8000 ? 3 : ring > 3200 ? 2 : ring > 1200 ? 1 : 0;   // to -12, -20 and -29 dB
    // ---- the section
    Head h, still;
    head_shape(k, 0, still);
    head_shape(k, k.flex, h);
    const u32 key_base = (k.model + 1) * 0x01000193u ^ (k.raw[0] << 3) ^ (k.raw[2] << 10) ^ (k.raw[3] << 17) ^ (k.raw[4] << 24)
                       ^ (k.raw[6] << 5) ^ (k.raw[7] << 12) ^ (k.raw[5] << 19);
    draw_head(frame, k, h, ((key_base * 2654435761u) ^ static_cast<u32>(k.flex) ^ (static_cast<u32>(k.spot) << 7)) | 1u);
    draw_shell(frame, k, (key_base ^ 0x5bd1e995u) | 1u);
    // Wires buzz and springs swing while the head rings.
    const i32 buzz = k.lit ? (((state.age >> 5) & 1) ? 1 : -1) * (ring >> 4) * U / 2048 * 2 : 0;
    draw_coupling(frame, k, still, buzz & ~(U / 4 - 1), state.age, (key_base ^ 0x9e3779b9u ^ static_cast<u32>(buzz)) | 1u);
    // ---- fixed parts: hidden, then every part of this model where the artwork
    // puts it; the controls then move the ones they move.
    const u32 m = k.model;
    for (u32 n = art::fixed_first; n < art::fixed_first + art::fixed_count; ++n) {
        const art::Fixed& f = art::fixed[n - art::fixed_first];
        if (f.models & (1u << m)) part(frame, k, n);
        else hide(frame, n);
    }
    rest(frame, art::floor_l);
    rest(frame, art::floor_r);
    // The striker over the strike point, lifted at rest and down on the head at a hit.
    i32 tx = -(k.a * 85 / 100) * (k.spot >> 5) / 1024;
    if (k.model == snare && k.c[3] > 26214) tx = mix(tx, -k.a - 2 * U, (k.c[3] - 26214) * 5);   // a rimshot lands on the rim
    const i32 lift_rest = 11 * U;
    i32 lift = lift_rest;
    if (!input.reduced_motion && state.age < 400) {
        const i32 back = 32767 - decay15(state.age * 1092);   // e^(-t / 60 ms)
        lift = ((lift_rest >> 5) * back >> 10) & ~(U / 2 - 1);
    }
    if (striker_kind[m] >= art::hand_striker) {
        // The hand pivots at the wrist: HAND (TABLA) cocks it further back at rest,
        // a fingertip's flick to a slap's full swing; HANDS (BOUNCE) takes the stick
        // back into the fist over its first 60% and swings the hand wider as palms take over.
        const i32 raise = dclamp(lift * 32 / (lift_rest >> 10), 0, 32767);
        const i32 c = k.c[6];
        const i32 press = m == tabla ? (k.c[3] >> 5) * 100 >> 10 : 0;   // PRESS: the heel goes down
        const i32 swing = m == tabla ? 60 + ((c >> 5) * 120 >> 10) : 80 + ((c >> 5) * 80 >> 10);
        const i32 slide = m == bounce ? dclamp(c * 17 / 10, 0, 32767) : 0;
        draw_hand(frame, art::cx * U + tx, k.y0 + top_at(still, tx), press, swing, slide, raise, m == bounce);
        // The reshaping outline the other strikers use is put away.
        Point q[2] = {Point{-U, 0}, Point{U, U}};
        if (frame.paths[3].key != 1) view::polyline(frame.paths[3], q, 2, 1);
        hide(frame, 3);
    } else {
        i32 turn_tenths = 0;
        draw_striker(frame, k, art::cx * U + tx, k.y0 + top_at(still, tx) - lift, turn_tenths, (key_base ^ 0x27d4eb2du) | 1u);
    }
    // DECAY: a folded cloth on the head, over more than half of it when short,
    // shrinking as the ring lengthens; at the longest it slides off the rim.
    {
        const i32 cx = (k.a * (38 + (k.c[1] * 67 >> 15))) / 100;
        const i32 cs = U * 145 / 100 - ((k.c[1] >> 5) * 1147 >> 10);   // 1.45x down to 0.33x
        const i32 cy = cx < k.a ? top_at(still, cx) : still.top[art::head_points - 1] + (cx - k.a) / 2;
        put(frame, art::cloth, art::cx * U + cx, k.y0 + cy, (cs * k.s) >> 10, art::body);   // outlined as structure, so it reads
        put(frame, art::fold, art::cx * U + cx, k.y0 + cy, (cs * k.s) >> 10, art::mid);
    }
    const bool drum = m != bar && m != metal && m != tabla;
    const i32 depth0 = (k.depth << 10) / k.s;   // the depth at TUNE 0, for parts placed in the section's frame
    if (drum) {
        // The bottom hoop, the lower lugs and the far wall's lower bolts follow the depth.
        part(frame, k, art::hoop_bl, 0, depth0 - 48 * U);
        part(frame, k, art::hoop_br, 0, depth0 - 48 * U);
        if (m == boom || m == tom || m == wire) {
            part(frame, k, art::lug2_l, 0, depth0 - 76 * U);
            part(frame, k, art::lug2_r, 0, depth0 - 76 * U);
            part(frame, k, art::inside_b, 0, depth0 - 76 * U);   // its foot at the bottom head
        }
        for (u32 i = 0; i < 3; ++i) {
            if (m == snare || m == bounce) hide(frame, art::bolt_10 + i);
            else part(frame, k, art::bolt_10 + i, 0, depth0 - 26 * U);
        }
        if (m == bounce) hide(frame, art::inside_a);
        // BEND: a slack head's tension rods backed out.
        if (m == boom || m == tom) {
            const i32 out = -((k.c[2] >> 5) * 12 * U >> 10);
            part(frame, k, art::rod_l, 0, out);
            part(frame, k, art::rod_r, 0, out);
        }
        // The plan's lugs round the hoop.
        for (u32 i = 0; i < 8; ++i) {
            const i32 tenths = static_cast<i32>(i) * 450 + 225;
            const i32 r = (art::plan_r + 9) * U;
            part(frame, k, art::plan_lug_0 + i, (r >> 10) * sin10(tenths + 900), (r >> 10) * sin10(tenths));
            tilt(frame, art::plan_lug_0 + i, tenths + 900);
        }
    }
    // The nodal lines of the mode the stroke excites, lit by a hit and gone as
    // it rings down: circles from the centre, diameters from the edge.
    static const u8 lit_shade[4] = {art::mid, art::mid, art::nodal2, art::struck};
    static const i16 angle[4] = {900, 450, 1350, 0};
    const i32 R = art::plan_r * U;
    if (k.lit) {
        if (m == bar) {
            // Across the bar: its first mode's two nodes from a strike near the
            // middle, its second mode's three from one near the end.
            const i32 second = k.spot * 3 / 2, first = (32767 - k.spot) * 3 / 2;
            for (u32 i = 0; i < 5; ++i)
                if ((i < 2 ? first : second) > 8192) part(frame, k, art::bar_node_0 + i, 0, 0, U, lit_shade[k.lit]);
        } else {
            const i32 diameters = k.spot * 3, circles = (32767 - k.spot) * 3;
            const i32 rs = m == tabla ? U * 9 / 10 : m == metal ? art::cymbal_q10 : U;
            for (u32 i = 0; i < 4; ++i)
                if (diameters - static_cast<i32>(i) * 32768 > 8192) {
                    part(frame, k, art::diameter_0 + i, 0, 0, rs, lit_shade[k.lit]);
                    tilt(frame, art::diameter_0 + i, angle[i]);
                }
            for (u32 i = 0; i < 3; ++i)
                if (circles - static_cast<i32>(i) * 32768 > 8192) part(frame, k, art::nodal_circle_0 + i, 0, 0, rs, lit_shade[k.lit]);
        }
    }
    // The strike mark, where HUMAN puts this hit, and the rings a hit sends out.
    const i32 reach = m == bar ? 58 * U : m == tabla ? R * 9 / 10 : m == metal ? R * art::cymbal_q10 >> 10 : R;
    const i32 mx = -(reach * 85 / 100 >> 10) * (k.spot >> 5) / 1;
    const i32 my = m == bar ? -art::bar_y * U : 0;
    part(frame, k, art::mark, mx >> 0, my);
    part(frame, k, art::mark_ring, mx, my);
    if (!input.reduced_motion && state.age < 300) {
        const i32 t = state.age;
        const i32 grow = (U * 2 / 5 + t * 4) & ~(U / 16 - 1);
        part(frame, k, art::ring_near, mx, my, dclamp(grow, U / 4, 2 * U), t < 150 ? art::ring1 : art::dim);
        if (t > 40) part(frame, k, art::ring_far, mx, my, dclamp((grow * 3 / 5) & ~(U / 16 - 1), U / 4, 2 * U), t < 200 ? art::ring1 : art::dim);
    }
    // ---- each model's own parts
    switch (m) {
    case boom: {
        // SHELL: the port and the spurs' brackets go down with the shell; the spurs
        // reach from them to the floor, so the deeper the drum the shorter they stand.
        {
            const i32 top = depth0 - 10 * U, reach = art::floor_depth[boom] * U - top;   // local Q10
            const i32 mul = dclamp(reach / 40, U / 3, 2 * U);
            for (u32 i = 0; i < 2; ++i) {
                const i32 sx = i ? 1 : -1;
                part(frame, k, art::spur_l + 2 * i, 0, top - 66 * U, mul);
                part(frame, k, art::spur_tip_l + 2 * i, sx * ((art::nominal + 3) * U + 14 * mul), art::floor_depth[boom] * U - U * 8 / 5);
            }
        }
        const i32 port_dy = depth0 * 55 / 100 - 50 * U;
        part(frame, k, art::port, 0, port_dy);
        // AIR: rings of air pushed out of the port, more and wider as it rises.
        const i32 air = k.c[7] * 3;
        for (u32 i = 0; i < 3; ++i) {
            const i32 a3 = air - static_cast<i32>(i) * 32768;
            if (a3 <= 0) hide(frame, art::air_0 + i);
            else part(frame, k, art::air_0 + i, 0, port_dy, U * 3 / 5 + (dclamp(a3, 0, 32767) >> 6) * 2 / 5, a3 < 16384 ? art::dim : 0xffu);
        }
        // BEATER past half way: a click pad on the head and seen from above.
        const i32 pad = k.c[3] - 18022;
        if (pad > 0) {
            put(frame, art::pad, art::cx * U + tx, k.y0 + top_at(still, tx), (((U / 3 + pad * 2 / 45) * k.s) >> 10), art::paste);
            part(frame, k, art::plan_pad, mx, 0, U / 3 + pad * 2 / 45);
        } else {
            hide(frame, art::pad);
            hide(frame, art::plan_pad);
        }
        break;
    }
    case snare: {
        // SNAP: the strainer's lever, hanging out from the shell when the wires
        // are loose, thrown up against it as they draw tight (110 degrees).
        tilt(frame, art::strainer, 1100 - ((k.c[6] >> 5) * 1100 >> 10));
        // HEAD: the muffling ring, a wide band for a dead head and a narrow one
        // at the rim for a ringing one; in section the ring's tube lying on the head.
        const i32 band = U * 3 / 5 - ((k.c[7] >> 5) * 520 >> 10);   // 0.6 of the radius .. 0.09
        part(frame, k, art::plan_head, 0, 0, U - band);
        const i32 rx = k.a - ((k.a >> 5) * (band >> 5));
        put(frame, art::oring_l, art::cx * U - rx, k.y0 + top_at(still, -rx) - sc(k, 2 * U), k.s, art::mark_ink);
        put(frame, art::oring_r, art::cx * U + rx, k.y0 + top_at(still, rx) - sc(k, 2 * U), k.s, art::mark_ink);
        break;
    }
    case tom:
        // HEADS: the second head, detuned, seen through the first.
        part(frame, k, art::plan_second, (k.c[7] >> 5) * 7 * U >> 10, (k.c[7] >> 5) * 3 * U >> 10, U * 94 / 100);
        break;
    case tabla: {
        // The gatte under the straps, hammered down by TUNE.
        const i32 belly = mix(4, 27, k.c[7]);
        const i32 yg = k.depth * mix(55, 85, k.c[0]) / 100;
        const i32 top = sc(k, 2 * U), bottom = k.depth - sc(k, 2 * U), half = (top + bottom) / 2;
        const i32 along = ((yg - half) >> 4) * 1024 / ((bottom - half) >> 4);   // Q10 down the strap's lower half
        for (u32 i = 0; i < 5; ++i) {
            // On the strap from its middle (on the belly) down to the low point i.
            const i32 xl = -k.a + 2 * k.a * static_cast<i32>(2 * i + 1) / 12;
            const i32 xm = (xl + k.a / 12) * (100 + belly) / 100;
            const i32 x = xm + (((xl - xm) >> 4) * along >> 6);
            put(frame, art::gatte_0 + i, art::cx * U + x, k.y0 + yg, k.s, art::chrome);
        }
        // LOAD: the syahi's layers grow from a fleck to the full paste.
        const i32 load = 60 + (k.c[2] >> 5) * 964 / 1024;
        for (u32 i = 0; i < 3; ++i) part(frame, k, art::syahi_0 + i, 0, 0, load);
        break;
    }
    case bar: {
        // BEAT: the twin key a touch shorter and turned as it beats faster.
        const i32 twin = U - ((k.c[2] >> 5) * 90 >> 10), twist = (k.c[2] >> 5) * 60 >> 10;
        part(frame, k, art::plan_twin, 0, 0, twin);
        tilt(frame, art::plan_twin, twist);
        // Its holes turn and close in with it.
        for (u32 i = 0; i < 2; ++i) {
            const art::Fixed& f = art::fixed[art::plan_hole_2l + i - art::fixed_first];
            const i32 hx = f.x * 64, x = (hx * twin) >> 10;
            part(frame, k, art::plan_hole_2l + i, x - hx, (x >> 10) * sin10(twist));
        }
        // The cord through each node's hole, where the bar is still in its first mode.
        for (u32 i = 0; i < 2; ++i) {
            const i32 x = (i ? 1 : -1) * k.a * 552 / 1000;
            const i32 y = k.y0 + along(still, still.top, x) + along(still, still.thick, x) / 2;
            put(frame, art::hole_l + 3 * i, art::cx * U + x, y, k.s, art::mid);
            put(frame, art::cord_end_l + 3 * i, art::cx * U + x, y, k.s, art::mark_ink);
        }
        // TUBE: the stopper at the resonator's foot.
        const i32 tube = mix(26 * U, 80 * U, k.c[6]);
        part(frame, k, art::cap, 0, 15 * U + tube + 2 * U);
        // ALLOY: the wood's grain fades out as the bar turns to bronze.
        const i32 grain = 32767 - k.c[7];
        for (u32 i = 0; i < 3; ++i) {
            if (i < 2) {
                // The keys either side are of the same wood.
                if (grain < 8192 * static_cast<i32>(i + 1)) { hide(frame, art::plan_grain_hi_0 + 2 * i); hide(frame, art::plan_grain_lo_0 + 2 * i); }
            }
            if (grain < 8192 * static_cast<i32>(i + 1)) { hide(frame, art::grain_0 + i); hide(frame, art::plan_grain_0 + i); continue; }
            const i32 y = still.thick[15] * static_cast<i32>(i + 1) / 4;
            put(frame, art::grain_0 + i, art::cx * U, k.y0 + still.top[15] + y, k.s, art::faint);
        }
        break;
    }
    case metal: {
        // WASH: the lathe's grooves deepen, one by one.
        const i32 wash = k.c[2] * 4;
        for (u32 i = 0; i < 4; ++i) {
            const i32 g = wash - static_cast<i32>(i) * 32768;
            if (g <= 0) hide(frame, art::groove_0 + i);
            else part(frame, k, art::groove_0 + i, 0, 0, U, g < 16384 ? art::mid : art::line);
        }
        // SIZZLE: rivets round the edge.
        const i32 rivets = k.c[7] * 6;
        static const i16 rvx[6] = {-63, -32, 32, 63, 32, -32}, rvy[6] = {0, -55, -55, 0, 55, 55};
        for (u32 i = 0; i < 6; ++i) {
            if (rivets > static_cast<i32>(i) * 32768) part(frame, k, art::rivet_0 + i, rvx[i] * U, rvy[i] * U);
            else hide(frame, art::rivet_0 + i);
        }
        // The bell's felts, sleeve, washer and wing nut sit on it, wherever FORM puts it.
        const i32 cup = (still.top[15] << 10) / k.s, th = (still.thick[15] << 10) / k.s;   // local Q10
        part(frame, k, art::rod_up, 0, cup);
        part(frame, k, art::sleeve, 0, cup + th / 2);
        part(frame, k, art::felt_top, 0, cup - U * 9 / 5);
        part(frame, k, art::nut, 0, cup - U * 18 / 5 - U * 8 / 5);
        part(frame, k, art::felt_bottom, 0, cup + th + U * 9 / 5);
        part(frame, k, art::washer, 0, cup + th + U * 22 / 5);
        break;
    }
    case bounce: {
        // The stick's marks from above: one per strike, up to eight, closing up or spreading as SHRINK says.
        const u32 count = k.spec.count > 8 ? 8 : k.spec.count;
        i32 x = mx, span = (7 + (k.c[3] >> 12)) * U;
        const i32 ratio = 19660 + (k.c[7] * 7 >> 3);
        for (u32 i = 0; i < 8; ++i) {
            if (i >= count || x > 60 * U) { hide(frame, art::bounce_0 + i); continue; }
            part(frame, k, art::bounce_0 + i, x, 0, U, i ? art::dim : art::mid);
            x += span;
            span = (span >> 15) * ratio + ((span & 32767) * ratio >> 15);   // span * ratio >> 15, which can pass 2^31
        }
        break;
    }
    case wire: {
        // The clamp at the spring's end.
        const i32 length = (k.depth * 6 / 10 + sc(k, (k.c[3] >> 5) * 44 * U / 1024)) + sc(k, 6 * U);
        put(frame, art::clamp, art::cx * U, k.y0 + length + sc(k, 2 * U), k.s, art::chrome);
        part(frame, k, art::plan_coil, 0, 0, U / 2 + (k.c[2] >> 5) * 5 / 8);
        break;
    }
    default:
        break;
    }
    // ---- the header: both rows; with Shift held both are Shift's, the edited one bright.
    static const char* const chokes[4] = {"none", "1", "2", "3"};
    const char* titles[8];
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) {
        titles[i] = kit::labels[m][i];
        value_text(values[i], k, i);
    }
    if (input.shift) {
        char pan[8], level[8], sends[2][4], human[16], grit_text[16];
        u32 n = 0;
        n = put_number(pan, 0, input.pan);
        pan[n] = 0;
        n = put_number(level, 0, input.level);
        level[n++] = 'd'; level[n++] = 'B'; level[n] = 0;
        for (u32 s = 0; s < 2; ++s) {
            // SDK 1.15: 0 follows the track's send (TRK), else the key's percent + 1.
            const i32 percent = input.pad[2 + s] - 1;
            if (percent >= 0) { n = put_number(sends[s], 0, percent > 100 ? 100 : percent); sends[s][n] = 0; }
            else copy(sends[s], sizeof(sends[s]), "TRK");
        }
        pad_caption(0, input.pad[0], human);
        pad_caption(1, input.pad[1], grit_text);
        static const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"HUMAN", "GRIT", "FX I", "FX II"}};
        // The model in lower case, as FORM's materials are (the upper-case names
        // don't fit), and BOUNCE short: "bounce" is 67 px of the column's 53.
        static const char* const machines[8] = {"boom", "snare", "tom", "tabla", "bar", "metal", "bnce", "wire"};
        const char* const machine = machines[m];
        const char* const shift_values[2][4] = {{machine, chokes[input.choices[1] & 3u], pan, level},
                                                {human, grit_text, sends[0], sends[1]}};
        // Both rows at once, stacked as the pages: the edited one bright on
        // the bar, the other grey, so Shift swaps all eight names and values.
        for (u32 line = 0; line < 2; ++line)
            for (u32 c = 0; c < 4; ++c) {
                titles[4 * line + c] = shift_titles[line][c];
                copy(values[4 * line + c], header_capacity, shift_values[line][c]);
            }
    }
    page_header(state.header, input, frame, titles, values, art::header);
    if (steady) hold_to_budget(state, frame, keys_before);
}
}
