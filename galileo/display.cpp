#include "display.h"

// GALILEO's screen: an 18th-century demonstration inclined plane (art.h,
// tools/make_art.py). The board's angle is the key's tilt, read on the brass
// quadrant at its hinge; bells hang in gallows along the groove for its frets,
// slid along by SPACE; the handful waits in a cup behind the catch; the ball's
// path is dotted as a stroboscope would catch it, and its hops over the
// instrument draw its bounce; the pendulum on the post swings the beat, or is
// tied back on FREE. At the foot stands the key's instrument, its four sound
// controls moving its parts. A note trips the catch and releases the ball: it
// rolls, rings the bells, falls, strikes the instrument and hops to rest,
// replayed from the same plan the engine plays (motion.h), lighting the dots
// of its path as it passes them.
namespace galileo {
namespace view {
using opxy::draw::unit;
using opxy::draw::Turn;
using opxy::draw::keep;
using opxy::draw::fill_and_stroke;
struct P { i32 x, y; };

// A clockwise turn by tenths of a degree, Q10.
inline Turn turning(i32 tenths) {
    const u32 phase = static_cast<u32>(tenths) * 1193046u;   // 2^32 / 3600
    Turn t = {dsp::sine(phase + 0x40000000u) >> 21, dsp::sine(phase) >> 21};
    return t;
}
inline P rotate(P p, Turn t) {
    P r = {(p.x * t.cos - p.y * t.sin) >> 10, (p.x * t.sin + p.y * t.cos) >> 10};
    return r;
}
inline P at(i32 x, i32 y) { P p = {x, y}; return p; }
// A raw control (0..127) as a Q10 share.
inline i32 share(u32 raw) { return static_cast<i32>((raw > 127 ? 127 : raw) * 8257u >> 10); }
inline i32 lerp(i32 a, i32 b, u32 raw) { return a + (((b - a) * share(raw)) >> 10); }

template <class F> inline void put(F& f, u32 n, P where, i32 scale = unit, Turn t = Turn{unit, 0}) {
    opxy::draw::place(f, n, where.x, where.y, scale, 0, keep);
    if (t.cos != unit || t.sin) opxy::draw::turn(f, n, t);
}
template <class F> inline void put_shaded(F& f, u32 n, P where, u32 shade, i32 scale = unit, Turn t = Turn{unit, 0}) {
    opxy::draw::place(f, n, where.x, where.y, scale, shade, fill_and_stroke);
    if (t.cos != unit || t.sin) opxy::draw::turn(f, n, t);
}
template <class F> inline void conceal(F& f, u32 n) { opxy::draw::hide(f, n); }
// The felt (kept on the screen however wide it grows: see magnify()).
template <class F> inline void put_felt(F& f, P where, i32 scale, Turn t = Turn{unit, 0}) {
    opxy::draw::place(f, art::felt, where.x, where.y, scale, 0, keep);
    if (t.cos != unit || t.sin) opxy::draw::turn(f, art::felt, t);
}

// The instrument at the foot is laid out at its own scale and drawn `zoom`
// times larger about (zoom_x, zoom_y), where the ball meets it: its artwork is
// authored that much larger (tools/make_art.py), so only places move here. Its
// floor is laid out at floor_y, which the zoom brings down to the base.
const i32 zoom = 1331;                     // 1.3, Q10
const i32 zoom_x = 420 * unit, zoom_y = 112 * unit;
const i32 floor_y = zoom_y + ((art::base_y - zoom_y) * 64) / (zoom >> 4);
// Each model then fitted between the hinge's pillar and the screen's edge,
// under the header, at every setting of its controls: a share of the zoom
// (Q10) about its foot on the floor, and a nudge (px).
const short fits[kit::models][2] = {
    {750, 4}, {984, 0}, {875, 0}, {778, 4}, {901, 0}, {983, -3}, {1024, 2}, {973, 2},
    {1024, 0}, {1024, 0}, {963, 0}, {1024, 0}, {818, 0}, {899, -6}, {1003, 4}};
struct Fit { i32 k, dx; };
inline Fit fit_of(u32 model) { Fit r = {fits[model][0], fits[model][1] * unit}; return r; }
inline P magnified(P p, Fit m) {
    const i32 x = zoom_x + (((p.x - zoom_x) * zoom) >> 10), y = zoom_y + (((p.y - zoom_y) * zoom) >> 10);
    return at(zoom_x + m.dx + (((x - zoom_x) * m.k) >> 10), art::base_y + (((y - art::base_y) * m.k) >> 10));
}
// Moves the instrument's nodes to their places at the zoom; the two reshaping
// outlines grow by their scale (their lines authored thinner to match).
template <class F> void magnify(F& f, Fit m) {
    for (u32 n = 0; n < art::node_count; ++n) {
        if (n > art::teeth && n < art::shell) continue;
        opxy::draw::Node& node = f.nodes[n];
        if (node.x == opxy::draw::hidden_x * unit && node.y == opxy::draw::hidden_y * unit) continue;
        const P p = magnified(at(node.x, node.y), m);
        node.x = p.x;
        node.y = p.y;
        node.scale = (node.scale * (n <= art::teeth ? (zoom * m.k) >> 10 : m.k)) >> 10;
        // Every line 1 px wide at any size: a scaled part takes its grey's
        // stroke for the band its scale falls in (art.h, tools/make_art.py).
        if (const u32 tone = art::line_tone[n]) {
            u32 band = 0;
            while (band < art::line_bands - 1 && node.scale >= art::line_bounds[band]) ++band;
            node.shade = art::line_palette + (tone - 1) * art::line_bands + band;
            node.paint = opxy::draw::stroke;
        }
    }
    // the felt (36 px long at its unit scale) kept on the screen
    opxy::draw::Node& felt = f.nodes[art::felt];
    const i32 right = 468 * unit - ((36 * zoom * felt.scale) >> 10);
    if (felt.x != opxy::draw::hidden_x * unit && felt.x > right) felt.x = right;
}

// Writes an outline of n points (Q10, relative to the node) into a reshaping
// path, adding points along its longest edges until it has the frame's 64:
// every point distinct, the outline closed by its first and last.
template <class Path> inline void outline(Path& path, const P* points, u32 n, u32 key) {
    if (path.key == key) return;
    P p[64] = {};
    for (u32 i = 0; i < n && i < 64; ++i) p[i] = points[i];
    u32 count = n < 64 ? n : 64;
    while (count < 64) {
        u32 best = 0;
        i32 longest = -1;
        for (u32 i = 0; i < count; ++i) {
            const P& a = p[i];
            const P& b = p[i + 1 < count ? i + 1 : 0];
            const i32 dx = (a.x - b.x) >> 4, dy = (a.y - b.y) >> 4;
            const i32 l = dx * dx + dy * dy;
            if (l > longest) { longest = l; best = i; }
        }
        const P& a = p[best];
        const P& b = p[best + 1 < count ? best + 1 : 0];
        const P mid = {(a.x + b.x) >> 1, (a.y + b.y) >> 1};
        for (u32 i = count; i > best + 1; --i) p[i] = p[i - 1];
        p[best + 1] = mid;
        ++count;
    }
    for (u32 i = 0; i < 64; ++i) {
        path.points[i].x = dsp::clamp(p[i].x, -63 * unit, 63 * unit);
        path.points[i].y = dsp::clamp(p[i].y, -63 * unit, 63 * unit);
    }
    path.key = key;
}
inline u32 mix_key(u32 h, i32 v) { return (h ^ static_cast<u32>(v)) * 16777619u; }

// Q10 seconds as samples.
inline u32 samples(i32 age) { return static_cast<u32>(dsp::clamp(age, 0, 40 * 1024)) * 44100u >> 10; }

// The board's geometry for a key.
struct Board {
    Turn turn;
    i32 tenths;
    P foot;
    // A point at s (Q16, head 0 .. foot 65536) along the board, `above` px (Q10) over its running surface.
    P along(i32 s, i32 above) const {
        return local_at(-(((65536 - s) >> 4) * (art::beam >> 10) >> 2), -above);
    }
    P local_at(i32 x, i32 y) const {
        const P r = rotate(at(x, y), turn);
        return at(foot.x + r.x, foot.y + r.y);
    }
};
const i32 run_start = 8520, run_end = 61604;   // 0.13 .. 0.94 of the board: from the catch to the lip
const i32 gap = 2560;                           // bells at least 10 px apart (Q16 of the board)
const i32 catch_s = 6881;                       // 0.105
const i32 cup_x = -246170;                      // the cup's centre line, board space (0.06 along), Q10
// Where the handful lies in the cup: columns and rows of a ball's width.
const signed char heap_slots[8][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}, {1, 2}};
const char* const chokes[4] = {"none", "1", "2", "3"};
const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"SPACE", "FEEL", "FX I", "FX II"}};

