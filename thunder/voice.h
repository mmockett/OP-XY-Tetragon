#pragma once
// One voice of THUNDER: a thin sheet hung in a frame and played. Every model
// is the same few parts, wired in its own way (models.h):
//
//   exciter -> the sheet's low modes (eight resonators) -> the sheet's dense
//   high modes (a six-line feedback delay network) -> a chain lying on the
//   sheet (a contact rattle) -> the swaying sheet (a moving comb) -> pickup
//
// Signals inside a voice are Q31 integers near 2^27 ("one" in the
// prototype's units, U below), well under full scale so sums cannot overflow.
#include "dsp.h"

namespace thunder {
const i32 U = 1 << 27;
const unsigned half = frames / 2;   // a block at half the sample rate

// A low mode of the sheet: a Chamberlin resonator scaled so that it sounds at
// its weight: the force enters its band state times its weight (gw, Q31), and
// its band state is its sound.
struct Mode { i32 lp, bp, f, r, gw; u32 inc; };   // r: its decay over 8 samples, Q31
const u32 field_samples = 1024;
const u32 line_samples = 256;   // each of the four lines' ring
const u32 swing_samples = 128;
const u32 swing_keep = 76;      // the swing's delay is 11..72 samples: it keeps the last 76

struct Voice {
    u32 owner, model, next, pending, done, quiet;
    i32 tune, drive;          // the key's pad controls (drum_pad)
    u32 seed, t;              // noise; samples since the note began
    u32 waiting;              // strikes, pops or echoes still to come
    i32 gain;                 // the model's output gain, Q31 of 64
    i32 lim;                  // the transient limiter's gain now, Q31 (levels::limit)
    // the sheet's low modes
    u32 nmodes;
    Mode mode[8];
    i32 bend, bend_k, bend_env;   // CAMBER's pitch rise (Q28), its decay a block, now
    // the dense high modes
    u32 fon;
    i32 fk, fin, flevel, fg;  // in-loop low-pass, input gain (Q24), output level (Q31 x2), loop gain
    u32 fw;                   // the rings' write position
    i32 fprev;                // the last half-rate sample heard
    u32 fquiet;               // blocks it has been quiet
    u32 fd[4];                // the four lines' lengths
    i32 flp[4];               // their low-passes
    u32 fdiff, dpos;          // the input smeared by two all-passes for a strike's first blocks (crisper, less comb)
    u32 floop;                // 1: an all-pass in the first line's loop; 2: the rings clip (field_clip); 4: two lines (field_small)
    i32 dbuf[23 + 41];
    u32 apos;                 // the first line also passes an all-pass in its loop (37 samples)
    i32 abuf[37];
    // the mallet: a raised-cosine contact
    u32 pphase, pinc;
    i32 pamp;
    // the click of a hard beater
    i32 cenv, ck, chk, clk;
    u32 cseed;                // the click's own noise, the same every hit
    OnePole chp, clp, clp2;
    // re-strikes (TOM's roll, BOUNCE's bounces, POP's snaps, SHAKE's jolts)
    u32 at, left;
    i32 samp, samp_k, sgap, sgap_k;   // amplitude and its factor; gap (samples, Q8) and its factor
    // the chain: a contact rattle
    i32 c_amount, c_p, c_env, c_rel, c_thr, c_hpk, c_lpk, c_l1, c_l2, c_f1, c_q;
    Svf c_r1;
    OnePole c_hp;
    // the swaying sheet: a comb whose delay swings
    i32 sw_depth;
    u32 sw_phase, sw_inc, sw_pos;   // (sw_pos unused: the swing keeps its buffer in order)
    // each model's own coefficients and states
    i32 k[24];
    i32 s[9];                 // (9 keeps the voice its declared size)
    i32 mprev;                // the half-rate modes' last sample heard
    u32 nhalf;                // how many of the modes (the first) run at half rate or lower
    i32 qprev;                // the quarter-rate modes' last sample heard
    u32 nquarter;             // how many of those (the first) run at a quarter of the rate
    u32 u[16];
    Svf sv[4];
    OnePole op[6];
    Ramp env[4];
    i16 sw_buf[swing_samples];
    i32 buf[field_samples];
};

// ------------------------------------------------------------ the sheet's modes
// Sets up to eight low modes of a sheet of fundamental lf0 (log2 Hz).
// tension: Q16 unit, plate (0) to membrane (1); spot: Q16, centre (0) to near a
// corner (1); ltau0: the fundamental's ring (log2 s); tau_exp, tilt: Q16, how
// much faster and quieter the higher modes are; n: at most this many.
inline i32 shape_weight(u32 m, u32 n, i32 x, i32 y) {
    const i32 a = sine(static_cast<u32>(m) * static_cast<u32>(x) << 15);
    const i32 b = sine(static_cast<u32>(n) * static_cast<u32>(y) << 15);
    return absolute(multiply(a, b));
}
// The share of a mode a mallet's contact of length Tc reaches, as the
// prototype filters it: 1 / sqrt(1 + (1.6 f Tc)^4), Q31.
inline i32 contact_share(i32 lf, i32 ltc) {
    const i32 a = 4 * (lf + ltc + L(1.6));
    if (a < -12 * octave) return one;
    if (a > 14 * octave) return frac2(-a / 2);
    const u32 x = 65536u + pow2(a + 16 * octave);
    return frac2(-(log2q(x) - 16 * octave) / 2);
}
// What the raised-cosine contact (strike, pulse) really gives a mode of
// frequency f: its force's spectrum, |sinc(u) / (1 - u^2)| at u = f Tc, as log2
// (Q16, 0 down to -6 octaves past its first null, u = 2). A model lifts its
// strike by it to keep its body's level as the beater softens.
inline i32 contact_reach(i32 lf, i32 ltc) {
    const i32 l = lf + ltc;   // log2 u
    if (l < -8 * octave) return 0;
    if (l >= octave) return tables::reach[32];
    const u32 x = pow2(l + 20 * octave);   // 16 u, Q16
    const u32 i = x >> 16;
    const i32 f = static_cast<i32>(x & 0xffffu);
    return tables::reach[i] + mul16(tables::reach[i + 1] - tables::reach[i], f);
}
// A softer beater reaches fewer of a sheet's modes; this is how far its
// strike is lifted (as the strike's amp, U and up) so that the modes it
// reaches ring with the energy the hardest beater gives them all (each
// mode's a^2 tau), up to 12 dB: BEATER then changes the stick, not the
// drum's weight. The modes' weights must be those the hardest beater leaves.
// ltc: the contact (log2 s); lbend: log2 of how far over their own pitch the
// modes are struck (CAMBER); lt0, tau_exp: their ring, as setup_modes gives
// it; the modes from `heads` on ring for lrest (log2 s).
inline i32 beater_lift(const Voice& v, i32 lf0, i32 ltc, i32 lbend, i32 lt0, i32 tau_exp, u32 heads, i32 lrest) {
    const i32 l0 = log2q(v.mode[0].inc);
    u32 all = 0, reached = 0;
    for (u32 k = 0; k < v.nmodes; ++k) {
        const Mode& m = v.mode[k];
        if (m.gw <= 0) continue;
        const i32 lr = log2q(m.inc) - l0;
        const i32 lt = k < heads ? lt0 - mul16(tau_exp, lr) : lrest;
        const i32 e = 2 * (log2q(static_cast<u32>(m.gw)) - 31 * octave) + lt + 20 * octave;
        all += pow2(e > 0 ? e : 0);
        const i32 er = e + 2 * contact_reach(lf0 + lr + lbend, ltc);
        reached += pow2(er > 0 ? er : 0);
    }
    const i32 lift = (log2q(all > 0 ? all : 1) - log2q(reached > 0 ? reached : 1)) / 2;
    return static_cast<i32>(pow2(clamp(lift, 0, 2 * octave) + 27 * octave));
}
// log2 of how far a pitch glide of bend (Q28, over 1) lifts the modes.
inline i32 bend_log(i32 bend) { return log2q(static_cast<u32>((1 << 28) + bend)) - 28 * octave; }
// first: the lowest mode to sound (a model that high-passes its body can skip the fundamental)
inline void setup_modes(Voice& v, i32 lf0, i32 tension, i32 spot, i32 ltau0, i32 tau_exp, i32 tilt, u32 n, i32 ltc, u32 first = 0) {
    const i32 tt = clamp(tension, 0, 65535) * 32;
    const u32 idx = static_cast<u32>(tt) >> 16;
    const i32 fr = tt & 0xffff;
    const i32 x = Q16(0.5) - multiply(Q16(0.44), spot << 15);
    const i32 y = Q16(0.5) - multiply(Q16(0.30), spot << 15);
    i32 w[8], lr[8];
    u32 count = 0;
    i32 sum = 0;
    for (u32 k = 0; k < n && k < 8; ++k) {
        const i32 r = mix(tables::mode_ratio[idx][k] << 4, tables::mode_ratio[idx + (idx < 32)][k] << 4, fr);
        if (lf0 + r > L(6800.0)) break;
        const i32 wa = shape_weight(tables::mode_m[idx][k], tables::mode_n[idx][k], x, y);
        const i32 wb = shape_weight(tables::mode_m[idx + (idx < 32)][k], tables::mode_n[idx + (idx < 32)][k], x, y);
        i32 wk = wa + multiply(wb - wa, fr << 15);
        wk = multiply(wk, frac2(-mul16(tilt, r)));   // 1 / ratio^tilt
        wk = wk > 64 ? wk : 64;
        sum += multiply(wk, contact_share(lf0 + r, ltc)) >> 4;   // a skipped mode still counts, so the rest keep their level
        if (k < first) continue;
        w[count] = wk;
        lr[count] = r;
        ++count;
    }
    // Modes too faint to hear (under 2% of the sum) are left out.
    const i32 lsum = log2q(static_cast<u32>(sum > 1 ? sum : 1)) + 4 * octave;   // log2 of the sum, Q31 units
    u32 kept = 0;
    for (u32 k = 0; k < count; ++k) {
        const i32 gw = static_cast<i32>(pow2(log2q(static_cast<u32>(w[k])) - lsum + 31 * octave));
        if (gw < Q31(0.02) && kept >= 3) continue;
        Mode& m = v.mode[kept++];
        const i32 lf = lf0 + lr[k];
        const i32 lt = ltau0 - mul16(tau_exp, lr[k]);
        m.inc = increment(lf);
        m.f = svf_f(m.inc);
        m.r = expneg(3 * octave - lt - lsr);   // exp(-8 / (tau SR))
        m.gw = gw > 0 && gw < one ? gw : one;
        m.lp = m.bp = 0;
    }
    v.nmodes = kept;
}
// Whether anything is in a block (noinline: inlined, the compiler gives this
// loop no hardware loop).
__attribute__((noinline)) inline i32 block_or(const i32* x) {
    i32 any = 0;
    for (u32 n = 0; n < frames; ++n) any |= *x++;
    return any;
}
// The modes low enough to (under 3.5 kHz at their highest bend or wobble)
// run at half the sample rate, which halves their cost, and those under
// 1 kHz at a quarter of it: they are moved to the front, slowest first,
// their pitch and their decay set for their rate. A model's setup calls this
// once its modes are set.
const u32 half_top = 690000000u / 2;   // svf_f's top, as a full-rate increment at half rate
const u32 quarter_top = 97391000u;     // 1 kHz (its images then stay 40 dB down)
inline u32 half_inc(u32 inc) { return inc > half_top ? 2 * half_top : inc << 1; }
inline u32 quarter_inc(u32 inc) { return inc > half_top / 2 ? 2 * half_top : inc << 2; }
inline void split_modes(Voice& v) {
    const i32 m28 = (1 << 28) + (v.bend > 0 ? v.bend : 0);
    Mode m[8];
    u32 n = 0;
    for (u32 pass = 0; pass < 3; ++pass) {
        for (u32 k = 0; k < v.nmodes; ++k) {
            u32 peak = static_cast<u32>(multiply(static_cast<i32>(v.mode[k].inc >> 1), m28)) << 4;
            peak += peak >> 4;   // and a wobble
            const u32 tier = peak <= quarter_top ? 0 : peak <= half_top ? 1 : 2;
            if (tier == pass) m[n++] = v.mode[k];
        }
        if (pass == 0) v.nquarter = n;
        if (pass == 1) v.nhalf = n;
    }
    for (u32 k = 0; k < n; ++k) {
        if (k < v.nquarter) {
            m[k].f = svf_f(quarter_inc(m[k].inc));
            const i32 r2 = multiply(m[k].r, m[k].r);
            m[k].r = multiply(r2, r2);   // its decay over 8 quarter-rate samples
        } else if (k < v.nhalf) {
            m[k].f = svf_f(half_inc(m[k].inc));
            m[k].r = multiply(m[k].r, m[k].r);   // its decay over 8 half-rate samples
        }
        v.mode[k] = m[k];
    }
}
// A mode's pitch coefficient for a (full-rate) increment, at the rate it runs.
inline i32 mode_f(const Voice& v, u32 k, u32 inc) {
    return svf_f(k < v.nquarter ? quarter_inc(inc) : k < v.nhalf ? half_inc(inc) : inc);
}
// CAMBER: every mode's pitch rises by bend * env, which falls away each block.
inline void glide_modes(Voice& v) {
    if (!v.bend) return;
    const i32 m28 = (1 << 28) + multiply(v.bend, v.bend_env);   // 1 + bend env, Q28
    for (u32 k = 0; k < v.nmodes; ++k) {
        Mode& m = v.mode[k];
        const u32 inc = static_cast<u32>(multiply(static_cast<i32>(m.inc >> 1), m28)) << 4;
        m.f = mode_f(v, k, inc);
    }
    v.bend_env = multiply(v.bend_env, v.bend_k);
}
// Runs the modes over a block of force x, adding their sound to out. Each
// mode is a lossless state-variable pair (its exact pitch, f = 2 sin(pi fc /
// SR)), decayed every 8 samples; two modes share a pass. A mode that has
// rung down under about -100 dB with nothing driving it is stopped: fixed
// point would otherwise keep it circling a few steps above zero. A pass runs
// over count samples (the block, or its 32 half-rate samples), and adds to
// out, or with add false writes it.
template <u32 count, bool add>
__attribute__((noinline)) inline void run_pair(Mode& a, Mode& b, const i32* x, i32* out, bool drive) {
    i32 al = a.lp, ab = a.bp, bl = b.lp, bb = b.bp;
    const i32 af = a.f, bf = b.f, ag = a.gw, bg = b.gw, ar = a.r, br = b.r;
    for (unsigned s = 0; s < count; s += 8) {
        i32 touch = 0;
        if (drive) for (unsigned i = s; i < s + 8; ++i) touch |= x[i];
        if (touch) {
            for (unsigned i = s; i < s + 8; ++i) {
                al += multiply(af, ab);
                ab += multiply(x[i], ag) - multiply(af, al);
                bl += multiply(bf, bb);
                bb += multiply(x[i], bg) - multiply(bf, bl);
                out[i] = add ? out[i] + ab + bb : ab + bb;
            }
        } else {
            for (unsigned i = s; i < s + 8; ++i) {
                al += multiply(af, ab);
                ab -= multiply(af, al);
                bl += multiply(bf, bb);
                bb -= multiply(bf, bl);
                out[i] = add ? out[i] + ab + bb : ab + bb;
            }
        }
        al = multiply(al, ar);
        ab = multiply(ab, ar);
        bl = multiply(bl, br);
        bb = multiply(bb, br);
    }
    if (!drive) {
        if (absolute(al) + absolute(ab) < (1 << 11)) al = ab = 0;
        if (absolute(bl) + absolute(bb) < (1 << 11)) bl = bb = 0;
    }
    a.lp = al;
    a.bp = ab;
    b.lp = bl;
    b.bp = bb;
}
// The quarter-rate modes' sound back at the full rate: from each sample to the
// next in four steps.
__attribute__((noinline)) inline i32 modes_up4(const i32* y, i32* out, i32 prev) {
    for (u32 n = 0; n < frames / 4; ++n) {
        const i32 s = *y++, d = s - prev;
        out[0] += prev + (d >> 2);
        out[1] += prev + (d >> 1);
        out[2] += s - (d >> 2);
        out[3] += s;
        prev = s;
        out += 4;
    }
    return prev;
}
// The half-rate modes' sound back at the full rate: each sample, and between
// two the mean of them.
__attribute__((noinline)) inline i32 modes_up(const i32* y, i32* out, i32 prev) {
    for (u32 n = 0; n < half; ++n) {
        const i32 s = *y++;
        out[0] += (prev + s) >> 1;
        out[1] += s;
        prev = s;
        out += 2;
    }
    return prev;
}
inline void run_modes(Voice& v, const i32* x, i32* out) {
    const i32 drive = block_or(x);
    // an odd mode out is paired with a silent one
    Mode spare = {0, 0, 0, one, 0, 0};
    const u32 nq = v.nquarter, nh = v.nhalf;
    if (nq || v.qprev) {
        // the force at a quarter rate: fours summed (a mode takes its area)
        i32 x4[frames / 4], y4[frames / 4];
        if (drive) for (u32 i = 0; i < frames / 4; ++i) x4[i] = x[4 * i] + x[4 * i + 1] + x[4 * i + 2] + x[4 * i + 3];
        bool sounding = false;
        for (u32 k = 0; k < nq; k += 2) {
            Mode& a = v.mode[k];
            Mode& b = k + 1 < nq ? v.mode[k + 1] : spare;
            if (!drive && !a.lp && !a.bp && !b.lp && !b.bp) continue;
            if (sounding) run_pair<frames / 4, true>(a, b, x4, y4, drive != 0);
            else run_pair<frames / 4, false>(a, b, x4, y4, drive != 0);
            sounding = true;
        }
        if (!sounding) for (u32 i = 0; i < frames / 4; ++i) y4[i] = 0;
        v.qprev = modes_up4(y4, out, v.qprev);
    }
    if (nh > nq || v.mprev) {
        // the force at half rate: pairs summed (a mode takes its area)
        i32 x2[half], y2[half];
        if (drive) for (u32 i = 0; i < half; ++i) x2[i] = x[2 * i] + x[2 * i + 1];
        bool sounding = false;
        for (u32 k = nq; k < nh; k += 2) {
            Mode& a = v.mode[k];
            Mode& b = k + 1 < nh ? v.mode[k + 1] : spare;
            if (!drive && !a.lp && !a.bp && !b.lp && !b.bp) continue;
            if (sounding) run_pair<half, true>(a, b, x2, y2, drive != 0);
            else run_pair<half, false>(a, b, x2, y2, drive != 0);
            sounding = true;
        }
        if (!sounding) for (u32 i = 0; i < half; ++i) y2[i] = 0;
        v.mprev = modes_up(y2, out, v.mprev);
    }
    for (u32 k = nh; k < v.nmodes; k += 2) {
        Mode& a = v.mode[k];
        Mode& b = k + 1 < v.nmodes ? v.mode[k + 1] : spare;
        if (!drive && !a.lp && !a.bp && !b.lp && !b.bp) continue;
        run_pair<frames, true>(a, b, x, out, drive != 0);
    }
}

// ------------------------------------------------------------ the high modes
// Four delay lines through a Hadamard mix (adds only), each with a low-pass
// in its loop, after two all-passes that smear what goes in: the dense,
// even forest of modes a slack sheet has above a few kHz. It runs at half
// the sample rate, and each of its samples is held for two: the images that
// leaves above 11 kHz are more of the same forest, its sizzle.
// lbase: log2 Hz of the base line; spread: Q16, how unequal the lines are;
// lt60: log2 s; bright: Q16 (the loop's low-pass, dark to open).
const i32 lhalf = lsr - octave;   // log2 of the half rate
// diffuse: 1 for a field struck by an impulse (its first 6 blocks' input is
// smeared), 2 for one fed noise (no smearing needed); either puts an all-pass
// in a line's loop.
inline void setup_field(Voice& v, i32 lbase, i32 spread, i32 lt60, i32 bright, u32 diffuse = 0) {
    static const i32 ratio[4] = {Q16(0.0), Q16(0.29), Q16(0.6), Q16(0.87)};
    v.floop = diffuse != 0;
    v.fdiff = diffuse == 1 ? 7 : 0;
    v.dpos = 0;
    for (u32 i = 0; i < 64; ++i) v.dbuf[i] = 0;
    v.apos = 0;
    for (u32 i = 0; i < 37; ++i) v.abuf[i] = 0;
    i32 ld0 = lhalf - lbase;                                   // log2 half-rate samples
    const i32 most = L((line_samples - 2) / 1.87);
    if (ld0 > most) ld0 = most;
    if (ld0 < L(32.0)) ld0 = L(32.0);   // half a block at least
    const i32 d0 = static_cast<i32>(pow2(ld0 + 8 * octave));   // Q8 samples
    u32 prev = 0, total = 0;
    for (u32 i = 0; i < 4; ++i) {
        u32 d = static_cast<u32>((d0 + multiply(d0, multiply(ratio[i], spread << 15) << 15)) >> 8);
        if (d < 32) d = 32;
        if (i && d <= prev) d = prev + 1;
        d |= 1u;   // odd lengths, so no two share a factor of two
        if (d >= line_samples) d = line_samples - 1;
        prev = d;
        v.fd[i] = d;
        v.flp[i] = 0;
        total += d;
    }
    // the first lap reads only each ring's last d places before it writes them
    for (u32 l = 0; l < 4; ++l) {
        i32* b = v.buf + (l + 1) * line_samples - v.fd[l];
        for (u32 n = 0; n < v.fd[l]; ++n) b[n] = 0;
    }
    v.fw = 0;
    v.fprev = 0;
    v.fquiet = 0;
    // one loop gain for the mean line: 10^(-3 d / (t60 SR)) = exp(-6.9078 d / (t60 SR))
    v.fg = expneg(L(6.907755) + log2q(total) - 2 * octave - lt60 - lhalf);
    // the loop's low-pass: a pole of 0.85 (dark) to 0.12 (open) at the full rate, squared at half
    const i32 a = frac2(2 * mix(L(0.85), L(0.12), bright));
    v.fk = one - a;
    v.fin = static_cast<i32>(pow2(55 * octave - log2q(static_cast<u32>(v.fk))));   // 1 / k, Q24
    v.fon = 1;
}
// A field driven hard enough to fill its rings clips them (ZAP's chirp does).
inline void field_clip(Voice& v) { v.floop |= 2u; }
// A small field: two of the four lines (the second and the fourth, so the
// spread stays), for a sheet whose high modes are a faint shimmer over its
// body (a kick's, a tom's, a bearing's, the thunder's): half the work. Its
// gain per pass is set for the two, over sqrt 2 for their mix, and its input
// is lifted by sqrt 2, as two lines take half the energy four do.
inline void field_small(Voice& v, i32 lt60) {
    v.floop |= 4u;
    v.fd[0] = v.fd[1];
    v.fd[1] = v.fd[3];
    for (u32 l = 0; l < 2; ++l) {
        i32* b = v.buf + (l + 1) * line_samples - v.fd[l];
        for (u32 n = 0; n < v.fd[l]; ++n) b[n] = 0;
        v.flp[l] = 0;
    }
    v.fg = multiply(expneg(L(6.907755) + log2q(v.fd[0] + v.fd[1]) - octave - lt60 - lhalf), Q31(0.70710678));
    v.fin = multiply(v.fin, Q31(0.70710678)) << 1;
}
// The network's passes, each over the block's 32 half-rate samples with its
// state in locals (noinline: inlined into one function they spill). The
// rings are walked by pointer and split where they wrap, so no loop masks an
// index or tests for a wrap.
// The input at half rate: pairs summed, times the input gain (the lines
// hold their signal at half the scale the input arrives at).
__attribute__((noinline)) inline void field_input(const i32* in, i32* x, i32 gin) {
    for (u32 i = 0; i < half; ++i) {
        const i32 p = (in[0] >> 1) + (in[1] >> 1);
        *x++ = multiply(p, gin) << 7;
        in += 2;
    }
}
// One line read from ring position at (0..255) and low-passed into o.
inline i32 field_read(const i32* ring, u32 at, i32 lp, i32 k, i32* o) {
    const u32 first = line_samples - at < half ? line_samples - at : half;
    const i32* p = ring + at;
    for (u32 n = 0; n < first; ++n) {
        lp += multiply(*p++ - lp, k);
        *o++ = lp;
    }
    p = ring;
    const u32 rest = half - first;
    for (u32 n = 0; n < rest; ++n) {
        lp += multiply(*p++ - lp, k);
        *o++ = lp;
    }
    return lp;
}
// The four lines (or two), each read and low-passed (one call for them all).
__attribute__((noinline)) inline void field_reads(Voice& v, i32* r, u32 w, u32 mask, i32 k, u32 lines) {
    for (u32 l = 0; l < lines; ++l) v.flp[l] = field_read(v.buf + l * line_samples, (w - v.fd[l]) & mask, v.flp[l], k, r + l * half);
}
// The all-pass (g 0.5, 37 samples) in the first line's loop, from position p.
inline void field_loop_run(i32* ap, i32* o, u32 count) {
    for (u32 n = 0; n < count; ++n) {
        const i32 y = *ap - (*o >> 1);
        const i32 t = *o + (y >> 1);
        *ap++ = t - (t >> 12);   // a little loss, so it cannot circle for ever
        *o++ = y;
    }
}
__attribute__((noinline)) inline u32 field_loop(i32* ap, u32 p, i32* o) {
    u32 left = half;
    while (left) {
        const u32 run = 37 - p < left ? 37 - p : left;
        field_loop_run(ap + p, o, run);
        o += run;
        left -= run;
        p += run;
        if (p == 37) p = 0;
    }
    return p;
}
// The mix: the four lines' sum is heard (back at the full rate, each
// half-rate sample and the step from the last), and the Hadamard pairs go back
// in, halved, with the loop's gain (g, already halved), plus the input
// (+ + + -). r: the four lines read, 32 apart; w: the first ring's write
// position (the others 256, 512 and 768 on). The rings hold 32-bit values,
// so a write needs no rounding or clamp; a field driven hard enough to fill
// them (ZAP's) clips them where 16-bit rings once did (clip).
const i32 ring_top = 32767 << 13, ring_bottom = -32768 * (1 << 13);
template <bool fed, bool clip>
inline i32 ring_write(i32 h) { return clip ? clamp(h, ring_bottom, ring_top) : h; }
template <bool fed, bool clip>
inline i32 field_mix_run(const i32* r, const i32* x, i32* out, i32* w, u32 count, i32 g, i32 level, i32 prev) {
    for (u32 n = 0; n < count; ++n) {
        const i32 r0 = r[0], r1 = r[half], r2 = r[2 * half], r3 = r[3 * half];
        const i32 a0 = r0 + r1, a1 = r0 - r1, a2 = r2 + r3, a3 = r2 - r3;
        const i32 y = multiply(a0 - a3, level) << 1;   // l0 + l1 - l2 + l3: with the inputs' signs, no resonance at DC
        out[0] += prev + y;
        out[1] += y << 1;
        prev = y;
        const i32 xs = fed ? *x++ : 0;
        w[0] = ring_write<fed, clip>(multiply(a0 + a2, g) + xs);
        w[line_samples] = ring_write<fed, clip>(multiply(a1 + a3, g) + xs);
        w[2 * line_samples] = ring_write<fed, clip>(multiply(a0 - a2, g) + xs);
        w[3 * line_samples] = ring_write<fed, clip>(multiply(a1 - a3, g) - xs);
        ++r;
        out += 2;
        ++w;
    }
    return prev;
}
template <bool fed, bool clip>
__attribute__((noinline)) inline i32 field_mix(const i32* r, const i32* x, i32* out, i32* b, u32 at, i32 g, i32 level, i32 prev) {
    const u32 first = line_samples - at < half ? line_samples - at : half;
    prev = field_mix_run<fed, clip>(r, x, out, b + at, first, g, level, prev);
    return field_mix_run<fed, clip>(r + first, x + first, out + 2 * first, b, half - first, g, level, prev);
}
// The small field's mix: two lines turned through 45 degrees each pass (their
// sum and difference over sqrt 2, which keeps the energy and, unlike a
// Hadamard pair, has no resonance at DC), the sum heard, the input into both.
// g: the loop's gain over sqrt 2.
template <bool fed>
inline i32 field_mix2_run(const i32* r, const i32* x, i32* out, i32* w, u32 count, i32 g, i32 level, i32 prev) {
    for (u32 n = 0; n < count; ++n) {
        const i32 r0 = r[0], r1 = r[half];
        const i32 a = r0 + r1;
        const i32 y = multiply(a, level) << 1;
        out[0] += prev + y;
        out[1] += y << 1;
        prev = y;
        const i32 xs = fed ? *x++ : 0;
        w[0] = multiply(a, g) + xs;
        w[line_samples] = multiply(r1 - r0, g) + xs;
        ++r;
        out += 2;
        ++w;
    }
    return prev;
}
template <bool fed>
__attribute__((noinline)) inline i32 field_mix2(const i32* r, const i32* x, i32* out, i32* b, u32 at, i32 g, i32 level, i32 prev) {
    const u32 first = line_samples - at < half ? line_samples - at : half;
    prev = field_mix2_run<fed>(r, x, out, b + at, first, g, level, prev);
    return field_mix2_run<fed>(r + first, x + first, out + 2 * first, b, half - first, g, level, prev);
}
// Runs the network over a block of input, adding level (Q31, x2) of its sound to
// out. Every line is at least half a block long, so a block's reads only see
// what earlier blocks wrote: each line is read and low-passed in a pass of its
// own, then the four are mixed and written back.
// x: the block's input at half rate, at the lines' scale (field_input's), or
// none (drive false).
inline void field_run(Voice& v, i32* x, bool drive, i32* out, i32 level) {
    if (!drive && !v.dpos) {
        // with nothing driving it, stop once it has rung down for longer than
        // its longest line (fixed point would otherwise keep it circling just
        // above zero)
        i32 most = 0;
        for (u32 l = 0; l < 4; ++l) most |= absolute(v.flp[l]);
        v.fquiet = most < (1 << 12) ? v.fquiet + 1 : 0;
        if (v.fquiet > line_samples / half + 1) { v.fon = 0; v.fprev = 0; return; }
    } else {
        v.fquiet = 0;
    }
    // the input smeared by two Schroeder all-passes (23 and 41 samples, g 0.6)
    bool fed = drive;
    if (v.fdiff > 1) --v.fdiff;
    if ((v.fdiff > 1 && drive) || v.dpos) {
        if (!fed) for (unsigned i = 0; i < half; ++i) x[i] = 0;
        fed = true;
        i32* a = v.dbuf;
        i32* c = v.dbuf + 23;
        u32 pa = v.dpos & 0xffu, pb = (v.dpos >> 8) & 0xffu;
        i32 most = 0;
        for (unsigned i = 0; i < half; ++i) {
            // after the strike's first blocks the input goes straight in, and the all-passes ring out
            const i32 in_ = v.fdiff > 1 ? x[i] : 0;
            const i32 ya = a[pa] - multiply(in_, Q31(0.6));
            a[pa] = in_ + multiply(ya, Q31(0.6));
            const i32 yb = c[pb] - multiply(ya, Q31(0.6));
            c[pb] = ya + multiply(yb, Q31(0.6));
            x[i] = v.fdiff > 1 ? yb : x[i] + yb;
            most |= absolute(yb);
            pa = pa == 22 ? 0 : pa + 1;
            pb = pb == 40 ? 0 : pb + 1;
        }
        v.dpos = (v.fdiff <= 1 || !drive) && most < (1 << 11) ? 0u : (pa | pb << 8 | 1u << 16);
        if (!v.dpos) for (u32 i = 0; i < 64; ++i) v.dbuf[i] = 0;
    }
    // each line, read and low-passed
    i32 r[4 * half];
    const u32 mask = line_samples - 1, w = v.fw;
    const i32 k = v.fk;
    const bool small = (v.floop & 4u) != 0;
    field_reads(v, r, w, mask, k, small ? 2 : 4);
    // the first line passes an all-pass (g 0.5) in its loop, so its echoes
    // never line up: a crisp forest, not a comb
    if (v.floop & 1u) v.apos = field_loop(v.abuf, v.apos, r);
    // mixed, heard and written back
    const i32 g = v.fg >> 1;
    if (small) v.fprev = fed ? field_mix2<true>(r, x, out, v.buf, w & mask, v.fg, level, v.fprev)
                             : field_mix2<false>(r, x, out, v.buf, w & mask, v.fg, level, v.fprev);
    else if (v.floop & 2u) v.fprev = fed ? field_mix<true, true>(r, x, out, v.buf, w & mask, g, level, v.fprev)
                                    : field_mix<false, true>(r, x, out, v.buf, w & mask, g, level, v.fprev);
    else v.fprev = fed ? field_mix<true, false>(r, x, out, v.buf, w & mask, g, level, v.fprev)
                       : field_mix<false, false>(r, x, out, v.buf, w & mask, g, level, v.fprev);
    v.fw = w + half;
}
inline void run_field(Voice& v, const i32* in, i32* out, i32 level) {
    if (!v.fon) return;
    const i32 drive = block_or(in);
    // the input at half rate (pairs summed)
    i32 x[half];
    if (drive) field_input(in, x, v.fin);
    field_run(v, x, drive != 0, out, level);
}
// The same for an input made at half rate: x (at the input's scale, as a
// block's pairs summed would be) is scaled to the lines' in place.
inline void run_field_half(Voice& v, i32* x, bool drive, i32* out, i32 level) {
    if (!v.fon) return;
    if (drive) {
        const i32 gin = v.fin;
        for (u32 i = 0; i < half; ++i) x[i] = multiply(x[i], gin) << 7;
    }
    field_run(v, x, drive, out, level);
}

// ------------------------------------------------------------ the mallet
// Starts a raised-cosine contact of length ltc (log2 s) carrying area amp.
inline void strike(Voice& v, i32 ltc, i32 amp) {
    v.pinc = pow2(32 * octave - ltc - lsr);   // 2^32 / (Tc SR)
    if (v.pinc > 0x7fffffffu) v.pinc = 0x7fffffffu;
    v.pphase = 0;
    // The peak is 2 area / Tc: area * inc / 2^31.
    v.pamp = multiply(amp, static_cast<i32>(v.pinc));
}
// The mallet's force over a block: only while it touches the sheet.
// (Without a branch a sample: live is -1 while it touches, 0 from the sample
// after its phase wraps, which also stops the phase.) Over count samples.
__attribute__((noinline)) inline void pulse_run(Voice& v, i32* x, u32 count) {
    u32 inc = v.pinc, phase = v.pphase;
    if (!inc) {
        for (u32 n = 0; n < count; ++n) *x++ = 0;
        return;
    }
    const i32 amp = v.pamp;
    i32 live = -1;
    for (u32 n = 0; n < count; ++n) {
        const i32 h = (one >> 1) - (sine(phase + 0x40000000u) >> 1);   // (1 - cos) / 2, never past i32
        const u32 next = phase + inc;
        *x++ = multiply(amp, h) & live;
        const i32 still = -static_cast<i32>(next >= phase);
        live &= still;
        inc &= static_cast<u32>(still);
        phase = next;
    }
    v.pinc = inc;
    v.pphase = phase;
}
__attribute__((noinline)) inline void silence(i32* x) {
    for (u32 n = 0; n < frames; ++n) *x++ = 0;
}
inline void excite(Voice& v, i32* x) {
    if (v.pinc) pulse_run(v, x, frames);
    else silence(x);
}
// The sample of the block at which the next strike lands (frames if none in
// it), from sample i on.
inline u32 next_strike(const Voice& v, u32 i) {
    return v.left && v.at >= v.t + i && v.at < v.t + frames ? v.at - v.t : frames;
}
// A hard beater's click: 2.5 ms of noise, high-passed at 2.5 kHz, falling with 0.6 ms.
// lhp moves its high-pass; llp, if given, low-passes it twice too (a felt
// knock), and lfall sets how fast it falls.
inline void click(Voice& v, i32 level, i32 ring = 0, i32 lhp = L(2500.0), i32 llp = 0, i32 lfall = L(0.0006)) {
    if (level <= 0) return;
    v.cenv = level;
    v.ck = decay(mix(lfall, L(0.0025), ring));   // a steel rod rings on
    v.chk = onepole_k(lhp);
    v.clk = llp ? onepole_k(llp) : 0;
    v.cseed = 0x2545f491u;   // every hit's click the same: a beater, not a dice roll
}
// Adds the click to a block, while there is one (its states rest once it has fallen away).
template <bool low>
__attribute__((noinline)) inline void click_run(Voice& v, i32* out) {
    i32 env = v.cenv, h = v.chp.s, l1 = v.clp.s, l2 = v.clp2.s;
    const i32 ck = v.ck, hk = v.chk, lk = v.clk;
    u32 seed = v.cseed ? v.cseed : 0x9e3779b9u;   // (noise's guard, once: xorshift never reaches zero)
    for (u32 n = 0; n < frames; ++n) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        const i32 x = static_cast<i32>(seed) >> 4;
        h += multiply(x - h, hk);
        i32 y = x - h;
        if (low) {
            l1 += multiply(y - l1, lk);
            l2 += multiply(l1 - l2, lk);
            y = l2 << 2;
        }
        env = multiply(env, ck);
        if (env < (1 << 16)) { env = 0; break; }
        *out++ += multiply(y, env) << 1;
    }
    v.cenv = env;
    v.chp.s = h;
    v.clp.s = l1;
    v.clp2.s = l2;
    v.cseed = seed;
}
inline void add_click(Voice& v, i32* out) {
    if (!v.cenv) return;
    if (v.clk) click_run<true>(v, out); else click_run<false>(v, out);
}

