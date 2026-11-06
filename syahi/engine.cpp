#include "core.h"
#include "human.hpp"
#include "pads.hpp"
#include "levels.h"

// Syahi: each key holds one of eight models (BOOM, SNARE, TOM, TABLA, BAR,
// METAL, BOUNCE, WIRE) and its eight controls, which the SDK saves with the
// project; Shift gives each key its pan, level, sends and two pad controls.
// Every model plays the one struck object in voice.hpp.
namespace syahi {
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls) {
    const u32 k = key >= 53 && key < 53 + kit::keys ? key - 53 : 0;
    *model = kit::key_model[k];
    for (u32 c = 0; c < 8; ++c) controls[c] = kit::key_values[k][c];
}
const char* model_name(u32 model) { return kit::model_names[model < kit::models ? model : 0]; }
const char* parameter_name(u32 model, u32 slot) { return slot < 8 ? kit::labels[model < kit::models ? model : 0][slot] : ""; }
u32 parameter_count(u32) { return 8; }

// Each model's gain to GRIT's working level: a default hit is normalised
// (Q24) to it before GRIT drives it.
constexpr i32 q24(double x) { return c_round(x * 16777216.0); }
const i32 model_gain[kit::models] = {
    // BOOM, SNARE, TOM, TABLA, BAR, METAL, BOUNCE, WIRE
    q24(28.15), q24(6.79), q24(27.47), q24(16.83), q24(18.27), q24(13.43), q24(62.14), q24(16.80),
};
// GRIT's working level: a default hit peaks here before the model's level.
const i32 working = 1 << 29;

inline void initialize(Voice& voice, Shared&, u32) {
    voice.next = 0;
    voice.pending = voice.quiet = voice.done = 0;
    voice.human = voice.grit = 0;
}
inline void start(Voice& voice, Shared&, u32) { voice.pending = 1; }
inline void destroy(Voice&, Shared&, u32) {}
inline void drum_select(Voice& voice, Shared&, u32, u32 model) { voice.next = model < kit::models ? model : 0; }
inline bool finished(const Voice& voice, const Shared&) { return voice.done != 0; }

// HUMAN (4 bits) and GRIT (signed 5 bits), before every render.
inline void drum_pad(Voice& voice, Shared&, u32, const i32* values) {
    voice.human = clamp(values[0], 0, 15);
    voice.grit = clamp(values[1], -16, 15);
}
// A key's identity: its model, controls and pad controls. Keys set alike count together.
inline u32 identity(const Voice& voice) {
    u32 h = voice.next * 0x9e3779b1u + static_cast<u32>(voice.human) * 0x85ebca77u + static_cast<u32>(voice.grit + 16) * 0xc2b2ae3du;
    for (u32 c = 0; c < 8; ++c) h = hash(h ^ (voice.raw[c] + (c << 8)));
    return h | 1u;
}
// This key's notes so far, counting this one.
inline u32 count_note(Shared& shared, u32 id) {
    ++shared.clock;
    Counter* slot = &shared.counters[0];
    for (u32 i = 0; i < counted_keys; ++i) {
        Counter& c = shared.counters[i];
        if (c.identity == id) { slot = &c; break; }
        if (c.used < slot->used) slot = &c;
    }
    if (slot->identity != id) { slot->identity = id; slot->count = 0; }
    slot->used = shared.clock;
    return slot->count++;
}

inline void read_controls(const opxy::i32* parameters, u32* raw) {
    for (u32 c = 0; c < 8; ++c) raw[c] = static_cast<u32>(positive(parameters[c])) >> 24;
}

// The hit's output gain (Q28): the model's level and its trim at each control
// (levels.h, tools/calibrate.py), so every hit sounds at its role's loudness
// whatever its controls, as far as its peak allows. Each lift is safe for its
// peak alone, so lifts don't add up: only the largest applies; cuts all do.
inline i32 level_of(u32 model, const u32* raw) {
    i32 cut = 0, lift = 0;
    for (u32 s = 0; s < 8; ++s) {
        const signed char* t = levels::trim[model][s];
        const i32 x = static_cast<i32>(static_cast<u32>(fraction(raw[s])) >> 13);   // 0..4 in Q16: the points 0, 32, 64, 96, 127
        const i32 i = x >> 16 < 3 ? x >> 16 : 3, f = x - (i << 16);
        const i32 v = t[i] * 2048 + (((t[i + 1] - t[i]) * f) >> 5);   // 1/32 octave as Q16
        if (v < 0) cut += v;
        else if (v > lift) lift = v;
    }
    return static_cast<i32>(exp2i(clamp(levels::gain[model] + cut + lift + 28 * octave, 0, 31 * octave - 1)));
}