struct Drum { P head; i32 scale; };
// The strikes so far: flex Q10 0..1 (the last strike, easing out), how many,
// and the samples since the latest (large for none).
struct Strike { i32 flex; u32 count; u32 since; };

// The shells a drum can stand on: none (a conga's barrel is its own), a floor
// tom's, a bass drum's, a snare's shallow one, a rack tom's deep one.
enum Shell { bare, floor_shell, bass_kind, snare_kind, tom_kind };
// A drum at the given place and size: shell, sheen, hoops, lugs and rods, the
// batter head (dented by the strike, dished by sag), its collar, and the felt
// laid on it (wider as decay falls). Returns where the ball lands.
template <class F> P drum(F& f, const Drum& d, i32 flex, i32 sag, u32 felt_raw, Shell kind = floor_shell) {
    using namespace art;
    if (kind != bare) {
        const u32 shells[5] = {0, shell, bass_shell, snare_shell, tom_shell};
        const u32 hoops[5] = {0, hoop_low, bass_hoop, snare_hoop, tom_hoop};
        put(f, shells[kind], d.head, d.scale);
        if (kind != snare_kind) put(f, sheen, d.head, d.scale);
        put(f, hoops[kind], d.head, d.scale);
        put(f, kind == snare_kind ? snare_lugs : lugs, d.head, d.scale);
    }
    put(f, hoop, d.head, d.scale);
    put(f, collar, d.head, d.scale);
    const i32 rx = (drum_rx * d.scale) >> 10, ry = (drum_ry * d.scale) >> 10;
    const i32 land = -(rx * 45) / 100;
    const i32 dent = (6 * flex * d.scale) >> 10;
    P pts[64];
    for (u32 i = 0; i < 64; ++i) {
        const u32 phase = i * 67108864u;           // 2^32 / 64
        const i32 c = dsp::sine(phase + 0x40000000u) >> 16, s = dsp::sine(phase) >> 16;   // Q15
        const i32 x = (rx * c) >> 15;
        i32 y = (ry * s) >> 15;
        if (s > 0) y += (sag * s) >> 15;
        const i32 dx = (x - land) >> 5;
        const i32 w = (rx * 3 / 10) >> 5;
        const i32 g = w ? 32767 - dsp::clamp((dx * dx * 32) / (w * w + 1) * 1024, 0, 32767) : 0;
        y += (dent * (g > 0 ? g : 0)) >> 15;
        pts[i] = at(x, y);
    }
    outline(f.paths[0], pts, 64, mix_key(mix_key(mix_key(2166136261u, d.scale), flex >> 6), sag >> 8) | 1u);
    put(f, head, d.head);
    put_felt(f, at(d.head.x + (rx * 12) / 100, d.head.y - 1024), lerp(1850, 560, felt_raw));
    return at(d.head.x + land, d.head.y - (ry >> 1) - 3 * unit);
}
// A stand: a column from `top` down to the base, its tripod on the base.
// A short stand's tripod is smaller, so its legs don't run on into the arms above.
template <class F> void stand(F& f, P top) {
    using namespace art;
    const i32 length = floor_y - top.y;
    // a holder's own post may already reach the tripod
    if (length < 6 * unit) conceal(f, column);
    else put(f, column, top, dsp::clamp((length << 10) / (64 << 10), 96, 2048));
    put(f, tripod, at(top.x, floor_y), dsp::clamp((length << 10) / (40 << 10), 450, 1024));
}

// The ball's flight from the board's lip to the instrument, u (Q10) 0 .. 1:
// a fall, as Galileo drew it, onto anything 6 px or more below the lip; onto
// anything higher, an arc thrown off the lip's ferrule that still comes down
// onto it (its bulge c = (6 px - drop) / 2 keeps it descending at the end).
inline P flight(P lip, P land, i32 u) {
    const i32 drop = land.y - lip.y;
    const i32 c = drop < 6 * unit ? (6 * unit - drop) >> 1 : 0;
    const i32 y = lip.y + ((drop * ((u * u) >> 10)) >> 10) - (((4 * c >> 4) * ((u * (1024 - u)) >> 10)) >> 6);
    return at(lip.x + (((land.x - lip.x) * u) >> 10), y);
}

// The ball's travel as the screen shows it. Where the sound's own travel to
// the landing is under min_travel (at a key's defaults, with no frets, it is
// none: the ball lands as the note starts), the ball would only jump onto the
// instrument. So the screen shows it travel anyway, along its whole path from
// the catch over min_travel, fast at first and easing into its landing, and
// shows the rest of the run (the strike, the hops) that much later. Longer
// travel follows the sound; with reduced motion the ball lands at once.
const u32 min_travel = 4410;               // 0.1 s
struct Travel {
    u32 shown;      // the travel shown (samples), 0 where the screen follows the sound
    u32 late;       // how much later than the sound the landing and all after it are shown
    i32 board;      // the path's length on the board, from the catch to the lip (Q10 px)
    i32 seg[8];     // the fall's eight stretches, from the lip to the landing (Q10 px)
    i32 total;      // the whole path (Q10 px)
};
inline i32 span(P a, P b) {
    const i32 dx = (a.x - b.x) >> 5, dy = (a.y - b.y) >> 5;
    return static_cast<i32>(dsp::isqrt(static_cast<u32>(dx * dx + dy * dy))) << 5;
}
// The share of the path (Q16) the ball has covered s samples into a shown
// travel: 1 - (1 - s / shown)^2, decelerating.
inline u32 eased(u32 s, u32 shown) {
    if (s >= shown) return 65536;
    const u32 r = 65536 - dsp::udiv(s << 16, shown);
    return 65536 - (((r >> 1) * (r >> 1)) >> 14);
}
// When (samples into a shown travel) the ball has covered d (Q10 px) of it.
inline u32 eased_time(i32 d, const Travel& tr) {
    if (d <= 0) return 0;
    if (d >= tr.total) return tr.shown;
    const u32 q = dsp::udiv(static_cast<u32>(d >> 4) << 16, static_cast<u32>(tr.total >> 4));   // Q16
    const u32 x = 65536 - dsp::isqrt((65536 - (q < 1 ? 1 : q > 65535 ? 65535 : q)) << 16);
    return (tr.shown * x) >> 16;
}

template <class F> void hide_bodies(F& f) {
    for (u32 n = art::shell; n < art::node_count; ++n) conceal(f, n);
    conceal(f, art::head);
    conceal(f, art::teeth);
}

