#include "display.h"
#include "models.hpp"
#include "human.hpp"
#include "kit.h"
#include "pads.hpp"

// Syahi's screen: a tabla in the stock isometric look (art.h,
// panel.svg.fragment). The ring it sits on is artwork; everything else is a
// node placed from the selected key's controls, whatever its model:
//   TUNE    the four tuning blocks slide down the straps (encoder 1, page 1)
//   DECAY   the resting palm's pad on the head shrinks as the ring lengthens
//   LOAD    the syahi builds up, ring by ring, its black disc widening
//   PRESS   more of the head's six nodal lines are lit
//   FORM    the body loses bands, four for a skin down to one for a gong,
//           and the head and everything on it goes down with it
//   SPOT    the stroke's spot moves out from the centre (HUMAN moves it hit to hit)
// A hit lights all six nodal lines white; they go back one by one over the
// ring's time to fall 60 dB. Nothing changes at rest.
namespace syahi {
namespace screen {
using namespace opxy::draw;
const i32 U = opxy::draw::unit;   // Q10 pixel

inline i32 dclamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
// A control's 0..127 rounded onto 0..n steps, as round(value / 100 * n).
inline i32 steps(u32 raw, i32 n) { return static_cast<i32>((raw * 2u * static_cast<u32>(n) + 127u) / 254u); }

struct Look {
    u32 model;
    u32 raw[8];
    Spec spec;
};

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
// A time in log2 s (Q16) as milliseconds.
inline i32 milliseconds(i32 log2_s) { return static_cast<i32>(exp2i(dclamp(log2_s + lg(1000), 0, 20 * 65536))); }
// A time in log2 s (Q16): "35ms", "0.47s", "2.6s".
inline void time_text(char* out, i32 log2_s) {
    const i32 ms = milliseconds(log2_s);
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

// ---- the drum
// A drum node where the artwork draws it, moved by (dx, dy) Q10 px, at a scale.
template <class F> inline void at(F& frame, u32 node, i32 dx = 0, i32 dy = 0, i32 scale = U) {
    place(frame, node, art::at_x[node - art::first] + dx, art::at_y[node - art::first] + dy, scale, 0, keep);
}
}

// No frame changes more than this many nodes. The header's bars and the nodal
// lines always go; the rest take what is left, from the head down, and the
// others follow in the next frames.
const u32 node_budget = 26;
inline bool moved(const opxy::draw::Node& a, const opxy::draw::Node& b) {
    return a.x != b.x || a.y != b.y || a.scale != b.scale || a.cos != b.cos || a.sin != b.sin || a.shade != b.shade || a.paint != b.paint;
}
inline bool always(u32 n) { return n < art::first || (n >= art::nodal_0 && n <= art::nodal_5); }
inline void hold_to_budget(DisplayState& state, Frame& frame) {
    u32 used = 0;
    for (u32 n = 0; n < Frame::node_count; ++n)
        if (always(n) && moved(frame.nodes[n], state.before[n])) ++used;
    for (u32 n = Frame::node_count; n-- > art::first;) {
        if (always(n) || !moved(frame.nodes[n], state.before[n])) continue;
        if (used < node_budget) ++used;
        else frame.nodes[n] = state.before[n];   // next frame
    }
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.initialized = 0;
    state.age = 1 << 20;
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
    for (u32 n = 0; n < Frame::node_count; ++n) state.before[n] = frame.nodes[n];

    Look k;
    k.model = input.model < kit::models ? input.model : 0;
    for (u32 i = 0; i < 8; ++i) k.raw[i] = static_cast<u32>(input.slots[i] < 0 ? 0 : input.slots[i]) >> 24;
    i32 fractions[8];
    for (u32 i = 0; i < 8; ++i) fractions[i] = fraction(k.raw[i]);
    spec(k.model, fractions, k.spec);

    // FORM: the body's bands, four for a skin down to one for a gong; the head
    // and everything on it sits on the top band.
    const i32 bands = art::bands - steps(k.raw[4], 3);
    for (u32 b = 0; b < static_cast<u32>(art::bands); ++b) {
        const bool on = static_cast<i32>(b) < bands;
        if (on) at(frame, art::band_0 + b);
        else hide(frame, art::band_0 + b);
        for (u32 j = 0; j < 7; ++j) {
            if (on) at(frame, art::strap_0_0 + 7 * b + j);
            else hide(frame, art::strap_0_0 + 7 * b + j);
        }
    }
    const i32 top = art::z_body + art::band_h * bands;              // the body's top on screen, Q10
    const i32 lift = art::z_top - top;                              // the head's drop on screen, Q10
    // TUNE: the tuning blocks slide down the straps, from just under the head to the foot.
    const i32 high = top - art::block_drop;
    const i32 foot = high + (art::block_lo - high) * static_cast<i32>(k.raw[0]) / 127;
    for (u32 i = art::block_0_left; i <= art::block_3_top; ++i) at(frame, i, 0, art::block_ref - foot);
    // The head.
    at(frame, art::head_side, 0, lift);
    at(frame, art::head_top, 0, lift);
    // PRESS: how many nodal lines are lit; a hit lights all six white, and they go
    // back one by one, the last first, over the ring's time to fall 60 dB.
    const i32 lit = 1 + steps(k.raw[3], 5);
    i32 struck = 0;
    if (!input.reduced_motion) {
        const i32 ring = dclamp(milliseconds(k.spec.decay) * 128 / 125, 64, 1 << 20);   // Q10 s
        if (state.age < ring) struck = (6 * (ring - state.age) + ring - 1) / ring;
    }
    for (u32 i = 0; i < 6; ++i) {
        const bool diameter = i < 4;
        const u32 shade = static_cast<i32>(i) < struck ? (diameter ? art::diameter_struck : art::circle_struck)
                        : static_cast<i32>(i) < lit ? (diameter ? art::diameter_lit : art::circle_lit)
                                                    : (diameter ? art::diameter_dim : art::circle_dim);
        place(frame, art::nodal_0 + i, art::at_x[art::nodal_0 + i - art::first], art::at_y[art::nodal_0 + i - art::first] + lift,
              U, shade, stroke);
    }
    // LOAD: the syahi's rings, one to five, and its black disc out past the last.
    const i32 rings = 1 + steps(k.raw[2], 4);
    at(frame, art::syahi, 0, lift, (art::syahi_r0 + rings * art::syahi_step) * U / (art::syahi_r0 + art::syahi_rings * art::syahi_step));
    for (u32 r = 0; r < static_cast<u32>(art::syahi_rings); ++r) {
        if (static_cast<i32>(r) < rings) at(frame, art::syahi_ring_0 + r, 0, lift);
        else hide(frame, art::syahi_ring_0 + r);
    }
    // DECAY: the resting palm's pad, large for a short ring, small for a long one.
    const i32 pad = art::pad_max - (art::pad_max - art::pad_min) * static_cast<i32>(k.raw[1]) / 127;
    at(frame, art::pad, 0, lift, pad * U / art::pad_max);
    // SPOT: where the stroke lands, out from the centre; HUMAN nudges this hit's,
    // as the engine does, from the key's note serial.
    const Variation v = vary(input.note_serial, input.pad[0]);
    const i32 spot = dclamp(static_cast<i32>(k.raw[5]) + v.spot, 0, 127);
    const i32 out = art::spot_lo + (art::spot_hi - art::spot_lo) * spot / 127;   // Q15
    at(frame, art::spot, (art::spot_run >> 5) * out >> 10, lift);

    // ---- the header: both rows; with Shift held both are Shift's, the edited one bright.
    static const char* const chokes[4] = {"none", "1", "2", "3"};
    const char* titles[8];
    char values[8][header_capacity];
    for (u32 i = 0; i < 8; ++i) {
        titles[i] = kit::labels[k.model][i];
        value_text(values[i], k, i);
    }
    if (input.shift) {
        char pan[8], level[8], sends[2][4], human[16], grit_text[16];
        u32 n = 0;
        n = put_number(pan, 0, input.pan);
        pan[n] = 0;
        // The level in plain dB, as PAN is plain: "-30dB" is 56.7 px at the header's 20 px.
        n = put_number(level, 0, input.level);
        level[n] = 0;
        for (u32 s = 0; s < 2; ++s) {
            const i32 code = input.pad[2 + s];
            if (code > 0) { n = put_number(sends[s], 0, (((code > 7 ? 7 : code) - 1) * 100 + 3) / 6); sends[s][n] = 0; }
            else copy(sends[s], sizeof(sends[s]), "TRK");
        }
        pad_caption(0, input.pad[0], human);
        pad_caption(1, input.pad[1], grit_text);
        static const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"HUMAN", "GRIT", "FX I", "FX II"}};
        // The model in lower case, as FORM's materials are (the upper-case names
        // don't fit), and BOUNCE short: "bounce" is 67 px of the column's 53.
        static const char* const machines[8] = {"boom", "snare", "tom", "tabla", "bar", "metal", "bnce", "wire"};
        const char* const shift_values[2][4] = {{machines[k.model], chokes[input.choices[1] & 3u], pan, level},
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
    if (!baseline) hold_to_budget(state, frame);
}
}