inline void set_up(Voice& voice) {
    i32 c[8];
    for (u32 i = 0; i < 8; ++i) c[i] = fraction(voice.raw[i]);
    c[5] = fraction(static_cast<u32>(clamp(static_cast<i32>(voice.raw[5]) + voice.spot_offset, 0, 127)));
    setup(voice.core, c, voice.raw[4], voice.pitch_offset);
    // The head sounds as velocity^1.3.
    const i32 v = voice.core.velocity;
    const i32 v13 = v >= one ? one : exp2q(multiply(log2u(static_cast<u32>(v)) - 31 * octave, q31(0.65)) * 2);
    voice.core.head_gain = multiply(multiply(voice.core.spec.head, v13), voice.core.wash_trim);
    voice.level = level_of(voice.core.model, voice.raw);
}

inline void begin_note(Voice& voice, Shared& shared) {
    Core& core = voice.core;
    static_assert(sizeof(Core) % 4 == 0 && alignof(Core) >= 4, "Core clears by words");
    u32* words = reinterpret_cast<u32*>(&core);
    for (unsigned i = 0; i < sizeof(Core) / 4; ++i) words[i] = 0;
    core.model = voice.next;
    const u32 serial = count_note(shared, identity(voice));
    const Variation hit = vary(serial, voice.human);
    voice.spot_offset = hit.spot;
    voice.pitch_offset = hit.pitch;
    core.velocity = hit.velocity;
    core.seed = hash(serial ^ identity(voice)) | 1u;
    set_up(voice);
    // The first strike lands on the block's first sample, the head already
    // lifted by its energy.
    for (u32 k = 0; k < partials; ++k) {
        Partial& m = core.part[k];
        m.phase = start_phase(k);
        m.inc = increment(k < head_modes ? lifted(m, one, 0) : m.rest);
    }
    core.part[body_mode].phase = core.part[air_mode].phase = 0;
    core.glide_left = core.swell_left = one;
    core.ramp = 0x80000000u;
    core.next_event = 0;
    core.noise_quiet = 0;
    voice.low = voice.held = voice.hp = 0;
    voice.hold_left = 0;
}

// GRIT on a working-level signal: below 0 a softening low-pass (tape-rounded),
// above 0 a tanh drive, above +9 also a lower rate and fewer bits. Its
// constants are the block's: the pass (finish_pass) runs one GRIT setting.
// tanh(drive x / working) * working / tanh(drive), from a table of tanh(i / 64).
// multiply(t, working) is t / 4 exactly (working is 2^29), so t >> 2.
static inline i32 grit_drive(i32 x, i32 drive, i32 makeup) {
    // Past the table's end the position holds at its last step (MIN, no branch).
    const u32 p = static_cast<u32>(multiply(absolute(x) >> 8, drive));   // Q8 table steps
    const u32 position = p < 65535u ? p : 65535u;
    const u32 index = position >> 8;
    const i32 f = static_cast<i32>((position & 255u) << 23);
    const i32 t = tables::tanh[index] + multiply(tables::tanh[index + 1] - tables::tanh[index], f);
    const i32 y = multiply(t >> 2, makeup) << 3;
    return x < 0 ? -y : y;
}

// A soft knee under the SDK's ceiling at 0.5 of full scale: a tanh from the
// model's knee (levels.h) up to the ceiling, so a hit's sharpest moments (a
// stick's crack, a tabla's slap, a full wash with sizzle, 24 claps) round off
// instead of clipping and the rest of the hit sounds louder under it.
struct Knee { i32 knee, scale, range; };
// Past the knee (rare: a hit's loudest moments), out of the per-sample loop.
__attribute__((noinline)) inline i32 round_off(i32 y, const Knee& k) {
    const i32 a = absolute(y);
    const u32 position = static_cast<u32>(multiply(a - k.knee, k.scale));   // Q8 steps of tanh(i / 64) over the range
    const u32 index = position >> 8;
    const i32 t = index >= 256 ? tables::tanh[256]
                : tables::tanh[index] + multiply(tables::tanh[index + 1] - tables::tanh[index], static_cast<i32>((position & 255u) << 23));
    const i32 out = clamp(k.knee + multiply(t, k.range), 0, half_scale);
    return y < 0 ? -out : out;
}

// A sound has ended once it has stayed under the floor for 90 ms with no strike to come.
const i32 quiet_floor = 1 << 15;
const u32 quiet_blocks = 62;