// A slat or tongue as an outline from `from` to `to`, half as thick as `w0`
// (Q10 px) at its root and `w1` at its tip, bowed sideways by `bow` (Q10 px).
template <class Path> void slat(Path& path, P origin, P from, P to, i32 w0, i32 w1, i32 bow, u32 key) {
    const i32 dx = to.x - from.x, dy = to.y - from.y;
    const u32 len = dsp::isqrt(static_cast<u32>(((dx >> 5) * (dx >> 5)) + ((dy >> 5) * (dy >> 5)))) << 5;   // Q10
    const i32 l = (len ? static_cast<i32>(len) : 1024) >> 5;
    const i32 nx = -dy * 32 / l, ny = dx * 32 / l;   // the unit normal, Q10
    P pts[24];
    for (u32 i = 0; i < 12; ++i) {
        const i32 u = static_cast<i32>(i) * 1024 / 11;                    // 0 .. 1, Q10
        const i32 bend = (bow * ((4 * u * (1024 - u)) >> 10)) >> 10;      // a parabola, most at the middle
        const i32 w = w0 + (((w1 - w0) * u) >> 10);
        const P c = at(from.x - origin.x + ((dx * u) >> 10) + ((nx * bend) >> 10), from.y - origin.y + ((dy * u) >> 10) + ((ny * bend) >> 10));
        pts[i] = at(c.x + ((nx * w) >> 10), c.y + ((ny * w) >> 10));
        pts[23 - i] = at(c.x - ((nx * w) >> 10), c.y - ((ny * w) >> 10));
    }
    outline(path, pts, 24, key);
}

