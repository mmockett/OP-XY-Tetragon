#include "display.h"

// CURIO's screen. The selected key's specimen stands alone, centred below the header;
// each control moves a part of it (art.h) and a hit makes it do its thing:
// the ball drops, the mallet swings, the drop falls, the wings flick. A hit's
// motion lasts as long as its sound, the specimen's own decay: a drum kit's
// voices play until their sound ends, untouched by the track's amp envelope
// and by note-off (the drum renderer's own amp setter reads neither), so the
// screen follows neither. Only the selected key's
// notes reach the screen (the SDK's per-key serial), and only its specimen is
// drawn.
namespace curio {
namespace view {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;
const i32 idle = -1;
const i32 u = 1024;   // Q10

// ---------------------------------------------------------------- captions
inline void put(char* out, u32 cap, const char* s) { copy(out, cap, s); }
inline u32 digits(char* t, u32 n, u32 v) {
    char b[10];
    u32 k = 0;
    do { b[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && k < 10);
    while (k) t[n++] = b[--k];
    return n;
}
// Hertz: "54Hz" under 100, "880" under 1000, "1.5k" under 10k, then "14k".
inline void hertz_text(char* out, u32 cap, u32 hz) {
    char t[12];
    u32 n = 0;
    if (hz < 100) { n = digits(t, 0, hz); t[n++] = 'H'; t[n++] = 'z'; }
    else if (hz < 1000) n = digits(t, 0, hz);
    else if (hz < 10000) { const u32 d = (hz + 50) / 100; n = digits(t, 0, d / 10); t[n++] = '.'; t[n++] = static_cast<char>('0' + d % 10); t[n++] = 'k'; }
    else { n = digits(t, 0, (hz + 500) / 1000); t[n++] = 'k'; }
    t[n] = 0;
    put(out, cap, t);
}
// Times: "38ms" under 100 ms, "0.31s" under 1 s, "1.2s" under 10 s, then "12s".
inline void time_text(char* out, u32 cap, u32 us) {
    char t[12];
    u32 n = 0;
    const u32 ms = (us + 500) / 1000;
    if (ms < 100) { n = digits(t, 0, ms); t[n++] = 'm'; t[n++] = 's'; }
    else if (ms < 1000) { const u32 c = (ms + 5) / 10; t[n++] = '0'; t[n++] = '.'; t[n++] = static_cast<char>('0' + c / 10 % 10); t[n++] = static_cast<char>('0' + c % 10); t[n++] = 's'; }
    else if (ms < 10000) { const u32 d = (ms + 50) / 100; n = digits(t, 0, d / 10); t[n++] = '.'; t[n++] = static_cast<char>('0' + d % 10); t[n++] = 's'; }
    else { n = digits(t, 0, (ms + 500) / 1000); t[n++] = 's'; }
    t[n] = 0;
    put(out, cap, t);
}
// A control's caption, from its Q31 value.
inline void caption(u32 model, u32 slot, i32 value, char* out, u32 cap) {
    const kit::Caption& k = kit::model[model].caption[slot];
    const i32 x = (value < 0 ? 0 : value) >> 19;   // Q12
    switch (k.kind) {
    case kit::hertz: hertz_text(out, cap, dsp::ex2(dsp::lg2(static_cast<u32>(k.a)) + (k.b * x >> 12))); break;
    case kit::linear_hz: hertz_text(out, cap, static_cast<u32>(k.a + (k.b * x >> 12))); break;
    case kit::time: time_text(out, cap, static_cast<u32>(k.a + dsp::sc(k.b, dsp::pw(x, k.e)))); break;
    case kit::count: {
        char t[8];
        const u32 n = digits(t, 0, static_cast<u32>(k.a + (k.b * (k.e == 4096 ? x : dsp::pw(x, k.e)) >> 12)));
        t[n] = 0;
        put(out, cap, t);
        break;
    }
    default: header_amount(out, cap, value); break;
    }
}
// A whole number and its unit, as stock shows a key's pan and level: no plus sign.
inline void signed_text(char* out, u32 cap, i32 v, const char* unit) {
    char t[12];
    u32 n = 0;
    if (v < 0) { t[n++] = '-'; v = -v; }
    n = digits(t, n, static_cast<u32>(v));
    for (u32 i = 0; unit[i] && n + 1 < sizeof(t); ++i) t[n++] = unit[i];
    t[n] = 0;
    put(out, cap, t);
}

// ------------------------------------------------------------ time and life
// How long a hit of this specimen sounds, ms, from its controls (Q12): the
// motion follows it. Close to the engine's own decays, rounded generously.
inline i32 sound_ms(u32 m, const i32* c) {
    using dsp::pw;
    using dsp::sc;
    switch (m) {
    case 0: return 4 * (60 + sc(1100, pw(c[4], 6554))) + sc(600, c[1]);
    case 1: return 4 * (40 + sc(900, pw(4095 - c[1], 9011))) + sc(150, c[7]);
    case 2: return dsp::mul(120 + sc(1600, pw(c[3], 6144)), dsp::F(1.0) - sc(dsp::F(0.5), c[6])) + 60;
    case 3: return 300 + sc(300, c[3]) + sc(1500, c[2]) + sc(230, c[7]);
    case 4: return 3 * (20 + sc(310, pw(c[4], 6554))) + sc(500, c[7]);
    case 5: return 150 + sc(3200, pw(c[3], 6554)) + sc(900, c[7]) + sc(700, c[4]);
    case 6: return 3 * (22 + sc(480, pw(c[3], 8192))) + 100;
    case 7: return 300 + sc(500, c[2]) + sc(300, c[7]) + sc(50, c[5]);
    case 8: return 60 + 4 * (12 + sc(400, pw(c[4], 6963))) + sc(250, c[0]);
    case 9: return 150 + (1 + (c[0] * 15 >> 12)) * 1000 / (10 + sc(70, c[1]));
    case 10: return 160 + sc(500, c[1]) + sc(300, c[7]);
    case 11: return 20 + sc(100, c[3]) + 3 * (40 + sc(300, c[1]));
    case 12: return dsp::mul(dsp::mul(350 + sc(4000, pw(c[2], 6144)), dsp::F(1.0) - sc(dsp::F(0.4), c[0])), dsp::F(1.0) - sc(dsp::F(0.6), c[6])) + sc(1500, c[4]);
    case 13: return (1 + (c[0] * 11 >> 12)) * 1000 / (16 + sc(10, c[0])) + dsp::mul(80 + sc(1400, pw(c[4], 5734)), dsp::F(1.0) - sc(dsp::F(0.7), c[2]));
    case 14: return 80 + sc(300, c[4]);
    default: return 300 + sc(2200, c[4]);
    }
}
// The hit's life: the specimen's own sound.
inline i32 life_ms(u32 m, const i32* c) { return clamp(sound_ms(m, c), 60, 6000); }

// The hit's shapes over its age (ms), Q12: a kick (up in 30 ms, then halving
// every 35 ms, gone by 300 ms), a ring that dies away over the life, and the
// share of the life gone.
inline i32 kick(i32 age) {
    if (age < 0 || age >= 300) return 0;
    if (age < 30) return age * 4096 / 30;
    return static_cast<i32>(dsp::ex2((12 << 12) - (age - 30) * 117)) ;
}
inline i32 ring(i32 age, i32 life) {
    if (age < 0 || age >= life) return 0;
    const i32 left = 4096 - age * 4096 / life;
    return left * left >> 12;
}
inline i32 gone(i32 age, i32 life) { return age < 0 ? 0 : age >= life ? 4096 : age * 4096 / life; }
// A whole-pixel wobble: +-size, flipping every 45 ms, dying away.
inline i32 wobble(i32 age, i32 life, i32 size) {
    const i32 a = (size * ring(age, life) + (size < 0 ? -2048 : 2048)) >> 12;
    return (age / 45) & 1 ? -a : a;
}

// ----------------------------------------------------------- reshaping paths
using Path = Frame::path_type;
// An ellipse drawn as a thin ring: 32 points around the outside, 32 back
// around the inside, w apart (Q10); lift[i] moves each pair down (Q10).
struct Outline {
    Path* path;
    i32 pts;
};
inline void ellipse_ring(Path& p, i32 rx, i32 ry, i32 w, const i32* lift) {
    for (u32 i = 0; i < 32; ++i) {
        const i32 tenths = static_cast<i32>(i) * 3600 / 32;
        const i32 cs = opxy::draw::sine(tenths + 900), sn = opxy::draw::sine(tenths);
        const i32 y = lift ? lift[i] : 0;
        p.points[i].x = cs * rx;
        p.points[i].y = sn * ry + y;
        p.points[63 - i].x = cs * (rx * u - w) >> 10;
        p.points[63 - i].y = (sn * (ry * u - w) >> 10) + y;
    }
}
// A line as a thin ribbon through 32 points (Q10), w wide (Q10).
inline void band(Path& p, const Point* c, i32 w) {
    for (u32 i = 0; i < 32; ++i) {
        const Point& a = c[i > 0 ? i - 1 : 0];
        const Point& b = c[i < 31 ? i + 1 : 31];
        i32 dx = b.x - a.x, dy = b.y - a.y;
        // A normal of length about w / 2, without a square root: scale by the larger part.
        const i32 m = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        i32 nx = 0, ny = w / 2;
        if (m > 0) { nx = -dy * (w / 2) / m; ny = dx * (w / 2) / m; }
        p.points[i].x = c[i].x + nx;
        p.points[i].y = c[i].y + ny;
        p.points[63 - i].x = c[i].x - nx;
        p.points[63 - i].y = c[i].y - ny;
    }
}
// A whole-number square root, for small numbers.
inline i32 root(i32 v) {
    i32 r = 0;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}
// 32 points along a polyline of n corners (2..32): every corner kept, the
// rest shared out along its segments by their lengths.
inline void resample(const Point* c, i32 n, Point* out) {
    i32 len[31], total = 0;
    for (i32 s = 0; s + 1 < n; ++s) {
        // Its length, near enough without a square root: the longer side and half the shorter.
        i32 dx = (c[s + 1].x - c[s].x) >> 6, dy = (c[s + 1].y - c[s].y) >> 6;
        dx = dx < 0 ? -dx : dx;
        dy = dy < 0 ? -dy : dy;
        len[s] = (dx > dy ? dx + dy / 2 : dy + dx / 2) + 1;
        total += len[s];
    }
    i32 spare = 32 - n, at = 0, used = 0, acc = 0;
    for (i32 s = 0; s + 1 < n; ++s) {
        acc += len[s];
        const i32 want = spare * acc / total;
        const i32 extra = want - used;
        used = want;
        for (i32 q = 0; q <= extra; ++q) {
            out[at].x = c[s].x + (c[s + 1].x - c[s].x) * q / (extra + 1);
            out[at].y = c[s].y + (c[s + 1].y - c[s].y) * q / (extra + 1);
            ++at;
        }
    }
    out[at] = c[n - 1];
}
// A liquid's surface: 32 points across 2 * half px, waving `waves` times
// across at most as steep as the device can keep points apart.
inline void surface(Point* pts, i32 half, i32 amp, i32 waves, i32 phase) {
    const i32 most = half * u / (3 * waves);
    if (amp > most) amp = most;
    for (u32 i = 0; i < 32; ++i) {
        pts[i].x = (static_cast<i32>(i) * 2 * half * u / 31) - half * u;
        pts[i].y = (opxy::draw::sine(static_cast<i32>(i) * 3600 * waves / 31 + phase) * amp) >> 10;
    }
}
inline u32 key_of(const Path& p) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < 64; ++i) { h = (h ^ static_cast<u32>(p.points[i].x)) * 16777619u; h = (h ^ static_cast<u32>(p.points[i].y)) * 16777619u; }
    return h | 1u;
}
inline void park(Frame& frame, u32 p) {
    for (u32 i = 0; i < 64; ++i) {
        frame.paths[p].points[i].x = opxy::draw::sine(static_cast<i32>(i) * 3600 / 64 + 900) * 10;
        frame.paths[p].points[i].y = opxy::draw::sine(static_cast<i32>(i) * 3600 / 64) * 10;
    }
    frame.paths[p].key = 0x51u + p * 2;
    hide(frame, p);
}
// Shows a reshaped outline drawn about (x, y), Q10. One wider than the
// outlines' 64 px reach is drawn at half size and shown at twice it, in a
// palette entry of half the stroke (7: the hero's, 8: structure's). Each is
// moved by its specimen's offset to the screen's middle (art.h's ox, oy).
// Any corner of an outline turning sharper than 150 degrees eased onto the
// midpoint of its neighbours: the OP-XY strokes with mitred joins and no mitre
// limit, so a turn near straight back (a ribbon folding at a bend) draws a spike.
inline void soften(Path& p) {
    for (u32 pass = 0; pass < 12; ++pass) {   // until none is left, as a rule in one or two
        bool eased = false;
        for (u32 i = 0; i < 64; ++i) {
            const Point& a = p.points[(i + 63) % 64];
            Point& b = p.points[i];
            const Point& c = p.points[(i + 1) % 64];
            const i32 ux = (b.x - a.x) >> 4, uy = (b.y - a.y) >> 4, vx = (c.x - b.x) >> 4, vy = (c.y - b.y) >> 4;
            const i32 dot = ux * vx + uy * vy, cross = ux * vy - uy * vx;
            if (dot < 0 && (cross < 0 ? -cross : cross) * 7 < -dot * 4) {   // tan 30 degrees, about 4/7
                b.x = a.x + (c.x - a.x) / 2;
                b.y = a.y + (c.y - a.y) / 2;
                eased = true;
            }
        }
        if (!eased) break;
    }
}
inline void show_path(Frame& frame, u32 m, u32 p, i32 x, i32 y, u32 shade, i32 scale = u) {
    soften(frame.paths[p]);
    frame.paths[p].key = key_of(frame.paths[p]);
    place(frame, p, x + art::ox[m] * u, y + art::oy[m] * u, scale, shade, stroke);
}
// The ball's drop from the clamp to the skin, px, and how long it falls, ms.
inline i32 ball_fall(const i32* c) {
    using dsp::sc;
    const i32 skin = 164 - (24 * (1024 - sc(264, c[2])) >> 10);
    const i32 bottom = 112 - sc(62, c[0]) + (20 * (1300 - sc(580, c[3])) >> 10);
    return skin > bottom ? skin - bottom : 0;
}
inline i32 ball_lands(const i32* c) { return 50 + ball_fall(c) * 2; }
// The ratchet's wheel: its teeth, and how far a hit turns it (tenths of a
// degree): a tooth for each tooth of the train while it sounds.
inline i32 cog_teeth(const i32* c) { return 6 + (c[0] * 10 >> 12); }
inline i32 cog_turn(const i32* c, i32 age, i32 life) {
    if (age < 0 || age >= life) return 0;
    const i32 train = 1 + (c[0] * 15 >> 12);
    return train * (3600 / cog_teeth(c)) * gone(age, life) >> 12;
}
// The chalk's stroke: drawn out over a hit, whole at rest. A point along it
// (t, Q10, of the whole stroke), Q10 px from the slate's middle.
struct Stroke { i32 len, rise, waves, chat, part, amp; };
inline Stroke chalk_stroke(const i32* c, i32 age, i32 life) {
    using dsp::sc;
    Stroke s;
    s.len = 20 + sc(100, c[4]);
    s.rise = sc(30, c[3]);
    s.chat = sc(4 * 1024, c[6]);
    // The squeal's pitch: from a wave every 60 px to one every 12 px.
    s.waves = 1 + (1 + sc(8, c[0])) * s.len / 120;
    // The wave's height, Q10 px: no sharper at its crests than a pixel's
    // radius (a wave of length L px and height A bends at L^2 / (4 pi^2 A)),
    // so the line's two edges stay apart there.
    const i32 wl = s.len / s.waves;
    s.amp = 2560 + s.chat;
    if (s.amp > wl * wl * 1024 / 40) s.amp = wl * wl * 1024 / 40;
    s.part = age >= 0 && age < life ? gone(age, life) >> 2 : 1024;
    if (s.part < 10 * 1024 / s.len) s.part = 10 * 1024 / s.len;   // at least 10 px drawn
    return s;
}
inline Point stroke_at(const Stroke& s, i32 t) {
    Point q;
    q.x = s.len * t - 60 * u;
    q.y = -(s.rise * t) + (opxy::draw::sine(t * s.waves * 36 / 10) * s.amp >> 10);
    return q;
}
// The bull-roarer's slat on its orbit: the angle a hit has carried it to, and
// where that puts it (Q10).
inline i32 roar_angle(const i32* c, i32 age, i32 life) {
    const i32 rate = 1 + dsp::sc(3, c[0]);
    return age >= 0 && age < life ? age * rate * 36 / 100 : 0;
}
inline void roar_at(const i32* c, i32 a, i32& x, i32& y) {
    const i32 ry = 12 + dsp::sc(26, c[3]);
    x = (210 + dsp::sc(16, c[2])) * u + 63 * opxy::draw::sine(a + 900);
    y = 124 * u + ry * opxy::draw::sine(a);
}
// The same on screen: moved with the bull-roarer to the screen's middle.
inline void roar_on_screen(const i32* c, i32 a, i32& x, i32& y) {
    roar_at(c, a, x, y);
    x += art::ox[15] * u;
    y += art::oy[15] * u;
}
}

