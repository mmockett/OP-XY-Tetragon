#include "core.h"
#include "captions.h"

// GALILEO's engine. Each key holds a model and its eight controls, which the
// SDK saves with the project: the body's four (tune, decay and the like) and
// the motion's four (tilt, frets, bounce, balls). A note lays the ball's run
// out (motion.h) and walks it a block at a time, striking the body (bodies.h)
// at each impact, to the sample.
namespace galileo {
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls) {
    const u32 k = key >= 53 && key < 53 + kit::keys ? key - 53 : 0;
    *model = kit::key_model[k];
    for (u32 c = 0; c < 8; ++c) controls[c] = kit::key_values[k][c];
}
const char* model_name(u32 model) { return kit::model[model < kit::models ? model : 0].name; }
const char* parameter_name(u32 model, u32 slot) {
    return slot < 8 ? kit::model[model < kit::models ? model : 0].labels[slot] : "";
}
u32 parameter_count(u32) { return 8; }

// A sound has ended once the run is over and it has stayed under the floor
// (-90 dB) for 40 blocks (58 ms).
const i32 quiet_floor = 1 << 16;
const u32 quiet_blocks = 40;
// The newest note's voice keeps counting the kit's clock for up to 2.5 s of
// silence after its sound, rendering nothing, so a sparse pattern's gaps still
// teach the tempo. Any other finished voice is freed at once.
const u32 keep_blocks = 1723;

inline void initialize(Voice& voice, Shared& shared, u32 owner) {
    voice.model = voice.next = 0;
    voice.owner = owner;
    voice.pending = 0;
    voice.fresh = 1;
    voice.stamp = shared.block;
    voice.done = 1;
    voice.quiet = voice.kept = 0;
    voice.space = voice.feel = 0;
    voice.seed = 0x6a09e667u ^ (owner * 0x9e3779b9u);
    ++shared.active;
}
inline void destroy(Voice&, Shared& shared, u32) {
    if (shared.active) --shared.active;
}
inline void start(Voice& voice, Shared& shared, u32 owner) {
    if (!shared.learner.p16) shared.learner.reset();
    // The clock ran unbroken since the last note if some other voice was alive
    // (this one, retriggered, counts: it was rendering).
    const bool continuous = shared.active > voice.fresh;
    shared.learner.note(shared.now, continuous);
    shared.latest = owner;
    ++shared.notes;
    voice.fresh = 0;
    voice.pending = 1;
}
inline void drum_select(Voice& voice, Shared&, u32, u32 model) { voice.next = model < kit::models ? model : 0; }
// The key's pad fields: SPACE (-16..15) and FEEL (0..15).
inline void drum_pad(Voice& voice, Shared&, u32, const i32* values) {
    voice.space = dsp::clamp(values[0], -16, 15);
    voice.feel = dsp::clamp(values[1], 0, 15);
}
// The project's tempo, where the SDK passes it (drum/kit-transport); without
// it, or at 0, the kit's own learned tempo.
inline void drum_transport(Voice&, Shared& shared, u32, const opxy::Transport& transport) {
    if (transport.bpm_x10 != shared.bpm_x10) {
        shared.bpm_x10 = transport.bpm_x10;
        shared.p16_transport = tempo::sixteenth(transport.bpm_x10);
    }
}
// The sixteenth the kit plays to now.
inline u32 sixteenth(const Shared& shared) {
    if (shared.bpm_x10 && shared.p16_transport) return shared.p16_transport;
    return shared.learner.p16 ? shared.learner.p16 : tempo::start;
}
// The pad fields' captions for the stock header, as the kit's own shows them.
inline bool pad_control_text(u32 field, i32 value, char* text) {
    if (field == 0) caption::space(text, 16, value);
    else if (field == 1) caption::feel(text, 16, value);
    else return false;
    return true;
}
inline bool finished(const Voice& voice, const Shared& shared) {
    return voice.done && !voice.pending && (shared.latest != voice.owner || voice.kept >= keep_blocks);
}
inline u32 raw(i32 parameter) { return parameter < 0 ? 0 : static_cast<u32>(parameter) >> 24; }
inline i32 q31(i32 parameter) { return raw(parameter) >= 127 ? dsp::one : static_cast<i32>(raw(parameter) << 24); }
// FEEL's looseness: up to `lax` samples late.
inline u32 jitter(Voice& voice, u32 lax) {
    if (!lax) return 0;
    voice.seed = voice.seed * 1664525u + 1013904223u;
    return ((voice.seed >> 16) * lax) >> 16;
}

// The model's output gain (Q26), trimmed by the loudness of its four body
// controls where they stand (kit::trim, interpolated between 0, 32, 64, 96 and
// 127), so a key sounds about as loud whatever its tune, decay or tone.
inline i32 trimmed_gain(u32 model, const i32* p) {
    i32 t = dsp::log2(static_cast<u32>(kit::gain[model]));            // Q16 octaves
    for (u32 s = 0; s < 4; ++s) {
        const signed char* tr = kit::trim[model][s];
        const i32 x = p[s] >> 13;                                       // 0..4, Q16
        const u32 i = static_cast<u32>(x >> 16) < 3 ? static_cast<u32>(x >> 16) : 3;
        const i32 f = x - static_cast<i32>(i << 16);
        t += tr[i] * 2048 + (((tr[i + 1] - tr[i]) * f) >> 5);
    }
    if (t > 31 * dsp::octave - 1) t = 31 * dsp::octave - 1;
    return static_cast<i32>(dsp::exp2(t));
}