// ------------------------------------------------------------ the chain
// sqrt of a Q16 unit, as a Q31 fraction.
inline i32 q31_root(i32 t) {
    if (t <= 0) return 0;
    if (t >= 65535) return one;
    return frac2((log2q(static_cast<u32>(t)) - 16 * octave) / 2);
}
// A loose chain lying on the sheet: wherever the sheet moves harder than the
// links weigh, they lift and slap down. Each slap rings the links' band,
// low-passed twice at its top so nothing fizzes above it. amount: Q31 x 2 (0..2); links: Q16,
// tight to loose (how often a lifted link slaps); lrel: log2 s, how long they
// keep slapping; lo, hi: log2 Hz of the links' band.
inline void setup_chain(Voice& v, i32 most, i32 links, i32 lrel, i32 lo, i32 hi, i32 count) {
    // count: CHAIN, how many links lie on the sheet (Q16): more of them slap
    // more often, louder and for longer.
    v.c_amount = count > 0 ? multiply(most << 13, frac2(mul16(log2q(static_cast<u32>(count)) - 16 * octave, Q16(0.7)))) : 0;   // most (Q16) count^0.7, Q29
    if (!v.c_amount) return;
    v.c_p = multiply(mix(Q31(0.04), Q31(0.7), links), Q31(0.15) + multiply(Q31(0.85), count << 15));
    lrel += log2q(static_cast<u32>(Q16(0.5) + mul16(count, Q16(1.6)))) - 16 * octave;
    v.c_rel = block_decay(lrel);            // how the links keep slapping as the lift falls away
    v.c_thr = Q31(0.08) >> 4;               // the links' weight, of U
    v.c_env = 0;
    // a link's ring: one band across lo..hi, centred between them
    const i32 lc = (lo + hi) >> 1;
    v.c_f1 = svf_f(increment(lc > L(6500.0) ? L(6500.0) : lc));
    // its damping 1 / Q = sqrt(hi / lo) - sqrt(lo / hi), Q30
    const i32 q = exp16(lc - lo) - exp16(lo - lc);
    v.c_q = clamp(q, Q16(0.2), Q16(1.9)) << 14;
    v.c_hpk = onepole_k(lo - octave);
    v.c_lpk = onepole_k(hi);
    v.c_l1 = v.c_l2 = 0;
    v.c_r1.lp = v.c_r1.bp = 0;
    v.c_hp.s = 0;
}
// The chain's passes (noinline, their state in locals: together they spill).
// (Run at half the sample rate, its band, wide and high, loses its resonance:
// the state-variable band is only a resonance for f q under 1. It stays at
// the full rate.)
// The dice: a slap, 0.4..1 of U / 16, either way, when they fall under the
// chance; thrown every other sample, the odd samples taking what passes
// certainty (both: any do).
template <bool both>
__attribute__((noinline)) inline u32 chain_dice(u32 seed, i32 chance2, i32 extra, i32* hit) {
    for (u32 n = 0; n < frames / 2; ++n) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        const i32 a = (U / 40) + static_cast<i32>((seed >> 8) & 0x3ffffu) * 64;
        // (without branches: s0, s1 flip a's sign; m0, m1 keep it or not)
        const i32 s0 = static_cast<i32>(seed & 1u) - 1;
        const i32 m0 = -static_cast<i32>(static_cast<i32>(seed >> 1) < chance2);
        hit[0] = ((a ^ s0) - s0) & m0;
        if (both) {
            const i32 s1 = static_cast<i32>((seed >> 1) & 1u) - 1;
            const i32 m1 = -static_cast<i32>(static_cast<i32>((seed << 7) >> 1) < extra);
            hit[1] = ((a ^ s1) - s1) & m1;
        } else {
            hit[1] = 0;
        }
        hit += 2;
    }
    return seed;
}
// Each slap rings the links' band, high-passed: x in, the band out.
__attribute__((noinline)) inline void chain_ring(i32* x, i32 f, i32 q, i32 hk, i32& lp_, i32& bp_, i32& hs_) {
    i32 lp = lp_, bp = bp_, hs = hs_;
    for (u32 n = 0; n < frames; ++n) {
        lp += multiply(f, bp);
        const i32 hp = *x - lp - (multiply(q, bp) << 1);
        bp += multiply(f, hp);
        hs += multiply(bp - hs, hk);
        *x++ = bp - hs;
    }
    lp_ = lp;
    bp_ = bp;
    hs_ = hs;
}
// and low-passed twice at its top, added to out at the chain's gain.
__attribute__((noinline)) inline void chain_low(const i32* x, i32* out, i32 lk, i32 gain, i32& l1_, i32& l2_) {
    i32 l1 = l1_, l2 = l2_;
    for (u32 n = 0; n < frames; ++n) {
        l1 += multiply(*x++ - l1, lk);
        l2 += multiply(l1 - l2, lk);
        *out++ += multiply(l2, gain) << 7;
    }
    l1_ = l1;
    l2_ = l2;
}
// How far the sheet moves this block (its largest |disp|), for a model that
// knows it without a pass over the block (a ramp's: the larger end).
inline i32 chain_reach(i32 most, i32 d) { d = d < 0 ? -d : d; return d > most ? d : most; }
inline void chain_most(Voice& v, i32 most, i32* out);
// a block's largest |x| (a value of -2^31 counts as none, as it always has)
__attribute__((noinline)) inline i32 block_most(const i32* x) {
    i32 most = 0;
    for (u32 n = 0; n < frames; ++n) {
        const i32 d = *x < 0 ? -*x : *x;
        most = d > most ? d : most;
        ++x;
    }
    return most;
}
inline void run_chain(Voice& v, const i32* disp, i32* out) {
    if (!v.c_amount) return;
    chain_most(v, block_most(disp), out);
}
// most: how far the sheet moves this block (near U at its loudest).
inline void chain_most(Voice& v, i32 most, i32* out) {
    // How far past their weight the sheet lifts the links this block, 0..U,
    // held by a leaky integrator (x4).
    i32 c = most - v.c_thr;
    c = c < 0 ? 0 : c >= U ? U - 1 : c;
    const i32 env0 = v.c_env;
    i32 env1 = (c << 4) + multiply(env0 - (c << 4), v.c_rel);   // Q29
    if (env1 > (3 << 28)) env1 = 3 << 28;
    v.c_env = env1;
    if (!env0 && !env1 && !v.c_r1.bp && !v.c_r1.lp) return;
    // A slap with probability p * env, each 0.4..1 as hard, ringing the band.
    const i32 chance = multiply(v.c_p, (env0 + env1) >> 1) << 2;
    const i32 gain = multiply(v.c_amount, env1) << 2;   // Q29 x Q29: of U / 16 hits
    const i32 f = v.c_f1, q = v.c_q, hk = v.c_hpk, lk = v.c_lpk;
    i32 lp = v.c_r1.lp, bp = v.c_r1.bp, hs = v.c_hp.s, l1 = v.c_l1, l2 = v.c_l2;
    u32 seed = v.seed ? v.seed : 1u;
    // the dice thrown every other sample, at twice the chance; past certainty
    // the odd samples take the rest
    const i32 chance2 = chance > (one >> 1) ? one : chance << 1;
    const i32 extra = chance > (one >> 1) ? (chance - (one >> 1)) << 1 : 0;
    i32 hits[frames];
    seed = extra ? chain_dice<true>(seed, chance2, extra, hits) : chain_dice<false>(seed, chance2, extra, hits);
    chain_ring(hits, f, q, hk, lp, bp, hs);
    chain_low(hits, out, lk, gain, l1, l2);
    if (!env1 && absolute(lp) + absolute(bp) + absolute(l1) + absolute(l2) < (1 << 10)) lp = bp = hs = l1 = l2 = 0;
    v.c_l1 = l1;
    v.c_l2 = l2;
    v.c_r1.lp = lp;
    v.c_r1.bp = bp;
    v.c_hp.s = hs;
    v.seed = seed;
}

