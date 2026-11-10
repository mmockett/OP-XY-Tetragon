#include "display.h"

// CURIO's screen: the cabinet's drawer pulled open, in the stock isometric
// look (art.h, panel.svg.fragment, from tools/make_panel.py). A small drum
// on a felt bed, the release clamp on its stand and the ball over the head;
// every key's specimen shares the drawer, and the card holder on its front
// carries the selected specimen's symbol. The controls move parts by their
// encoder: 1 the clamp (DROP), 2 the bounce marks (BOUNCE), 3 the rods (SKIN),
// 4 the ball (BALL); on the second row 1 the damper on the head (DECAY), 2
// the crate (FLOOR), 3 the spot (SPOT), 4 the grains (GRIT), each the first
// row's control in its place for the specimen in hand. A hit drops the ball
// down the plumb line onto the head; it bounces to each mark, rests on the
// head while the sound lasts and is back under the clamp 200 ms after.
//
// A hit's motion lasts as long as its sound, the specimen's own decay: a drum
// kit's voices play until their sound ends, untouched by the track's amp
// envelope and by note-off (the drum renderer's own amp setter reads
// neither), so the screen follows neither. Only the selected key's notes
// reach the screen (the SDK's per-key serial).
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

// ---------------------------------------------------------------- the drop
// A whole-number square root, for small numbers.
inline i32 root(i32 v) {
    i32 r = 0;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}
