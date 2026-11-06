#include "core.h"
#include "levels.h"

// CURIO's engine: each key holds its own specimen and controls (the SDK
// saves them with the project), and its own TUNE and VARY (pad controls).
namespace curio {
using namespace dsp;

void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls) {
    const u32 k = key >= 53 && key < 53 + kit::keys ? key - 53 : 0;
    *model = kit::key_model[k];
    for (u32 c = 0; c < 8; ++c) controls[c] = kit::key_values[k][c];
}
const char* model_name(u32 model) { return kit::model[model < kit::models ? model : 0].name; }
const char* parameter_name(u32 model, u32 slot) { return slot < 8 ? kit::model[model < kit::models ? model : 0].labels[slot] : ""; }
u32 parameter_count(u32) { return 8; }


inline void initialize(Voice& voice, Shared&, u32 owner) {
    voice.model = voice.next = 0;
    voice.owner = owner;
    voice.pending = 0;
    voice.quiet = voice.done = 0;
    voice.tune = voice.vary = 0;
    voice.level = full;
    voice.gain = 0;
    voice.lim = full;
}
inline void start(Voice& voice, Shared&, u32) { voice.pending = 1; }
inline void destroy(Voice&, Shared&, u32) {}
inline void drum_select(Voice& voice, Shared&, u32, u32 model) { voice.next = model < kit::models ? model : 0; }
// TUNE (-16..15 semitones) and VARY (0..15), before every render.
inline void drum_pad(Voice& voice, Shared&, u32, const i32* values) {
    voice.tune = clamp(values[0], -16, 15);
    voice.vary = clamp(values[1], 0, 15);
}
inline bool pad_control_text(u32 field, i32 value, char* text) {
    if (field == 0) kit::tune_text(clamp(value, -16, 15), text);
    else if (field == 1) kit::vary_text(clamp(value, 0, 15), text);
    else return false;
    return true;
}

// The specimen's eight controls (Q12) with VARY's moves, and its pitch.
// VARY makes each hit a slightly different specimen: every control moves by
// up to +-0.1 of its travel (a spread of 0.06 at 15), the pitch by up to
// +-0.4 semitone and the level down by up to a quarter.
inline Ctx strike_context(Voice& voice, Shared& shared, const i32* parameters) {
    Ctx x;
    u32 seed = (++shared.hits) * 2654435761u ^ (voice.owner * 40503u);
    x.seed = seed | 1u;
    const i32 v = voice.vary;   // 0..15
    for (u32 c = 0; c < 8; ++c) {
        i32 q = static_cast<i32>(static_cast<u32>(parameters[c]) >> 19);
        if (v) q += (jitter(seed) * 27 >> 12) * v;   // +-27 * v of 4096
        x.c[c] = clamp(q, 0, 4095);
    }
    x.tune = voice.tune * 4096 / 12;   // semitones to Q12 octaves (a note's work)
    voice.level = full;
    if (v) {
        x.tune += (jitter(seed) * 9 >> 12) * v;   // +-0.4 semitone at 15
        voice.level = full - mul(F(0.25) / 15 * v, draw(seed) << 19);
    }
    return x;
}

// The hit's output gain: the specimen's level, its controls' trims (each
// control's log2 gain at 0, 32, 64, 96 and 127, interpolated and summed, so a
// hit sounds about as loud as the defaults whatever its controls; levels.h,
// from tools/calibrate.py) and VARY's cut.
inline i32 hit_gain(u32 model, const Ctx& x, i32 vary_level) {
    i32 t = 0;   // Q12 octaves
    for (u32 s = 0; s < 8; ++s) {
        const signed char* p = levels::trim[model][s];
        const i32 c = x.c[s], i = c >> 10, f = c & 1023;
        t += (p[i] << 7) + (((p[i + 1] - p[i]) * f) >> 3);
    }
    return gn(mul(levels::gain[model], vary_level), oct(clamp(t, -(6 << 12), 3 << 12)));
}