template <class F> P scene_body(F& f, u32 model, const u32* c, const Strike& hit) {
    using namespace art;
    using namespace kit;
    const i32 flex = hit.flex;
    switch (model) {
    case kick: {
        // On its spurs, beater ready: tune sets its size, decay the felt; punch
        // draws the beater back; beater swaps a big felt head for a small hard one.
        Drum d;
        d.scale = lerp(1015, 820, c[0]);
        d.head = at(body_x + 14 * unit, floor_y - (((bass_depth + drum_ry) * d.scale) >> 10) - 6 * unit);
        const P land = drum(f, d, flex, 0, c[2], bass_kind);
        const i32 rx = (drum_rx * d.scale) >> 10;
        const i32 low = floor_y - 36 * unit;   // the spurs reach the floor
        put(f, spur_l, at(d.head.x - rx + 9 * unit, low));
        put(f, spur_r, at(d.head.x + rx - 9 * unit, low));
        const P pivot = at(d.head.x - rx - 7 * unit, floor_y - 5 * unit);
        put(f, footboard, at(pivot.x - 7 * unit, floor_y - 2 * unit));
        const Turn t = turning(lerp(-40, -330, c[1]) + ((flex * 160) >> 10));
        put(f, shaft, pivot, unit, t);
        put(f, beater, at(pivot.x + 52 * t.sin, pivot.y - 52 * t.cos), lerp(1750, 600, c[3]));
        return land;
    }
    case snare: {
        // A shallow snare on its stand: tune sizes it, decay the felt; the wires
        // pressed up under the bottom head, or dropped clear on the throw-off,
        // its lever up or down beside them; tone lays a stick across the head
        // from the far side (dark) or raises it onto the rim for a ringing crack.
        Drum d;
        d.scale = lerp(1015, 840, c[0]);
        const i32 rx = (drum_rx * d.scale) >> 10;
        const i32 bottom = ((snare_depth + drum_ry) * d.scale) >> 10;
        d.head = at(body_x, 124 * unit);
        const P land = drum(f, d, flex, 0, c[2], snare_kind);
        const P under = at(d.head.x, d.head.y + bottom);
        put(f, basket, under, (d.scale * 9) / 10);
        stand(f, at(under.x, under.y + 8 * unit));
        put(f, wires, at(d.head.x, under.y + 2 * unit + lerp(8 * unit, 0, c[1])), (d.scale * 9) / 10,
            turning(lerp(-90, 0, c[1])));
        const P strain = at(d.head.x - rx + 2 * unit, d.head.y + (((snare_depth * d.scale) >> 10) >> 1) + 6 * unit);
        put(f, strainer, strain);
        put(f, lever, at(strain.x - unit, strain.y + 2 * unit), unit, turning(lerp(1900, 3500, c[1])));
        // the stick from the far hoop, pointing back across the head
        put(f, stick, at(d.head.x + rx + 5 * unit, d.head.y - unit), unit, turning(lerp(1820, 2130, c[3])));
        // the felt on the head's near half, clear of the stick above it and of the ball's landing
        put_felt(f, at(d.head.x - (rx / 10), d.head.y + (((drum_ry * d.scale) >> 10) / 2)), lerp(1100, 450, c[2]));
        return land;
    }
    case rim: {
        // A rim-click: the stick laid across the head, its butt on the heavy
        // counter-hoop where the ball strikes; wood lowers it from a raised
        // rim shot onto the head as a side-stick, tune shortens it, body deepens
        // the shell under the click.
        Drum d;
        d.scale = lerp(840, 1015, c[3]);
        const i32 rx = (drum_rx * d.scale) >> 10, ry = (drum_ry * d.scale) >> 10;
        const i32 bottom = ((snare_depth + drum_ry) * d.scale) >> 10;
        d.head = at(body_x + 4 * unit, 126 * unit);
        drum(f, d, flex, 0, c[2], snare_kind);
        put(f, rimhoop, d.head, d.scale);
        const P under = at(d.head.x, d.head.y + bottom);
        put(f, basket, under, (d.scale * 9) / 10);
        stand(f, at(under.x, under.y + 8 * unit));
        const P butt = at(d.head.x - rx - 2 * unit, d.head.y + (ry >> 2));
        put(f, stick, butt, lerp(1300, 760, c[0]), turning(lerp(-260, -40, c[1]) - ((flex * 60) >> 10)));
        return at(butt.x + 6 * unit, butt.y - 5 * unit);
    }
    case tom: case fill: {
        // A rack tom, deep, held by its bracket on the L-arm of a stand: tune
        // its size, decay the felt; its head sags with bend and its muffling
        // ring lifts off as skin opens. The fill is a floor tom on three legs
        // with two rack toms on a post, smaller as spread widens their steps.
        Drum d;
        P land;
        if (model == tom) {
            d.scale = lerp(1015, 760, c[0]);
            d.head = at(body_x - 10 * unit, floor_y - 30 * unit - (((tom_depth + drum_ry) * d.scale) >> 10));
            land = drum(f, d, flex, lerp(0, 6 * unit, c[2]), c[1], tom_kind);
            const i32 rx = (drum_rx * d.scale) >> 10;
            const P clamp_at = at(d.head.x + rx + 8 * unit, floor_y - 40 * unit);   // the post stands on the base
            put(f, mount, clamp_at, unit);
            put(f, tripod, at(clamp_at.x - 10 * unit, floor_y), 700);
        } else {
            d.scale = lerp(920, 720, c[0]);
            d.head = at(body_x + 18 * unit, floor_y - 42 * unit - drum_depth * d.scale / 1024);
            land = drum(f, d, flex, 0, c[1]);
            put(f, legs, at(d.head.x, floor_y), d.scale);
        }
        const i32 lift = lerp(0, 36 * unit, c[3]);
        put(f, ring, at(d.head.x + (d.scale >> 3) * 4, d.head.y - lift), d.scale, turning(lerp(0, -140, c[3])));
        if (model == fill) {
            const i32 k1 = lerp(860, 600, c[2]), k2 = (k1 * k1) >> 10;
            const i32 s1 = (d.scale * k1) >> 10, s2 = (d.scale * k2) >> 10;
            put(f, rack, at(body_x - 60 * unit, floor_y), 2048);
            const P h1 = at(body_x - 16 * unit, 98 * unit), h2 = at(body_x - 48 * unit + lerp(0, 6 * unit, c[2]), 76 * unit);
            put(f, rack_shell2, h2, s2);
            put(f, rack_head2, h2, s2);
            put(f, rack_shell1, h1, s1);
            put(f, rack_head1, h1, s1);
        }
        return land;
    }
    case hand: {
        // A conga: tune sizes the barrel; slap raises and cups the slapping hand,
        // mute lowers the other palm onto the head; decay the felt.
        Drum d;
        d.scale = lerp(860, 680, c[0]);
        d.head = at(body_x, floor_y - 12 * unit - 100 * d.scale);
        put(f, barrel, d.head, d.scale);
        for (u32 k = 0; k < 2; ++k) put(f, stave0 + k, d.head, d.scale);
        put(f, ringstand, at(body_x, floor_y - 10 * unit));
        const P land = drum(f, d, flex, 0, c[2], bare);
        const i32 rx = (drum_rx * d.scale) >> 10;
        put(f, slap, at(d.head.x - (rx >> 2), d.head.y - lerp(10 * unit, 32 * unit, c[1]) + flex * 6),
            unit, turning(lerp(0, -300, c[1])));
        put(f, palm, at(d.head.x + (rx >> 1), d.head.y - lerp(24 * unit, 3 * unit, c[3])));
        return land;
    }
    case hat: case ride: {
        if (model == hat) {
            // decay opens the pair; metal tilts the bottom cymbal; tone sizes the cups
            const i32 size = lerp(1060, 820, c[0]);
            const P low = at(body_x, 148 * unit);
            const Turn tilt = turning(lerp(0, 80, c[3]));
            put(f, cymbal_low, low, size, tilt);
            const i32 open = lerp(3 * unit, 24 * unit, c[1]);
            const P top = at(low.x, low.y - open + flex * 2);
            const Turn wob = turning((flex * 30) >> 10);
            put(f, cymbal, top, size, wob);
            put(f, lathe, top, size, wob);
            put(f, dome, at(top.x, top.y - unit), lerp(560, 1500, c[2]));
            put(f, clutch, at(top.x, top.y - 7 * unit));
            put(f, rod, at(low.x, top.y - 12 * unit), dsp::clamp(((floor_y - top.y + 12 * unit) << 10) / (60 << 10), 200, 2048));
            put(f, tripod, at(low.x, floor_y));
            put(f, pedal, at(low.x + 16 * unit, floor_y - 2 * unit));
            return at(top.x - 44 * size, low.y - open - 5 * unit);
        }
        // the ride: bell sizes the cup, wash hangs rivets round its edge, decay
        // stacks the felts that press on it
        const i32 size = lerp(1140, 880, c[0]);
        const P top = at(body_x, 122 * unit + flex * 2);
        const Turn wob = turning(40 + ((flex * 40) >> 10));
        put(f, cymbal, top, size, wob);
        put(f, lathe, top, size, wob);
        put(f, lathe2, top, size, wob);
        put(f, dome, at(top.x, top.y - unit), lerp(480, 1650, c[2]));
        put(f, felts, at(top.x, top.y - 8 * unit), lerp(1900, 600, c[1]));
        put(f, wingnut, at(top.x, top.y - 12 * unit - lerp(4 * unit, 0, c[1])));
        const u32 rivets = (static_cast<u32>(share(c[3])) * 4 + 512) >> 10;
        for (u32 i = 0; i < 4; ++i) {
            if (i < rivets) {
                const i32 x = ((i & 1) ? 1 : -1) * (30 + 12 * static_cast<i32>(i >> 1)) * size;
                put(f, rivet0 + i, at(top.x + x, top.y - unit + static_cast<i32>(i >> 1) * 500 + ((i & 1) ? 900 : -900)));
            } else conceal(f, rivet0 + i);
        }
        stand(f, at(top.x, top.y + 3 * unit));
        return at(top.x - 42 * size, 122 * unit - 6 * unit);   // at rest: the hops stay put as it wobbles
    }
    case tine: {
        // Tubular chimes in a frame: tune sets the tubes' length, step how many
        // steps of the scale hang there, hard the hammer's head, and decay drops
        // the felt damper bar away from their ends.
        put(f, frame, at(body_x, floor_y));
        const u32 tubes = 1 + ((static_cast<u32>(share(c[2])) * 4 + 512) >> 10);
        const i32 length = lerp(1120, 680, c[0]);
        const i32 cap = 96 * unit;
        i32 lowest = cap;
        for (u32 i = 0; i < 5; ++i) {
            if (i < tubes) {
                const i32 s = (length * (1024 - 80 * static_cast<i32>(i))) >> 10;
                const i32 x = body_x - 30 * unit + static_cast<i32>(i) * 15 * unit;
                const Turn sway = turning(i == 0 ? (flex * 40) >> 10 : 0);
                put(f, tube0 + i, at(x, cap), s, sway);
                if (cap + 66 * s > lowest) lowest = cap + 66 * s;
            } else conceal(f, tube0 + i);
        }
        const i32 bar = lowest + 3 * unit + lerp(0, 14 * unit, c[1]);
        put(f, damper_bar, at(body_x - 6 * unit, bar < floor_y - 6 * unit ? bar : floor_y - 6 * unit));   // on the frame's foot at the lowest
        const P pivot = at(body_x + 44 * unit, floor_y - 4 * unit);
        const Turn t = turning(-260 + ((flex * 120) >> 10));
        put(f, hammer_shaft, pivot, unit, t);
        put(f, hammer, at(pivot.x + 48 * t.sin, pivot.y - 48 * t.cos), lerp(1650, 560, c[3]), t);
        return at(body_x - 34 * unit, cap - 5 * unit);
    }
    case bell: {
        // Alpine cowbells on a yoke: tune sizes them, pair grows the second;
        // metal flares the first one's mouth; decay wraps a tape round it. A
        // strike swings the clappers in their mouths.
        const P yoke_at = at(body_x + 2 * unit, 84 * unit);
        put(f, yoke, yoke_at, 1150);
        stand(f, at(yoke_at.x, yoke_at.y + 5 * unit));
        const i32 s = lerp(1500, 1000, c[0]);
        const Turn swing = turning((flex * 50) >> 10);
        const P b1 = at(body_x - 24 * unit, yoke_at.y + 5 * unit + 11 * s);
        put(f, strap, b1, s, swing);
        put(f, clapper, at(b1.x, b1.y + 4 * s), s, turning(-((flex * 160) >> 10)));
        put(f, cowbell, b1, s, swing);
        put(f, lipband, b1, s, swing);
        put(f, seam, b1, s, swing);
        const i32 flare = lerp(0, 6 * unit, c[3]);
        P mouth[32];
        for (u32 i = 0; i < 32; ++i) {
            const u32 phase = i * 134217728u;   // 2^32 / 32
            const i32 cx = (dsp::sine(phase + 0x40000000u) >> 16), sy = (dsp::sine(phase) >> 16);
            mouth[i] = at((((18 * s + flare) >> 5) * cx) >> 10, 38 * s + ((((3 * unit) >> 5) * sy) >> 10));
        }
        outline(f.paths[1], mouth, 32, mix_key(mix_key(2166136261u, s), flare) | 1u);
        put(f, teeth, b1, unit, swing);
        put(f, tape, at(b1.x, b1.y + 12 * s), lerp(1800, 600, c[1]), swing);
        const i32 s2 = (s * lerp(560, 1100, c[2])) >> 10;
        const P b2 = at(body_x + 30 * unit, yoke_at.y + 5 * unit + 11 * s2);
        put(f, strap2, b2, s2);
        put(f, clapper2, at(b2.x, b2.y + 4 * s2), s2);
        put(f, cowbell2, b2, s2);
        put(f, mouth2, at(b2.x, b2.y + 38 * s2), s2);
        return at(b1.x - 6 * unit, b1.y - 5 * unit);
    }
    case clock: {
        // A bracket clock: case sizes the case, tune the dial, tock the
        // pendulum's swing behind its glazed door, decay the bell on top; each
        // strike ticks the hands and swings the pendulum to its other side.
        const i32 cs = lerp(1150, 1520, c[3]);
        const P foot = at(body_x, floor_y);
        put(f, clockcase, foot, cs);
        put(f, door, foot, cs);
        const P dial_at = at(foot.x, foot.y - 42 * cs);
        const i32 throw_ = lerp(40, 240, c[2]);   // tenths of a degree
        // From the last side to the next over about 0.1 s, never a snap.
        const i32 side = (hit.count & 1) ? throw_ : -throw_;
        i32 swing_ = side;
        if (hit.since < 8820) {
            const i32 e = dsp::fraction(-static_cast<i32>((hit.since * 1371u) >> 4)) >> 21;   // easing out over about 0.1 s, Q10
            swing_ = side - ((2 * side * e) >> 10);
        }
        put(f, bob, dial_at, cs, turning((swing_ / 10) * 10));
        put(f, dial, dial_at, (lerp(780, 1180, c[0]) * cs) >> 10);
        put(f, hands, dial_at, cs, turning(static_cast<i32>(hit.count % 60u) * 60));
        const P bell_at = at(foot.x, foot.y - 74 * cs - unit);
        const i32 g = lerp(640, 1500, c[1]);
        put(f, gong, bell_at, g);
        put(f, finial, at(bell_at.x, bell_at.y - 9 * g), unit);
        return at(foot.x - 11 * g, bell_at.y - 10 * g);
    }
    case block: {
        // A woodblock on its holder: tune sizes it, hollow opens its slit, click
        // swaps the soft mallet for a hard stick, decay the felt on it.
        const i32 s = lerp(1120, 760, c[0]);
        const P center = at(body_x, 122 * unit + flex * 2);
        put(f, holder, at(body_x, center.y + 15 * s));
        stand(f, at(body_x, center.y + 15 * s + 40 * unit));
        put(f, woodblock, center, s);
        put(f, woodgrain, center, s);
        put(f, slot, at(center.x, center.y + 3 * s), lerp(320, 1250, c[2]));
        put_felt(f, at(center.x - 6 * unit, center.y - 16 * s), lerp(1700, 520, c[1]));
        const P pivot = at(center.x + 48 * s + 8 * unit, floor_y - 4 * unit);
        const Turn t = turning(-380 + ((flex * 100) >> 10));
        put(f, mallet_shaft, pivot, unit, t);
        put(f, mallet, at(pivot.x + 48 * t.sin, pivot.y - 48 * t.cos), lerp(1750, 380, c[3]));
        return at(center.x - 22 * s, 122 * unit - 15 * s - 5 * unit);
    }
    case clap: {
        // A slapstick on its sound box: tone sets the boards' length, snap
        // springs the upper board wider open to slap, room sizes the box, decay
        // the felt between the boards. Each strike slaps it shut.
        const i32 room = lerp(700, 1300, c[2]);
        const P box = at(body_x + 4 * unit, floor_y - 36 * room);
        put(f, chamber, box, room);
        put(f, soundhole, at(box.x, floor_y - 18 * room), room);
        const i32 s = lerp(1150, 840, c[0]);
        const P hinge_at = at(body_x + 62 * unit, box.y - 28 * unit);
        const i32 open = lerp(80, 320, c[3]) - ((flex * 260) >> 10);
        const Turn t = turning(open);
        put(f, knob2, at(hinge_at.x - 34 * s, hinge_at.y + 9 * unit), unit, turning(1800));
        put(f, plank_low, hinge_at, s);
        put(f, planegrain, hinge_at, s);
        put_felt(f, at(hinge_at.x - 30 * unit, hinge_at.y - 2 * unit), lerp(1300, 420, c[1]), turning(open >> 1));
        put(f, plank_up, hinge_at, s, t);
        const P up_knob = rotate(at(-34 * s, -9 * unit), t);
        put(f, knob, at(hinge_at.x + up_knob.x, hinge_at.y + up_knob.y), unit, t);
        put(f, hinge, hinge_at);
        // where the ball lands: on the upper board at rest (not as it slaps)
        const P lip = rotate(at(-92 * s, -9 * unit), turning(lerp(80, 320, c[3])));
        return at(hinge_at.x + lip.x, hinge_at.y + lip.y - 5 * unit);
    }
    case shaker: {
        // A cabasa lying on its cradle: tone sizes the cylinder, shell strings
        // more chains of beads along it, grit sizes the front chain's beads
        // (fine sand to coarse shot), decay tapes the handle. A strike turns
        // the chains a little, as a player's twist does.
        const i32 s = lerp(1250, 900, c[0]);
        const P end = at(body_x - 56 * unit, 128 * unit);      // the open end's centre
        const i32 rattle = (flex * 3) >> 10;
        const i32 len = 60 * s, R = 24 * s;
        put(f, cylinder, end, s);
        put(f, cap, end, s);
        const u32 rows = (static_cast<u32>(share(c[2])) * 3 + 512) >> 10;
        const i32 places[3] = {800, 565, 95};   // shares of the length: with the front chain's, evenly spaced at the most
        for (u32 i = 0; i < 3; ++i) {
            if (i < rows) put(f, chain0 + i, at(end.x + ((len >> 10) * places[i]) + ((i & 1) ? -rattle : rattle) * unit, end.y), s);
            else conceal(f, chain0 + i);
        }
        // The front chain: n round beads, fewer and bigger with grit.
        const i32 n = 12 - ((share(c[3]) * 5) >> 10);
        const i32 bump = 2200 + ((share(c[3]) * 2400) >> 10);
        const i32 Rx = (7 * s * 1106) >> 10, Ry = (R * 1106) >> 10;   // the loop's half-depth and half-height, 1.08 out
        P pts[64];
        u32 k = 0;
        // A point on the loop at phase (from its top), d (Q10 px) out along its normal.
        auto loop = [&](u32 phase, i32 d) {
            const i32 cs_ = dsp::sine(phase + 0x40000000u) >> 16, sn = dsp::sine(phase) >> 16;   // cos a, sin a (Q15)
            const i32 nx = -(((Ry >> 6) * cs_) >> 15), ny = ((Rx >> 6) * sn) >> 15;              // the normal, Q4 px
            const i32 l = static_cast<i32>(dsp::isqrt(static_cast<u32>(nx * nx + ny * ny)));
            const i32 lx = l ? l : 1;
            return at(-(((Rx >> 5) * cs_) >> 10) + (d * nx) / lx, (((Ry >> 5) * sn) >> 10) + (d * ny) / lx);
        };
        const u32 step = dsp::udiv(0x80000000u, static_cast<u32>(n));   // pi / n
        for (i32 j = 0; j <= n; ++j)
            for (u32 q = 0; q < (j < n ? 3u : 1u); ++q)
                pts[k++] = loop(0xc0000000u + static_cast<u32>(j) * step + (q == 0 ? 0 : q == 1 ? (step * 3) / 10 : (step * 7) / 10),
                                q ? bump : 400);
        for (i32 j = 6; j >= 0; --j) pts[k++] = loop(0xc0000000u + static_cast<u32>(j) * 0x15555555u, -500);   // back along the string
        outline(f.paths[1], pts, k, mix_key(mix_key(2166136261u, n), s ^ (bump << 12)) | 1u);
        put(f, teeth, at(end.x + ((len >> 10) * 330) - rattle * unit, end.y));
        const P grip_at = at(end.x + len + 7 * s, end.y);
        put(f, grip, grip_at);
        put(f, tape, at(grip_at.x + 22 * unit, grip_at.y - 3 * unit), lerp(1700, 520, c[1]), turning(-900));
        put(f, cradle, at(grip_at.x + 14 * unit, grip_at.y + 6 * unit));
        stand(f, at(grip_at.x + 14 * unit, grip_at.y + 17 * unit));
        return at(end.x + 20 * s, end.y - R - 6 * unit);
    }
    case ratchet: default: {
        // A watchman's ratchet: teeth cuts its gear, tune sizes it; the rails
        // hold the tongue in their slot, its tip on the teeth, thicker for
        // wood; decay lays the felt on the rails. Each strike turns the gear a
        // tooth and springs the tongue.
        const u32 count = 1 + ((static_cast<u32>(share(c[2])) * 11 + 512) >> 10);
        const i32 r = lerp(32 * unit, 21 * unit, c[0]);
        P pts[60];
        for (u32 i = 0; i < 60; ++i) {
            const u32 phase = i * 71582788u;                         // 2^32 / 60
            const u32 tooth = phase * count;                          // the saw's phase across a tooth
            const i32 rad = r + (((5 << 10) * static_cast<i32>(tooth >> 22)) >> 10);
            pts[i] = at((rad * (dsp::sine(phase + 0x40000000u) >> 16)) >> 15, (rad * (dsp::sine(phase) >> 16)) >> 15);
        }
        outline(f.paths[1], pts, 60, mix_key(mix_key(2166136261u, static_cast<i32>(count)), r) | 1u);
        const P hub_at = at(body_x - 26 * unit, 124 * unit);
        const u32 turns = hit.count % count;
        put(f, teeth, hub_at, unit, turning(static_cast<i32>(dsp::udiv(turns * 3600u, count))));
        put(f, handle, at(hub_at.x, hub_at.y + 4 * unit));
        stand(f, at(hub_at.x, hub_at.y + 54 * unit));
        put(f, frame_r, hub_at);
        put(f, slot_r, hub_at);
        put(f, hub, hub_at);
        // the tongue in the slot, from the end block to the teeth, bowed by the strike
        const P root = at(hub_at.x + 70 * unit, hub_at.y + unit);
        const P tip = at(hub_at.x + r + 2 * unit, hub_at.y + unit);
        const i32 w0 = lerp(700, 3200, c[3]), bow = -(flex * 4);
        const P mid = at((root.x + tip.x) >> 1, root.y);
        slat(f.paths[0], mid, root, tip, w0, 600, bow, mix_key(mix_key(mix_key(2166136261u, r), w0), bow >> 7) | 1u);
        put(f, head, mid);
        put_felt(f, at(hub_at.x + 40 * unit, hub_at.y - 13 * unit), lerp(1300, 420, c[1]));
        return at(hub_at.x - (r >> 1), hub_at.y - r - 7 * unit);
    }
    }
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.initialized = 0;
    state.age = 1 << 20;
    state.clock = 0;
    state.planned = 0;
    state.shown = 0;
    state.lit = 0;
    state.swing = 0;
    state.learner.reset();
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& f) {
    using namespace view;
    using namespace art;
    const u32 model = input.model < kit::models ? input.model : 0;
    u32 c[8];
    for (u32 i = 0; i < 8; ++i) c[i] = input.slots[i] < 0 ? 0 : static_cast<u32>(input.slots[i]) >> 24;
    const i32 space = input.pad[0], feel = input.pad[1];

    // A track change or the first frame is never a note.
    const bool baseline = !state.initialized || state.track != input.track;
    const bool fresh = !baseline && input.note_serial != state.serial;
    const i32 dt = dsp::clamp(input.elapsed, 0, 1024);
    state.serial = input.note_serial;
    state.track = input.track;
    state.initialized = 1;
    state.notes.update(input);
    state.clock += static_cast<u32>(dt) * 44100u >> 10;
    if (baseline) state.age = 1 << 20;
    else if (fresh) {
        state.age = 0;
        state.learner.note(state.clock, true);
    } else state.age = dsp::clamp(state.age + dt, 0, 1 << 20);
    if (state.shown != model + 1) {
        state.shown = model + 1;
        for (u32 p = 0; p < Frame::path_count; ++p) f.paths[p].key = 0;
    }
    // Paths not drawn this frame still need a shape: a small ribbon, parked.
    for (u32 p = 0; p < Frame::path_count; ++p)
        if (!f.paths[p].key) {
            const P ends[4] = {{-20480, 0}, {20480, 0}, {20480, 1024}, {-20480, 1024}};
            outline(f.paths[p], ends, 4, 0x7fffffffu - p);
        }

    // The run, laid out again when the key's motion, its pads or the beat change.
    const u32 p16 = state.learner.p16 ? state.learner.p16 : tempo::start;
    const motion::Feel feeling = motion::feel(feel);
    u32 key = 2166136261u;
    for (u32 i = 4; i < 8; ++i) key = mix_key(key, static_cast<i32>(c[i]));
    key = mix_key(mix_key(mix_key(key, space), feel), static_cast<i32>(p16)) | 1u;
    if (key != state.planned) {
        motion::plan(state.run, c[4], c[5], c[6], c[7], space, feeling, p16);
        state.planned = key;
    }
    const motion::Run& run = state.run;

    // A kit's voice plays until its own sound ends: nothing on the track (its
    // amp envelope, a note let go) cuts the run, so the screen plays it out.
    const u32 t = samples(state.age);
    const u32 rest = motion::rest(run);
    // A travel too quick to see is shown over min_travel, and all after it as much later.
    Travel travel;
    travel.shown = !input.reduced_motion && run.at[run.n] < min_travel ? min_travel : 0;
    travel.late = travel.shown ? travel.shown - run.at[run.n] : 0;
    const bool running = state.age < (1 << 19) && t < rest + travel.late + 13230;     // 0.3 s after the last ball rests

    // The board, turned about the hinge by tilt: 3 .. 17 degrees.
    Board board;
    board.tenths = 30 + static_cast<i32>((c[4] * 140u * 129u) >> 14);
    board.turn = turning(board.tenths);
    board.foot = at(foot_x, foot_y);
    put(f, art::board, board.foot, unit, board.turn);
    put(f, grain, board.foot, unit, board.turn);
    put(f, lining, board.foot, unit, board.turn);
    put(f, ferrule_head, board.foot, unit, board.turn);
    put(f, ferrule_foot, board.foot, unit, board.turn);
    put(f, index, board.foot, unit, board.turn);
    put(f, cup, board.foot, unit, board.turn);
    put(f, catch_post, board.foot, unit, board.turn);
    // The peg holding the head end, and its thumbscrew.
    const P head_end = board.local_at(-beam, board_t);
    put(f, peg, at(post_x, head_end.y));
    put(f, screw, at(post_x - 9 * unit, head_end.y + 2 * unit));
    // The catch: across the groove at rest; a note trips it up, and it falls back.
    {
        i32 lift = 0;
        if (state.age < 400) lift = state.age < 60 ? (state.age * 1024) / 60 : 1024 - ((state.age - 60) * 1024) / 340;
        const i32 local = 680 - ((lift * 1100) >> 10);   // tenths: down across the groove .. up and clear
        const i32 gx = -((beam >> 4) * ((65536 - catch_s) >> 4) >> 8);
        put(f, catcher, board.local_at(gx, -12 * unit), unit, turning(board.tenths + local));
    }

    // The bells: their gallows at (i/n)^k along the run, at least 10 px apart.
    i32 places[16];
    const u32 n = run.n;
    for (u32 i = 0; i < n; ++i) {
        i32 frac = 0;
        if (i) {
            const i32 l = table::log2_whole[i] - table::log2_whole[n];
            frac = static_cast<i32>(dsp::exp2(dsp::multiply(l, run.space << 14) * 4 + 16 * dsp::octave));   // (i/n)^k
        }
        places[i] = run_start + ((((run_end - run_start) >> 4) * frac) >> 12);
        if (i && places[i] < places[i - 1] + gap) places[i] = places[i - 1] + gap;
    }
    for (i32 i = static_cast<i32>(n) - 1; i >= 0; --i) {
        const i32 limit = i == static_cast<i32>(n) - 1 ? run_end - gap : places[i + 1] - gap;
        if (places[i] > limit) places[i] = limit;
    }
    for (u32 i = 0; i < 16; ++i) {
        if (i >= n) { conceal(f, gallows0 + i); conceal(f, bell0 + i); continue; }
        put(f, gallows0 + i, board.along(places[i], 0), unit, board.turn);
    }

    // The instrument and where the ball lands on it.
    hide_bodies(f);
    Strike hit = {0, 0, 0xffffffu};
    if (running) {
        const u32 balls = run.m < 6 ? run.m : 6;
        for (u32 b = 0; b < balls; ++b) {
            motion::Cursor cur;
            motion::begin(cur, run, b, 0);
            while (!cur.done && cur.next + travel.late <= t) {
                const motion::Impact h = motion::take(cur, run, b, 0);
                if (h.kind == motion::fret) continue;
                ++hit.count;
                const u32 since = t - (h.at + travel.late);
                if (since < hit.since) hit.since = since;
                if (since < 7938) {   // 0.18 s
                    const i32 ease = dsp::fraction(-static_cast<i32>((since * 1902u) >> 4)) >> 21;   // exp(-since / 50 ms), Q10
                    const i32 v = (dsp::clamp(h.v, 0, 32767) * ease) >> 15;
                    if (v > hit.flex) hit.flex = v;
                }
            }
        }
        hit.flex = (hit.flex >> 7) << 7;   // eighths: fewer reshapes
    }
    const Fit fit = fit_of(model);
    const P land = magnified(scene_body(f, model, c, hit), fit);
    magnify(f, fit);

    // The ball's path as a stroboscope would catch it: a dot each equal moment
    // of its fall from the lip (wider apart and larger as it speeds up), then of
    // each hop (smaller as the hops die away). The dots the ball has passed are
    // lit, so the path reads in its direction; once it rests they go out again
    // a few a frame.
    const P lip = board.along(run_end + 1500, 5 * unit);
    P hop_dot[7 + 24];
    i32 hop_size[7 + 24];
    u32 hop_hop[24], hop_phase[24];
    for (u32 i = 0; i < 7; ++i) {
        const i32 u = static_cast<i32>((i + 1) * 1024 / 8);
        const P where = flight(lip, land, u);
        hop_dot[i] = where;
        hop_size[i] = 820 + ((u * 400) >> 10);
    }
    // The hops over the instrument: higher for a steeper plane, longer for more bounce.
    const i32 e = table::restitution[c[6]];          // Q15
    const i32 h0 = (30 * unit) * (461 + ((563 * share(c[4])) >> 10)) >> 10;
    i32 hop_x[8], hop_w[8], hop_h[8];
    u32 hops = 0, dots = 0;
    {
        i32 x = land.x;
        const i32 e2 = (e * e) >> 15;
        i32 h = (h0 * e2) >> 15, w = (34 * unit) * e >> 15, size = 1024;
        while (hops < 8 && e > 983 && h >= 2 * unit && w >= 6 * unit && x + w < 470 * unit) {
            hop_x[hops] = x; hop_w[hops] = w; hop_h[hops] = h;
            const i32 steps = dsp::clamp((w >> 10) * 2 / 9, 3, 8);
            for (i32 q = 1; q < steps && dots < 24; ++q) {
                const i32 u = (q << 10) / steps;
                hop_dot[7 + dots] = at(x + ((w * u) >> 10), land.y - ((4 * h >> 10) * ((u * (1024 - u)) >> 10)));
                hop_size[7 + dots] = size;
                hop_hop[dots] = hops + 1;
                hop_phase[dots] = static_cast<u32>(u) << 6;
                ++dots;
            }
            x += w;
            ++hops;
            h = (h * e2) >> 15;
            w = (w * e) >> 15;
            size = dsp::clamp((size * 15) >> 4, 800, 1024);
        }
    }
    // The path a shown travel follows: along the board from the catch to the
    // lip, then the fall's eight stretches between the dots, to the landing.
    const i32 start_s = catch_s - 1400, lip_s = run_end + 1500;
    travel.board = (lip_s - start_s) << 2;
    travel.total = travel.board;
    {
        P from = lip;
        for (u32 k = 0; k < 8; ++k) {
            const P to = k < 7 ? hop_dot[k] : land;
            travel.seg[k] = span(from, to);
            travel.total += travel.seg[k];
            from = to;
        }
    }
    // Where along a shown travel a ball is, d (Q10 px) from the catch.
    auto travelled = [&](i32 d, i32 lift) -> P {
        if (d < travel.board) return board.along(start_s + (d >> 2), lift);
        d -= travel.board;
        P from = lip;
        for (u32 k = 0; k < 8; ++k) {
            const P to = k < 7 ? hop_dot[k] : land;
            if (d < travel.seg[k] && travel.seg[k] > 0) {
                const i32 u = static_cast<i32>(dsp::udiv(static_cast<u32>(d) << 6, static_cast<u32>(travel.seg[k]) >> 4));   // Q10
                return at(from.x + (((to.x - from.x) >> 4) * u >> 6), from.y + (((to.y - from.y) >> 4) * u >> 6));
            }
            d -= travel.seg[k];
            from = to;
        }
        return land;
    };
    // How far along a shown travel ball b is at t (Q10 px), or -1 where it isn't travelling.
    auto shown_at = [&](u32 b) -> i32 {
        if (!travel.shown || t < run.ball[b].offset || t - run.ball[b].offset >= travel.shown) return -1;
        return static_cast<i32>((static_cast<u32>(travel.total >> 4) * (eased(t - run.ball[b].offset, travel.shown) >> 4)) >> 8);
    };

    // The bells: the four struck last swing, lit, for 0.22 s. On a shown
    // travel each is struck as the ball passes it.
    u32 struck[16];
    u32 latest = 0;
    for (u32 i = 0; i < n; ++i) {
        struck[i] = travel.shown ? eased_time((places[i] - start_s) << 2, travel) : run.at[i];
        if (struck[i] <= t) latest = i + 1;
    }
    for (u32 i = 0; i < n; ++i) {
        const P crown = board.along(places[i], gallows_h - 1300);
        i32 angle = 0;
        if (running && struck[i] <= t && i + 4 >= latest) {
            const u32 since = t - struck[i];
            if (since < 9702) {
                const i32 fade = dsp::fraction(-static_cast<i32>((since * 1371u) >> 4));   // exp(-since / 70 ms)
                const i32 wave = dsp::sine(since * 697316u + 0x40000000u);
                angle = dsp::multiply(dsp::multiply(380, fade), wave);
                angle = (angle / 10) * 10;
            }
        }
        if (angle) put_shaded(f, bell0 + i, crown, 0, unit, turning(angle));
        else put_shaded(f, bell0 + i, crown, 1);
    }

    // How far along its path the first ball is: the dots it has passed.
    u32 passed = 0;
    const i32 first = running ? shown_at(0) : -1;
    if (first >= 0) {
        i32 reach = travel.board;
        for (u32 k = 0; k < 7; ++k) {
            reach += travel.seg[k];
            if (reach <= first) passed = k + 1;
        }
    } else if (running) {
        const motion::Place p = motion::where(run, 0, t - travel.late);
        if (p.state == 1 && p.along >= 61000) {
            const u32 u = (p.along - 61000) * 1024u / 4536u;
            for (u32 i = 0; i < 7; ++i) if (static_cast<u32>((i + 1) * 1024 / 8) <= u) passed = i + 1;
        } else if (p.state >= 2) {
            passed = 7;
            for (u32 j = 0; j < dots; ++j)
                if (p.state == 3 || hop_hop[j] < p.hop || (hop_hop[j] == p.hop && hop_phase[j] <= (p.phase & 0xffffu))) passed = 8 + j;
        }
    }
    // Lit at once as the ball passes; dark again three a frame.
    if (passed >= state.lit) state.lit = passed;
    else state.lit = state.lit > passed + 3 ? state.lit - 3 : passed;
    for (u32 i = 0; i < 7 + 24; ++i) {
        const u32 n = i < 7 ? fall0 + i : hop0 + (i - 7);
        if (i >= 7 + dots) { conceal(f, n); continue; }
        put_shaded(f, n, hop_dot[i], i < state.lit ? 4 : 3, i < state.lit ? (hop_size[i] * 5) >> 2 : hop_size[i]);
    }

    // The handful in the cup: the balls still waiting.
    u32 waiting = run.m - 1;
    if (running) {
        waiting = 0;
        for (u32 b = 1; b < run.m; ++b) if (run.ball[b].offset > t) ++waiting;
    }
    const u32 heaped = waiting > 8 ? 8 : waiting;
    for (u32 i = 0; i < 8; ++i) {
        if (i < heaped) {
            const i32 row = heap_slots[i][1];
            const i32 x = cup_x - 4 * unit + heap_slots[i][0] * 4500 + (row & 1) * 2250;
            put(f, heap0 + i, board.local_at(x, -(3700 + row * 3800)));
        } else conceal(f, heap0 + i);
    }
    // The ball and the shot in flight.
    bool glinted = false;
    for (u32 b = 0; b < 6; ++b) {
        const u32 node_ = b ? shot0 + b - 1 : ball;
        const i32 lift = b ? 3 * unit : 5400;
        if (b && (b >= run.m || !running)) { conceal(f, node_); continue; }
        if (!running) {
            const P rest_at = board.along(catch_s - 1400, lift);
            put_shaded(f, ball, rest_at, 2);
            put(f, glint, rest_at);
            glinted = true;
            continue;
        }
        const i32 d = shown_at(b);
        const motion::Place p = motion::where(run, b, t > travel.late ? t - travel.late : 0);
        P where;
        if (d >= 0) where = travelled(d, lift);
        else if (p.state == 0 || t < run.ball[b].offset) {
            if (b) { conceal(f, node_); continue; }
            where = board.along(catch_s - 1400, lift);
        } else if (p.state == 1) {
            // On the board to the lip, then falling along the dotted arc.
            const i32 s = run_start + (((run_end + 1500 - run_start) >> 4) * static_cast<i32>(p.along >> 4) >> 8);
            if (p.along < 61000) where = board.along(s, lift);
            else {
                const i32 u = static_cast<i32>((p.along - 61000) >> 2) * 1024 / 1134;
                where = flight(lip, land, u);
            }
        } else if (p.state == 2 && hops) {
            const u32 j = p.hop - 1 < hops ? p.hop - 1 : hops - 1;
            const i32 u = static_cast<i32>(p.phase >> 6);
            where = at(hop_x[j] + ((hop_w[j] * u) >> 10), land.y - ((4 * hop_h[j] >> 10) * ((u * (1024 - u)) >> 10)));
        } else {
            where = hops ? at(hop_x[hops - 1] + hop_w[hops - 1], land.y) : land;
        }
        if (b) put(f, node_, where);
        else {
            put_shaded(f, ball, where, 2);
            put(f, glint, where);
            glinted = true;
        }
    }
    if (!glinted) conceal(f, glint);

    // The pendulum: it swings the beat while the ball runs (locked), easing in
    // and out over about 0.17 s, and is tied back to the post on FREE.
    const i32 target = running && feeling.locked ? 140 : 0;  // tenths of a degree
    const i32 step = (dt * 140 * 6) >> 10;
    state.swing += dsp::clamp(target - state.swing, -step - 1, step + 1);
    i32 angle = 0;
    if (!feeling.locked) {
        angle = -190;                                 // tied back against the post
        put(f, tie, at(pivot_x + 13 * unit, pivot_y + 40 * unit));
    } else {
        conceal(f, tie);
        if (state.swing && running) {
            const u32 beats = dsp::udiv(t << 8, 4 * p16);             // Q8
            angle = dsp::multiply(state.swing, dsp::sine(beats << 23));   // half a cycle a beat
            angle = (angle / 10) * 10;
        }
    }
    put(f, pendulum, at(pivot_x, pivot_y), unit, turning(angle));

    // The header: both rows, the edited one bright; Shift shows its controls on both.
    const char* titles[8];
    char values[8][opxy::draw::header_capacity];
    for (u32 i = 0; i < 8; ++i) {
        titles[i] = kit::model[model].labels[i];
        caption::value(values[i], opxy::draw::header_capacity, model, i, c[i], feel);
    }
    if (input.shift) {
        char pan[8], level[8], sends[2][4], space_text[8], feel_text[8];
        const i32 pv = input.pan, lv = input.level;
        if (pv < 0) caption::number(pan, sizeof(pan), "-", static_cast<u32>(-pv), "");
        else caption::number(pan, sizeof(pan), "", static_cast<u32>(pv), "");
        if (lv < 0) caption::number(level, sizeof(level), "-", static_cast<u32>(-lv), "dB");
        else caption::number(level, sizeof(level), "", static_cast<u32>(lv), "dB");
        for (u32 s = 0; s < 2; ++s) {
            const i32 code = input.pad[2 + s];
            if (code > 0) caption::number(sends[s], sizeof(sends[s]), "", static_cast<u32>((((code > 7 ? 7 : code) - 1) * 100 + 3) / 6), "");
            else caption::put(sends[s], sizeof(sends[s]), "TRK");
        }
        caption::space(space_text, sizeof(space_text), space);
        caption::feel(feel_text, sizeof(feel_text), feel);
        const char* const shift_values[2][4] = {{kit::model[model].name, chokes[input.choices[2] & 3u], pan, level},
                                                {space_text, feel_text, sends[0], sends[1]}};
        // Both rows at once, stacked as the pages: the edited one bright on
        // the bar, the other grey, so Shift swaps all eight names and values.
        for (u32 line = 0; line < 2; ++line)
            for (u32 col = 0; col < 4; ++col) {
                titles[4 * line + col] = shift_titles[line][col];
                caption::put(values[4 * line + col], opxy::draw::header_capacity, shift_values[line][col]);
            }
        titles[0] = kit::groups[kit::model[model].group];
    }
    opxy::draw::page_header(state.header, input, f, titles, values, header_node);
}
}