// The time a fall of h (Q10 px) takes, ms: 40 ms for each root pixel.
inline i32 fall_ms(i32 h) { return 5 * root(h >> 4); }
// The ball's height over the head (Q10 px), age ms into a hit: down the
// plumb line from its rest (rest), then a bounce to each mark's height (each
// the last times e, Q10, from the whole drop len, while at least 3 px), then
// still on the head.
inline i32 ball_height(i32 len, i32 rest, i32 e, i32 age) {
    const i32 tf = fall_ms(rest);
    if (age < tf) {
        const i32 q = age * 1024 / tf;
        return rest - (rest * (q * q >> 10) >> 10);
    }
    i32 t = age - tf, h = len;
    for (u32 k = 0; k < 5; ++k) {
        h = h * e >> 10;
        if (h < 3 * u) break;
        const i32 span = 2 * fall_ms(h);
        if (t < span) {
            const i32 q = t * 1024 / span;
            return 4 * (h * (q * (1024 - q) >> 10) >> 10);
        }
        t -= span;
    }
    return 0;
}
using Path = Frame::path_type;
inline void put(Frame& frame, u32 node, i32 x, i32 y) { place(frame, node, x, y, u, 0, keep); }
inline void put_box(Frame& frame, u32 left, i32 x, i32 y) {
    for (u32 f = 0; f < 3; ++f) put(frame, left + f, x, y);
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

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    const u32 m = input.model < kit::models ? input.model : 0;
    state.notes.update(input);
    const i32 dt = clamp(input.elapsed * 1000 >> 10, 0, 100);
    if (!state.initialized || input.track != state.track) {
        state.serial = input.note_serial;
        state.track = input.track;
        state.age = idle;
    } else if (input.note_serial != state.serial) {
        state.serial = input.note_serial;
        state.age = input.reduced_motion ? idle : 0;
    } else if (state.age >= 0) {
        state.age += dt;
    }
    i32 c[8];   // the controls, Q12
    for (u32 i = 0; i < 8; ++i) c[i] = (input.slots[i] < 0 ? 0 : input.slots[i]) >> 19;
    state.life = life_ms(m, c);
    // Done: the ball is back under the clamp 200 ms after the sound.
    if (state.age > state.life + 200) state.age = idle;
    state.initialized = 1;
    state.shown = m + 1;
    const i32 age = state.age, life = state.life;

    // 1 DROP: the clamp and the stand's upper length rise; the plumb line, a
    // thin ribbon from the clamp's height to the head, lengthens.
    const i32 lift = art::drop_span * c[0] / 4095;
    const i32 len = art::drop_lo + lift;
    put_box(frame, art::arm_left, art::arm_x, art::arm_y - lift);
    put_box(frame, art::stand_left, art::stand_x, art::stand_y - lift);
    {
        Path& p = frame.paths[art::plumb];
        p.points[0].x = 128;  p.points[0].y = 0;
        p.points[1].x = -128; p.points[1].y = 0;
        p.points[2].x = -128; p.points[2].y = -len;
        p.points[3].x = 128;  p.points[3].y = -len;
        p.key = static_cast<u32>(len) * 2u + 1u;
        put(frame, art::plumb, art::plumb_x, art::plumb_y);
    }
    // 2 BOUNCE: a mark at the top of each bounce, each the last times e, while at least 3 px.
    const i32 e = 256 + 563 * c[1] / 4095;
    {
        i32 h = len;
        bool more = true;
        for (u32 k = 0; k < 5; ++k) {
            h = h * e >> 10;
            if (h < 3 * u) more = false;
            if (more) put(frame, art::mark_0 + k, art::mark_x[k], art::mark_y[k] - h);
            else hide(frame, art::mark_0 + k);
        }
    }
    // 3 SKIN: 4 to 10 rods round the drum.
    {
        const i32 n = 4 + (12 * c[2] + 4095) / 8190;
        for (i32 k = 0; k < 10; ++k) {
            if (k < n) put(frame, art::rod_0 + static_cast<u32>(k), art::rod_x[n - 4][k], art::rod_y[n - 4][k]);
            else hide(frame, art::rod_0 + static_cast<u32>(k));
        }
    }
    // 4 BALL: one of five sizes, resting 0.55 of the way up the plumb line; a
    // hit drops it (above).
    {
        const i32 size = (c[3] * 5) >> 12;
        const i32 rest = len * 563 >> 10;
        i32 z = rest;
        if (age >= 0 && age < life) z = ball_height(len, rest, e, age);
        else if (age >= life) {
            const i32 at = ball_height(len, rest, e, life);
            z = at + (rest - at) * (age - life) / 200;
        }
        for (i32 k = 0; k < 5; ++k) {
            const u32 n = art::ball_0 + 2 * static_cast<u32>(k);
            if (k == size) {
                put(frame, n, art::plumb_x, art::plumb_y - z);
                put(frame, n + 1, art::plumb_x, art::plumb_y - z);
            } else {
                hide(frame, n);
                hide(frame, n + 1);
            }
        }
    }
    // 5 DECAY: the damper slides toward the rim, off the middle of the head.
    put_box(frame, art::pad_left, art::pad_x + art::pad_dx * c[4] / 4095, art::pad_y + art::pad_dy * c[4] / 4095);
    // 6 FLOOR: the crate slides toward the drum.
    put_box(frame, art::crate_left, art::crate_x + art::crate_dx * c[5] / 4095, art::crate_y + art::crate_dy * c[5] / 4095);
    // 7 SPOT: the ring on the head, from its middle toward the rim.
    put(frame, art::spot, art::spot_x + art::spot_dx * c[6] / 4095, art::spot_y + art::spot_dy * c[6] / 4095);
    // 8 GRIT: 0 to 10 grains on the head.
    {
        const i32 n = (c[7] * 10 + 2047) / 4095;
        for (i32 k = 0; k < 10; ++k) {
            if (k < n) put(frame, art::grit_0 + static_cast<u32>(k), art::grit_x[k], art::grit_y[k]);
            else hide(frame, art::grit_0 + static_cast<u32>(k));
        }
    }
    // The specimen's symbol on the card holder.
    for (u32 s = 0; s < kit::models; ++s) {
        if (s == m) put(frame, art::symbol_first + s, art::symbol_x, art::symbol_y);
        else hide(frame, art::symbol_first + s);
    }


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
