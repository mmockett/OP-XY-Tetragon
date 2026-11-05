#include "display.h"

// THUNDER's screen: a theatre thunder sheet hung from its batten in a
// gallows frame, played the way the selected key's model plays it. The
// sheet's outline, its hem and its rolled lip are the two reshaping paths; everything else is a part placed from the key's controls. A hit sets
// the sheet rippling in the model's mode shape for as long as the model's own
// sound rings (a kit voice plays to its end: stock gives kits no amp envelope).
namespace thunder {
namespace view {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;
// the models, as the engine numbers them (models.h)
struct M { enum : u32 { BOOM, SLAP, POP, FLICK, TOM, CRASH, CLANK, GRIT, HOP, ZAP, BOW, SHAKE }; };
const u32 models = 12;

// ---------------------------------------------------------------- arithmetic
// sin of a phase (65536 a turn), Q14.
static const opxy::i16 quarter[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270,
    6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585,
    11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137,
    15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384};
inline i32 sn(i32 phase) {
    const u32 p = static_cast<u32>(phase) & 0xffffu;
    const u32 q = p >> 14, r = p & 0x3fffu;
    const u32 i = (q & 1u ? 0x4000u - r : r);   // within the quarter, 0..16384
    const u32 k = i >> 8, f = i & 0xffu;
    const i32 a = quarter[k], b = quarter[k < 64 ? k + 1 : 64];
    const i32 v = a + (((b - a) * static_cast<i32>(f)) >> 8);
    return q >= 2 ? -v : v;
}
inline i32 cs(i32 phase) { return sn(phase + 16384); }
// 2^(l / 65536) in Q16, for l from -16 to 14 octaves.
static const opxy::u32 octave_steps[17] = {65536, 68438, 71468, 74632, 77936, 81386, 84990, 88752, 92682, 96785, 101070,
                                           105545, 110218, 115098, 120194, 125515, 131072};
inline u32 pow2v(i32 l) {
    if (l < -16 * 65536) return 0;
    if (l > 14 * 65536) l = 14 * 65536;
    const i32 n = l >> 16;   // floor
    const u32 f = static_cast<u32>(l - n * 65536);
    const u32 k = f >> 12, r = f & 0xfffu;
    const u32 m = octave_steps[k] + (((octave_steps[k + 1] - octave_steps[k]) * r) >> 12);
    return n >= 0 ? m << n : m >> -n;
}
// exp(-a / b) in Q15, for a and b in the same units.
inline i32 fall(i32 a, i32 b) {
    if (a <= 0) return 32768;
    if (b <= 0 || a > 16 * b || a >= (1 << 21)) return 0;
    const i32 x = ((a << 10) / b) << 6;                       // a / b, Q16
    const i32 l = x + (x >> 1) - (x >> 4) - (x >> 7);        // times log2 e
    return static_cast<i32>(pow2v(-l) >> 1);
}
inline i32 iabs(i32 v) { return v < 0 ? -v : v; }
inline i32 lerp(i32 a, i32 b, i32 t) { return a + (((b - a) >> 2) * t >> 8); }   // t Q10; |b - a| < 2^21
inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
// A tenth-degree turn as a unit vector.
inline Turn turned(i32 tenths) {
    const i32 ph = (tenths * 4096) / 225;   // 65536 / 3600
    const Turn t = {cs(ph) >> 4, sn(ph) >> 4};
    return t;
}
// A value a (b / a)^t for t a Q10 unit, as log2 Q16: lerp of the logs.
inline i32 lmix(i32 la, i32 lb, i32 t) { return la + (((lb - la) >> 1) * t >> 9); }
// The direction of (dx, dy) in tenths of a degree, as turned() takes it:
// bisection on the cross product within the half-turn that holds it.
inline i32 angle_of(i32 dx, i32 dy) {
    const i32 base = dy >= 0 ? 0 : 1800;
    if (dy < 0) { dx = -dx; dy = -dy; }
    i32 lo = 0;
    for (i32 b = 1024; b; b >>= 1) {
        if (lo + b > 1800) continue;
        const Turn t = turned(lo + b);
        if ((static_cast<i32>(t.cos) * (dy >> 6) - static_cast<i32>(t.sin) * (dx >> 6)) >= 0) lo += b;
    }
    return base + lo;
}

// ---------------------------------------------------------------- the frame
// The chain's links and GRIT's beads are authored larger than they are drawn
// (tools/make_panel.py, LINK_K and BEAD_K) and placed that much smaller, so
// their lines draw near 1 px wide where the parts are drawn large.
const i32 link_k = 1331, bead_k = 1178;
template <class F>
inline void put(F& f, u32 node, i32 x, i32 y, i32 scale, u32 shade, i32 tenths = 0) {
    place(f, node, x, y, clampi(scale, 64, 2 * unit), shade, shade ? stroke : keep);
    if (tenths) turn(f, node, turned(tenths));
}
template <class F>
inline void put_shaded(F& f, u32 node, i32 x, i32 y, i32 scale, i32 shade, i32 tenths = 0) {
    // a part that fades through the greys: parked once it has faded out
    if (shade <= 0) { hide(f, node); return; }
    put(f, node, x, y, scale, static_cast<u32>(shade > 8 ? 8 : shade), tenths);
}

// ---------------------------------------------------------------- the sheet
// Every model's view of the one sheet: which of its controls move which
// part (an index into the key's eight, or -1), its mode shape when struck,
// how fast and far it rings on screen.
struct Look {
    signed char tension, curve, sway, ring, chain, glide;
    unsigned char m, n, hz, px;
};
const Look looks[models] = {
    {4, 5, -1, -1, 7, 6, 1, 1, 4, 13},    // M::BOOM: CAMBER curves it, GLIDE weighs it
    {4, -1, -1, 7, 5, -1, 2, 1, 7, 9},    // M::SLAP: METAL rings it
    {-1, -1, -1, 5, 6, -1, 2, 2, 8, 8},   // M::POP
    {4, -1, 6, -1, 5, -1, 5, 3, 11, 7},   // M::FLICK: SWING sways it
    {4, 5, -1, -1, 7, -1, 1, 1, 5, 12},   // M::TOM
    {4, -1, 6, -1, 7, -1, 3, 2, 6, 10},    // M::CRASH
    {-1, -1, -1, 5, 6, -1, 2, 1, 7, 7},   // M::CLANK: SHEET rings it
    {-1, -1, -1, 5, 6, -1, 4, 2, 9, 6},   // M::GRIT
    {6, -1, -1, -1, 7, -1, 2, 1, 7, 8},   // M::HOP
    {-1, 4, -1, 5, 6, -1, 3, 1, 9, 8},    // M::ZAP: DEPTH bows it
    {5, 7, -1, -1, 6, -1, 2, 1, 5, 6},    // M::BOW: BEND flexes it
    {4, -1, 7, -1, 5, -1, 1, 1, 3, 11},    // M::SHAKE: SWELL sways it
};

struct Sheet {
    i32 cx, top, w, h;          // Q10 px
    i32 curve, slack, sway;     // Q10 px of camber, Q10 of slack, Q10 px of lean
    i32 shake;                  // Q14: the struck ripple now, signed
    u32 m, n;
    i32 amp;                    // Q10 px
};
// A point on the sheet (u, v Q10, 0..1024 across and down) on screen, Q10 px.
inline void at(const Sheet& s, i32 u, i32 v, i32& x, i32& y) {
    // (Q10 px times Q10 fractions: every product stays under 2^31)
    x = s.cx + (((u - 512) * (s.w >> 4)) >> 6) + ((v * s.sway) >> 10);   // it leans from its clamped top
    y = s.top + ((v * (s.h >> 4)) >> 6);
    const i32 su = sn(u * 32) >> 4;   // sin(pi u), Q10
    // camber: a curved shell under the straight batten
    y += ((s.curve * su >> 10) * (154 + ((870 * v) >> 10))) >> 10;
    // slack: buckles along its edges, flattening as it is pulled taut
    if (s.slack) {
        const i32 b = sn(u * 205 + v * 45) >> 4;          // sin(2 pi (3.2 u + 0.7 v)), Q10
        const i32 k = (s.slack * 2867) >> 10;             // 2.8 px at full slack, Q10 px
        y += ((((k * b) >> 10) * su >> 10) * (410 + v)) >> 10;
    }
    // the struck ripple, in the model's mode shape
    if (s.shake) {
        // a hanging sheet: clamped along its top, free at its foot, so its
        // shapes run sin((2n - 1) pi v / 2) down it, largest at the foot
        const i32 mode = ((sn(static_cast<i32>(s.m) * u * 32) >> 4) * (sn(static_cast<i32>(2 * s.n - 1) * v * 16) >> 4)) >> 10;   // Q10
        const i32 d = (mode * (s.shake >> 4)) >> 10;                 // Q10
        y += (s.amp * d) >> 10;
        x += ((s.amp * d) >> 13) * (sn(v * 32) >> 13);
    }
}

// ---------------------------------------------------------------- captions
// A whole number with a unit.
inline void number(char* out, i32 v, const char* unit) {
    char t[12];
    u32 n = 0;
    if (v < 0) { t[n++] = '-'; v = -v; }
    if (v > 9999) v = 9999;
    if (v >= 1000) t[n++] = static_cast<char>('0' + v / 1000);
    if (v >= 100) t[n++] = static_cast<char>('0' + v / 100 % 10);
    if (v >= 10) t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit[i] && n < 11; ++i) t[n++] = unit[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
// tenths as "1.3" with a unit
inline void tenths(char* out, i32 v, const char* unit) {
    char t[12];
    u32 n = 0;
    if (v >= 100) t[n++] = static_cast<char>('0' + v / 100 % 10);
    t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = '.';
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit[i] && n < 11; ++i) t[n++] = unit[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
// hundredths as "0.29" with a unit
inline void hundredths(char* out, i32 v, const char* unit) {
    char t[12];
    u32 n = 0;
    t[n++] = static_cast<char>('0' + v / 100 % 10);
    t[n++] = '.';
    t[n++] = static_cast<char>('0' + v / 10 % 10);
    t[n++] = static_cast<char>('0' + v % 10);
    for (u32 i = 0; unit[i] && n < 11; ++i) t[n++] = unit[i];
    t[n] = 0;
    copy(out, header_capacity, t);
}
enum Kind : unsigned char { amount, note, hz, ms, rate, count, cm, ratio, semis, mode };
struct Caption { Kind kind; i32 la, lb; };   // log2 of the range's ends, Q16 (counts: the ends themselves)
inline void caption(char* out, const Caption& c, i32 t, i32 tune) {   // t: Q10 unit
    switch (c.kind) {
    case note: {
        // midi = 69 + 12 log2(f / 440): its log, Q16, to semitones Q8
        const i32 l = lmix(c.la, c.lb, t) - L(440.0);
        const i32 st = 69 * 256 + ((l >> 4) * 3072 >> 12) + tune * 256;
        const i32 key = (st + 128) >> 8;
        static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const i32 k = key < 0 ? 0 : key;
        char s[8];
        u32 n = 0;
        for (const char* p = names[k % 12]; *p; ++p) s[n++] = *p;
        const i32 oct = k / 12 - 1;
        if (oct < 0) { s[n++] = '-'; s[n++] = static_cast<char>('0' - oct); } else s[n++] = static_cast<char>('0' + oct);
        s[n] = 0;
        copy(out, header_capacity, s);
        return;
    }
    case hz: {
        const i32 l = lmix(c.la, c.lb, t) + tune * 5461;
        const u32 f = pow2v(l);   // Hz, Q16
        const u32 hz_ = (f + 32768) >> 16;
        if (hz_ >= 1000) tenths(out, static_cast<i32>((hz_ + 50) / 100), "k");
        else number(out, static_cast<i32>(hz_), "");
        return;
    }
    case ms: {
        const u32 s = pow2v(lmix(c.la, c.lb, t));   // seconds, Q16
        const u32 msv = (s * 1000u + 32768u) >> 16;
        if (msv < 100) number(out, static_cast<i32>(msv), "ms");
        else if (msv < 1000) hundredths(out, static_cast<i32>((msv + 5) / 10), "s");
        else tenths(out, static_cast<i32>((msv + 50) / 100), "s");
        return;
    }
    case rate: {
        const u32 r = pow2v(lmix(c.la, c.lb, t));
        const i32 d = static_cast<i32>((r * 10u + 32768u) >> 16);
        if (d >= 100) number(out, (d + 5) / 10, "Hz");
        else tenths(out, d, "Hz");
        return;
    }
    case count: number(out, c.la + ((t * (c.lb - c.la) + 512) >> 10), ""); return;
    case mode: number(out, 1 + ((t * 4990) >> 20), ""); return;
    case cm: {
        // a ball whose first bounce lasts T falls from g T^2 / 8
        const u32 s = pow2v(lmix(c.la, c.lb, t));   // T, Q16 s
        const u32 tt = (s >> 4) * (s >> 4) >> 8;    // T^2, Q16
        number(out, static_cast<i32>((tt * 123u + 32768u) >> 16), "cm");   // 981 / 8 cm
        return;
    }
    case ratio: hundredths(out, lerp(c.la, c.lb, t), ""); return;
    case semis: {
        const i32 st = (lmix(c.la, c.lb, t) * 12 + 32768) >> 16;
        number(out, st, "st");
        return;
    }
    default: header_amount(out, header_capacity, t >= 1024 ? 0x7fffffff : t * 2097152 - (t >> 10));   // 1024: 2^31 - 1, unwrapped
    }
}
const Caption A = {amount, 0, 0};
#define NOTE(a, b) {note, L(a), L(b)}
#define HZ(a, b) {hz, L(a), L(b)}
#define MS(a, b) {ms, L(a), L(b)}
const Caption captions[models][8] = {
    {NOTE(30, 105), MS(1.3, 0.025), A, A, A, A, MS(0.006, 0.14), A},
    {NOTE(110, 330), MS(0.45, 0.035), A, A, A, A, A, A},
    {HZ(650, 2800), MS(0.35, 0.025), {count, 1, 7}, MS(0.004, 0.032), A, A, A, A},
    {HZ(2600, 9000), MS(1.1, 0.022), A, A, A, A, A, MS(0.0015, 0.06)},
    {NOTE(55, 420), MS(1.3, 0.07), A, A, A, A, {count, 1, 8}, A},
    {NOTE(110, 650), MS(3.2, 0.55), A, A, A, A, A, A},
    {NOTE(260, 2100), MS(1.4, 0.03), A, A, A, A, A, {count, 0, 10}},
    {HZ(1800, 9000), MS(0.2, 0.012), A, {rate, L(40), L(1200)}, A, A, A, MS(0.05, 0.5)},
    {NOTE(180, 1300), MS(0.6, 0.02), A, A, {cm, L(0.06), L(0.38)}, {ratio, 88, 55}, A, A},
    {HZ(1400, 9000), MS(0.8, 0.03), MS(0.015, 0.3), {count, 0, 5}, {semis, L(4), L(120)}, A, A, A},
    {NOTE(70, 900), MS(1.6, 0.08), A, {mode, 0, 0}, MS(0.05, 1.5), A, A, A},
    {NOTE(18, 75), MS(1.8, 0.15), MS(0.3, 3.5), {rate, L(3), L(16)}, A, A, A, A},
};
#undef NOTE
#undef HZ
#undef MS
// GRAINS: grains a second at the peak, "378"
inline void grains(char* out, i32 t) { number(out, static_cast<i32>((pow2v(lmix(L(40), L(1200), t)) + 32768u) >> 16), ""); }

// as the Shift row shows them: lower case, so the widest fits its column
const char* const names[models] = {"boom", "slap", "pop", "flick", "tom", "crash", "clank", "grit", "hop", "zap", "bow", "shake"};
const char* const labels[models][8] = {
    {"SIZE", "DAMP", "BEATER", "SPOT", "TENSION", "CAMBER", "GLIDE", "CHAIN"},
    {"SIZE", "DAMP", "BEATER", "SPOT", "TENSION", "CHAIN", "LINKS", "METAL"},
    {"SIZE", "DAMP", "POPS", "SPREAD", "SNAP", "METAL", "CHAIN", "SCATTER"},
    {"SIZE", "DAMP", "NAIL", "SPOT", "TENSION", "CHAIN", "SWING", "SCRAPE"},
    {"SIZE", "DAMP", "BEATER", "SPOT", "TENSION", "CAMBER", "ROLL", "CHAIN"},
    {"SIZE", "DAMP", "BEATER", "SPOT", "TENSION", "SWELL", "SWING", "CHAIN"},
    {"SIZE", "DAMP", "BEATER", "SPOT", "SHAPE", "SHEET", "CHAIN", "BUZZ"},
    {"SIZE", "DAMP", "BEAD", "GRAINS", "SWELL", "METAL", "CHAIN", "POUR"},
    {"SIZE", "DAMP", "BALL", "SPOT", "DROP", "LOSS", "TENSION", "CHAIN"},
    {"SIZE", "DAMP", "SWEEP", "ECHOS", "DEPTH", "METAL", "CHAIN", "CURVE"},
    {"SIZE", "DAMP", "PRESS", "SPOT", "SPEED", "TENSION", "CHAIN", "BEND"},
    {"SIZE", "DAMP", "RUMBLE", "RATE", "TENSION", "CHAIN", "CRACK", "SWELL"},
};
// The pad controls' captions, as the engine gives them to the stock header.
inline void tune_text(char* out, i32 v) {
    v = clampi(v, -16, 15);
    if (v > 0) { char s[1 + opxy::draw::header_capacity] = "+"; number(s + 1, v, ""); copy(out, header_capacity, s); }
    else number(out, v, "");
}
inline void drive_text(char* out, i32 v) {
    v = clampi(v, 0, 15);
    if (!v) copy(out, header_capacity, "OFF");
    else number(out, v, "");
}

// ---------------------------------------------------------------- how long it rings
// The model's own ring on screen, in Q10 seconds (a rough match to its sound),
// and whether the note is a gesture that lasts (the bow, the pour, the storm).
inline i32 ring(u32 m, const i32* t) {
    switch (m) {
    case M::BOOM: return static_cast<i32>(pow2v(lmix(L(1.3), L(0.025), t[1])) >> 6);
    case M::TOM: return static_cast<i32>(pow2v(lmix(L(1.3), L(0.07), t[1])) >> 6);
    case M::SLAP: return static_cast<i32>(pow2v(lmix(L(0.45), L(0.035), t[1])) >> 6);
    case M::POP: return static_cast<i32>(pow2v(lmix(L(0.35), L(0.025), t[1])) >> 6);
    case M::FLICK: return static_cast<i32>(pow2v(lmix(L(1.1), L(0.022), t[1])) >> 6);
    case M::CRASH: return static_cast<i32>(pow2v(lmix(L(3.2 / 3), L(0.55 / 3), t[1])) >> 6);
    case M::CLANK: return static_cast<i32>(pow2v(lmix(L(1.4), L(0.03), t[1])) >> 6);
    case M::GRIT: return static_cast<i32>(pow2v(lmix(L(0.2), L(0.012), t[1])) >> 6);
    case M::HOP: return static_cast<i32>(pow2v(lmix(L(0.6), L(0.02), t[1])) >> 6);
    case M::ZAP: return static_cast<i32>(pow2v(lmix(L(0.8), L(0.03), t[1])) >> 6);
    case M::BOW: return static_cast<i32>(pow2v(lmix(L(1.6), L(0.08), t[1])) >> 6);
    default: return static_cast<i32>(pow2v(lmix(L(1.8), L(0.15), t[1])) >> 6);
    }
}
// How long a gesture holds before it rings down, Q10 s (0 for a strike).
inline i32 gesture(u32 m, const i32* t) {
    switch (m) {
    case M::BOW: return static_cast<i32>((pow2v(lmix(L(0.05), L(1.5), t[4])) + pow2v(lmix(L(0.6), L(0.08), t[1]))) >> 6);
    case M::SHAKE: return static_cast<i32>(pow2v(lmix(L(0.3), L(3.5), t[2])) >> 6);
    case M::GRIT: return static_cast<i32>(pow2v(lmix(L(0.05), L(0.5), t[7])) >> 6);
    case M::HOP: {
        // the bounces: the first gap over (1 - e)
        const i32 first = static_cast<i32>(pow2v(lmix(L(0.06), L(0.38), t[4])) >> 6);
        const i32 e = lerp(901, 563, t[5]);   // 0.88..0.55, Q10
        return (first << 10) / (1024 - e);
    }
    case M::TOM: return (t[6] * 7 >> 10) * 40;
    case M::ZAP: return static_cast<i32>(pow2v(lmix(L(0.015), L(0.3), t[2])) >> 6) * (1 + (t[3] * 5 >> 10)) * 3 / 2;
    case M::POP: return static_cast<i32>(pow2v(lmix(L(0.004), L(0.032), t[3])) >> 6) * (1 + (t[2] * 6 >> 10));
    default: return 0;
    }
}
}

using namespace view;

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
    state.age = 1 << 24;
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    const u32 m = input.model < models ? input.model : 0;
    const Look& look = looks[m];
    i32 t[8], raw[8];
    for (u32 i = 0; i < 8; ++i) {
        const u32 r = input.slots[i] < 0 ? 0u : static_cast<u32>(input.slots[i]) >> 24;
        raw[i] = static_cast<i32>(r > 127 ? 127 : r);
        t[i] = (raw[i] * 8257) >> 10;   // Q10, 127 -> 1024
    }
    const i32 tune = clampi(input.pad[0], -16, 15);
    const bool still = input.reduced_motion != 0;

    // --- the hit: the selected key's notes only (the platform's serial is the key's)
    const i32 step = clampi(input.elapsed, 0, 1024);
    const bool baseline = !state.initialized || state.track != input.track;
    const bool fresh = !baseline && input.note_serial != state.serial;
    state.serial = input.note_serial;
    state.track = input.track;
    state.initialized = 1;
    if (baseline) state.age = 1 << 24;
    else if (fresh) state.age = 0;
    else state.age = clampi(state.age + step, 0, 1 << 24);
    // the model's own motion: a ring that falls with its sound, or a gesture
    // that holds and then falls
    const i32 tau = clampi(ring(m, t), 256, 4096);   // at least a quarter second, so a short hit still reads
    const i32 hold = gesture(m, t);
    i32 motion = state.age < hold ? 32768 : fall(state.age - hold, tau);
    if (still || state.age >= (1 << 24) - 2048) motion = 0;
    // quantised so that a still sheet is exactly still
    const i32 amp_q = motion >> 9;   // 0..64
    const i32 phase = static_cast<i32>((static_cast<u32>(state.age) * look.hz * 64u) & 0xffffu);   // 2 pi hz age
    const i32 shake = amp_q ? (amp_q * (cs(phase) >> 6)) : 0;   // Q14

    // --- the sheet: the hero, 264 to 336 px wide, hung close under the beam
    Sheet s;
    s.w = (264 + ((72 * t[0]) >> 10)) << 10;
    s.h = (100 + ((16 * t[0]) >> 10)) << 10;
    s.cx = 240 << 10;
    s.top = 59 << 10;
    const i32 tens = look.tension >= 0 ? (m == M::CRASH ? (t[4] * 6) / 10 : m == M::BOOM ? 360 + ((664 * t[4]) >> 10) : t[look.tension]) : 700;
    s.slack = 1024 - tens;
    s.curve = look.curve >= 0 ? (t[look.curve] * 13) : 0;
    s.sway = look.sway >= 0 ? (t[look.sway] * 14) : 0;   // up to 14 px at its foot
    s.m = look.m;
    s.n = look.n;
    // a hit throws the sheet hardest in its first frames, then it rings on
    // at its own size as the sound falls
    s.amp = (look.px << 10) + (amp_q ? ((look.px * fall(state.age, 160)) >> 5) : 0);
    s.shake = 0;
    // the parts hang from the sheet at rest; only its outlines carry a hit's motion
    const Sheet rest = s;
    s.shake = shake;
    if (m == M::BOW && amp_q) s.curve += (t[7] * 3 * (sn(static_cast<i32>(static_cast<u32>(state.age) * 341u)) >> 6)) >> 8;   // the hand flexing it as it sings
    if ((m == M::FLICK || m == M::CRASH || m == M::SHAKE) && amp_q && look.sway >= 0)
        s.sway = clampi(s.sway + (((t[look.sway] * 8 >> 4) * amp_q * (sn(static_cast<i32>(static_cast<u32>(state.age) * 128u)) >> 6)) >> 10), -(14 << 10), 14 << 10);
    const i32 cy = s.top + s.h / 2 + (8 << 10);
    const i32 pcx = s.cx + s.sway / 2;   // the paths' centre, half way down its lean
    const i32 qx = rest.w >> 2;          // each half's centre, a quarter of the width out
    // The sheet is wider than one outline can reach (a path spans 256 px at
    // scale 2), so it is two outlines, the left half and the right, meeting
    // under a clip at the middle of its top and at its foot. Each goes out
    // along the sheet's edge and back inside it: along the fold of its hem
    // (so the hem is the band between them), 0.4 px inside its side, and
    // along the top of the rolled lip at its foot (a bead 3.6 px deep). Both
    // at half size and scale 2.
    u32 key = 2166136261u;
    for (u32 i = 0; i < 8; ++i) key = (key ^ static_cast<u32>(raw[i])) * 16777619u;
    key = (key ^ m) * 16777619u;
    key = (key ^ static_cast<u32>(shake)) * 16777619u;
    key = (key ^ static_cast<u32>(s.amp)) * 16777619u;
    key = (key ^ static_cast<u32>(s.curve)) * 16777619u;
    key = (key ^ static_cast<u32>(s.sway)) * 16777619u;
    if (!key) key = 1;
    const i32 hem_v = 82;     // the hem's fold, about 9 px under the top edge
    const i32 lip = 1843;     // the lip, 3.6 px deep (half-size Q10)
    if (frame.paths[0].key != key) {
        for (u32 half = 0; half < 2; ++half) {
            Frame::path_type& o = frame.paths[half];
            const i32 ox = half ? pcx + qx : pcx - qx;
            for (u32 i = 0; i < 32; ++i) {
                // from the top's middle out to the corner, down the side, back along the foot
                i32 u, v, iv, nx = 0, ny = 0;
                if (i < 8) { u = 512 - static_cast<i32>(i) * 64; v = 0; iv = hem_v; }
                else if (i == 8) { u = 0; v = 0; iv = hem_v; nx = 200; }
                else if (i < 24) { u = 0; v = 112 + ((static_cast<i32>(i) - 9) * 858) / 14; iv = v; nx = 200; }
                else { u = (static_cast<i32>(i - 24) * 512) / 7; v = 1024; iv = v; nx = i == 24 ? 200 : 0; ny = -lip; }
                if (half) { u = 1024 - u; nx = -nx; }
                i32 x, y, xi, yi;
                at(s, u, v, x, y);
                if (iv != v) at(s, u, iv, xi, yi);
                else { xi = x; yi = y; }
                const u32 a = (i + 48) & 63, b = (47 - i) & 63;
                o.points[a].x = (x - ox) / 2;
                o.points[a].y = (y - cy) / 2;
                o.points[b].x = (xi - ox) / 2 + nx;
                o.points[b].y = (yi - cy) / 2 + ny;
            }
            o.key = key + half;
        }
    }
    place(frame, 0, pcx - qx, cy, 2 * unit, 9, stroke);
    place(frame, 1, pcx + qx, cy, 2 * unit, 9, stroke);
    Sheet hang = s;
    hang.shake = 0;

    // corners and edges, Q10 px
    i32 tlx, tly, trx, try_, blx, bly, brx, bry, bcx, bcy;
    at(rest, 0, 0, tlx, tly);
    at(rest, 1024, 0, trx, try_);
    at(rest, 0, 1024, blx, bly);
    at(rest, 1024, 1024, brx, bry);
    at(rest, 512, 1024, bcx, bcy);

    // --- the sheet's iron: C-clips onto the batten, rivets along its hem
    // and down its sides
    for (u32 side = 0; side < 3; ++side) {
        // left, right, and one over the middle, where the two outlines meet
        const i32 x = side == 2 ? (tlx + trx) / 2 : side ? trx - (40 << 10) : tlx + (40 << 10), y = (side ? try_ : tly) - (1 << 10);
        put(frame, art::clamp_l + 2 * side, x, y, unit, 5);
        put(frame, art::clamp_bolt_l + 2 * side, x, y, unit, 5);
    }
    {
        // the sheet as it hangs, leaning but not rippling: the side rivets go with it
        for (u32 k = 0; k < 7; ++k) {
            i32 x, y;
            if (k < 5) at(rest, 80 + static_cast<i32>(k) * 216, 44, x, y);
            else at(hang, k == 5 ? 26 : 998, 700, x, y);
            put(frame, art::rivet_0 + k, x, y, unit, 6);
        }
    }
    {
        // DAMP: a felt pad on a lever hung from the batten beside the sheet's
        // top left corner. Off, it hangs clear of the sheet; turned up, the
        // lever swings down onto the iron and the felt squashes wider.
        const i32 d = t[1];
        const i32 bx = tlx - (8 << 10), by = 62 << 10;
        put(frame, art::damper_bracket, bx, by, unit, 5);
        const i32 ang = 1100 - ((750 * d) >> 10);   // tenths of a degree below the horizontal
        const Turn r = turned(ang);
        put(frame, art::damper_arm, bx, by, unit, 8, ang);
        put(frame, art::damper_pivot, bx, by, unit, 5);
        const i32 ex = bx + 32 * r.cos, ey = by + 32 * r.sin;
        const i32 sq = unit + ((d * 300) >> 10);
        put(frame, art::damper_plate, ex, ey, sq, 5);
        put(frame, art::damper_pad, ex, ey, sq, 8);
        put_shaded(frame, art::damper_felt_a, ex, ey, sq, 3 + ((d * 3) >> 10));
        put_shaded(frame, art::damper_felt_b, ex, ey, sq, 3 + ((d * 3) >> 10));
        put_shaded(frame, art::damper_felt_c, ex, ey, sq, 3 + ((d * 3) >> 10));
    }
    for (u32 side = 0; side < 2; ++side) {
        // the stays, wire rope from a grommet in each lower corner out to an
        // eye bolt on its post; TENSION screws the turnbuckles in
        i32 x0, y0;
        at(rest, side ? 984 : 40, 950, x0, y0);
        const i32 x1 = (side ? 465 : 15) << 10, y1 = 200 << 10;
        const i32 dx = x1 - x0, dy = y1 - y0;
        const i32 len2 = (dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4);
        i32 len = 0;
        for (i32 b = 1 << 13; b; b >>= 1) if ((len + b) * (len + b) <= len2) len += b;   // sqrt, Q6
        len <<= 4;
        const i32 ang = angle_of(dx, dy);
        put(frame, side ? art::stay_r : art::stay_l, x0, y0, clampi((len * 1024) / (100 << 10), 300, 2047), 6, ang);   // authored 100 long; a long 1 px diagonal, a step brighter
        put(frame, side ? art::grommet_r : art::grommet_l, x0, y0, unit, 5);
        const i32 f = 560 - ((tens * 160) >> 10);   // along the stay, Q10
        const i32 bx = x0 + ((dx * f) >> 10), byy = y0 + ((dy * f) >> 10);
        const i32 bs = unit + (((1024 - tens) * 500) >> 10);   // an unscrewed turnbuckle is longer
        put(frame, side ? art::buckle_r : art::buckle_l, bx, byy, bs, 5, ang);
    }
    if (m != M::BOW) {
        // the rigging sandbag on its snap hook under the sheet (taken off to
        // bow the edge): GLIDE weighs it (BOOM)
        i32 g = look.glide >= 0 ? 650 + ((t[look.glide] * 760) >> 10) : 1024;   // 0.65..1.4
        const i32 room = (219 << 10) - bcy;
        if (g > room / 27) g = room / 27;
        // it rides the sheet's foot as a hit throws it, in whole pixels
        i32 hx, hy;
        at(s, 512, 1024, hx, hy);
        hy = bcy + (((hy - bcy) + 512) & ~1023);
        const i32 low = (220 << 10) - 27 * g;   // and never off the bottom of the screen
        if (hy > low) hy = low;
        put(frame, art::bag_hook, bcx, hy, g, 5);
        put(frame, art::bag_rope, bcx, hy, g, 5);
        put(frame, art::bag_neck, bcx, hy, g, 5);
        put(frame, art::bag, bcx, hy, g, 8);
        put(frame, art::bag_seam, bcx, hy, g, 5);
    } else {
        for (u32 n = art::bag_hook; n <= art::bag_seam; ++n) hide(frame, n);
    }
    {
        // CHAIN: links leave the cleat on the left post and lie across the
        // sheet's lower face as one run, growing from its left; the rest
        // hang from the cleat as another. A link never floats between them:
        // the one moving fades out of the hanging run and into the lying one.
        // LINKS lengthens them (SLAP)
        const i32 on = look.chain >= 0 ? t[look.chain] * 12 : 0;   // Q10 links
        const i32 size = m == M::SLAP ? 1100 + ((t[6] * 500) >> 10) : 1280;   // drawn large enough to read as iron links
        const i32 hop_on = amp_q > 2 && state.age < 400;
        const i32 pitch = (13 * size) / (2 * (rest.w >> 10));   // 6.5 px a link at unit size, in u
        for (u32 k = 0; k < 12; ++k) {
            const i32 j = static_cast<i32>(k);
            const i32 f = clampi(on - (11 - j) * 1024, 0, 1024);   // the free end leaves first
            if (f < 512) {
                // hanging from the cleat, interlocked, fading as it leaves
                // larger than the lying run, so each link reads on the post
                const i32 hang = 1480;
                const i32 hang_y = (102 << 10) + ((j * 6656 * hang) >> 10);
                put_shaded(frame, art::link_0 + k, 17 << 10, hang_y, (hang << 10) / link_k, 5 - ((f * 9) >> 10), 900);
            } else {
                // lying on the sheet: the free end at the left, a gentle sag
                const i32 n = 11 - j;   // its place in the run
                const i32 u = 120 + n * pitch;
                i32 x, y;
                at(rest, u, 840 + ((sn(u * 40) * 30) >> 14), x, y);
                if (hop_on && state.age >= n * 23) {
                    // the rattle runs along the chain from its free end, each
                    // link in its own steps of 72 ms, so a frame moves only a
                    // few of them
                    const i32 tq = ((state.age - n * 23) / 72) * 72 + 36;
                    const i32 aq = tq < hold ? 64 : fall(tq - hold, tau) >> 9;   // the ring as it was at that step
                    y -= ((aq * (iabs(sn(tq * 96 + j * 7000)) >> 6) * 900) >> 14) & ~511;
                }
                const i32 slope = (cs(u * 40) * 3) >> 12;   // follows the sag, tenths of a degree
                put_shaded(frame, art::link_0 + k, x, y, (size << 10) / link_k, 1 + (((f - 512) * 14) >> 10), slope);
            }
        }
    }

    // --- the implement
    // every model's parts: parked unless this model uses them
    const bool mallet = m == M::BOOM || m == M::SLAP || m == M::TOM || m == M::CRASH || m == M::CLANK;
    for (u32 n = art::mallet_handle; n < art::node_count; ++n) hide(frame, n);
    i32 sx, sy;
    const i32 spot = t[3];
    at(rest, 512 + ((380 * spot) >> 10), 512 - ((340 * spot) >> 10), sx, sy);
    i32 swing = 0;   // the mallet's handle: down and right, or down and left under the shackle
    if (m == M::CLANK) {
        // the shackle hangs from the beam by its strap, between the sheet and
        // the right post; SHAPE bends its arms up into a stirrup, BUZZ backs
        // its nut off the pin; the mallet strikes its bar from below
        const i32 px = 436 << 10, py = 90 << 10;   // drawn a third as large again, to read at 1:1
        put(frame, art::shackle_strap, px, py, unit, 5);
        put(frame, art::shackle_eye, px, py, unit, 5);
        put(frame, art::shackle_pin, px, py, unit, 5);
        put(frame, art::shackle_nut, px, py - (11 << 10) - t[7] * 11, unit, 8, (t[7] * 1200) >> 10);
        put(frame, art::shackle_bar, px, py, unit, 8);
        const i32 bend = (t[4] * 800) >> 10;
        put(frame, art::shackle_arm_l, px - (15 << 10), py, unit, 8, bend);
        put(frame, art::shackle_arm_r, px + (15 << 10), py, unit, 8, -bend);
        sx = px - (22 << 10) + spot * 40;
        sy = py + (19 << 10);
        swing = 900;
    }
    if (mallet) {
        // the head lands on SPOT; BEATER turns it from a wrapped felt ball to
        // a wooden barrel to a bare steel rod; a hit throws it back
        const i32 b = t[2];
        const i32 rec = still ? 0 : ((fall(state.age, 61) * 14) >> 5) & ~511;   // Q10 px, in half pixels
        const i32 hx = swing ? sx - rec : sx + rec, hy = sy - rec;
        const i32 ang = swing + (swing ? (rec >> 7) : -(rec >> 7));   // a few degrees back
        put(frame, art::mallet_handle, hx, hy, unit, 8, ang);
        put(frame, art::mallet_grip, hx, hy, unit, 5, ang);
        put(frame, art::mallet_collar, hx, hy, unit, 8, ang);
        put(frame, art::mallet_cord_a, hx, hy, unit, 5, ang);
        put(frame, art::mallet_cord_b, hx, hy, unit, 5, ang);
        const i32 felt = 8 - ((b * 14) >> 10);
        const i32 fs = unit - ((b * 300) >> 10);
        put_shaded(frame, art::mallet_felt, hx, hy, fs, felt, ang);
        put_shaded(frame, art::mallet_wrap_a, hx, hy, fs, felt - 3, ang);
        put_shaded(frame, art::mallet_wrap_b, hx, hy, fs, felt - 3, ang);
        const i32 wood = 8 - (iabs(b - 512) * 20 >> 10);
        put_shaded(frame, art::mallet_wood, hx, hy, unit, wood, ang);
        const i32 rod = ((b - 480) * 15) >> 10;
        put_shaded(frame, art::mallet_rod, hx, hy, unit, rod, ang);
        put_shaded(frame, art::mallet_ferrule, hx, hy, unit, rod - 1, ang);
    }
    if (m == M::TOM) {
        // ROLL: the strikes it bounces, marked round the spot
        const i32 n = t[6] * 7;   // Q10 marks
        for (u32 k = 0; k < 7; ++k) {
            const i32 f = clampi(n - static_cast<i32>(k) * 1024, 0, 1024);
            put_shaded(frame, art::roll_0 + k, sx, sy, unit, (f * 7) >> 10);
        }
    }
    if (m == M::CRASH) {
        // SWELL: the bloom spreading from the strike
        for (u32 k = 0; k < 3; ++k) {
            const i32 f = clampi(t[5] * 3 - static_cast<i32>(k) * 1024, 0, 1024);
            const i32 grow = (amp_q >> 2) * 20;   // in steps, so it redraws only as it visibly shrinks
            put_shaded(frame, art::swell_0 + k, sx, sy, 700 + ((f * 324) >> 10) + grow, (f * 6) >> 10);
        }
    }
    if (look.ring >= 0) {
        // METAL, SHEET: the sheet's own ring, as rings spreading over its
        // face, one more and wider the longer it rings; a hit swells them
        const i32 metal = t[look.ring];
        i32 rx, ry;
        at(rest, 330, 330, rx, ry);
        for (u32 k = 0; k < 3; ++k) {
            const i32 f = clampi(metal * 3 - static_cast<i32>(k) * 1024, 0, 1024);
            const i32 grow = f ? (amp_q >> 2) * 20 : 0;   // in steps, as CRASH's bloom
            put_shaded(frame, art::swell_0 + k, rx, ry, 640 + ((f * 384) >> 10) + grow, f ? 4 + ((f * 4) >> 10) : 0);
        }
    }
    if (m == M::FLICK) {
        // the flicker's tip on SPOT, from the top right corner toward the
        // middle; NAIL from a fingertip to a coin; SCRAPE drags a brush across
        i32 fx, fy;
        at(rest, 860 - ((360 * spot) >> 10), 300 + ((300 * spot) >> 10), fx, fy);
        const i32 sp = still ? 0 : (((fall(state.age, 82) * 90) >> 15) * (cs(static_cast<i32>(static_cast<u32>(state.age) * 64u)) >> 4)) >> 10;
        const i32 fk = 1150, lean = 250;   // drawn large, its grip low over the corner
        put(frame, art::flick_grip, fx, fy, fk, 5, lean + (sp >> 1));
        put(frame, art::flick_ferrule, fx, fy, fk, 8, lean + (sp >> 1));
        put(frame, art::flick_strip, fx, fy, fk, 8, lean + sp);
        put_shaded(frame, art::flick_pad, fx, fy, fk, 8 - ((t[2] * 11) >> 10), lean + sp);
        put_shaded(frame, art::flick_coin, fx, fy, fk, ((t[2] * 11) >> 10) - 3, lean + sp);
        put_shaded(frame, art::flick_scuff, fx, fy, 300 + ((t[7] * 1600) >> 10), (t[7] * 7) >> 10);
    }
    if (m == M::POP) {
        // a G-clamp on the lower edge; SNAP drives its screw in; POPS dents
        // the sheet, SPREAD spaces them, SCATTER scatters them
        i32 cx0, cy0;
        at(rest, 700, 1024, cx0, cy0);
        const i32 drive = t[4] * 16;   // Q10 px: the screw driven in
        const i32 jolt = still ? 0 : ((fall(state.age, 60) * 4) >> 5) & ~511;
        put(frame, art::pop_frame, cx0, cy0, unit, 5);
        put(frame, art::pop_screw, cx0, cy0 + drive, unit, 8);
        put(frame, art::pop_thread_a, cx0, cy0 + drive, unit, 5);
        put(frame, art::pop_thread_b, cx0, cy0 + drive, unit, 5);
        put(frame, art::pop_pad, cx0, cy0 + drive, unit, 8);
        const i32 ty = cy0 - (63 << 10) + drive - jolt;   // the T-bar on the screw's head
        put(frame, art::pop_handle, cx0, ty, unit, 8);
        put(frame, art::pop_knob_l, cx0, ty, unit, 8);
        put(frame, art::pop_knob_r, cx0, ty, unit, 8);
        const i32 n = 1 + ((t[2] * 6 + 512) >> 10);
        const i32 gap = (10 << 10) + t[3] * 16;
        i32 dx0, dy0;
        at(rest, 470, 560, dx0, dy0);
        for (i32 k = 0; k < 7; ++k) {
            if (k >= n) continue;
            const i32 off = (2 * k - (n - 1)) * gap / 2;
            const i32 jx = (sn(k * 23000 + 3000) * t[7]) >> 10, jy = (sn(k * 17000 + 9000) * t[7]) >> 11;
            // each dent snaps through in turn as it pops
            const i32 lit = amp_q > 4 && state.age < (k + 1) * 30 + 60 ? 8 : 6;
            put(frame, art::dent_0 + static_cast<u32>(k), dx0 + off + jx, dy0 + jy, unit + (lit == 8 ? 200 : 0), static_cast<u32>(lit));
        }
    }
    if (m == M::GRIT) {
        // a scoop tipped by POUR, beads falling from its lip: GRAINS how
        // many, BEAD how big, SWELL whether they start in a rush
        const i32 hx = trx + (26 << 10), hy = 84 << 10;   // held off its top right corner
        const i32 ang = -60 - ((t[7] * 420) >> 10);
        put(frame, art::scoop_handle, hx, hy, unit, 5, ang);
        put(frame, art::scoop_bowl, hx, hy, unit, 8, ang);
        put(frame, art::scoop_lip, hx, hy, unit, 5, ang);
        put(frame, art::scoop_ring, hx, hy, unit, 5, ang);
        const Turn r = turned(ang);
        const i32 lx = hx - 66 * r.cos, ly = hy - 66 * r.sin;   // the lip
        // the stream falls straight down from the lip onto the sheet's face
        i32 tx, ty;
        at(rest, clampi(((lx - rest.cx) << 6) / (rest.w >> 4) + 512, 0, 1024), 800, tx, ty);
        tx = lx;
        const i32 n = 2 + ((t[3] * 8) >> 10);
        const i32 size = 800 + ((t[2] * 1000) >> 10);
        const i32 drop = still ? 0 : (state.age < hold + 200 ? (state.age * 8) & 1023 : 0);
        for (i32 k = 0; k < 10; ++k) {
            if (k >= n) continue;
            // spaced down the stream: bunched at the lip (a rush), or spread to the sheet
            const i32 f0 = (k * 1024) / n;
            const i32 f = ((f0 + drop / n) & 1023);
            const i32 bunch = lerp((f * f) >> 10, f, t[4]);
            put(frame, art::bead_0 + static_cast<u32>(k), lerp(lx, tx, bunch) + ((sn(k * 9000) * 3) >> 4), lerp(ly + (4 << 10), ty, bunch), (size << 10) / bead_k, 8);
        }
    }
    if (m == M::HOP) {
        // the ball waits at the height it drops from (DROP), over where it
        // lands (SPOT); LOSS shrinks each bounce; BALL from rubber to steel
        i32 lx, ly;
        at(rest, 300 + ((480 * spot) >> 10), 760, lx, ly);
        const i32 e = lerp(901, 563, t[5]);   // restitution, Q10
        const i32 height = (20 << 10) + t[4] * 50;   // Q10 px
        i32 px = lx, h = height;
        i32 sx0 = lx;
        for (u32 k = 0; k < 3; ++k) {
            h = (((h * e) >> 10) * e) >> 10;   // each bounce e^2 as high
            const i32 sc = h / 36;
            const i32 wid = 40 * sc;
            put_shaded(frame, art::arc_0 + k, px + wid, ly, clampi(sc, 120, 2047), 5 - static_cast<i32>(k));
            px += wid;
        }
        // the ball: resting high, or in flight after a hit
        i32 bx = sx0, by = ly - height;
        if (!still && state.age < hold + 600) {
            const i32 first = static_cast<i32>(pow2v(lmix(L(0.06), L(0.38), t[4])) >> 6);   // Q10 s
            i32 at_ = state.age, gap = first, hh = height, x0 = sx0;
            // the fall: half a gap
            if (at_ < first / 2) {
                const i32 p = (at_ << 11) / (first > 1 ? first : 1);   // Q10 through the fall
                by = ly - hh + ((hh * ((p * p) >> 10)) >> 10);
            } else {
                at_ -= first / 2;
                for (u32 k = 0; k < 8; ++k) {
                    hh = (((hh * e) >> 10) * e) >> 10;
                    gap = (gap * e) >> 10;
                    const i32 wid = 40 * (hh / 36);
                    if (at_ < gap && gap > 0) {
                        const i32 p = (at_ << 10) / gap;   // Q10 through the bounce
                        bx = x0 + ((wid >> 2) * p >> 8);
                        by = ly - ((hh * ((4 * p * (1024 - p)) >> 10)) >> 10);
                        break;
                    }
                    at_ -= gap;
                    x0 += wid;
                    bx = x0;
                    by = ly;
                }
            }
        }
        const i32 bs = 1500 - ((t[2] * 640) >> 10);   // rubber (big) to a steel bearing
        put(frame, art::ball, bx, by, bs, 8);
        put_shaded(frame, art::ball_band, bx, by, bs, 6 - ((t[2] * 8) >> 10));
        put_shaded(frame, art::ball_shine, bx, by, bs, ((t[2] * 9) >> 10) - 1);
    }
    if (m == M::ZAP) {
        // a pickup clipped at the far corner, the stylus tapping SWEEP away
        // (the farther, the longer the fall); CURVE leans it; ECHOS the
        // wavefronts it sends back
        i32 px, py;
        at(rest, 940, 870, px, py);
        put(frame, art::pickup, px, py, unit, 8);
        put(frame, art::pickup_core, px, py, unit, 5);
        put(frame, art::pickup_cable, px, py, unit, 5);
        put(frame, art::pickup_clip, px, py, unit, 5);
        const i32 d = 200 + ((t[2] * 560) >> 10);
        i32 qx, qy;
        at(rest, 940 - d, 870 - ((d * 700) >> 10), qx, qy);
        const i32 tap = still ? 0 : ((fall(state.age, 61) * 8) >> 5) & ~511;
        put(frame, art::stylus, qx + tap, qy - tap, unit, 8, (t[7] * 500) >> 10);
        put(frame, art::stylus_handle, qx + tap, qy - tap, unit, 5, (t[7] * 500) >> 10);
        put(frame, art::stylus_tip, qx + tap, qy - tap, unit, 8);
        const i32 n = (t[3] * 5 + 512) >> 10;
        for (u32 k = 0; k < 5; ++k) {
            const i32 grow = amp_q ? (amp_q << 3) : 0;
            put_shaded(frame, art::wave_0 + k, px, py, unit + grow, static_cast<i32>(k) < n ? 6 - static_cast<i32>(k) : 0);
        }
    }
    if (m == M::BOW) {
        // the bow under the lower edge at SPOT (which mode sings); PRESS
        // pushes it into the edge and tilts it; SPEED sets how far along its
        // length it has been drawn, and a note draws it
        i32 ex, ey;
        at(rest, 372 + ((t[3] * 300) >> 10), 1024, ex, ey);
        const i32 draw_ = amp_q ? (sn(static_cast<i32>(static_cast<u32>(state.age) * 24u)) * 20 >> 4) : 0;
        const i32 bx = ex + ((512 - t[4]) * 20) + draw_;
        const i32 by = ey + (11 << 10) - t[2] * 10;   // PRESS: from well clear of the edge to against it
        const i32 tilt = -((t[2] * 80) >> 10);       // and 8 degrees of tilt
        put(frame, art::bow_hair, bx, by, unit, 8, tilt);
        put(frame, art::bow_stick, bx, by, unit, 8, tilt);
        put(frame, art::bow_frog, bx, by, unit, 5, tilt);
        put(frame, art::bow_eye, bx, by, unit, 8, tilt);
        put(frame, art::bow_head, bx, by, unit, 5, tilt);
        put(frame, art::bow_button, bx, by, unit, 5, tilt);
    }
    if (m == M::SHAKE) {
        // the handle at the lower left corner: RUMBLE how far it is pulled,
        // RATE how short and quick a throw; CRACK creases the corner
        const u32 hz_ = pow2v(lmix(L(3), L(16), t[3])) >> 12;   // Q4 Hz
        const i32 jerk = amp_q ? ((sn(static_cast<i32>((static_cast<u32>(state.age) * hz_ * 4u) & 0xffffu)) * amp_q) >> 11) : 0;
        const i32 ang = 560 + ((t[2] * 420) >> 10) + jerk;   // out to the left, pulled up further
        const i32 len = unit - ((t[3] * 300) >> 10);
        put(frame, art::handle_grip, blx, bly, len, 8, ang);
        put(frame, art::handle_plate, blx, bly, unit, 5);
        put(frame, art::handle_bolt, blx, bly, unit, 8);
        // CRACK: a crease running up from the corner, longer and brighter
        i32 kx, ky;
        at(rest, 70, 900, kx, ky);
        put_shaded(frame, art::crease, kx, ky, 500 + ((t[6] * 900) >> 10), (t[6] * 8 + 1000) >> 10);
    }

    // --- the header: both rows, the edited one bright; while Shift is held
    // both rows show it: the key's mixer channel (MACHINE, CHOKE, PAN, LEVEL)
    // over its pad controls and sends (TUNE, DRIVE, FX I, FX II)
    const char* titles[8];
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) {
        titles[i] = labels[m][i];
        if (m == M::GRIT && i == 3) grains(values[i], t[i]);
        else caption(values[i], captions[m][i], t[i], tune);
    }
    if (input.shift) {
        static const char* const chokes[4] = {"none", "1", "2", "3"};
        char pan[8], level[8], sends[2][8], tune_[8], drive_[8];
        number(pan, input.pan, "");
        number(level, input.level, "dB");
        for (u32 k = 0; k < 2; ++k) {
            const i32 code = input.pad[2 + k];
            if (code > 0) number(sends[k], ((code > 7 ? 7 : code) - 1) * 100 / 6 + (code == 3 || code == 6 ? 1 : 0), "");
            else copy(sends[k], sizeof(sends[k]), "TRK");
        }
        tune_text(tune_, input.pad[0]);
        drive_text(drive_, input.pad[1]);
        static const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"TUNE", "DRIVE", "FX I", "FX II"}};
        const char* const shift_values[2][4] = {{names[m], chokes[input.choices[1] & 3u], pan, level}, {tune_, drive_, sends[0], sends[1]}};
        // Both rows at once, stacked as the pages: the edited one bright on
        // the bar, the other grey, so Shift swaps all eight names and values.
        for (u32 line = 0; line < 2; ++line)
            for (u32 c = 0; c < 4; ++c) {
                titles[4 * line + c] = shift_titles[line][c];
                copy(values[4 * line + c], header_capacity, shift_values[line][c]);
            }
    }
    page_header(state.header, input, frame, titles, values, art::header_node);
}
}