// ------------------------------------------------------------ the swaying sheet
// The sheet swinging on its chains: a comb whose delay swings 0.25..1.65 ms.
inline void setup_swing(Voice& v, i32 depth, i32 lrate) {
    v.sw_depth = depth;
    if (!depth) return;
    v.sw_inc = increment(lrate) << 6;   // a block's worth
    v.sw_phase = v.seed;
    v.sw_pos = 0;
    for (u32 i = 0; i < swing_keep; ++i) v.sw_buf[i] = 0;
}
// The comb and the turn in one pass (noinline, its state in locals). The
// delay is 11..72 samples, so the pass keeps the last 76 samples heard (the
// first 76 places of the buffer, in order) and works in a straight line: the
// 76 kept, then the block's own.
typedef u32 __attribute__((may_alias)) word;
// up: the result shifted up.
template <u32 up>
__attribute__((noinline)) inline void swing_run(i16* kept, i32* x, i32 d, i32 step, i32 wet, i32 g, i32 gs) {
    word line_words[(swing_keep + frames) / 2];
    i16* line = reinterpret_cast<i16*>(line_words);
    const word* from = reinterpret_cast<const word*>(kept);
    word* in = line_words;
    for (u32 n = 0; n < swing_keep / 2; ++n) *in++ = *from++;
    i16* at = line + swing_keep;
    for (u32 n = 0; n < frames; ++n) {
        const i32 xi = *x;
        *at = static_cast<i16>(clamp(xi >> 14, -32768, 32767));
        const i16* back = at - (static_cast<u32>(d) >> 8);
        const i32 s0 = back[0], s1 = back[-1];
        const i32 delayed = (s0 << 8) + (s1 - s0) * (d & 0xff);   // Q8 of the stored scale
        // Kept as it is, overflow and all: on a loud hit, delayed * wet can
        // pass 2^31 and wrap (key 62's default does). Apple clang -O2 wraps it
        // too here, so the host renders, the calibration and the OP-XY all
        // agree; computing the unwrapped product would change the calibrated
        // sound (UB audit, 2026-11-06). UBSan reports it; it is known.
        *x++ = multiply(xi + ((delayed * wet) >> 10), g) << up;       // back to signal: << 14 >> 8 >> 16
        ++at;
        d += step;
        g += gs;
    }
    word* to = reinterpret_cast<word*>(kept);
    const word* back = line_words + frames / 2;
    for (u32 n = 0; n < swing_keep / 2; ++n) *to++ = *back++;
}
template <u32 up = 0>
inline void run_swing(Voice& v, i32* x) {
    if (!v.sw_depth) return;
    // The delay (samples, Q8) at the block's start and end: (0.25 + 1.4 depth swing) ms.
    const i32 depth = v.sw_depth;   // Q16
    const i32 a = (1 << 15) + (sine(v.sw_phase) >> 16);   // 0..65535
    v.sw_phase += v.sw_inc;
    const i32 b = (1 << 15) + (sine(v.sw_phase) >> 16);
    i32 d = 2822 + multiply(15805, multiply(depth << 15, a << 15));
    const i32 d1 = 2822 + multiply(15805, multiply(depth << 15, b << 15));
    const i32 step = (d1 - d) >> 6;
    const i32 wet = multiply(Q31(0.9), q31_root(depth)) >> 15;   // Q16
    // and the sheet turning away as it swings: up to 9 dB quieter at the far end of its swing
    const i32 turn = multiply(q31(depth), Q31(0.65));
    const i32 g = one - multiply(turn, a << 15);
    const i32 gs = ((one - multiply(turn, b << 15)) - g) >> 6;
    swing_run<up>(v.sw_buf, x, d, step, wet, g, gs);
}
}