inline void begin_note(Voice& voice, Shared& shared, opxy::Parameters parameters) {
    voice.pending = 0;
    voice.done = voice.quiet = voice.kept = 0;
    voice.model = voice.next;
    voice.t = 0;
    // A fresh body: nothing rings over from the last note this voice played.
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&voice.body);
    for (unsigned i = 0; i < sizeof(voice.body); ++i) bytes[i] = 0;
    voice.body.due = 0xffffffffu;
    voice.body.seed = 0x243f6a88u ^ (shared.notes * 0x85ebca6bu) ^ voice.owner;
    i32 p[4];
    for (u32 i = 0; i < 4; ++i) p[i] = q31(parameters[i]);
    body::prepare(voice.body, voice.model, p);
    voice.gain = trimmed_gain(voice.model, p);
    const motion::Feel feel = motion::feel(voice.feel);
    motion::plan(voice.run, raw(parameters[4]), raw(parameters[5]), raw(parameters[6]), raw(parameters[7]),
                 voice.space, feel, sixteenth(shared));
    voice.balls = voice.run.m;
    for (u32 b = 0; b < voice.run.m; ++b) motion::begin(voice.cursor[b], voice.run, b, jitter(voice, feel.lax));
}

// The block's impacts, in time order: a sort of a few.
inline u32 gather(Voice& voice, motion::Impact* hits) {
    const u32 end = voice.t + opxy::block_frames;
    const u32 lax = motion::feel(voice.feel).lax;
    u32 count = 0;
    for (u32 b = 0; b < voice.run.m; ++b) {
        motion::Cursor& c = voice.cursor[b];
        while (!c.done && c.next < end && count < max_impacts) {
            motion::Impact hit = motion::take(c, voice.run, b, jitter(voice, lax));
            if (hit.at < voice.t) hit.at = voice.t;
            u32 i = count++;
            while (i && hits[i - 1].at > hit.at) { hits[i] = hits[i - 1]; --i; }
            hits[i] = hit;
            if (c.done && voice.balls) --voice.balls;
        }
    }
    return count;
}

// The kit's soft ceiling: straight to 0.42 of full scale (1.5 dB under the
// SDK's half-scale limit), then bending into it. Every default key peaks
// under the bend; only a harder stroke (a steeper tilt, a control's extreme)
// reaches it. x and the result are Q31.
inline i32 ceiling(i32 x) {
    const i32 knee = 901943132;          // 0.42 of full scale
    const i32 a = x < 0 ? -x : x;
    if (a <= knee) return x;
    // The 0.08 left: 0.08 tanh(over / 0.08), tanh reading 2^28 as 1, so the
    // over is scaled by 1 / (8 x 0.08) = 1.5625.
    const i32 over = a - knee;
    const i32 bent = dsp::multiply(dsp::tanh(over + dsp::multiply(over, 1207959552)), 171798692);
    const i32 y = knee + bent;
    return x < 0 ? -y : y;
}

inline void render(Voice& voice, Shared& shared, u32, opxy::Output output, opxy::Parameters parameters, opxy::Pitch) {
    // The kit's clock: the first voice to render in a block moves it on.
    if (voice.stamp == shared.block) {
        ++shared.block;
        shared.now += opxy::block_frames;
    }
    voice.stamp = shared.block;
    if (voice.pending) begin_note(voice, shared, parameters);
    if (voice.done) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
        if (voice.kept < keep_blocks) ++voice.kept;
        return;
    }
    body::Body& b = voice.body;
    // A turned control (a step's lock, a knob) re-voices the body as it rings.
    for (u32 i = 0; i < 4; ++i)
        if (q31(parameters[i]) != b.p[i]) {
            i32 p[4];
            for (u32 k = 0; k < 4; ++k) p[k] = q31(parameters[k]);
            body::prepare(b, voice.model, p);
            voice.gain = trimmed_gain(voice.model, p);
            break;
        }
    motion::Impact hits[max_impacts];
    const u32 count = gather(voice, hits);
    i32 sum[opxy::block_frames];
    u32 at = 0, next = 0;
    const u32 n = voice.run.n;
    // Impacts and ghosts strike on an 8-sample grid (0.18 ms): a block splits into
    // at most eight stretches however many balls land in it, and each stretch
    // runs the body's parts once.
    while (at < opxy::block_frames) {
        u32 until = opxy::block_frames;
        if (next < count && ((hits[next].at - voice.t) & ~7u) < until) until = (hits[next].at - voice.t) & ~7u;
        const u32 ghost = body::ghost_due(b);
        if (ghost != 0xffffffffu) {
            const u32 g = ghost > b.now ? ((ghost - b.now + at) & ~7u) : at;
            if (g < until) until = g < at ? at : g;
        }
        if (until > at) {
            body::run(b, sum + at, until - at);
            at = until;
        }
        if (at >= opxy::block_frames) break;
        while (next < count && ((hits[next].at - voice.t) & ~7u) <= at) {
            body::strike(b, hits[next].v, hits[next].kind, hits[next].step, n);
            ++next;
        }
        // Ghosts due within this stretch of the grid strike now.
        const u32 now = b.now;
        b.now = now + 7;
        body::strike_ghosts(b, n);
        b.now = now;
    }
    voice.t += opxy::block_frames;
    const i32 g = voice.gain;
    i32 peak = 0;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const i32 y = ceiling(dsp::multiply(sum[i], g) * 32);
        output[i] = y;
        const i32 a = y < 0 ? -y : y;
        if (a > peak) peak = a;
    }
    const bool waiting = voice.balls > 0 || body::ghost_due(b) != 0xffffffffu;
    voice.quiet = peak < quiet_floor && !waiting ? voice.quiet + 1 : 0;
    if (voice.quiet >= quiet_blocks) voice.done = 1;
}
}
