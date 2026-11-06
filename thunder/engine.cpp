#include "core.h"
#include "levels.h"

// THUNDER's engine: the kit's callbacks, and a voice's note from start to
// silence. The sound itself is in models.h and voice.h.
namespace thunder {
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls) {
    const u32 k = key >= 53 && key < 53 + kit::keys ? key - 53 : 0;
    *model = kit::key_model[k];
    for (u32 c = 0; c < 8; ++c) controls[c] = kit::key_values[k][c];
}
const char* model_name(u32 model) { return kit::model[model < kit::models ? model : 0].name; }
const char* parameter_name(u32 model, u32 slot) { return slot < 8 ? kit::model[model < kit::models ? model : 0].labels[slot] : ""; }
u32 parameter_count(u32) { return 8; }

inline void initialize(Voice& voice, Shared&, u32 owner) {
    voice.owner = owner;
    voice.model = voice.next = 0;
    voice.pending = 0;
    voice.done = 1;
    voice.quiet = 0;
    voice.tune = voice.drive = 0;
}
inline void start(Voice& voice, Shared&, u32) { voice.pending = 1; }
inline void destroy(Voice&, Shared&, u32) {}
inline void drum_select(Voice& voice, Shared&, u32, u32 model) { voice.next = model < kit::models ? model : 0; }

// The key's pad controls: TUNE, -16..15 semitones; DRIVE, 0..15.
inline void drum_pad(Voice& voice, Shared&, u32, const i32* values) {
    voice.tune = clamp(values[0], -16, 15);
    voice.drive = clamp(values[1], 0, 15);
}
inline void copy_text(char* text, const char* from) {
    u32 n = 0;
    for (; from[n] && n < 15; ++n) text[n] = from[n];
    text[n] = 0;
}
// Their captions: TUNE in semitones ("+3", "-12", "0"), DRIVE "OFF" or 1..15.
inline bool pad_control_text(u32 field, i32 value, char* text) {
    char s[8];
    u32 n = 0;
    if (field == 0) {
        value = clamp(value, -16, 15);
        if (value > 0) s[n++] = '+';
        if (value < 0) { s[n++] = '-'; value = -value; }
    } else if (field == 1) {
        value = clamp(value, 0, 15);
        if (!value) { copy_text(text, "OFF"); return true; }
    } else {
        return false;
    }
    if (value >= 10) s[n++] = static_cast<char>('0' + value / 10);
    s[n++] = static_cast<char>('0' + value % 10);
    s[n] = 0;
    copy_text(text, s);
    return true;
}

// A note's sound has ended once its output has stayed under -96 dB for
// 90 ms with nothing still to come (a roll, a bounce, a snap, an echo, a
// jolt, a bow still drawn). Its voice then writes silence without running the
// sheet, and finished tells the SDK it may free the voice.
const i32 quiet_under = 1 << 15;
const u32 quiet_blocks = 62;
inline bool finished(const Voice& voice, const Shared&) { return voice.done != 0; }

// A control word (raw << 24) as a Q16 unit.
inline i32 unit7(i32 word) {
    const u32 raw = static_cast<u32>(word < 0 ? 0 : word) >> 24;
    return static_cast<i32>(((raw > 127 ? 127 : raw) * 528416u) >> 10);
}

// DRIVE: the contact pickup overloaded. The output (near 0.4 of full scale at
// its loudest) through a cubic soft clip after a gain of 1.4..7.
inline i32 overdrive(i32 x, i32 drive) {
    const i32 g = Q31((1.0 * 2.5) / 32) + drive * Q31((6.0 / 15 * 2.5) / 32);   // g / 0.4 / 32
    i32 z = multiply(x, g);
    const i32 lim = one >> 5;
    z = z > lim ? lim : z < -lim ? -lim : z;
    z <<= 5;
    const i32 z3 = multiply(multiply(z, z), z);
    return multiply(z - multiply(z3, Q31(1.0 / 3.0)), Q31(0.6));
}

// A soft knee over the last quarter under the ceiling: what the level trim
// leaves over passes as K + (H - K)(2e - e^2), e the excess over 2 (H - K).
inline i32 knee(i32 y) {
    const i32 k = half_scale - (half_scale >> 2);
    const i32 a = y < 0 ? (y < -one ? one : -y) : y;
    if (a <= k) return y;
    const i32 e = a - k >= (half_scale >> 1) ? one : (a - k) << 2;   // Q31
    const i32 b = k + multiply(half_scale >> 2, e + multiply(e, one - e));
    return y < 0 ? -b : b;
}