inline void begin(Voice& voice, Shared& shared, const i32* parameters) {
    const Ctx x = strike_context(voice, shared, parameters);
    voice.gain = hit_gain(voice.model, x, voice.level);
    voice.lim = full;
    switch (voice.model) {
    case 0: voice.u.ball.start(x); break;
    case 1: voice.u.cask.start(x); break;
    case 2: voice.u.stone.start(x); break;
    case 3: voice.u.drip.start(x); break;
    case 4: voice.u.ice.start(x); break;
    case 5: voice.u.sheet.start(x); break;
    case 6: voice.u.spark.start(x); break;
    case 7: voice.u.cork.start(x); break;
    case 8: voice.u.cicada.start(x); break;
    case 9: voice.u.ratchet.start(x); break;
    case 10: voice.u.pod.start(x); break;
    case 11: voice.u.match.start(x); break;
    case 12: voice.u.glass.start(x); break;
    case 13: voice.u.belljar.start(x); break;
    case 14: voice.u.chalk.start(x); break;
    default: voice.u.roar.start(x); break;
    }
}
inline bool waiting(const Voice& voice) {
    switch (voice.model) {
    case 0: return voice.u.ball.waiting();
    case 1: return voice.u.cask.waiting();
    case 2: return voice.u.stone.waiting();
    case 3: return voice.u.drip.waiting();
    case 4: return voice.u.ice.waiting();
    case 5: return voice.u.sheet.waiting();
    case 6: return voice.u.spark.waiting();
    case 7: return voice.u.cork.waiting();
    case 8: return voice.u.cicada.waiting();
    case 9: return voice.u.ratchet.waiting();
    case 10: return voice.u.pod.waiting();
    case 11: return voice.u.match.waiting();
    case 12: return voice.u.glass.waiting();
    case 13: return voice.u.belljar.waiting();
    case 14: return voice.u.chalk.waiting();
    default: return voice.u.roar.waiting();
    }
}
// One function per specimen, so each sample loop is compiled on its own.
__attribute__((noinline)) void run0(Voice& v, i32* o) { v.u.ball.run(o); }
__attribute__((noinline)) void run1(Voice& v, i32* o) { v.u.cask.run(o); }
__attribute__((noinline)) void run2(Voice& v, i32* o) { v.u.stone.run(o); }
__attribute__((noinline)) void run3(Voice& v, i32* o) { v.u.drip.run(o); }
__attribute__((noinline)) void run4(Voice& v, i32* o) { v.u.ice.run(o); }
__attribute__((noinline)) void run5(Voice& v, i32* o) { v.u.sheet.run(o); }
__attribute__((noinline)) void run6(Voice& v, i32* o) { v.u.spark.run(o); }
__attribute__((noinline)) void run7(Voice& v, i32* o) { v.u.cork.run(o); }
__attribute__((noinline)) void run8(Voice& v, i32* o) { v.u.cicada.run(o); }
__attribute__((noinline)) void run9(Voice& v, i32* o) { v.u.ratchet.run(o); }
__attribute__((noinline)) void run10(Voice& v, i32* o) { v.u.pod.run(o); }
__attribute__((noinline)) void run11(Voice& v, i32* o) { v.u.match.run(o); }
__attribute__((noinline)) void run12(Voice& v, i32* o) { v.u.glass.run(o); }
__attribute__((noinline)) void run13(Voice& v, i32* o) { v.u.belljar.run(o); }
__attribute__((noinline)) void run14(Voice& v, i32* o) { v.u.chalk.run(o); }
__attribute__((noinline)) void run15(Voice& v, i32* o) { v.u.roar.run(o); }

// A sound has ended once it has stayed under -84 dB for 90 ms with nothing
// still to come (a bounce, a shard, a tooth, a stroke, an echo). Its voice
// then writes silence, and finished tells the SDK it may free the voice.
const i32 quiet_floor = 1 << 17;
const u32 quiet_blocks = 62;
inline bool finished(const Voice& voice, const Shared&) { return voice.done != 0; }