using namespace view;

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.initialized = 0;
    state.shown = 0;
    state.serial = 0;
    state.track = 0;
    state.age = idle;
    state.held = 0;
    state.life = 0;
    for (u32 p = 0; p < 4; ++p) state.keys[p] = 0;
}

// The paths each specimen reshapes (palette 4: the hero's grey; 5: structure; 6: engraving; 3: a spark;
// 7 and 8: the hero's and structure's for an outline drawn at half size).
inline void draw_paths(Frame& frame, u32 m, const i32* c, i32 age, i32 life) {
    using dsp::sc;
    const i32 k = kick(age), r = ring(age, life);
    bool used[4] = {false, false, false, false};
    Point pts[32];
    switch (m) {
    case 0: {
        // The skin: dented where the ball lands (spot moves it to the edge), rippling as it rings.
        const i32 s = 1024 - sc(264, c[2]);
        const i32 rx = 64 * s >> 10, ry = 11 * s >> 10;
        const i32 spot = sc(30, c[6]), zone = 18 * s >> 10;
        i32 lift[32];
        // The dent comes when the ball lands.
        const i32 depth = (kick(age - ball_lands(c)) * (4 + sc(6, c[0])) >> 2) + (age >= ball_lands(c) ? r * 2 >> 2 : 0);
        for (u32 i = 0; i < 32; ++i) {
            const i32 x = opxy::draw::sine(static_cast<i32>(i) * 3600 / 32 + 900) * rx;
            const i32 near = (x > spot * u ? x - spot * u : spot * u - x) >> 10;
            lift[i] = near < zone ? depth * (zone - near) / zone : 0;
            // Ripples run round the skin while it rings: three waves, at most 1.5 px.
            if (age >= ball_lands(c)) lift[i] += ((r * 1536 >> 12) * opxy::draw::sine(static_cast<i32>(i) * 337 + age * 20)) >> 10;
        }
        ellipse_ring(frame.paths[0], rx, ry, u / 2, lift);
        show_path(frame, m, 0, 200 * u, 164 * u - 24 * s, 4);
        used[0] = true;
        // The bounces' path: arcs falling away to the right of where it lands.
        if (c[1] > 60) {
            const i32 h0 = 6 + sc(30, c[1]);
            for (u32 i = 0; i < 32; ++i) {
                const i32 x = static_cast<i32>(i) * 2 + 2;              // 2..64 px
                const i32 seg = x < 34 ? 0 : x < 58 ? 1 : 2;
                const i32 x0 = seg == 0 ? 0 : seg == 1 ? 34 : 58, x1 = seg == 0 ? 34 : seg == 1 ? 58 : 76;
                const i32 h = seg == 0 ? h0 : seg == 1 ? h0 * 9 / 20 : h0 / 5;
                const i32 t = (x - x0) * 1024 / (x1 - x0);               // 0..1 Q10
                pts[i].x = x * u - 32 * u;
                pts[i].y = -(h * 4 * (t * (1024 - t) >> 10));           // a smooth parabola, Q10 px
            }
            band(frame.paths[1], pts, u / 2);
            show_path(frame, m, 1, (200 + spot + 32) * u, 132 * u, 5);
            used[1] = true;
        }
        break;
    }
    case 1: {
        // The liquid's surface inside the cask, at its fill, sloshing; as wide as the bilge there.
        const i32 s = 860 + sc(340, c[0]);
        const i32 cy = 141 - sc(17, c[0]);
        const i32 level = 44 - sc(88, c[1]);                    // px from the cask's middle, down
        const i32 half = (42 - 8 * level * level / 2500 - 2) * s >> 10;
        const i32 amp = sc(4 * 1024, c[6]) + r / 2 + k / 4;
        surface(pts, half, amp, 2, age > 0 ? age * 9 : 0);
        band(frame.paths[2], pts, u / 2);
        show_path(frame, m, 2, 170 * u, cy * u + level * s, 4);
        used[2] = true;
        break;
    }
    case 2: {
        // The stone bar: as long as its pitch is low, its underside carved to
        // the rock, a chip taken from its corner; its front face and, behind,
        // its top and end. Struck, it bends in its first mode.
        static const signed char rough[24] = {0, -2, 1, 3, -1, 0, 2, 1, -2, 0, 2, -3, -1, 1, 3, 1, -1, 2, 0, -2, 1, 2, 1, 0};
        Path& q = frame.paths[0];
        const i32 hl = 57 - sc(27, c[0]), arch = sc(8, c[2]), chip = sc(5, c[4]);
        const i32 bend = age >= 0 ? wobble(age, life, 2) : 0;
        const i32 top = -9, bottom = 11;
        for (i32 i = 0; i < 64; ++i) {
            i32 x, y;
            if (i < 24) { x = -hl * u + 2 * hl * u * i / 23; y = top * u + rough[i] * u / 10 + (i >= 21 && i < 23 ? chip * u : 0); }
            else if (i < 32) { x = hl * u; y = top * u + (bottom - top) * u * (i - 23) / 9; }
            else if (i < 56) {
                x = hl * u - 2 * hl * u * (i - 32) / 23;
                const i32 lim = hl * u * 6 / 10, ax = x < 0 ? -x : x;
                const i32 f = ax < lim ? ax * 1024 / lim : 1024;   // ax under 64 px: no overflow
                y = bottom * u - arch * (1024 - (f * f >> 10)) - rough[55 - i] * u / 10;
            } else { x = -hl * u; y = bottom * u - (bottom - top) * u * (i - 55) / 9; }
            if (bend) y += bend * opxy::draw::sine(x * 1630 / (hl * u) + 900);
            // Half as large again (the artwork's scale for it), at half size.
            q.points[i].x = x * 3 / 4;
            q.points[i].y = y * 3 / 4;
        }
        show_path(frame, m, 0, 185 * u, 126 * u + u / 2, 7, 2 * u);
        used[0] = true;
        // The top face's back edge and the end, seen above and to the right.
        for (i32 i = 0; i < 32; ++i) {
            i32 x, y;
            if (i == 0) { x = -hl * u; y = top * u; }
            else if (i < 30) { x = (-hl + 6) * u + (2 * hl) * u * (i - 1) / 28; y = (top - 7) * u + rough[(i + 5) % 24] * u / 10 + (i >= 27 && i < 29 ? chip * u : 0); }
            else if (i == 30) { x = (hl + 6) * u; y = (bottom - 7) * u; }
            else { x = hl * u + u / 2; y = bottom * u; }
            if (bend) y += bend * opxy::draw::sine(x * 1630 / (hl * u) + 900);
            pts[i].x = x * 3 / 4;
            pts[i].y = y * 3 / 4;
        }
        band(frame.paths[1], pts, u / 4);
        show_path(frame, m, 1, 185 * u, 126 * u + u / 2, 8, 2 * u);
        used[1] = true;
        break;
    }
    case 3: {
        // The water in the basin: its depth, rippling from the drop; as wide as the bowl there.
        const i32 s = 820 + sc(360, c[2]);
        const i32 y0 = 150 - sc(20, c[2]);
        const i32 lev = 6 - sc(12, c[4]);
        const i32 e = lev + 8;
        i32 half = (58 * root(1764 - e * e) / 42 - 3) * s >> 10;
        if (half < 6) half = 6;
        const i32 amp = sc(3 * 1024, c[6]) + r / 2;
        surface(pts, half, amp, 4, age > 0 ? -age * 12 : 0);
        band(frame.paths[2], pts, u / 2);
        show_path(frame, m, 2, 184 * u, y0 * u + lev * s, 4);
        used[2] = true;
        break;
    }
    case 4: {
        // The crack across the slab's face: more shards, more branches; a hit runs it.
        const i32 ss = 900 + sc(500, c[1]);
        const i32 n = 1 + (c[2] * 7 >> 12);
        u32 seed = 0x9e3779b9u;
        for (u32 i = 0; i < 32; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const i32 zig = static_cast<i32>((seed >> 28) & 3) - 1;
            const i32 branch = (static_cast<i32>(i) % (32 / n + 1)) == 0 ? 2 : 0;
            pts[i].x = (static_cast<i32>(i) * 88 * u / 31 - 44 * u) * ss >> 10;
            pts[i].y = (zig + branch) * u * (2 + sc(3, 4095 - c[1])) / 4;
        }
        band(frame.paths[1], pts, u / 2);
        show_path(frame, m, 1, 180 * u, 156 * u - 9 * ss + (k > 2048 ? u : 0), k > 600 ? 3 : 4);
        used[1] = true;
        // The meltwater in the tray beneath: fuller and livelier for more lake.
        const i32 amp = sc(2 * 1024, c[6]) + r / 2;
        surface(pts, 28, amp, 3, age > 0 ? age * 6 : 0);
        band(frame.paths[2], pts, u / 2);
        show_path(frame, m, 2, 180 * u, (192 - sc(5, c[6])) * u, 4);
        used[2] = true;
        break;
    }
    case 5: {
        // The sheet itself, larger for a lower sheet, hung lower on longer
        // chains, flexing from the blow and pulled aside at its foot by the shake.
        const i32 s = 768 + sc(256, c[0]);
        const i32 top = 75 + sc(16, c[3]);
        const i32 hw = 72 * s >> 10, hh = 44 * s >> 10;
        const i32 amp = (r * (2 + sc(4, c[1]))) + (k * 3);
        for (u32 i = 0; i < 32; ++i) {
            // Around the rectangle: the straight top edge 0..3, the right side
            // 4..15, the bottom 16..19, the left 20..31: most points on the
            // sides, where the sheet flexes in a smooth curve. Q10 px.
            const i32 q = static_cast<i32>(i);
            i32 x, y;
            if (q < 4) { x = -hw * u + 2 * hw * u * q / 4; y = -hh * u; }
            else if (q < 16) { x = hw * u; y = -hh * u + 2 * hh * u * (q - 4) / 12; }
            else if (q < 20) { x = hw * u - 2 * hw * u * (q - 16) / 4; y = hh * u; }
            else { x = -hw * u; y = hh * u - 2 * hh * u * (q - 20) / 12; }
            const i32 bend = ((opxy::draw::sine(y * 40 / u + (age > 0 ? age * 8 : 0)) * amp) >> 12)
                             + sc(12 * u, c[1]) * ((y + hh * u) >> 4) / ((2 * hh > 0 ? 2 * hh : 1) * u >> 4);
            // At half size (it is wider than an outline reaches); the inner
            // edge two pixels in: the sheet's rolled hem.
            frame.paths[0].points[i].x = (x + bend) / 2;
            frame.paths[0].points[i].y = y / 2;
            frame.paths[0].points[63 - i].x = (x + bend - 2 * x / (hw > 0 ? hw : 1)) / 2;
            frame.paths[0].points[63 - i].y = (y - 2 * y / (hh > 0 ? hh : 1)) / 2;
        }
        show_path(frame, m, 0, 172 * u, (top + hh) * u, 7, 2 * u);
        used[0] = true;
        break;
    }
    case 6: {
        // The jar's outer foil: up to its height for the body, its torn edge wavy.
        const i32 fh = 22 + sc(40, c[6]);
        for (u32 i = 0; i < 32; ++i) {
            const i32 q = static_cast<i32>(i);
            if (q < 8) { pts[i].x = -33 * u; pts[i].y = -(fh * u) * q / 8; }
            else if (q < 24) {
                pts[i].x = -33 * u + 66 * u * (q - 8) / 15;
                pts[i].y = -fh * u + ((q & 1) ? 3 * u / 2 : 0);
            } else { pts[i].x = 33 * u; pts[i].y = -(fh * u) * (31 - q) / 7; }
        }
        band(frame.paths[0], pts, u / 2);
        show_path(frame, m, 0, 108 * u, 186 * u, 6);
        used[0] = true;
        // The spark: a jagged line across the gap, from knob to knob, while it sounds.
        if (age >= 0 && age < life) {
            const i32 kr = 6 * (1300 - sc(700, c[2])) >> 10;
            const i32 x0 = 192 + kr, x1 = 214 + sc(36, c[1]) - kr;
            const i32 len = x1 - x0 > 4 ? x1 - x0 : 4;
            u32 seed = static_cast<u32>(age / 30) * 2654435761u + 1u;
            i32 key[9];
            for (u32 j = 0; j < 9; ++j) {
                seed = seed * 1664525u + 1013904223u;
                key[j] = (j == 0 || j == 8) ? 0 : (static_cast<i32>((seed >> 22) & 1023) - 512) * (8 + sc(24, c[5])) / 64;
            }
            for (u32 i = 0; i < 32; ++i) {
                const i32 seg = static_cast<i32>(i) / 4, f = static_cast<i32>(i) % 4;
                const i32 j1 = seg + 1 < 9 ? seg + 1 : 8;
                pts[i].x = len * u * static_cast<i32>(i) / 31;
                pts[i].y = key[seg] + (key[j1] - key[seg]) * f / 4;
            }
            band(frame.paths[1], pts, u / 2);
            show_path(frame, m, 1, x0 * u, 74 * u, 3);
            used[1] = true;
        }
        break;
    }
    case 7: {
        // The wine inside, lower for the glugs, stirred by the pop.
        const i32 s = 820 + sc(204, c[3]);
        const i32 cy = 163 - sc(7, c[3]);
        const i32 lvl = -20 + sc(28, c[7]);
        // The bottle is drawn 1.1 times as large about its foot (197), at 186.
        const i32 half = 21 * 11 * s / 10240;
        const i32 amp = (r / 2 + k / 4) * 11 / 10;
        surface(pts, half, amp, 3, age > 0 ? age * 10 : 0);
        band(frame.paths[2], pts, u / 2);
        show_path(frame, m, 2, 186 * u, 197 * u - (197 * u - cy * u - lvl * s) * 11 / 10, 4);
        used[2] = true;
        break;
    }
    case 8: {
        // The abdomen: ribbed in as many segments as the ribs, as wide as the
        // body, as long as the tail, curling with the chirp; it pulses as it sings.
        const i32 ss = 1024 - sc(363, c[2]);
        const i32 len = (34 + sc(28, c[4])) * ss >> 10;
        const i32 wid = (11 + sc(14, c[3])) * ss >> 10;
        const i32 curl = sc(20, c[5]);
        const i32 n = 2 + (c[0] * 7 >> 12);
        const i32 pulse = (wobble(age, life, 2) * (r > 600 ? 1 : 0));
        for (u32 i = 0; i < 32; ++i) {
            const i32 q = static_cast<i32>(i);
            const i32 t = q * 1024 / 31;                                     // 0..1 along, Q10
            i32 f = 1024 - ((t * t >> 10) * t >> 10);                       // tapering to the tail
            if (t < 256 && f > 800 + t * 224 / 256) f = 800 + t * 224 / 256;  // a waist where it meets the thorax
            const i32 half = (wid + pulse) * f + 300;                       // Q10 px
            const bool joint = q >= 2 && q <= 29 && (q * (n + 1)) / 31 != ((q - 1) * (n + 1)) / 31;
            const i32 seg = joint ? u : 0;
            const i32 bend = curl * (t * t >> 10);
            frame.paths[3].points[i].x = bend - half + seg;
            frame.paths[3].points[i].y = len * t;
            frame.paths[3].points[63 - i].x = bend + half - seg;
            frame.paths[3].points[63 - i].y = len * t + u / 4;
        }
        show_path(frame, m, 3, 186 * u, 108 * u + 17 * ss, 4);
        used[3] = true;
        // Its segments' joints, across the abdomen, as many as the ribs: one
        // line across at each, joined along its sides.
        Point corner[20];
        i32 nc = 0;
        for (i32 j = 1; j <= n && nc < 20; ++j) {
            const i32 t = j * 1024 / (n + 1);
            i32 f = 1024 - ((t * t >> 10) * t >> 10);
            if (t < 256 && f > 800 + t * 224 / 256) f = 800 + t * 224 / 256;
            const i32 half = (wid + pulse) * f * 9 / 10;
            const i32 bend = curl * (t * t >> 10);
            const i32 side = j % 2 ? -1 : 1;
            corner[nc].x = bend - side * half; corner[nc].y = len * t; ++nc;
            corner[nc].x = bend + side * half; corner[nc].y = len * t + u / 2; ++nc;
        }
        resample(corner, nc, pts);
        band(frame.paths[2], pts, u / 2);
        show_path(frame, m, 2, 186 * u, 108 * u + 17 * ss, 5);
        used[2] = true;
        break;
    }
    case 9: {
        // The ratchet's wheel: as many ramped teeth as the control, turned by a hit.
        const i32 n = cog_teeth(c);
        u32 at = 0;
        for (i32 j = 0; j < n; ++j) {
            const i32 m0 = 64 * j / n, mm = 64 * (j + 1) / n - m0;
            for (i32 q = 0; q < mm; ++q, ++at) {
                const i32 a = (j * 1024 + q * 1024 / mm) * 3600 / (n * 1024);
                const i32 rr = 15 * u + 6 * u * q / (mm > 1 ? mm - 1 : 1);
                frame.paths[0].points[at].x = rr * opxy::draw::sine(a + 900) >> 10;
                frame.paths[0].points[at].y = rr * opxy::draw::sine(a) >> 10;
            }
        }
        show_path(frame, m, 0, 128 * u, 124 * u, 4);
        const i32 turn = cog_turn(c, age, life);
        if (turn) opxy::draw::turn(frame, 0, rotation(turn));
        used[0] = true;
        break;
    }
    case 10: {
        // The capsule's wall, cut through round the window: a hairline for a
        // thin, high shell to six pixels for a thick, low one. It nods with
        // the pod on its stem and shakes with it.
        const i32 s = 1050 + sc(400, c[3]);
        const i32 th = u + sc(5 * u, 4095 - c[2]);                      // the wall, Q10 px
        const i32 tilt = sc(200, c[5]);
        const i32 cs = opxy::draw::sine(tilt + 900), sn = opxy::draw::sine(tilt);
        const i32 cy = -28 * s;                                         // the window's middle, Q10 px from the pod's foot
        Path& q = frame.paths[0];
        for (u32 i = 0; i < 32; ++i) {
            const i32 a = static_cast<i32>(i) * 3600 / 32;
            const i32 ca = opxy::draw::sine(a + 900), sa = opxy::draw::sine(a);
            for (u32 side = 0; side < 2; ++side) {
                // Outside the window's edge (its fill would hide it): inner at the edge, outer a wall beyond.
                const i32 rx = (23 * s >> 1) + (side ? 0 : th * s >> 10), ry = (31 * s >> 1) + (side ? 0 : th * s >> 10);
                const i32 ex = ca * rx >> 10, ey = sa * ry >> 10;   // about the window's middle
                Point& o = q.points[side ? 63 - i : i];
                o.x = (ex * cs - ey * sn) >> 10;
                o.y = (ex * sn + ey * cs) >> 10;
            }
        }
        const i32 shake = age >= 0 ? wobble(age, life, 2) : 0;
        // The window's middle, turned with the pod about its foot.
        const i32 wx = -(cy * sn >> 10), wy = cy * cs >> 10;
        show_path(frame, m, 0, (180 + sc(12, c[5]) + shake) * u + wx, (150 + sc(2, c[5])) * u + wy, 5);
        used[0] = true;
        break;
    }
    case 12: {
        // The bowl: a thin flute to a wide goblet (rim), higher on a longer stem (ring).
        const i32 w = 22 + sc(20, c[3]), h = 48;
        const i32 base = 196 - (54 * (850 + sc(300, c[2])) >> 10);
        const i32 v = wobble(age, life, 2);
        for (u32 i = 0; i < 32; ++i) {
            // Up the left side, round the bottom, up the right: a U of 32 points as a ribbon.
            const i32 t = static_cast<i32>(i) * 1024 / 31;
            const i32 a = t * 1800 / 1024 + 900;                          // tenths of a degree, 90..270 round the bottom
            pts[i].x = -(opxy::draw::sine(a) * (w + v));            // Q10 px: a smooth curve, not whole-pixel steps
            pts[i].y = -(opxy::draw::sine(a + 900) * h);
        }
        band(frame.paths[0], pts, u / 2);
        show_path(frame, m, 0, 165 * u, (base - h) * u, 4);
        used[0] = true;
        // Its cut: a band of mitred facets round the lower bowl. A facet's
        // lower point that would fall outside the bowl at 42 px rises up the
        // bowl's side until it is inside, rather than squeezing in sideways:
        // a squeezed V turns sharper than the device draws (a spike).
        const i32 ww = w + v, cut = ww * root(h * h - 30 * 30) / h - 2;
        const i32 n = clamp(cut / 5, 3, 6);
        Point corner[13];
        for (i32 j = 0; j <= 2 * n; ++j) {
            const i32 x = -cut * 1024 + 2 * cut * 1024 * j / (2 * n);
            corner[j].x = j % 2 ? x * 9 / 10 : x;
            i32 y = 30;
            if (j % 2) {
                const i32 ax = (corner[j].x < 0 ? -corner[j].x : corner[j].x) >> 10;
                const i32 q = h * (ax + 1) / (ww > 1 ? ww : 1);
                y = clamp(q < h ? root(h * h - q * q) : 0, 34, 42);
            }
            corner[j].y = y * u;
        }
        // And no facet deeper than its V can be without its point turning
        // sharper than 150 degrees: about 3.3 times its narrower half.
        for (i32 j = 1; j < 2 * n; j += 2) {
            const i32 a = corner[j].x - corner[j - 1].x, b = corner[j + 1].x - corner[j].x;
            const i32 d = (a < b ? a : b) > u ? (a < b ? a : b) : u;
            if (corner[j].y > 30 * u + d * 10 / 3) corner[j].y = 30 * u + d * 10 / 3;
        }
        resample(corner, 2 * n + 1, pts);
        band(frame.paths[3], pts, u / 2);
        show_path(frame, m, 3, 165 * u, (base - h) * u, 6);
        used[3] = true;
        // The water: its level falls with more water (the line rises), sloshing.
        const i32 level = h - sc(h * 3 / 4, c[0]) - 2;                    // px below the rim
        const i32 depth = h - level;                                       // above the bottom
        i32 half = w * (1024 - ((1024 - depth * 1024 / h) * (1024 - depth * 1024 / h) >> 10)) >> 10;
        if (half < 8) half = 8;
        // Slosh tilts the surface, up to 9 px at each end, narrowing it so
        // its low end stays inside the bowl.
        const i32 tilt = sc(9, c[5]), across = half - 2 - tilt / 3 > 6 ? half - 2 - tilt / 3 : 6;
        const i32 amp = sc(3 * 1024, c[5]) + r / 2;
        surface(pts, across, amp, 2, age > 0 ? age * 7 : 0);
        for (u32 i = 0; i < 32; ++i) pts[i].y += pts[i].x * tilt / across;
        band(frame.paths[2], pts, u / 2);
        show_path(frame, m, 2, 165 * u, (base - h + level) * u, 4);
        used[2] = true;
        break;
    }
    case 13: {
        // The bell: a dome flaring wider and flatter towards a gong, smaller for a higher bell; it shivers when struck.
        const i32 s = 1200 - sc(500, c[1]);
        const i32 w = (16 + sc(8, c[5])) * s >> 10, h = (18 - sc(6, c[5])) * s >> 10;
        const i32 v = wobble(age, life, 1) * (r > 400 ? 1 : 0);
        for (u32 i = 0; i < 32; ++i) {
            const i32 t = static_cast<i32>(i) * 1024 / 31;
            const i32 a = t * 1800 / 1024;                                 // 0..180 degrees over the top
            const i32 flare = (t < 128 || t > 896) ? 3 : 0;
            pts[i].x = -(opxy::draw::sine(a + 900) * (w + v + flare));   // Q10 px
            pts[i].y = -(opxy::draw::sine(a) * h);
        }
        band(frame.paths[0], pts, u / 2);
        show_path(frame, m, 0, 165 * u, 144 * u, 4);
        used[0] = true;
        break;
    }
    case 14: {
        // The chalk's stroke on the slate: its length, its glide upward, its
        // chatter; a hit draws it out behind the chalk.
        const Stroke s = chalk_stroke(c, age, life);
        for (u32 i = 0; i < 32; ++i) pts[i] = stroke_at(s, static_cast<i32>(i) * s.part / 31);
        band(frame.paths[1], pts, u / 2);
        show_path(frame, m, 1, 175 * u, 150 * u, 4);
        used[1] = true;
        break;
    }
    case 15: {
        // The slat's orbit, traced faintly: its height the swell, as far round as the length.
        const i32 ry = 12 + sc(26, c[3]);
        const i32 sweep = 900 + sc(2600, c[4]);   // tenths of a degree
        for (u32 i = 0; i < 32; ++i) {
            const i32 a = sweep * static_cast<i32>(i) / 31;
            pts[i].x = 63 * opxy::draw::sine(a + 900);
            pts[i].y = ry * opxy::draw::sine(a);
        }
        band(frame.paths[1], pts, u / 2);
        show_path(frame, m, 1, (210 + sc(16, c[2])) * u, 124 * u, 5);
        used[1] = true;
        // The cord: from the stick's knot to the slat's hole, sagging a little.
        i32 sx, sy;
        roar_at(c, roar_angle(c, age, life), sx, sy);
        const i32 size = 1300 - sc(600, c[1]), turn = sc(300, c[5]);
        sx -= 35 * size * opxy::draw::sine(turn + 900) >> 10;
        sy -= 35 * size * opxy::draw::sine(turn) >> 10;
        const i32 x0 = 96 * u, y0 = 76 * u;
        const i32 mx = (x0 + sx) / 2, my = (y0 + sy) / 2;
        // At half size: the cord reaches further than an outline does.
        for (u32 i = 0; i < 32; ++i) {
            const i32 t = static_cast<i32>(i) * 1024 / 31;
            const i32 sag = (8 * t >> 10) * (1024 - t) >> 8;
            pts[i].x = ((x0 - mx) + ((sx - x0) * t >> 10)) / 2;
            pts[i].y = ((y0 - my) + ((sy - y0) * t >> 10) + sag * u / 4) / 2;
        }
        band(frame.paths[3], pts, u / 4);
        show_path(frame, m, 3, mx, my, 8, 2 * u);
        used[3] = true;
        break;
    }
    default: break;
    }
    for (u32 p = 0; p < 4; ++p)
        if (!used[p]) park(frame, p);
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    using dsp::sc;
    const u32 m = input.model < kit::models ? input.model : 0;
    const bool first = !state.initialized;
    state.notes.update(input);
    const i32 dt = clamp(input.elapsed * 1000 >> 10, 0, 100);
    const bool held = input.held_notes != notes_unavailable && input.held_notes > 0;
    if (first || input.track != state.track) {
        state.serial = input.note_serial;
        state.track = input.track;
        state.age = idle;
    } else if (input.note_serial != state.serial) {
        state.serial = input.note_serial;
        state.age = input.reduced_motion ? idle : 0;
        state.held = 0;
    } else if (state.age >= 0) {
        state.age += dt;
        if (held) state.held += dt;
    }
    i32 c[8];
    for (u32 i = 0; i < 8; ++i) c[i] = (input.slots[i] < 0 ? 0 : input.slots[i]) >> 19;
    state.life = life_ms(m, c);
    // Done: the motion has eased back (200 ms after the sound).
    if (state.age > state.life + 200) state.age = idle;
    const i32 age = state.age, life = state.life;

    // A new specimen: put the last one away (every one, the first time).
    if (first || state.shown != m + 1) {
        for (u32 i = 0; i < sizeof(art::parts) / sizeof(art::parts[0]); ++i)
            if (first || art::parts[i].node < art::node_count) {
                const art::Part& p = art::parts[i];
                bool mine = i >= art::first[m] && i < static_cast<u32>(art::first[m] + art::count[m]);
                if (!mine) hide(frame, p.node);
            }
        state.shown = m + 1;
    }
    state.initialized = 1;

    // The specimen's parts.
    const i32 k = kick(age), r = ring(age, life), g = gone(age, life);
    i32 tip_x = 0, tip_y = 0;
    if (m == 14) {
        const Stroke s = chalk_stroke(c, age, life);
        const Point q = stroke_at(s, s.part);
        tip_x = (175 + art::ox[14]) * u + q.x;
        tip_y = (150 + art::oy[14]) * u + q.y;
    }
    for (u32 i = art::first[m]; i < static_cast<u32>(art::first[m] + art::count[m]); ++i) {
        const art::Part& p = art::parts[i];
        if (p.hide_c < 8) {
            const bool below = c[p.hide_c] < p.hide_at;
            if (p.hide_above ? !below : below) { hide(frame, p.node); continue; }
        }
        i32 x = p.x * 256, y = p.y * 256, s = p.base, turn = 0;
        for (u32 e = 0; e < 4; ++e) {
            const art::Effect4& f = p.fx[e];
            if (!f.k) continue;
            const i32 v = f.lo + ((f.hi - f.lo) * c[f.c] >> 12);
            if (f.k == art::dx) x += v * u;
            else if (f.k == art::dy) y += v * u;
            else if (f.k == art::scale) s = s * v >> 10;
            else turn += v;
        }
        bool hidden = false;
        if (p.hit == art::orbit || p.hit == art::ghost) {
            // The bull-roarer's slat travels its orbit while it sounds; its ghosts trail it.
            const i32 a = roar_angle(c, age, life) - (p.hit == art::ghost ? p.size * (120 + sc(480, c[0])) : 0);
            roar_on_screen(c, a, x, y);
        } else if (p.hit == art::tip) {
            x = tip_x;
            y = tip_y;
        }
        if (age >= 0) {
            switch (p.hit) {
            case art::kick_dx: x += (p.size * k >> 12) * u; break;
            case art::kick_dy: y += (p.size * k >> 12) * u; break;
            case art::kick_rot: turn += p.size * k >> 12; break;
            case art::ring_dx: x += wobble(age, life, p.size) * u; break;
            case art::ring_dy: y += wobble(age, life, p.size) * u; break;
            case art::ring_rot: turn += wobble(age, life, p.size); break;
            case art::rise: y += ((p.size * g) >> 12) * u; break;
            case art::flash: hidden = age >= life; break;
            case art::strike: turn += (p.size - turn) * k >> 12; break;
            case art::swing: {
                // The crank turns with the wheel, then eases back once the sound is done.
                const i32 whole = (1 + (c[0] * 15 >> 12)) * (3600 / cog_teeth(c));
                turn += age < life ? cog_turn(c, age, life) : whole * (200 - (age - life)) / 200;
                break;
            }
            case art::fall: {
                i32 d;
                if (p.size == 1) {
                    // The ball: from the clamp onto the skin, bouncing, back in the clamp after the sound.
                    const i32 fall = ball_fall(c), tf = ball_lands(c);
                    if (age < tf) d = fall * age / tf * age / tf;
                    else if (age < life) {
                        const i32 b = age - tf;
                        const i32 hop = (fall * (2 + sc(10, c[1])) / 14) * opxy::draw::sine(b * 1800 / 140 % 1800) >> 10;
                        d = fall - ((hop < 0 ? -hop : hop) * r >> 12);
                    } else d = fall - fall * (age - life) / 200;
                } else if (p.size == 2) {
                    // The drop: falls to the water and is gone until the next forms.
                    const i32 water = 150 - sc(20, c[2]) + ((6 - sc(12, c[4])) * (820 + sc(360, c[2])) >> 10);
                    // From the spout (its top hangs at 87 whatever its size) to the water.
                    const i32 ds = 600 + sc(900, c[0]);
                    const i32 fall = water - 87 - (15 * ds >> 10);
                    if (age < 110) d = fall * age / 110 * age / 110;
                    else { d = 0; hidden = age < life; }
                } else {
                    // The cork (and the corkscrew in it): out of the neck at a
                    // slant to the right and back, further for a stronger pop;
                    // a third as high as it goes across, below the header.
                    const i32 reach = 8 + sc(20, c[1]);
                    const i32 tt = age < 400 ? age : 400;
                    const i32 along = reach * 4 * tt / 400 * (400 - tt) / 400;
                    d = -(along / 3);
                    x += along * u;
                }
                y += d * u;
                break;
            }
            default: break;
            }
        }
        if (hidden) { hide(frame, p.node); continue; }
        if (s < 16) s = 16;
        if (s > 2 * u) s = 2 * u;
        place(frame, p.node, x, y, s, 0, keep);
        if (turn) opxy::draw::turn(frame, p.node, rotation(turn));
    }
    (void)r;
    draw_paths(frame, m, c, age, life);

    // The header: both rows, the edited one bright; under Shift both rows show
    // Shift's controls: the specimen (its drawer the title), choke, pan and
    // level; then TUNE, VARY, FX I and FX II.
    static const char* const chokes[4] = {"none", "1", "2", "3"};
    const char* titles[8];
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) {
        titles[i] = kit::model[m].labels[i];
        caption(m, i, input.slots[i], values[i], header_capacity);
    }
    if (input.shift) {
        char pan[8], level[8], tune[8], vary[8], sends[2][4];
        signed_text(pan, sizeof(pan), input.pan, "");
        signed_text(level, sizeof(level), input.level, "dB");
        kit::tune_text(clamp(input.pad[0], -16, 15), tune);
        kit::vary_text(clamp(input.pad[1], 0, 15), vary);
        for (u32 s = 0; s < 2; ++s) {
            const i32 code = input.pad[2 + s];
            if (code > 0) signed_text(sends[s], sizeof(sends[s]), (((code > 7 ? 7 : code) - 1) * 100 + 3) / 6, "");
            else copy(sends[s], sizeof(sends[s]), "TRK");
        }
        static const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"TUNE", "VARY", "FX I", "FX II"}};
        const char* const shift_values[2][4] = {{kit::model[m].name, chokes[input.choices[2] & 3u], pan, level}, {tune, vary, sends[0], sends[1]}};
        // Both rows at once, stacked as the pages: the edited one bright on the
        // bar, the other grey, so Shift swaps all eight names and values.
        for (u32 row = 0; row < 2; ++row)
            for (u32 col = 0; col < 4; ++col) {
                titles[4 * row + col] = shift_titles[row][col];
                copy(values[4 * row + col], header_capacity, shift_values[row][col]);
            }
        titles[0] = kit::drawer_name[kit::model[m].drawer];
    }
    page_header(state.header, input, frame, titles, values, art::header_node);
}
}
