#pragma once
// Syahi's one voice: a struck object of eight modes, played by any model.
//
//   strike(s) -> 8 modes (FORM, LOAD, SPOT; pitch lifted by the struck energy)
//             -> + wash (metal's mode coupling) + noise (brush, palm, clap)
//             -> METAL's tilt -> + rattle (wires that buzz only while the head swings past them)
//             -> + body (one resonance below) + air -> GRIT -> out
//
// Each mode is a sine whose increment and amplitude are planned once a block
// and stepped linearly through it, so a pitch that falls fast (a zap) glides
// sample by sample and never steps. A strike lands on its own sample: its
// energy ramps in over the contact time as a raised cosine, so a felt beater
// is soft and a brass tip clicks. Signals are in units of a full strike on a
// mode of weight one, 2^24, well inside Q31.
#include "models.hpp"

namespace syahi {
const i32 strike_unit = 1 << 24;   // a full strike on a mode of weight one
const u32 head_modes = 8;
const u32 body_mode = 8, air_mode = 9, partials = 10;
const i32 silent = 1 << 11;   // a partial this quiet (units) is skipped: -78 dB of a strike

struct Partial {
    u32 phase, inc;
    i32 dinc;       // increment's step a sample this block
    i32 g, dg;      // amplitude drawn, and its step a sample
    i32 pe;         // the pending strike's amplitude this block, ramped in
    i32 a, p;       // established and pending amplitude, before modulation
    i32 m;          // this block's modulation (head, nyquist, swell, beat), Q28
    i32 decay;      // per-block multiplier, Q31
    i32 rest;       // log2 Hz at rest
    i32 weight;     // a full strike's amplitude, units
    i32 bend;       // octaves at full struck energy, Q16
    u32 beat_phase, beat_inc;
    i32 rank;       // the swell's share, Q28
    i32 u;          // the wash's unweighted amplitude, units
};

// The wash's pairs: products of modes (0,3) (1,5) (2,6) (4,7), as a cymbal's
// modes couple, then the square-wave products of (0,2) (3,6): their
// odd harmonics' sidebands fill the top octaves with a dense, hissing spray.
const u32 wash_pairs = 6;
const u8 wash_a[wash_pairs] = {0, 1, 2, 4, 0, 3};
const u8 wash_b[wash_pairs] = {3, 5, 6, 7, 2, 6};

struct Core {
    Partial part[partials];
    Spec spec;
    u32 model;
    i32 head_gain;      // head * velocity^1.3, Q28
    i32 wash_trim;      // METAL's level kept as WASH rises, Q31
    i32 velocity;       // Q31
    i32 body_rise;      // the body's 2 ms rise, an exponential subtracted (units)
    i32 body_rise_decay, body_bend;
    i32 energy, energy_decay;     // the struck energy (Q31) and its per-block fall
    i32 glide_left, glide_decay;  // e^(-t / glide time), Q31
    i32 swell_left, swell_decay;  // e^(-t / 0.18 s), Q31
    u32 ramp, ramp_step;          // the strike's raised cosine: phase 0 .. 2^31 over the contact
    u32 struck;                   // strikes so far this note
    i32 count_trim;               // log2 of each strike's share of a single strike's level
    i32 next_event;               // samples to the next strike, Q8; -1 when none
    i32 gap;                      // the next gap, Q8 samples
    u32 air_time;                 // samples since the note started
    u32 air_length, air_step;     // the push's length (samples) and 2^31 / length
    u32 seed;
    // noise burst: established and pending level (units), decay a sample, ramp
    i32 nb, np, nd, nramp, nstep, nd_tail;
    u32 noise_quiet;
    Svf n1, n2;
    SvfCoefficients cn;
    Chamberlin r0, r1;
    // rattle
    i32 follow, peak, drive, follow_c, drive_c, wire_gain;
    Svf wb;
    SvfCoefficients cwb;
    i32 wire_high, wire_high_c, wire_low1, wire_low2, wire_top_c;
    // wash
    Chamberlin xh;
    i32 wash_low, wash_c;
    i32 e_start[wash_pairs];      // the wash pairs' envelopes at the block's start
    i32 pair[wash_pairs], pair_decay[wash_pairs];   // and their rings, each the geometric mean of its two modes'
    i32 pair_in, pair_flow;       // a strike's share still coupling into the pairs, and its flow a block (Q31)
    i32 wash_lift;                // the flow's time over the ring's, Q31: the level the slower flow gives back
    i32 tilt, tilt_last;          // METAL's zero at Nyquist (Q31, 0.5) and the last sample it saw
};

// Mode k's resting phase: the prototype's k * 1.3 radians.
static inline u32 start_phase(u32 k) { return k * 888650287u; }

// ---- the strike's shape (setup: when a control moves)

// The pitch a mode reaches at the struck energy e (Q31), log2 Hz.
static inline i32 lifted(const Partial& m, i32 e, i32 glide) { return m.rest + multiply(e, m.bend) + glide; }

// The rattle's gain on the head's swing past the gap. The prototype integrated
// that swing (x 40); here it is followed, rising at once, so a snare's buzz
// starts with the stick, and this gain gives the same buzz energy.
const double wire_drive = 26.0;

static inline void setup(Core& v, const i32* controls, u32 form_raw, i32 human_pitch) {
    Spec& s = v.spec;
    spec(v.model, controls, s);
    s.pitch += human_pitch;
    form::Shape sh;
    form::shape(form_raw, s.spot, sh);
    if (s.node) sh.weight[0] = multiply(sh.weight[0], one - s.node);
    i32 alpha = sh.alpha;
    i32 pitch = s.pitch;
    if (s.load > 0) {
        // Raman: the paste pulls every mode toward the harmonic series on the lowest,
        // softens the material's tilt and lowers the note.
        static const i32 harmonic[8] = {0, lg(2), lg(3), lg(4), lg(5), lg(6), lg(7), lg(8)};
        for (u32 k = 0; k < head_modes; ++k)
            sh.ratio[k] += multiply(s.load, harmonic[k] + sh.ratio[0] - sh.ratio[k]);
        alpha -= multiply(alpha, s.load >> 1);
        pitch += log2u(static_cast<u32>(65536 - multiply(s.load, q16(0.18)))) - 16 * octave;
    }
    if (s.alpha >= 0) alpha = s.alpha;
    alpha = (alpha * s.tilt) >> 10;
    // Tilt and dispersion count from the first mode, the named partial: a bell's
    // hum under it rings a little longer than its prime, as a bell's does.
    const i32 lowest = sh.ratio[0];
    const i32 bend = multiply(s.bend, v.velocity);
    i32 shaped[head_modes], lives[head_modes];
    const i32 log_bend = bend > 0 ? log2u(static_cast<u32>(bend)) : 0;
    for (u32 k = 0; k < head_modes; ++k) {
        Partial& m = v.part[k];
        const i32 above = clamp(sh.ratio[k] - lowest, -octave / 4, 20 * octave);   // a hum rings a little longer, no more
        m.rest = pitch + sh.ratio[k];
        i32 life = s.decay - ((alpha * (above >> 4)) >> 6);
        // SWELL: energy flowing into the upper modes keeps them ringing, up to 1.5 octaves longer.
        if (s.swell) life += multiply(s.swell, q16(1.5) * static_cast<i32>(k) / 7);
        if (k == 0 && s.tube > 0) life += log2_1p(multiply(s.tube, q16(2.5)));
        // The contact's roll-off at this mode (Q15 weights).
        const i32 u = octave + m.rest + s.contact + 8 * octave;
        const i32 index = clamp(u >> 12, 0, 256);
        shaped[k] = multiply(sh.weight[k] << 16, tables::mallet[index]) >> 16;
        if (s.bright) shaped[k] += multiply(shaped[k], s.bright / 7 * static_cast<i32>(k));
        m.decay = per_block(life);
        lives[k] = life;
        // Dispersion: a mode r times the lowest bends r^(1.6 spring) as far.
        if (bend > 0) {
            const i32 lb = log_bend + ((s.spring >> 2) * (above >> 2) >> 12);
            m.bend = static_cast<i32>(exp2i(clamp(lb, 0, 20 * octave)));
        } else {
            m.bend = 0;
        }
        if (s.beat > 0) {
            // cos(pi beat t (1 + 0.15 k)): the twin's beat, a little faster on higher modes.
            const i32 hz = log2u(static_cast<u32>(s.beat)) - 16 * octave - octave + log2_1p(q16(0.15) * static_cast<i32>(k));
            m.beat_inc = exp2i(hz + increment_per_hz + 6 * octave);
        } else {
            m.beat_inc = 0;
            m.beat_phase = 0;
        }
        m.rank = multiply(s.swell, q28(5.0 / 7.0) * static_cast<i32>(k));   // up to 6x on the top mode
    }
    // A low drum made of plate or bell (FORM past 100, not METAL or BAR, whose
    // gongs and keys beat on purpose) clangs where two strong modes lie under
    // 50 Hz apart: the beat sits in the ear's roughest band. Such a mode is
    // struck as softly as its nearness asks (to a tenth, by the square); the energy
    // below gives it back to the others, so the note rings and the level holds.
    if (form_raw > 100 && v.model != metal && v.model != bar) {
        const i32 depth = static_cast<i32>(form_raw > 112 ? 12 : form_raw - 100);   // fades in from 100 to 112
        u32 hz[head_modes];
        for (u32 k = 0; k < head_modes; ++k) hz[k] = exp2i(clamp(v.part[k].rest, 0, 15 * octave));
        for (u32 k = 1; k < head_modes; ++k) {
            u32 nearest = 50;
            for (u32 j = 0; j < head_modes; ++j) {
                if (j == k || shaped[j] < (shaped[k] >> 1)) continue;   // only a mode at least half as strong beats it rough
                const u32 d = hz[k] > hz[j] ? hz[k] - hz[j] : hz[j] - hz[k];
                if (d < nearest) nearest = d;
            }
            if (nearest < 50) {
                const i32 n = static_cast<i32>(nearest);
                const i32 keep = 3277 + n * n * 29491 / 2500;   // Q15: a tenth at unison .. whole at 50 Hz
                const i32 g = 32768 - (32768 - keep) * depth / 12;
                shaped[k] = (shaped[k] * g) >> 15;
            }
        }
    }
    // A soft contact takes the top off without taking the level down: the
    // weights keep the energy FORM gave them (up to four times back), so the
    // beater, mallet or hammer changes the tone, not the loudness.
    {
        u32 sum = 0;
        for (u32 k = 0; k < head_modes; ++k) {
            const u32 h = static_cast<u32>(shaped[k]) >> 1;
            sum += h * h;
        }
        u32 root = isqrt(sum);   // Q14; the unshaped weights make 2^14
        if (root < (1u << 12)) root = 1u << 12;
        for (u32 k = 0; k < head_modes; ++k) {
            i32 weight = static_cast<i32>((static_cast<u32>(shaped[k]) << 14) / root) << 9;   // Q15 -> units (2^24)
            if (k == 0 && s.tube > 0) weight += multiply(weight, s.tube) + multiply(weight, s.tube >> 1);
            v.part[k].weight = weight;
        }
    }
    // The wash's pairs ring as long as their two modes on average: the
    // geometric mean of the two rings, so the spray lasts as the modes do.
    for (u32 j = 0; j < wash_pairs; ++j) v.pair_decay[j] = per_block((lives[wash_a[j]] + lives[wash_b[j]]) >> 1);
    v.tilt = v.model == metal ? q31(0.5) : 0;
    // The strike's flow into the pairs, and the level that slower flow gives back.
    {
        const i32 flow = clamp(s.decay + lg(0.3), lg(0.04), lg(0.15));
        v.pair_flow = one - per_block(flow);
        v.wash_lift = exp2q(flow - s.decay < 0 ? flow - s.decay : 0);
    }
    // The body: one resonance under the head.
    Partial& b = v.part[body_mode];
    b.rest = s.body_pitch;
    b.weight = multiply(s.body, q31(0.9 / 16.0)) << 0;   // Q28 -> units: * 2^-4
    b.weight = multiply(b.weight, v.velocity);
    b.decay = per_block(s.body_decay);
    v.body_rise_decay = multiply(b.decay, per_block(lg(0.002 * 6.9078)));
    v.body_bend = multiply(bend, q31(0.28));
    // The air: a 48 Hz push, a cycle and a half long.
    Partial& air = v.part[air_mode];
    air.rest = lg(48);
    air.weight = multiply(multiply(s.air >> 4, q31(1.1 / 2)) << 1, v.velocity);
    const i32 air_log = log2u(static_cast<u32>(s.air_cycles)) - 16 * octave + lg(44100.0 / 48.0);
    v.air_length = exp2i(air_log);
    v.air_step = exp2i(31 * octave - air_log);
    v.count_trim = -multiply(log2u(s.count), q31(0.25));
    // METAL: the wash takes over from the modes as WASH rises; together they
    // keep one level (1 / (1 + 4 WASH^2)), so WASH changes the colour, not the loudness.
    if (s.dense) {
        const u32 denominator = (1u << 28) + 4u * static_cast<u32>(multiply(s.dense, s.dense) << 3);   // 1 + 4 WASH^2, Q28
        v.wash_trim = static_cast<i32>(((1u << 28) / (denominator >> 16)) << 15);                  // Q16 -> Q31
        if (v.wash_trim < 0) v.wash_trim = one;
    } else {
        v.wash_trim = one;
    }
    // The struck energy and the glide.
    v.energy_decay = per_block(s.bend_time + lg(6.9078));
    v.glide_decay = per_block(s.glide_time + lg(6.9078));
    v.swell_decay = per_block(lg(0.18 * 6.9078));
    // The contact's raised cosine.
    v.ramp_step = exp2i(clamp(31 * octave - (s.contact + lg(44100)), 0, 31 * octave - 1));
    // Noise: its band, cascaded twice, and the two lowest modes' rings.
    v.cn.set(s.noise_q, s.noise_tone < lg(9000) ? s.noise_tone : lg(9000));
    v.r0.set(q_drum, v.part[0].rest);
    v.r1.set(q_ring, v.part[1].rest < lg(15000) ? v.part[1].rest : v.part[0].rest);
    v.nd = one - one_pole(s.noise_length);
    v.nd_tail = one - one_pole(s.noise_length + s.tail);
    v.nstep = static_cast<i32>(exp2i(clamp(31 * octave - (s.noise_attack + lg(44100)), 0, 31 * octave - 1)));
    // The rattle.
    v.cwb.set(q_wires, s.wire_tone);
    v.wire_high_c = one_pole(-lg(2 * 3.14159265 * 900));
    // Two one-poles at 1.55 x the corner make the prototype's second-order corner.
    v.wire_top_c = one_pole(-(s.wire_top + lg(1.55 * 2 * 3.14159265)));
    v.follow_c = one_pole(lg(0.0006));
    v.drive_c = one_pole(s.wire_time - lg(4.5));
    // 40 x the threshold-passed motion, through noise of a third of its swing.
    v.wire_gain = multiply(s.wires, q31(0.4 * wire_drive * 1.732 / 32));   // Q28 * 2^-5 -> Q23
    // The wash.
    // Its band follows the cymbal's size: a big, low one keeps its body (from
    // twice the note, at least 900 Hz) and a darker top (ten times it); a small
    // hat sprays from 3 kHz to the top octave.
    v.xh.set(q_butter, clamp(pitch + lg(2), lg(900), lg(3000)));
    v.wash_c = one_pole(-(clamp(pitch + lg(10), lg(5000), lg(12000)) + lg(2 * 3.14159265)));
}

// ---- strikes

// A strike's level (Q31) on every partial at sample offset `at` of the block,
// folding what the last strike's ramp had already brought in.
static inline void land(Partial& m, i32 level, i32 ramp) {
    if (m.p) {
        m.g += multiply(m.pe, ramp);
        const i32 in = multiply(m.p, ramp);
        m.a += in;
        m.p -= in;
    }
    m.p += multiply(level, m.weight);
    m.pe = multiply(m.p, m.m) << 3;
}

// The level of strike n of the note, Q31.
static inline i32 strike_level(const Core& v, u32 n) {
    const Spec& s = v.spec;
    const i32 forward = s.rebound * static_cast<i32>(n);
    const u32 last = s.count - 1;
    i32 backward = s.rebound * static_cast<i32>(last - n);
    if (n < last) backward += lg(0.7);
    // Many strikes share the energy of one: each is count^-0.25 as loud.
    return exp2q(forward + multiply(s.last_loud, backward - forward) + v.count_trim);
}

// Plans the strikes landing in this block: their offsets and levels.
struct Strikes {
    u32 count;
    u32 at[4];
    i32 level[4];
    bool last[4];
};
static inline void schedule(Core& v, Strikes& out) {
    out.count = 0;
    while (v.next_event >= 0 && (v.next_event >> 8) < 64 && out.count < 4) {
        const u32 n = v.struck;
        out.at[out.count] = static_cast<u32>(v.next_event >> 8);
        out.level[out.count] = strike_level(v, n);
        out.last[out.count] = n + 1 >= v.spec.count;
        ++out.count;
        ++v.struck;
        if (v.struck >= v.spec.count) {
            v.next_event = -1;
            break;
        }
        // The next gap: the first is GAP, each after it SHRINK times the last,
        // each nudged by the hands' unevenness. At least two samples apart.
        const i32 jitter = multiply(v.spec.jitter, noise(v.seed) >> 1);
        i32 gap = static_cast<i32>(exp2i(clamp(v.spec.gap + v.spec.shrink * static_cast<i32>(n) + lg(44100) + 8 * octave, 0, 30 * octave)));
        gap += multiply(gap, jitter);
        v.next_event += gap > 512 ? gap : 512;
    }
    if (v.next_event >= 0) v.next_event -= 64 << 8;
}

// ---- a block

// The planned values at the block's end for partial k.
static inline void plan_mode(Core& v, Partial& m, u32 k, i32 energy_end, i32 glide_now) {
    // A finished ramp's strike joins the established ring.
    if (v.ramp >= 0x80000000u && m.p) {
        m.a += m.p;
        m.g += m.pe;
        m.p = 0;
        m.pe = 0;
    }
    m.a = multiply(m.a, m.decay);
    m.p = multiply(m.p, m.decay);
    m.u = multiply(m.u, m.decay);
    // Pitch at the block's end; the increment glides there sample by sample.
    const i32 pitch = lifted(m, energy_end, glide_now);
    const u32 cap = increment(lg(19500));
    u32 target = increment(pitch);
    if (target > cap) target = cap;
    if (m.inc > cap) m.inc = cap;
    m.dinc = (static_cast<i32>(target) - static_cast<i32>(m.inc)) >> 6;
    // Modulation: the head's gain, the fade under Nyquist, swell and beat.
    i32 mod = v.head_gain;
    const i32 hz = static_cast<i32>(exp2i(clamp(pitch, 0, 15 * octave)));
    if (hz >= 18000) mod = 0;
    else if (hz > 15000) mod = multiply(mod, (18000 - hz) * 715828);
    if (k < head_modes && m.rank) mod = multiply(mod, q28(1.0) + multiply(m.rank, one - v.swell_left)) << 3;
    if (m.beat_inc) {
        m.beat_phase += m.beat_inc;
        mod = multiply(mod, sine(m.beat_phase + 0x40000000u));
    }
    m.m = mod;
    m.pe = multiply(m.p, mod) << 3;
    const i32 g_end = multiply(m.a, mod) << 3;
    m.dg = (g_end - m.g) >> 6;
}

// One partial over samples [begin, end) of the block, added into out. The
// pending strike ramps in along r (Q31 0..1) when it has one.
static inline void run(Partial& m, i32* out, const i32* r, u32 begin, u32 end, bool ramping) {
    u32 phase = m.phase, inc = m.inc;
    const i32 dinc = m.dinc;
    i32 g = m.g;
    const i32 dg = m.dg;
    if (ramping && m.pe) {
        const i32 pe = m.pe;
        for (u32 n = begin; n < end; ++n) {
            phase += inc;
            inc += static_cast<u32>(dinc);
            out[n] += multiply(g + multiply(pe, r[n]), sine(phase));
            g += dg;
        }
    } else {
        for (u32 n = begin; n < end; ++n) {
            phase += inc;
            inc += static_cast<u32>(dinc);
            out[n] += multiply(g, sine(phase));
            g += dg;
        }
    }
    m.phase = phase;
    m.inc = inc;
    m.g = g;
}
// The same, writing each sample's unit sine for the wash (METAL).
static inline void run_metal(Partial& m, i32* out, i32* sines, const i32* r, u32 begin, u32 end, bool ramping) {
    u32 phase = m.phase, inc = m.inc;
    const i32 dinc = m.dinc;
    i32 g = m.g;
    const i32 dg = m.dg;
    if (ramping && m.pe) {
        const i32 pe = m.pe;
        for (u32 n = begin; n < end; ++n) {
            phase += inc;
            inc += static_cast<u32>(dinc);
            const i32 s = sine(phase);
            sines[n] = s;
            out[n] += multiply(g + multiply(pe, r[n]), s);
            g += dg;
        }
    } else {
        for (u32 n = begin; n < end; ++n) {
            phase += inc;
            inc += static_cast<u32>(dinc);
            const i32 s = sine(phase);
            sines[n] = s;
            out[n] += multiply(g, s);
            g += dg;
        }
    }
    m.phase = phase;
    m.inc = inc;
    m.g = g;
}
static inline void skip(Partial& m, u32 count) {
    m.phase += m.inc * count + static_cast<u32>(m.dinc) * (count * (count + 1) / 2);
    m.inc += static_cast<u32>(m.dinc) * count;
    m.g += m.dg * static_cast<i32>(count);
}
// Where `count` samples of run() leave a partial: phase += inc, then inc += dinc,
// a sample at a time, and g += dg.
static inline void advance(Partial& m, u32 count) {
    m.phase += m.inc * count + static_cast<u32>(m.dinc) * (count * (count - 1) / 2);
    m.inc += static_cast<u32>(m.dinc) * count;
    m.g += m.dg * static_cast<i32>(count);
}
// One partial over samples [begin, begin + count): a counted loop (a
// hardware loop), the table address computed a sample ahead so the load never
// waits on it. The phase and increment at the end are advance()'s sums, exact
// in u32. With `ramp` the pending strike ramps in along r; with `metal` the
// unit sine is kept for the wash.
template <bool ramp, bool metal>
__attribute__((noinline)) static void run_mode(Partial& a, i32* __restrict out, i32* __restrict sa, const i32* __restrict r,
                                              u32 begin, u32 count) {
    const i16* const table = tables::sine;
    u32 pa = a.phase + a.inc, ia = a.inc + static_cast<u32>(a.dinc);
    const u32 da = static_cast<u32>(a.dinc);
    i32 ga = a.g;
    const i32 dga = a.dg, ea = ramp ? a.pe : 0;
    const i16* ta = table + (pa >> 18);
    i32* o = out + begin;
    const i32* rr = r + begin;
    i32* qa = metal ? sa + begin : nullptr;
    for (u32 n = 0; n < count; ++n) {
        const i32 xa = static_cast<i32>(*ta) << 16;
        pa += ia;
        ia += da;
        ta = table + (pa >> 18);
        if (metal) qa[n] = xa;
        o[n] += ramp ? multiply(ga + multiply(ea, rr[n]), xa) : multiply(ga, xa);
        ga += dga;
    }
    advance(a, count);
}
static inline bool quiet(const Partial& m) {
    return absolute(m.a) < silent && absolute(m.p) < silent && absolute(m.g) < silent && absolute(m.dg) < 2;
}


// Scratch for the wash: each mode's unit sine through the block. Voices render
// one at a time, so they share it.
struct Scratch {
    i32 sines[head_modes][64];
};

// Three passes after the modes, each a tight loop with its state held in
// registers: the wash and the noise add to the head; the wires then follow all of it.
// The wash pairs' rings at the block's end: each falls at its pair's rate and
// takes a strike as its modes do. Once a block, before the pass.
static inline void wash_envelopes(Core& v, const Strikes& strikes) {
    // The coupling takes a moment to pass a strike into the spray: its share
    // flows in over a third of the ring (40 to 150 ms to -60 dB), so the wash
    // swells in under the strike's click and a hat's peak sits nearer its body.
    i32 struck = 0;
    for (u32 i = 0; i < strikes.count; ++i) struck += strikes.level[i] >> 7;
    v.pair_in += struck;
    const i32 flowing = multiply(v.pair_in, v.pair_flow);
    v.pair_in -= flowing;
    for (u32 j = 0; j < wash_pairs; ++j) v.pair[j] = multiply(v.pair[j], v.pair_decay[j]) + flowing;
}
__attribute__((noinline)) static void wash_pass(Core& v, const Scratch& scratch, i32* __restrict out) {
    const i32* end = v.pair;
    const Spec& s = v.spec;
    // The wash pairs' envelopes, interpolated through the block.
    i32 e[wash_pairs], de[wash_pairs];
    for (u32 j = 0; j < wash_pairs; ++j) {
        e[j] = v.e_start[j];
        de[j] = (end[j] - e[j]) >> 6;
        v.e_start[j] = end[j];
    }
    const i32 dense = multiply(s.dense, v.velocity);
    // The wash's level: 1.5 x WASH, rising faster near the top (the prototype had 1.4 x): it rings
    // as long as its modes, so it needs less than twice. A slow flow (a hat's) is given back its level.
    i32 wash_gain = multiply(multiply(multiply(dense, q31(2.0 / 8)) * 8 + (multiply(multiply(dense, s.dense) << 3, q31(3.0 / 4)) << 2), v.wash_trim), q31(0.76));
    wash_gain += multiply(wash_gain, v.wash_lift);
    // Noise in the wash, as loud as modes 1 and 4 together and as dense: 0.7 x a unit normal.
    const i32 hiss = multiply(multiply((v.part[1].u + v.part[4].u) >> 1, q31(0.7 * 1.732 / 2)), dense << 2) << 1;
    const i32 *s0 = scratch.sines[0], *s1 = scratch.sines[1], *s2 = scratch.sines[2], *s3 = scratch.sines[3];
    const i32 *s4 = scratch.sines[4], *s5 = scratch.sines[5], *s6 = scratch.sines[6], *s7 = scratch.sines[7];
    // The pairs' envelopes held through the block, at its middle: they move slowly.
    const i32 e0 = e[0] + (de[0] << 5), e1 = e[1] + (de[1] << 5), e2 = e[2] + (de[2] << 5), e3 = e[3] + (de[3] << 5);
    const i32 e4 = (e[4] + (de[4] << 5)) >> 1, e5 = (e[5] + (de[5] << 5)) >> 1;
    u32 seed = v.seed;
    Chamberlin high = v.xh;
    i32 low = v.wash_low;
    const i32 c = v.wash_c;
    for (u32 n = 0; n < 64; ++n) {
        i32 w = multiply(multiply(s0[n], s3[n]), e0) + multiply(multiply(s1[n], s5[n]), e1)
              + multiply(multiply(s2[n], s6[n]), e2) + multiply(multiply(s4[n], s7[n]), e3);
        w += (s0[n] ^ s2[n]) < 0 ? -e4 : e4;
        w += (s3[n] ^ s6[n]) < 0 ? -e5 : e5;
        w += multiply(noise(seed), hiss) << 1;
        low += multiply(high.highpass(w) - low, c);
        out[n] += multiply(low, wash_gain) << 3;
    }
    v.seed = seed;
    v.xh = high;
    v.wash_low = low;
}
// The burst between strikes: samples [begin, end), every value in a local.
struct NoiseRun {
    i32 nb, np, ramp, step, nd;
    u32 seed;
    Svf f1, f2;
    SvfCoefficients cn;
    Chamberlin g0, g1;
    i32 ring, air, gain;
};
static inline void noise_run(NoiseRun& z, i32* __restrict out, u32 begin, u32 end) {
    i32 nb = z.nb, np = z.np, ramp = z.ramp, nd = z.nd;
    const i32 step = z.step, ring = z.ring, air = z.air, gain = z.gain;
    u32 seed = z.seed;
    Svf f1 = z.f1, f2 = z.f2;
    const SvfCoefficients cn = z.cn;
    Chamberlin g0 = z.g0, g1 = z.g1;
    for (u32 n = begin; n < end; ++n) {
        const i32 level = nb + multiply(np, ramp);
        ramp = ramp > one - step ? one : ramp + step;
        nb = multiply(nb, nd);
        np = multiply(np, nd);
        const i32 x = multiply(noise(seed), level);
        // The band, twice; what the first stage passes above it is the air
        // (a stick's crack, a clap's spread), as much as the model asks.
        i32 band, low;
        svf_step(f1, cn, x, band, low);
        band = multiply(cn.k, band) * 2;
        const i32 tone = svf_bandpass(f2, cn, band) + multiply(band + ((x - low) >> 2), air);
        const i32 rung = multiply(g0.bandpass(x), q31(0.6)) + multiply(g1.bandpass(x), q31(0.4));
        out[n] += multiply(tone + multiply(rung, ring), gain) << 1;
    }
    z.nb = nb; z.np = np; z.ramp = ramp; z.seed = seed;
    z.f1 = f1; z.f2 = f2; z.g0 = g0; z.g1 = g1;
}
__attribute__((noinline)) static void noise_pass(Core& v, const Strikes& strikes, i32* __restrict out) {
    const Spec& s = v.spec;
    // The burst: established and pending level, the pending part ramping in.
    NoiseRun z;
    z.nb = v.nb; z.np = v.np; z.ramp = v.nramp; z.step = v.nstep; z.nd = v.nd; z.seed = v.seed;
    z.f1 = v.n1; z.f2 = v.n2; z.cn = v.cn; z.g0 = v.r0; z.g1 = v.r1;
    z.ring = s.noise_ring; z.air = s.noise_air; z.gain = multiply(v.velocity, q31(1.732 / 2));
    u32 begin = 0;
    for (u32 i = 0; i <= strikes.count; ++i) {
        const u32 end = i < strikes.count ? strikes.at[i] : 64;
        noise_run(z, out, begin, end);
        if (i < strikes.count) {
            z.nb += multiply(z.np, z.ramp);
            z.np -= multiply(z.np, z.ramp);
            z.np += multiply(strikes.level[i], s.noise >> 4);
            z.ramp = 0;
            if (s.jitter) {
                // Each hand claps a little brighter or duller; the last,
                // ringing on as the room's tail, through a wider band.
                const i32 tone = s.noise_tone + (multiply(noise(z.seed), s.jitter) >> 15);
                v.cn.set(strikes.last[i] && s.tail ? q_tone : s.noise_q, tone < lg(9000) ? tone : lg(9000));
                z.cn = v.cn;
            }
            if (strikes.last[i] && s.tail) z.nd = v.nd_tail;
        }
        begin = end;
    }
    v.nb = z.nb; v.np = z.np; v.nramp = z.ramp; v.nd = z.nd; v.seed = z.seed;
    v.n1 = z.f1; v.n2 = z.f2; v.r0 = z.g0; v.r1 = z.g1;
    v.noise_quiet = z.nb > silent || z.np > silent ? 0 : v.noise_quiet + 1;
}
__attribute__((noinline)) static void rattle_pass(Core& v, i32* __restrict out) {
    const Spec& s = v.spec;
    // The wires: a follower on the head's swing (0.6 ms, so they slap with the
    // stick); past the gap they buzz, answering at once (a 0.3 ms rise) and
    // settling over WIRE's time.
    i32 follow = v.follow, peak = v.peak, drive = v.drive, high = v.wire_high, low1 = v.wire_low1, low2 = v.wire_low2;
    const i32 fc = v.follow_c, dc = v.drive_c, hc = v.wire_high_c, tc = v.wire_top_c, gap = s.wire_gap, wg = v.wire_gain;
    const i32 tilt = v.tilt;
    i32 last = v.tilt_last;
    u32 seed = v.seed;
    Svf band = v.wb;
    const SvfCoefficients cwb = v.cwb;
    for (u32 n = 0; n < 64; ++n) {
        // METAL's tilt first: a zero at Nyquist (half the sum of this sample
        // and the last) darkens the spray to a cymbal's with no filter pole.
        const i32 x = out[n];
        const i32 y = x + multiply(last - x, tilt);
        last = x;
        follow += multiply(absolute(y) - follow, fc);
        if (follow > peak) peak = follow;
        const i32 past = positive(follow - multiply(peak, gap));
        drive += multiply(past - drive, past > drive ? q31(0.073) : dc);
        const i32 b = svf_bandpass(band, cwb, multiply(noise(seed), drive));
        high += multiply(b - high, hc);                // a one-pole high-pass at 900 Hz
        low1 += multiply((b - high) - low1, tc);       // two one-poles for the low-pass: no notch at
        low2 += multiply(low1 - low2, tc);             // Nyquist, so the top octave keeps its air
        out[n] = y + shl(multiply(low2, wg), 8);
    }
    v.follow = follow; v.peak = peak; v.drive = drive; v.wire_high = high; v.wire_low1 = low1; v.wire_low2 = low2;
    v.tilt_last = last;
    v.seed = seed;
    v.wb = band;
}

static inline void render_core(Core& v, Scratch& scratch, i32* out) {
    Strikes strikes;
    schedule(v, strikes);
    // The struck energy and the glide at the block's end.
    i32 energy_end = multiply(v.energy, v.energy_decay);
    for (u32 i = 0; i < strikes.count; ++i)
        if (strikes.level[i] > energy_end) energy_end = strikes.level[i];
    v.energy = energy_end;
    v.glide_left = multiply(v.glide_left, v.glide_decay);
    v.swell_left = multiply(v.swell_left, v.swell_decay);
    const i32 glide_now = v.spec.glide - multiply(v.spec.glide, v.glide_left);
    // The ramp through the block, restarting at each strike. Once it has
    // finished with no strike to come, nothing reads it (ramping is false).
    i32 r[64];
    i32 ramp_at[4];
    if (v.ramp >= 0x80000000u && !strikes.count) {
        v.ramp = 0x80000000u;
        r[0] = one;
    } else {
        u32 ramp = v.ramp, step = v.ramp_step, next = 0;
        for (u32 n = 0; n < 64; ++n) {
            if (next < strikes.count && strikes.at[next] == n) {
                ramp_at[next] = n ? r[n - 1] : (v.ramp >= 0x80000000u ? one : (one >> 1) - (sine(v.ramp + 0x40000000u) >> 1));
                ramp = 0;
                ++next;
            }
            ramp = ramp + step < ramp || ramp + step > 0x80000000u ? 0x80000000u : ramp + step;
            r[n] = ramp >= 0x80000000u ? one : (one >> 1) - (sine(ramp + 0x40000000u) >> 1);
        }
        v.ramp = ramp;
    }
    const bool ramping = r[0] < one || strikes.count;
    const bool metal_wash = v.spec.dense > 0;
    // The head's modes: plan each, skip the quiet, run the rest between the
    // strikes, landing each strike as it comes.
    u32 live[head_modes], lives = 0;
    for (u32 k = 0; k < head_modes; ++k) {
        Partial& m = v.part[k];
        plan_mode(v, m, k, energy_end, glide_now);
        for (u32 i = 0; i < strikes.count; ++i) m.u += strikes.level[i] >> 7;
        if (!strikes.count && quiet(m)) {
            skip(m, 64);
            if (metal_wash) for (u32 n = 0; n < 64; ++n) scratch.sines[k][n] = 0;
            continue;
        }
        live[lives++] = k;
    }
    {
        u32 begin = 0;
        for (u32 i = 0; i <= strikes.count; ++i) {
            const u32 end = i < strikes.count ? strikes.at[i] : 64;
            if (end > begin)
                for (u32 j = 0; j < lives; ++j) {
                    Partial& a = v.part[live[j]];
                    i32* sa = scratch.sines[live[j]];
                    const bool rp = ramping && a.pe;
                    if (metal_wash) {
                        if (rp) run_mode<true, true>(a, out, sa, r, begin, end - begin);
                        else run_mode<false, true>(a, out, sa, r, begin, end - begin);
                    } else {
                        if (rp) run_mode<true, false>(a, out, sa, r, begin, end - begin);
                        else run_mode<false, false>(a, out, sa, r, begin, end - begin);
                    }
                }
            if (i < strikes.count)
                for (u32 j = 0; j < lives; ++j) land(v.part[live[j]], strikes.level[i], ramp_at[i]);
            begin = end;
        }
    }
    // The wash, the noise and the rattle, a sample at a time.
    const Spec& s = v.spec;
    const bool noisy = s.noise > 0 && (v.nb > silent || v.np > silent || strikes.count || v.noise_quiet < 40);
    const bool rattling = s.wires > 0 || v.tilt;   // METAL always passes through: its tilt is there
    // The wash and the noise add to the head; the wires then follow all of it.
    if (metal_wash) {
        wash_envelopes(v, strikes);
        wash_pass(v, scratch, out);
    }
    if (noisy) noise_pass(v, strikes, out);
    if (rattling) rattle_pass(v, out);
    // The body and the air.
    Partial& b = v.part[body_mode];
    if (b.weight) {
        b.a = multiply(b.a, b.decay);
        v.body_rise = multiply(v.body_rise, v.body_rise_decay);
        for (u32 i = 0; i < strikes.count; ++i) {
            const i32 hit = multiply(strikes.level[i], b.weight);
            b.a += hit;
            v.body_rise += hit;
        }
        const u32 target = increment(b.rest + multiply(energy_end, v.body_bend));
        b.dinc = (static_cast<i32>(target) - static_cast<i32>(b.inc)) >> 6;
        b.dg = (b.a - v.body_rise - b.g) >> 6;
        if (absolute(b.a) < silent && absolute(b.g) < silent) skip(b, 64);
        else run(b, out, r, 0, 64, false);
    }
    Partial& air = v.part[air_mode];
    if (air.weight && v.air_time < v.air_length + 64) {
        // sin^2 (pi t / T) over T, AIR's 1.5 to 4 cycles of 48 Hz.
        const u32 t = v.air_time + 64;
        const i32 window = t >= v.air_length ? 0 : sine(t * v.air_step);
        const i32 g_end = multiply(air.weight, multiply(window, window));
        air.inc = increment(air.rest);
        air.dinc = 0;
        air.dg = (g_end - air.g) >> 6;
        run(air, out, r, 0, 64, false);
    }
    v.air_time += 64;
}
}

namespace syahi {
inline void Chamberlin::set(QClass q, i32 log2_hz) {
    // sin(pi fc / fs): half the phase increment, as a phase.
    f = sine(increment(log2_hz < lg(8000) ? log2_hz : lg(8000)) >> 1);
    damping = tables::svf_k[q];
}
}