// The output stage: the hit's gain, then a peak limiter that sees each block
// before it plays (the specimen renders the block first), so it needs no
// delay: where a block would peak over the specimen's limit (levels.h: 0.42
// of full scale, -1.5 dB under the SDK's half-scale ceiling, or lower where a
// sharp strike's true peak runs over its samples) its gain drops at the
// block's start to bring that peak to the limit, and recovers about 1 dB a
// block (1.5 ms) once the peaks fall. At a specimen's defaults it trims only
// a hit's first milliseconds, by at most 3 dB (tools/calibrate.py). A soft knee from 0.44 that never
// passes half scale (the SDK's ceiling, outputPeakQ31) catches what a rising
// gain within a block leaves over.
inline i32 knee(i32 y) {
    const i32 k = U(0.44), room = U(0.06);
    const i32 a = y < 0 ? -y : y;
    if (a <= k) return y << 3;
    const i32 over = lo(a - k, U(0.25));
    const i32 s = k + mul(room, tanh(over * 16) << 3);
    return (y < 0 ? -s : s) << 3;
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch) {
    if (voice.pending || voice.next != voice.model) {
        voice.pending = 0;
        voice.quiet = voice.done = 0;
        voice.model = voice.next;
        begin(voice, shared, parameters);
    }
    if (voice.done) {
        for (u32 i = 0; i < frames; ++i) output[i] = 0;
        return;
    }
    i32* out = output;
    switch (voice.model) {
    case 0: run0(voice, out); break;
    case 1: run1(voice, out); break;
    case 2: run2(voice, out); break;
    case 3: run3(voice, out); break;
    case 4: run4(voice, out); break;
    case 5: run5(voice, out); break;
    case 6: run6(voice, out); break;
    case 7: run7(voice, out); break;
    case 8: run8(voice, out); break;
    case 9: run9(voice, out); break;
    case 10: run10(voice, out); break;
    case 11: run11(voice, out); break;
    case 12: run12(voice, out); break;
    case 13: run13(voice, out); break;
    case 14: run14(voice, out); break;
    default: run15(voice, out); break;
    }
    const i32 g = voice.gain;
    i32 p = 0;
    for (u32 i = 0; i < frames; ++i) { const i32 a = out[i] < 0 ? -out[i] : out[i]; p = a > p ? a : p; }
    const i32 q = mul(p, g), peak_limit = levels::limit[voice.model];   // an output sample is mul(signal, gain) << 6
    const i32 want = q > peak_limit ? static_cast<i32>(ex2(31 * 4096 + lg2(static_cast<u32>(peak_limit)) - lg2(static_cast<u32>(q)))) : full;
    i32 l = voice.lim;
    bool loud = false;
    if (l == full && want == full) {
        // Loud if any sample reaches the floor: the block's highest and lowest.
        i32 top = -full - 1, bottom = full;
        for (u32 i = 0; i < frames; ++i) {
            const i32 y = mul(out[i], g) << 6;
            output[i] = y;
            top = y > top ? y : top;
            bottom = y < bottom ? y : bottom;
        }
        loud = top >= quiet_floor || bottom <= -quiet_floor;
    } else {
        // Down at once; up 1 dB a block, ramped across it, never past what the block allows.
        if (want < l) l = want;
        const i32 to = lo(l > F(0.888) ? full : l + (l >> 3), want);
        const i32 d = (to - l) >> 6;
        i32 top = -full - 1, bottom = full;
        for (u32 i = 0; i < frames; ++i) {
            const i32 y = knee(mul(mul(out[i], g), l) << 3);
            l += d;
            output[i] = y;
            top = y > top ? y : top;
            bottom = y < bottom ? y : bottom;
        }
        loud = top >= quiet_floor || bottom <= -quiet_floor;
        voice.lim = to;
    }
    voice.quiet = loud ? 0 : voice.quiet + 1;
    if (voice.quiet >= quiet_blocks && !waiting(voice)) voice.done = 1;
}
}
