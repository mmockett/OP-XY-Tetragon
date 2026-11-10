#include "display.h"

// THUNDER's screen: the iron sheet hung on two wires from a wooden gallows,
// in the stock look (tools/make_panel.py draws it). Encoder 1 sizes the sheet,
// a hollow outline and the one reshaping path; encoder 2 clamps one to four
// felt pads on its lower edge; encoder 3 strips the felt rings off the
// mallet's core; encoder 4 moves the chalk cross where it is struck. On the
// second row the model's TENSION lets the sheet sag on its wires, its CAMBER
// (or DEPTH, or BEND) bows it, and CHAIN lays links across it. A hit sets the
// sheet's free foot rippling in the model's mode shape for as long as the
// model's own sound rings (a kit voice plays to its end: stock gives kits no
// amp envelope); the marks on its face ride the ripple.
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
inline i32 lerp(i32 a, i32 b, i32 t) { return a + (((b - a) >> 2) * t >> 8); }   // t Q10; |b - a| < 2^21
inline i32 clampi(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
// A value a (b / a)^t for t a Q10 unit, as log2 Q16: lerp of the logs.
inline i32 lmix(i32 la, i32 lb, i32 t) { return la + (((lb - la) >> 1) * t >> 9); }
// A Q10 fraction t of three or nine, rounded: how many pads, rings or links.
inline i32 count_of(i32 t, i32 most) { return (t * most + 512) >> 10; }

// ---------------------------------------------------------------- the projection
// A world point (Q10 units: X down to the right, Y down to the left, Z up) on
// screen, Q10 px: the isometric view, scaled to fit the stage (art.h).
inline i32 screen_x(i32 x, i32 y) { return art::ox + (((x - y) * art::kc) >> 10); }
inline i32 screen_y(i32 x, i32 y, i32 z) { return art::oy + (((x + y) * art::kh) >> 10) - ((z * art::kz) >> 10); }
template <class F>
inline void put(F& f, u32 node, i32 x, i32 y, i32 z) {
    place(f, node, screen_x(x, y), screen_y(x, y, z), unit, 0, keep);
}

// ---------------------------------------------------------------- each model's sheet
// Which of the model's controls let the sheet sag (TENSION), bow it (CAMBER,
// DEPTH, BEND) and lay the chain on it (an index into the key's eight, or -1),
// and the mode shape a hit rings it in: m half waves across its foot, n down
// it, hz times a second on screen.
struct Look {
    signed char tension, curve, chain;
    unsigned char m, n, hz;
};
const Look looks[models] = {
    {4, 5, 7, 1, 1, 4},     // M::BOOM
    {4, -1, 5, 2, 1, 7},    // M::SLAP
    {-1, -1, 6, 2, 2, 8},   // M::POP
    {4, -1, 5, 5, 3, 11},   // M::FLICK
    {4, 5, 7, 1, 1, 5},     // M::TOM
    {4, -1, 7, 3, 2, 6},    // M::CRASH
    {-1, -1, 6, 2, 1, 7},   // M::CLANK
    {-1, -1, 6, 4, 2, 9},   // M::GRIT
    {6, -1, 7, 2, 1, 7},    // M::HOP
    {-1, 4, 6, 3, 1, 9},    // M::ZAP: DEPTH bows it
    {5, 7, 6, 2, 1, 5},     // M::BOW: BEND bows it
    {4, -1, 5, 1, 1, 3},    // M::SHAKE
};

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
    const i32 amp_q = motion >> 9;   // 0..64, quantised so that a still sheet is exactly still
    const i32 phase = static_cast<i32>((static_cast<u32>(state.age) * look.hz * 64u) & 0xffffu);   // 2 pi hz age
    // the ripple's depth at the sheet's foot, Q10 world units: up to 2, in
    // eighths, so a dying ripple stops rather than creeps
    const i32 ripple = amp_q ? ((amp_q * 32 * cs(phase)) >> 14) & ~127 : 0;

    // --- the sheet, in the plane y = 22: SIZE sizes it, TENSION lets it sag
    // on its wires, CAMBER bows it out of its plane
    const i32 w = (78 << 10) + 34 * t[0], h = (46 << 10) + 22 * t[0];
    const i32 x0 = (75 << 10) - w / 2;
    const i32 taut = look.tension >= 0 ? t[look.tension] : 1024;
    const i32 top = (80 << 10) - 7 * (1024 - taut);
    const i32 cam = look.curve >= 0 ? 5 * t[look.curve] : 0;
    // the ripple at (u, v) on the sheet (Q10 across and down): clamped at its
    // top, free at its foot, so its shapes run sin((2n - 1) pi v / 2) down it
    const i32 m_ = look.m, n_ = 2 * look.n - 1;
    const i32 ox = screen_x(75 << 10, art::sheet_y), oy = screen_y(75 << 10, art::sheet_y, top - h / 2);
    u32 key = 2166136261u;
    const i32 shape[6] = {w, h, top, cam, ripple, static_cast<i32>(m)};
    for (u32 i = 0; i < 6; ++i) key = (key ^ static_cast<u32>(shape[i])) * 16777619u;
    if (!key) key = 1;
    if (frame.paths[0].key != key) {
        // its top edge left to right, then its foot right to left, 32 points each
        Frame::path_type& o = frame.paths[0];
        for (i32 k = 0; k < 32; ++k) {
            const i32 u = (k * 1024) / 31;
            const i32 x = x0 + ((w >> 4) * u >> 6);
            const i32 y = art::sheet_y + ((cam * sn(u * 32)) >> 14);
            o.points[k].x = screen_x(x, y) - ox;
            o.points[k].y = screen_y(x, y, top) - oy;
            const i32 ub = 1024 - u;
            const i32 xb = x0 + ((w >> 4) * ub >> 6);
            const i32 yb = art::sheet_y + ((cam * sn(ub * 32)) >> 14) + ((ripple * sn(m_ * ub * 32)) >> 14);
            o.points[32 + k].x = screen_x(xb, yb) - ox;
            o.points[32 + k].y = screen_y(xb, yb, top - h) - oy;
        }
        o.key = key;
    }
    place(frame, 0, ox, oy, unit, art::sheet_shade, stroke);
    // the depth the ripple gives a point on the face at (u, v), in quarters
    struct Face {
        i32 left, width, depth, across, down;
        i32 at(i32 x, i32 v) const {
            if (!depth) return 0;
            const i32 u = clampi(((x - left) << 4) / (width >> 6), 0, 1024);
            return (((depth * sn(across * u * 32)) >> 14) * sn(down * v * 16) >> 14) & ~255;
        }
    } face = {x0, w, ripple, m_, n_};

    // the wires, from the beam down to the sheet's top, 8 in from its sides
    for (u32 side = 0; side < 2; ++side) {
        const i32 x = side ? x0 + w - art::wire_in : x0 + art::wire_in;
        put(frame, side ? art::wire_r_a : art::wire_l_a, x, art::sheet_y, art::beam_z - art::wire_a);
        put(frame, side ? art::wire_r_b : art::wire_l_b, x, art::sheet_y, top);
    }
    // the hammered marks on its face
    static const i32 marks[3][2] = {{307, 358}, {635, 563}, {461, 799}};   // across, down: Q10
    for (u32 k = 0; k < art::marks; ++k) {
        const i32 x = x0 + ((w >> 4) * marks[k][0] >> 6);
        put(frame, art::mark_0 + k, x, art::sheet_y + face.at(x, marks[k][1]), top - ((h >> 4) * marks[k][1] >> 6));
    }
    // SPOT (encoder 4): the chalk cross where it is struck, a fifth of the way
    // across to four fifths
    {
        const i32 x = x0 + ((w >> 4) * lerp(205, 819, t[3]) >> 6);
        const i32 z = top - ((h >> 4) * 430 >> 6);
        const i32 y = art::sheet_y + face.at(x, 430);
        put(frame, art::cross_a, x, y, z);
        put(frame, art::cross_b, x, y, z);
    }
    // CHAIN: up to nine links lying across the sheet's lower face
    {
        const i32 n = look.chain >= 0 ? count_of(t[look.chain], 9) : 0;
        for (i32 k = 0; k < 9; ++k) {
            if (k >= n) { hide(frame, art::link_0 + static_cast<u32>(k)); continue; }
            const i32 x = x0 + ((10 + 7 * k) << 10);
            const i32 z = top - ((h >> 4) * 717 >> 6) - ((k & 1) << 10);
            put(frame, art::link_0 + static_cast<u32>(k), x, (21 << 10) + face.at(x, 717), z);
        }
    }
    // DAMP (encoder 2): one to four felt pads clamped on the sheet's lower
    // edge, from its right
    {
        const i32 n = 1 + count_of(t[1], 3);
        for (i32 k = 0; k < 4; ++k) {
            const u32 node = art::pad_0_left + 3 * static_cast<u32>(k);
            if (k >= n) {
                for (u32 f = 0; f < 3; ++f) hide(frame, node + f);
                continue;
            }
            const i32 x = x0 + w - (14 << 10) - k * (11 << 10);
            for (u32 f = 0; f < 3; ++f) put(frame, node + f, x, 19 << 10, top - h - (3 << 10));
        }
    }
    // encoder 3 (BEATER, and the model's third control): the mallet's felt
    // rings, three at 0, stripped off one by one to its bare wooden core
    {
        const i32 rings = 3 - count_of(t[2], 3);
        for (i32 k = 1; k <= 3; ++k) {
            const u32 node = art::ring_1_side - 2 * static_cast<u32>(k - 1);
            for (u32 f = 0; f < 2; ++f) {
                if (k <= rings) place(frame, node + f, 0, 0, unit, 0, keep);
                else hide(frame, node + f);
            }
        }
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