// The block out: the model's gain to the working level, GRIT, a 24 Hz
// high-pass, the hit's level and the model's soft knee; whether it is silent.
// One pass for each GRIT kind (none, soft, drive, crush), its state in locals.
struct Finish {
    i32 gain, level, hp;
    Knee knee;
    // GRIT: the soft low-pass and its coefficient; the drive's table words; the
    // crush's held sample, samples left of it, its hold, its step and its output.
    i32 low, soft, drive, makeup, held, step, held_y;
    u32 hold_left, hold;
};
enum GritKind { grit_none, grit_soft, grit_drive_only, grit_crush };
template <int kind>
__attribute__((noinline)) static u32 finish_pass(Finish& f, const i32* __restrict out, i32* __restrict output) {
    const i32 gain = f.gain, level = f.level, knee_at = f.knee.knee;
    i32 hp = f.hp, low = f.low, held = f.held, held_y = f.held_y;
    const i32 soft = f.soft, drive = f.drive, makeup = f.makeup, step = f.step;
    u32 hold_left = f.hold_left;
    const u32 hold = f.hold;
    u32 under = 0;
    i32 most = 0;
    for (u32 n = 0; n < 64; ++n) {
        i32 y = shl_clamp(multiply(out[n], gain), 7);
        if (kind == grit_soft) {
            low += multiply(y - low, soft);
            y = low;
        } else if (kind == grit_drive_only) {
            y = grit_drive(y, drive, makeup);
        } else if (kind == grit_crush) {
            // The held sample's output holds with it: computed once a hold.
            if (!hold_left) {
                held = y;
                hold_left = hold;
                held_y = (grit_drive(held, drive, makeup) + (step >> 1)) & ~(step - 1);
            }
            --hold_left;
            y = held_y;
        }
        hp += multiply(y - hp, 7323000);   // 1 - e^(-2 pi 24 / 44100), Q31
        y = shl_clamp(multiply(y - hp, level), 3);   // level is Q28
        output[n] = y;
        const i32 a = absolute(y);
        most = a > most ? a : most;
        under += static_cast<u32>(y + quiet_floor - 1) < static_cast<u32>(2 * quiet_floor - 1);   // |y| < the floor
    }
    // The knee, past the loop (rare: a hit's loudest moments), so the pass
    // stays a hardware loop. Past the knee is far above the quiet floor.
    if (most > knee_at)
        for (u32 n = 0; n < 64; ++n)
            if (output[n] > knee_at || output[n] < -knee_at) output[n] = round_off(output[n], f.knee);
    f.hp = hp;
    f.low = low;
    f.held = held;
    f.held_y = held_y;
    f.hold_left = hold_left;
    return under;
}
static inline void finish(Voice& voice, const i32* out, i32* output) {
    const u32 m = voice.core.model;
    const i32 g = voice.grit;
    Finish f;
    f.gain = model_gain[m];
    f.level = voice.level;
    f.hp = voice.hp;
    f.knee = {levels::knee[m][0], levels::knee[m][1], levels::knee[m][2]};
    f.low = voice.low;
    f.held = voice.held;
    f.hold_left = voice.hold_left;
    f.soft = g < 0 ? tables::grit_soft[-g] : 0;
    f.drive = g > 0 ? tables::grit_drive[g] : 0;
    f.makeup = g > 0 ? tables::grit_makeup[g] : 0;
    f.hold = g > 9 ? static_cast<u32>(1 + (g - 9) * 2) : 0;
    f.step = g > 9 ? static_cast<i32>(1) << (16 + ((g - 10) * 7) / 5) : 0;   // 2^(14 - 1.4 (g - 9)) steps a working level
    f.held_y = g > 9 && f.hold_left ? (grit_drive(f.held, f.drive, f.makeup) + (f.step >> 1)) & ~(f.step - 1) : 0;
    const u32 under = g == 0 ? finish_pass<grit_none>(f, out, output)
                    : g < 0 ? finish_pass<grit_soft>(f, out, output)
                    : g <= 9 ? finish_pass<grit_drive_only>(f, out, output)
                    : finish_pass<grit_crush>(f, out, output);
    voice.hp = f.hp;
    voice.low = f.low;
    voice.held = f.held;
    voice.hold_left = f.hold_left;
    voice.quiet = under == 64 ? voice.quiet + 1 : 0;
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch) {
    if (voice.pending) {
        voice.pending = 0;
        voice.quiet = voice.done = 0;
        read_controls(parameters, voice.raw);
        begin_note(voice, shared);
    } else if (voice.done) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        return;
    } else {
        u32 raw[8];
        read_controls(parameters, raw);
        bool moved = false;
        for (u32 c = 0; c < 8; ++c) moved = moved || raw[c] != voice.raw[c];
        if (moved) {
            for (u32 c = 0; c < 8; ++c) voice.raw[c] = raw[c];
            set_up(voice);
        }
    }
    i32 out[64];
    for (u32 n = 0; n < 64; ++n) out[n] = 0;
    render_core(voice.core, shared.scratch, out);
    finish(voice, out, output);
    if (voice.quiet >= quiet_blocks && voice.core.next_event < 0) voice.done = 1;
}
}
