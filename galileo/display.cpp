#include "display.h"

// GALILEO's screen: Galileo's inclined plane on a bench, in the stock
// isometric look (art.h, tools/make_panel.py). A grooved board slopes from a
// stack of risers down to a trestle, bells hang over its groove, the handful
// of balls waits at its head, and a snare stands at its foot, the bounce from
// the board onto its head dotted between them.
//
// The grey is the encoder's. Page 1 on the drum: the first control its size
// (one of seven copies, smaller as it rises, its head 323232), the second its
// wires (4B4B4B, one to six), the third its ring (969696, the one reshaping
// outline), the fourth a tab sliding up its shell (CDCDCD). Page 2 on the
// plane: TILT the risers (323232, one to four), FRETS the bells (4B4B4B, none
// to nine at Galileo's spacing), BOUNCE the dotted arc (969696), BALLS the
// balls waiting (CDCDCD, one to six). 3D parts only slide or repeat.
//
// A note releases the balls: each rolls down the groove as the engine plays
// it (motion.h), lifting each bell it passes, flies the arc onto the head and
// hops to rest there; once the last has rested they wait at the head again.
namespace galileo {
namespace view {
using opxy::draw::unit;
using opxy::draw::keep;
struct P { i32 x, y; };
inline P at(i32 x, i32 y) { P p = {x, y}; return p; }
// A plan point (Q10 px) on screen (Q10).
inline P iso(i32 x, i32 y, i32 z) {
    return at(art::ox + (((x - y) * art::cos30) >> 10), art::oy + ((x + y) >> 1) - z);
}
// A raw control (0..127) as a Q10 share, and as one of n + 1 steps, rounded.
inline i32 share(u32 raw) { return static_cast<i32>((raw > 127 ? 127 : raw) * 8257u >> 10); }
inline u32 steps(u32 raw, u32 n) { return ((raw > 127 ? 127 : raw) * n + 63) / 127u; }
inline i32 lerp(i32 a, i32 b, i32 q10) { return a + (((b - a) >> 2) * q10 >> 8); }

template <class F> inline void put(F& f, u32 n, P where) { opxy::draw::place(f, n, where.x, where.y, unit, 0, keep); }
template <class F> inline void show(F& f, u32 n) { put(f, n, at(art::home[n][0], art::home[n][1])); }
template <class F> inline void park(F& f, u32 n) { opxy::draw::hide(f, n); }
template <class F> inline void group(F& f, u32 first, u32 count, bool on) {
    for (u32 n = first; n < first + count; ++n) { if (on) show(f, n); else park(f, n); }
}
template <class F> inline void slide(F& f, u32 first, u32 count, P where) {
    for (u32 n = first; n < first + count; ++n) put(f, n, where);
}
inline u32 mix_key(u32 h, i32 v) { return (h ^ static_cast<u32>(v)) * 16777619u; }

// The groove's surface at x (Q10) on a board whose head rests at zh.
inline i32 groove_z(i32 x, i32 zh) {
    return zh + art::board_t + ((((art::foot_z - zh) >> 2) * ((x - art::head_x) >> 2)) / 126 >> 6);
}
inline i32 risers_head(u32 risers) { return art::riser_z0 + static_cast<i32>(risers) * art::riser_h; }
// 2^20 / n^2 for n up to 9: (k / n)^2 in Q10 is k^2 times this, over 2^10.
const u32 inverse_square[10] = {0, 1048576, 262144, 116508, 65536, 41943, 29127, 21400, 16384, 12945};
// sin(pi k / 9), Q10, for the bounce's dots.
const i32 arc_sine[9] = {0, 350, 658, 887, 1008, 1008, 887, 658, 350};

// Seconds (Q10) as samples.
inline u32 samples(i32 age) { return static_cast<u32>(dsp::clamp(age, 0, 40 * 1024)) * 44100u >> 10; }
// The roll the screen shows is never quicker than 0.1 s: where the sound lands
// sooner (no frets), it rolls over that, and all after is shown that much
// later. With reduced motion it follows the sound.
const u32 min_travel = 4410;
const i32 board_share = 58000;     // of the roll's Q16: down the board; the rest the arc
const u32 ring_time = 3528;        // 80 ms: how long a bell stays lifted

const char* const chokes[4] = {"none", "1", "2", "3"};
const char* const shift_titles[2][4] = {{"MACHINE", "CHOKE", "PAN", "LEVEL"}, {"SPACE", "FEEL", "FX I", "FX II"}};

// The drum's ring: an isometric circle of radius r (Q10) as 64 points around its centre.
template <class Path> inline void ring(Path& path, i32 r) {
    const u32 key = mix_key(2166136261u, r) | 1u;
    if (path.key == key) return;
    const i32 rx = (r * 1254) >> 10, ry = (r * 724) >> 10;   // r 1.2247, r 0.7071
    for (u32 i = 0; i < 64; ++i) {
        const u32 phase = i * 67108864u;           // 2^32 / 64
        const i32 c = dsp::sine(phase + 0x40000000u) >> 16, s = dsp::sine(phase) >> 16;   // Q15
        path.points[i].x = (rx * c) >> 15;
        path.points[i].y = (ry * s) >> 15;
    }
    path.key = key;
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.initialized = 0;
    state.age = 1 << 20;
    state.clock = 0;
    state.planned = 0;
    state.bell_risers = 0;
    state.learner.reset();
}

inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& f) {
    using namespace view;
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

    // The run, laid out again when the key's motion, its pads or the beat change.
    const u32 p16 = state.learner.p16 ? state.learner.p16 : tempo::start;
    u32 key = 2166136261u;
    for (u32 i = 4; i < 8; ++i) key = mix_key(key, static_cast<i32>(c[i]));
    key = mix_key(mix_key(mix_key(key, space), feel), static_cast<i32>(p16)) | 1u;
    if (key != state.planned) {
        motion::plan(state.run, c[4], c[5], c[6], c[7], space, motion::feel(feel), p16);
        state.planned = key;
    }
    const motion::Run& run = state.run;
    // A kit's voice plays until its own sound ends: nothing on the track (its
    // amp envelope, a note let go) cuts the run, so the screen plays it out.
    const u32 t = samples(state.age);
    const u32 landing = run.at[run.n];
    const u32 shown = !input.reduced_motion && landing < min_travel ? min_travel : landing;
    const u32 late = shown - landing;
    const bool running = state.age < (1 << 19) && t < motion::rest(run) + late + 13230;   // 0.3 s after the last ball rests

    // TILT: one to four risers under the board's head, and the board resting on them.
    const u32 risers = 1 + steps(c[4], 3);
    const i32 zh = risers_head(risers);
    for (u32 k = 1; k < 4; ++k) group(f, art::riser1_left + (k - 1) * art::riser_nodes, art::riser_nodes, k < risers);
    for (u32 k = 0; k < 4; ++k) group(f, art::board0_left + k * art::board_nodes, art::board_nodes, k + 1 == risers);

    // Page 1 on the drum: its size, its wires, its ring and its tab.
    const u32 size = steps(c[0], art::sizes - 1);
    const i32 r = lerp(art::r_big, art::r_small, static_cast<i32>((size << 10) / (art::sizes - 1)));
    const u32 wires = 1 + steps(c[1], art::wires - 1);
    for (u32 s = 0; s < art::sizes; ++s) {
        const u32 first = art::drum0_shell + s * art::drum_nodes;
        group(f, first, 3, s == size);
        for (u32 k = 0; k < art::wires; ++k) group(f, first + 3 + k, 1, s == size && k < wires);
    }
    ring(f.paths[0], (r >> 4) * (307 + ((share(c[2]) * 512) >> 10)) >> 6);   // r (0.3 .. 0.8)
    put(f, art::ring, iso(art::drum_x, art::drum_y, art::head_z + 307));
    const i32 tab_z = lerp(art::tab_z0, art::tab_z1, share(c[3]));
    slide(f, art::tab_left, 3, iso(art::drum_x + r - 2 * unit, art::drum_y - 2 * unit, tab_z));

    // BOUNCE: the arc from the board's foot onto the head.
    const i32 bounce = lerp(art::arc_lo, art::arc_hi, share(c[6]));
    for (u32 k = 0; k < art::dots; ++k) {
        const i32 x = art::arc_x0 + (((art::arc_x1 - art::arc_x0) * static_cast<i32>(k + 1)) / 9);
        put(f, art::dot0 + k, iso(x, art::groove_y, art::head_z + ((bounce * arc_sine[k + 1]) >> 10)));
    }

    // FRETS: none to nine bells at Galileo's spacing, x ~ k^2. A change of
    // risers reaches them a frame after the board, so no frame moves both.
    const u32 bells = steps(c[5], art::bells);
    const u32 bell_risers = baseline || !state.bell_risers ? risers : state.bell_risers;
    state.bell_risers = risers;
    const i32 bell_zh = risers_head(bell_risers);
    i32 bell_x[art::bells];
    for (u32 j = 0; j < art::bells; ++j) {
        const u32 k = j + 1;
        const i32 frac = static_cast<i32>((k * k * inverse_square[bells]) >> 10);   // Q10
        bell_x[j] = art::bell_x0 + ((((art::bell_x1 - art::bell_x0) >> 5) * ((art::bell_span * frac) >> 10)) >> 5);
    }

    // BALLS: one to six waiting at the head; the lowest goes first.
    const u32 balls = 1 + steps(c[7], art::balls - 1);
    const u32 moving = balls < run.m ? balls : run.m;
    u32 rang[art::bells] = {};
    for (u32 k = 0; k < art::balls; ++k) {
        const u32 n = art::ball5 + (art::balls - 1 - k) * art::ball_nodes;
        if (k >= balls) { park(f, n); park(f, n + 1); continue; }
        const i32 x0 = art::ball_x0 + static_cast<i32>(k) * art::ball_step;
        P where = iso(x0, art::groove_y, groove_z(x0, zh));
        const u32 b = balls - 1 - k;   // the order it rolls in
        if (running && b < moving && t >= run.ball[b].offset) {
            const u32 s = t - run.ball[b].offset;
            if (s < shown) {
                // x ~ t^2 down the board, then the arc onto the head.
                const u32 q = dsp::udiv(s << 8, shown >> 8 ? shown >> 8 : 1);   // Q16
                const u32 qq = q > 65535 ? 65535 : q;
                const i32 along = static_cast<i32>((qq * qq) >> 16);
                if (along < board_share) {
                    const i32 x = x0 + (((((art::lip_x - x0) >> 4) * along) / board_share) << 4);
                    where = iso(x, art::groove_y, groove_z(x, zh));
                } else {
                    const i32 u = ((along - board_share) << 10) / (65536 - board_share);   // Q10
                    const i32 x = art::arc_x0 + ((((art::arc_x1 - art::arc_x0) >> 2) * u) >> 8);
                    const i32 lift = (bounce * (dsp::sine(static_cast<u32>(u) << 21) >> 21)) >> 10;
                    where = iso(x, art::groove_y, lerp(art::foot_z + art::board_t, art::head_z, u) + lift);
                }
                // each bell this ball has just passed lifts a pixel
                for (u32 j = 0; j < bells; ++j) {
                    if (bell_x[j] < x0) continue;
                    const u32 ratio = dsp::udiv(static_cast<u32>((bell_x[j] - x0) >> 2) << 16, static_cast<u32>((art::lip_x - x0) >> 2));
                    const u32 need = (static_cast<u32>(board_share) * (ratio > 65536 ? 65536 : ratio)) >> 16;
                    const u32 when = ((shown >> 5) * dsp::isqrt(need << 14)) >> 10;
                    if (s >= when && s - when < ring_time) rang[j] = 1;
                }
            } else {
                // on the head: hops from the landing to its own spot, then rests
                const P spot = at(art::land_x - 6 * unit + static_cast<i32>(b % 3) * 6 * unit,
                                  art::drum_y - 3 * unit + static_cast<i32>(b / 3) * 6 * unit);
                const motion::Place p = motion::where(run, b, t - late);
                i32 x = spot.x, y = spot.y, z = art::head_z;
                if (p.state == 2) {
                    const u32 gone = ((p.hop - 1) << 16) + p.phase;
                    const i32 w = static_cast<i32>(gone >= (3u << 16) ? 1024 : gone / 192u);   // Q10 of the way
                    x = art::land_x + ((((spot.x - art::land_x) >> 2) * w) >> 8);
                    y = art::groove_y + ((((spot.y - art::groove_y) >> 2) * w) >> 8);
                    const i32 ph = static_cast<i32>(p.phase >> 6);                               // Q10
                    const i32 high = static_cast<i32>(((static_cast<u32>(bounce >> 1) >> 4) * (p.height >> 4)) >> 8);
                    z += ((high >> 2) * ((4 * ph * (1024 - ph)) >> 10)) >> 8;
                }
                where = iso(x, y, z);
            }
        }
        put(f, n, where);
        put(f, n + 1, where);
    }
    // The bells over the groove, each lifted a pixel just after a ball passes it.
    for (u32 j = 0; j < art::bells; ++j) {
        const u32 n = art::bell0_hanger + j * art::bell_nodes;
        if (j >= bells) { for (u32 i = 0; i < art::bell_nodes; ++i) park(f, n + i); continue; }
        const i32 z = groove_z(bell_x[j], bell_zh) + art::bell_drop + (rang[j] ? unit : 0);
        slide(f, n, art::bell_nodes, iso(bell_x[j], art::groove_y, z));
    }

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