// A transient limiter, on the models levels::limit names (a clap's snaps, a
// pour's loudest beads, a shackle's and a bearing's strikes, whose peaks stand
// far over their body, and a tom's hard beater): a block's peak over 0.8 of
// the ceiling pulls the gain down to it, ramped in over the samples before
// the first one over, and the gain comes back with a 5 ms time constant, so
// it shapes the transient and leaves the body. The calibration lets it take
// at most 3 dB off a default hit's peak (4 on the clap's snaps); a steel
// beater's click it takes whole. No divide: the gain is 2^(log2 T - log2 peak).
const i32 limit_top = half_scale - (half_scale / 5);
const i32 limit_log = L(0.8 * 1073741824.0);
const i32 limit_back = Q31(0.2516);   // 1 - exp(-64 / (0.005 * 44100)) a block
inline void limit(Voice& voice, i32* y) {
    i32 peak = 0;
    for (unsigned i = 0; i < frames; ++i) {
        const i32 a = absolute(y[i]);
        if (a > peak) peak = a;
    }
    i32 g = voice.lim;
    i32 target = g >= one ? one : g + multiply(one - g, limit_back);
    if (one - target < (one >> 12)) target = one;
    if (peak > limit_top) {
        const i32 need = frac2(limit_log - log2q(static_cast<u32>(peak)));
        if (need < target) target = need;
    }
    if (target == one && g == one) return;
    if (target < g) {
        // down by the block's peak, over the largest power of two of samples
        // before the first one over the limit (those pass at any gain): a
        // click's noise has samples near its peak well before it
        u32 first = 0;
        while (absolute(y[first]) <= limit_top) ++first;
        u32 k = 0;
        while ((2u << k) <= first + 1) ++k;
        const i32 step = (target - g) >> k;
        const unsigned n = 1u << k;
        for (unsigned i = 0; i < n; ++i) { g += step; y[i] = multiply(y[i], g); }
        for (unsigned i = n; i < frames; ++i) y[i] = multiply(y[i], target);
    } else {
        const i32 step = (target - g) >> 6;
        for (unsigned i = 0; i < frames; ++i) { g += step; y[i] = multiply(y[i], g); }
    }
    voice.lim = target;
}

inline void begin(Voice& voice, Shared& shared, opxy::Parameters parameters) {
    // Clear everything but the voice's identity and its pad controls; the
    // delay lines are cleared by the field's setup, as far as it uses them.
    const u32 owner = voice.owner, model = voice.next;
    const i32 tune = voice.tune, drive = voice.drive;
    u32* words = reinterpret_cast<u32*>(&voice);
    const u32 count = static_cast<u32>(reinterpret_cast<const char*>(voice.sw_buf) - reinterpret_cast<const char*>(&voice)) / 4;
    for (u32 i = 0; i < count; ++i) words[i] = 0;
    voice.owner = owner;
    voice.model = voice.next = model;
    voice.tune = tune;
    voice.drive = drive;
    voice.seed = (++shared.notes) * 2654435761u + owner * 40503u;
    i32 c[8];
    for (u32 i = 0; i < 8; ++i) c[i] = unit7(parameters[i]);
    setup_model(voice, model, c, tune * 5461);
    voice.lim = one;
    // The level (levels.h, tools/calibrate.py): the model's gain brings its
    // default hit to its role's loudness; each control's trim holds that
    // loudness as it turns, and its rise tracks how far that lifts the hit's
    // peak. Where the rises of all eight controls together pass the room the
    // default leaves under the ceiling, the hit comes down by the excess, so
    // a combination of controls is held by its peak only as far as it must be.
    i32 trim = levels::gain[model], rise = 0;
    for (u32 s = 0; s < 8; ++s) {
        const signed char* t = levels::loud[model][s];
        const signed char* r = levels::rise[model][s];
        const i32 x = c[s] * 4;                       // 0..4 in Q16
        const u32 i = static_cast<u32>(x >> 16) < 3 ? static_cast<u32>(x >> 16) : 3;
        const i32 f = x - static_cast<i32>(i << 16);
        trim += (t[i] << 11) + mul16((t[i + 1] - t[i]) << 11, f);
        rise += (r[i] << 11) + mul16((r[i + 1] - r[i]) << 11, f);
    }
    if (rise > levels::room[model]) trim -= rise - levels::room[model];   // level: cap
    voice.gain = static_cast<i32>(pow2(clamp(trim + 25 * octave, 0, 31 * octave - 1)));   // Q31 of 64
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch) {
    if (voice.pending) begin(voice, shared, parameters);
    if (voice.done) {
        for (unsigned i = 0; i < frames; ++i) output[i] = 0;
        return;
    }
    Work work;
    i32 x[frames];
    block_model(voice, work, x);
    voice.t += frames;
    // the model's gain; then DRIVE, if any; then the soft knee, if anything
    // reaches it; and whether anything was loud enough to keep the note going
    const i32 gain = voice.gain, drive = voice.drive;
    i32 most = 0;
    if (!drive) {
        for (unsigned i = 0; i < frames; ++i) {
            const i32 y = clamp(multiply(x[i], gain), -(one >> 6), one >> 6) << 6;
            output[i] = y;
            most |= y < 0 ? -y : y;
        }
        if (levels::limit[voice.model]) limit(voice, output);   // level: limiter
    } else {
        // DRIVE in the same pass: 0.6 z (1 - z^2 / 3), z the output at a gain of 1.4..7,
        // held at full scale (which the output alone, at a gain of 1.4 or more, reaches first)
        const i32 g = Q31((1.0 * 2.5) / 32) + drive * Q31((6.0 / 15 * 2.5) / 32);
        const i32 lim = one >> 11;
        for (unsigned i = 0; i < frames; ++i) {
            const i32 z = clamp(multiply(multiply(x[i], gain), g), -lim, lim) << 11;
            const i32 y = multiply(z, Q31(0.6) - multiply(multiply(z, z), Q31(0.2)));
            output[i] = y;
            most |= y < 0 ? -y : y;
        }
    }
    // (tools/calibrate.py builds a copy without the lines marked "level:", to
    // measure the peaks the cap, the limiter and the knee take)
    if (most > half_scale - (half_scale >> 2)) for (unsigned i = 0; i < frames; ++i) output[i] = knee(output[i]);   // level: knee
    const bool loud = most >= quiet_under;
    voice.quiet = loud ? 0 : voice.quiet + 1;
    if (voice.quiet >= quiet_blocks && !voice.waiting) voice.done = 1;
}
}
