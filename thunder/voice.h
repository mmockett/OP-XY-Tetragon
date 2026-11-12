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
struct Mode { i32 lp, bp, f, r, gw; u32 inc; };   // r: its decay over 8 samples, Q31 (over 16 at its rate once split_modes has run)
const u32 field_samples = 512;
const u32 wide_stride = 128;    // a wide field's four rings
const u32 small_stride = 256;   // a small field's two rings
const u32 swing_keep = 38;      // the swing's delay is 5.5..36.4 half-rate samples: it keeps the last 38 (and the last wet value)

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
    u32 floop;                // 2: the rings clip (field_clip); 4: two lines (a small field); 8: heard at half rate (field_raw)
    i32 dbuf[23 + 41];
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
    i32 c_amount, c_p, c_env, c_rel, c_thr, c_hpk, c_lpk, c_l1, c_l2;   // (c_l1: the band's low-pass; c_l2: its last half-rate sample)
    OnePole c_hp;
    // the swaying sheet: a comb whose delay swings
    i32 sw_depth, sw_wet, sw_turn;
    u32 sw_phase, sw_inc;
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
    i32 sw_buf[swing_keep + 1];
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
// top: the highest a mode may ring (log2 Hz)
inline void setup_modes(Voice& v, i32 lf0, i32 tension, i32 spot, i32 ltau0, i32 tau_exp, i32 tilt, u32 n, i32 ltc, u32 first = 0, i32 top = L(6800.0)) {
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
        if (lf0 + r > top) break;
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
inline u32 mode_tier(const Voice& v, u32 k, i32 m28, bool quarter) {
    u32 peak = static_cast<u32>(multiply(static_cast<i32>(v.mode[k].inc >> 1), m28)) << 4;
    if (!v.bend) peak += peak >> 4;   // a sheet that doesn't glide may wobble (SHAKE, by 4%)
    return quarter && peak <= quarter_top ? 0 : peak <= half_top ? 1 : 2;
}
// quarter: whether the model may use the quarter-rate tier (CRASH keeps its
// body at half rate, where it filters it).
inline void split_modes(Voice& v, bool quarter_ok = true) {
    const i32 m28 = (1 << 28) + (v.bend > 0 ? v.bend : 0);
    // a quarter-rate tier of one mode would cost more than it saves: then none
    u32 low = 0;
    for (u32 k = 0; k < v.nmodes; ++k) low += mode_tier(v, k, m28, true) == 0;
    const bool quarter = quarter_ok && low >= 2;
    Mode m[8];
    u32 n = 0;
    for (u32 pass = 0; pass < 3; ++pass) {
        for (u32 k = 0; k < v.nmodes; ++k)
            if (mode_tier(v, k, m28, quarter) == pass) m[n++] = v.mode[k];
        if (pass == 0) v.nquarter = n;
        if (pass == 1) v.nhalf = n;
    }
    for (u32 k = 0; k < n; ++k) {
        // its decay over 16 samples at its rate (r is over 8 at the full rate)
        i32 r = multiply(m[k].r, m[k].r);
        if (k < v.nquarter) {
            m[k].f = svf_f(quarter_inc(m[k].inc));
            r = multiply(r, r);
            r = multiply(r, r);
        } else if (k < v.nhalf) {
            m[k].f = svf_f(half_inc(m[k].inc));
            r = multiply(r, r);
        }
        m[k].r = r;
        v.mode[k] = m[k];
    }
}
// A mode's pitch coefficient for a (full-rate) increment, at the rate it runs.
inline i32 mode_f(const Voice& v, u32 k, u32 inc) {
    return svf_f(k < v.nquarter ? quarter_inc(inc) : k < v.nhalf ? half_inc(inc) : inc);
}
// Every mode's pitch times m28 (Q28): a tier at a time, without a branch a mode.
__attribute__((noinline)) inline void retune_modes(Voice& v, i32 m28) {
    u32 k = 0;
    for (; k < v.nquarter; ++k) v.mode[k].f = svf_f(quarter_inc(static_cast<u32>(multiply(static_cast<i32>(v.mode[k].inc >> 1), m28)) << 4));
    for (; k < v.nhalf; ++k) v.mode[k].f = svf_f(half_inc(static_cast<u32>(multiply(static_cast<i32>(v.mode[k].inc >> 1), m28)) << 4));
    for (; k < v.nmodes; ++k) v.mode[k].f = svf_f(static_cast<u32>(multiply(static_cast<i32>(v.mode[k].inc >> 1), m28)) << 4);
}
// CAMBER: every mode's pitch rises by bend * env, which falls away each block.
inline void glide_modes(Voice& v) {
    if (!v.bend || !v.bend_env) return;
    if (v.bend_env < (1 << 21)) v.bend_env = 0;   // settled (under a thousandth of its rise): at its own pitch from now on
    retune_modes(v, (1 << 28) + multiply(v.bend, v.bend_env));   // 1 + bend env, Q28
    v.bend_env = multiply(v.bend_env, v.bend_k);
}
// Runs the modes over a block of force x, adding their sound to out. Each
// mode is a lossless state-variable pair (its exact pitch, f = 2 sin(pi fc /
// SR)), decayed every 16 samples; two modes share a pass. A mode that has
// rung down under about -100 dB with nothing driving it is stopped: fixed
// point would otherwise keep it circling a few steps above zero. A pass runs
// over count samples (the block, or its 32 half-rate or 16 quarter-rate
// samples), and adds to out, or with add false writes it; driven, the block's
// force goes in.
template <u32 count, bool add, bool driven>
__attribute__((noinline)) inline void run_pair(Mode& a, Mode& b, const i32* x, i32* out) {
    i32 al = a.lp, ab = a.bp, bl = b.lp, bb = b.bp;
    const i32 af = a.f, bf = b.f, ar = a.r, br = b.r;
    const i32 ag = driven ? a.gw : 0, bg = driven ? b.gw : 0;
    for (unsigned s = 0; s < count; s += 16) {
        for (unsigned i = 0; i < 16; ++i) {
            al += multiply(af, ab);
            if (driven) ab += multiply(*x, ag) - multiply(af, al);
            else ab -= multiply(af, al);
            bl += multiply(bf, bb);
            if (driven) bb += multiply(*x++, bg) - multiply(bf, bl);
            else bb -= multiply(bf, bl);
            *out = add ? *out + ab + bb : ab + bb;
            ++out;
        }
        al = multiply(al, ar);
        ab = multiply(ab, ar);
        bl = multiply(bl, br);
        bb = multiply(bb, br);
    }
    if (!driven) {
        if (absolute(al) + absolute(ab) < (1 << 11)) al = ab = 0;
        if (absolute(bl) + absolute(bb) < (1 << 11)) bl = bb = 0;
    }
    a.lp = al;
    a.bp = ab;
    b.lp = bl;
    b.bp = bb;
}
template <u32 count>
inline void run_pair_into(Mode& a, Mode& b, const i32* x, i32* out, bool drive, bool sounding) {
    if (drive) {
        if (sounding) run_pair<count, true, true>(a, b, x, out);
        else run_pair<count, false, true>(a, b, x, out);
    } else {
        if (sounding) run_pair<count, true, false>(a, b, x, out);
        else run_pair<count, false, false>(a, b, x, out);
    }
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
    const bool drive = block_or(x) != 0;
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
            run_pair_into<frames / 4>(a, b, x4, y4, drive, sounding);
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
            run_pair_into<half>(a, b, x2, y2, drive, sounding);
            sounding = true;
        }
        if (!sounding) for (u32 i = 0; i < half; ++i) y2[i] = 0;
        v.mprev = modes_up(y2, out, v.mprev);
    }
    for (u32 k = nh; k < v.nmodes; k += 2) {
        Mode& a = v.mode[k];
        Mode& b = k + 1 < v.nmodes ? v.mode[k + 1] : spare;
        if (!drive && !a.lp && !a.bp && !b.lp && !b.bp) continue;
        run_pair_into<frames>(a, b, x, out, drive, true);
    }
}

// For a voice whose modes all run at half rate: their sound at half rate, as
// it is, into y2 (32 values), for the model to filter there and bring up.
inline void run_modes_half(Voice& v, const i32* x, i32* y2) {
    const bool drive = block_or(x) != 0;
    Mode spare = {0, 0, 0, one, 0, 0};
    const u32 nh = v.nhalf;
    i32 x2[half];
    if (drive) for (u32 i = 0; i < half; ++i) x2[i] = x[2 * i] + x[2 * i + 1];
    bool sounding = false;
    for (u32 k = 0; k < nh; k += 2) {
        Mode& a = v.mode[k];
        Mode& b = k + 1 < nh ? v.mode[k + 1] : spare;
        if (!drive && !a.lp && !a.bp && !b.lp && !b.bp) continue;
        run_pair_into<half>(a, b, x2, y2, drive, sounding);
        sounding = true;
    }
    if (!sounding) for (u32 i = 0; i < half; ++i) y2[i] = 0;
}

// ------------------------------------------------------------ the high modes
// Delay lines through an orthogonal mix (adds only), each with a low-pass in
// its loop, after two all-passes that smear what goes in: the dense, even
// forest of modes a slack sheet has above a few kHz. It runs at half the
// sample rate, and each of its samples is held for two: the images that
// leaves above 11 kHz are more of the same forest, its sizzle.
// A wide field (a cymbal's wash, a hat's ring, a bearing's and the thunder's
// shimmer) has four lines of up to 127 places through a Hadamard mix; every
// other sheet's high modes are a small field of two lines of up to 255
// places, turned through 45 degrees each pass: half the work, and half the
// memory a voice touches.
// lbase: log2 Hz of the base line; spread: Q16, how unequal the lines are;
// lt60: log2 s; bright: Q16 (the loop's low-pass, dark to open).
const i32 lhalf = lsr - octave;   // log2 of the half rate
// diffuse: 1 for a field struck by an impulse (its first 6 blocks' input is
// smeared).
inline void setup_field(Voice& v, i32 lbase, i32 spread, i32 lt60, i32 bright, u32 diffuse = 0, bool wide = false) {
    static const i32 ratio[4] = {Q16(0.0), Q16(0.29), Q16(0.6), Q16(0.87)};
    const u32 stride = wide ? wide_stride : small_stride;
    v.floop = wide ? 0u : 4u;
    v.fdiff = diffuse == 1 ? 7 : 0;
    v.dpos = 0;
    for (u32 i = 0; i < 64; ++i) v.dbuf[i] = 0;
    i32 ld0 = lhalf - lbase;                                   // log2 half-rate samples
    const i32 most = wide ? L((wide_stride - 2) / 1.87) : L((small_stride - 2) / 1.87);
    if (ld0 > most) ld0 = most;
    if (ld0 < L(32.0)) ld0 = L(32.0);   // half a block at least
    const i32 d0 = static_cast<i32>(pow2(ld0 + 8 * octave));   // Q8 samples
    u32 prev = 0, total = 0;
    for (u32 i = 0; i < 4; ++i) {
        u32 d = static_cast<u32>((d0 + multiply(d0, multiply(ratio[i], spread << 15) << 15)) >> 8);
        if (d < 32) d = 32;
        if (i && d <= prev) d = prev + 1;
        d |= 1u;   // odd lengths, so no two share a factor of two
        if (d >= stride) d = stride - 1;
        prev = d;
        v.fd[i] = d;
        v.flp[i] = 0;
        total += d;
    }
    if (!wide) {
        // the second and the fourth, so the spread stays
        v.fd[0] = v.fd[1];
        v.fd[1] = v.fd[3];
    }
    // the first lap reads only each ring's last d places before it writes them
    for (u32 l = 0; l < (wide ? 4u : 2u); ++l) {
        i32* b = v.buf + (l + 1) * stride - v.fd[l];
        for (u32 n = 0; n < v.fd[l]; ++n) b[n] = 0;
    }
    v.fw = 0;
    v.fprev = 0;
    v.fquiet = 0;
    // one loop gain for the mean line: 10^(-3 d / (t60 SR)) = exp(-6.9078 d / (t60 SR))
    if (wide) v.fg = expneg(L(6.907755) + log2q(total) - 2 * octave - lt60 - lhalf);
    else v.fg = multiply(expneg(L(6.907755) + log2q(v.fd[0] + v.fd[1]) - octave - lt60 - lhalf), Q31(0.70710678));   // (over sqrt 2 for the turn)
    // the loop's low-pass: a pole of 0.85 (dark) to 0.12 (open) at the full rate, squared at half
    const i32 a = frac2(2 * mix(L(0.85), L(0.12), bright));
    v.fk = one - a;
    v.fin = static_cast<i32>(pow2(55 * octave - log2q(static_cast<u32>(v.fk))));   // 1 / k, Q24
    // two lines take half the energy four do: their input is lifted by sqrt 2
    if (!wide) v.fin = multiply(v.fin, Q31(0.70710678)) << 1;
    v.fon = 1;
}
// A field driven hard enough to fill its rings clips them (ZAP's chirp does).
inline void field_clip(Voice& v) { v.floop |= 2u; }
// A field whose sound is written at half rate, as it is (32 values a block,
// replacing what was there), for its model to filter and bring up itself.
inline void field_raw(Voice& v) { v.floop |= 8u; }
// A half-rate signal added to a block at the full rate: each sample, and
// between two their mean (prev: the last of the block before).
__attribute__((noinline)) inline i32 up_add(const i32* r, i32* out, i32 prev) {
    for (u32 n = 0; n < half; ++n) {
        const i32 x = *r++;
        out[0] += (prev + x) >> 1;
        out[1] += x;
        prev = x;
        out += 2;
    }
    return prev;
}
// A half-rate signal written to a block at the full rate, as up_add adds it.
// It runs from the end back, so r may be out (its first half).
__attribute__((noinline)) inline i32 up_write(const i32* r, i32* out, i32 prev) {
    const i32 last = r[half - 1];
    const i32* p = r + half - 1;
    i32* o = out + frames - 1;
    i32 x = *p;
    for (u32 n = 1; n < half; ++n) {
        const i32 before = *--p;
        o[0] = x;
        o[-1] = (before + x) >> 1;
        o -= 2;
        x = before;
    }
    out[1] = x;
    out[0] = (prev + x) >> 1;
    return last;
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
// One line read from ring position at and low-passed into o.
inline i32 field_read(const i32* ring, u32 at, u32 stride, i32 lp, i32 k, i32* o) {
    const u32 first = stride - at < half ? stride - at : half;
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
// The lines, each read and low-passed (one call for them all).
__attribute__((noinline)) inline void field_reads(Voice& v, i32* r, u32 w, u32 stride, i32 k, u32 lines) {
    for (u32 l = 0; l < lines; ++l)
        v.flp[l] = field_read(v.buf + l * stride, (w - v.fd[l]) & (stride - 1), stride, v.flp[l], k, r + l * half);
}
// The wide mix: the four lines' sum is heard (back at the full rate, each
// half-rate sample and the step from the last), and the Hadamard pairs go back
// in, halved, with the loop's gain (g, already halved), plus the input
// (+ + + -). r: the four lines read, 32 apart; w: the first ring's write
// position (the others a stride on each).
// raw: the sum heard is written at half rate (one value a pass), as it is,
// for a model that filters it there before bringing it back to the full rate.
template <bool fed, bool raw>
inline i32 field_mix_run(const i32* r, const i32* x, i32* out, i32* w, u32 count, i32 g, i32 level, i32 prev) {
    for (u32 n = 0; n < count; ++n) {
        const i32 r0 = r[0], r1 = r[half], r2 = r[2 * half], r3 = r[3 * half];
        const i32 a0 = r0 + r1, a1 = r0 - r1, a2 = r2 + r3, a3 = r2 - r3;
        const i32 y = multiply(a0 - a3, level) << 1;   // l0 + l1 - l2 + l3: with the inputs' signs, no resonance at DC
        if (raw) {
            *out++ = y;
        } else {
            out[0] += prev + y;
            out[1] += y << 1;
            prev = y;
            out += 2;
        }
        const i32 xs = fed ? *x++ : 0;
        w[0] = multiply(a0 + a2, g) + xs;
        w[wide_stride] = multiply(a1 + a3, g) + xs;
        w[2 * wide_stride] = multiply(a0 - a2, g) + xs;
        w[3 * wide_stride] = multiply(a1 - a3, g) - xs;
        ++r;
        ++w;
    }
    return prev;
}
template <bool fed, bool raw>
__attribute__((noinline)) inline i32 field_mix(const i32* r, const i32* x, i32* out, i32* b, u32 at, i32 g, i32 level, i32 prev) {
    const u32 first = wide_stride - at < half ? wide_stride - at : half;
    prev = field_mix_run<fed, raw>(r, x, out, b + at, first, g, level, prev);
    return field_mix_run<fed, raw>(r + first, x + first, out + (raw ? 1 : 2) * first, b, half - first, g, level, prev);
}
// The small field's mix: two lines turned through 45 degrees each pass (their
// sum and difference over sqrt 2, which keeps the energy and, unlike a
// Hadamard pair, has no resonance at DC), the sum heard, the input into both.
// g: the loop's gain over sqrt 2. A field driven hard enough to fill them
// (ZAP's) clips its rings where 16-bit rings once did (clip).
const i32 ring_top = 32767 << 13, ring_bottom = -32768 * (1 << 13);
template <bool clip>
inline i32 ring_write(i32 h) { return clip ? clamp(h, ring_bottom, ring_top) : h; }
template <bool fed, bool clip, bool raw>
inline i32 field_mix2_run(const i32* r, const i32* x, i32* out, i32* w, u32 count, i32 g, i32 level, i32 prev) {
    for (u32 n = 0; n < count; ++n) {
        const i32 r0 = r[0], r1 = r[half];
        const i32 a = r0 + r1;
        const i32 y = multiply(a, level) << 1;
        if (raw) {
            *out++ = y;
        } else {
            out[0] += prev + y;
            out[1] += y << 1;
            prev = y;
            out += 2;
        }
        const i32 xs = fed ? *x++ : 0;
        w[0] = ring_write<clip>(multiply(a, g) + xs);
        w[small_stride] = ring_write<clip>(multiply(r1 - r0, g) + xs);
        ++r;
        ++w;
    }
    return prev;
}
template <bool fed, bool clip, bool raw>
__attribute__((noinline)) inline i32 field_mix2(const i32* r, const i32* x, i32* out, i32* b, u32 at, i32 g, i32 level, i32 prev) {
    const u32 first = small_stride - at < half ? small_stride - at : half;
    prev = field_mix2_run<fed, clip, raw>(r, x, out, b + at, first, g, level, prev);
    return field_mix2_run<fed, clip, raw>(r + first, x + first, out + (raw ? 1 : 2) * first, b, half - first, g, level, prev);
}
// Runs the network over a block of input, adding level (Q31, x2) of its sound to
// out. Every line is at least half a block long, so a block's reads only see
// what earlier blocks wrote: each line is read and low-passed in a pass of its
// own, then the lines are mixed and written back.
// x: the block's input at half rate, at the lines' scale (field_input's), or
// none (drive false).
__attribute__((noinline)) inline void field_run(Voice& v, i32* x, bool drive, i32* out, i32 level) {
    const bool small = (v.floop & 4u) != 0;
    const u32 lines = small ? 2 : 4, stride = small ? small_stride : wide_stride;
    i32 most = 0;
    for (u32 l = 0; l < lines; ++l) most |= absolute(v.flp[l]);
    // A quiet field (its lines' low-passes all under 2^24) loses on each pass
    // what 16-bit rings, rounding every value toward zero, once took from it
    // (half their 2^13 step of a value as large as half the largest): its
    // tail falls away as theirs did.
    i32 fg = v.fg;
    if (most < (1 << 24)) {
        const i32 l = 13 * octave - log2q(static_cast<u32>(most > 1 ? most : 1));
        fg = l >= 0 ? 0 : fg - multiply(fg, frac2(l));
    }
    if (!drive && !v.dpos) {
        // with nothing driving it, stop once it has rung down for longer than
        // its longest line (fixed point would otherwise keep it circling just
        // above zero)
        v.fquiet = most < (1 << 12) ? v.fquiet + 1 : 0;
        if (v.fquiet > stride / half + 1) { v.fon = 0; v.fprev = 0; return; }
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
        v.dpos = (v.fdiff <= 1 || !drive) && most < (1 << 16) ? 0u : (pa | pb << 8 | 1u << 16);
        if (!v.dpos) for (u32 i = 0; i < 64; ++i) v.dbuf[i] = 0;
    }
    // each line, read and low-passed
    i32 r[4 * half];
    const u32 w = v.fw & (stride - 1);
    field_reads(v, r, w, stride, v.fk, lines);
    // mixed, heard and written back
    const bool raw = (v.floop & 8u) != 0;
    if (small) {
        const i32 g = fg;
        if (raw) {
            if (v.floop & 2u) {
                if (fed) field_mix2<true, true, true>(r, x, out, v.buf, w, g, level, 0);
                else field_mix2<false, true, true>(r, x, out, v.buf, w, g, level, 0);
            } else {
                if (fed) field_mix2<true, false, true>(r, x, out, v.buf, w, g, level, 0);
                else field_mix2<false, false, true>(r, x, out, v.buf, w, g, level, 0);
            }
        } else {
            v.fprev = fed ? field_mix2<true, false, false>(r, x, out, v.buf, w, g, level, v.fprev)
                          : field_mix2<false, false, false>(r, x, out, v.buf, w, g, level, v.fprev);
        }
    } else {
        const i32 g = fg >> 1;
        if (raw) {
            if (fed) field_mix<true, true>(r, x, out, v.buf, w, g, level, 0);
            else field_mix<false, true>(r, x, out, v.buf, w, g, level, 0);
        } else {
            v.fprev = fed ? field_mix<true, false>(r, x, out, v.buf, w, g, level, v.fprev)
                          : field_mix<false, false>(r, x, out, v.buf, w, g, level, v.fprev);
        }
    }
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
__attribute__((noinline)) inline void field_scale(i32* x, i32 gin) {
    for (u32 i = 0; i < half; ++i) x[i] = multiply(x[i], gin) << 7;
}
// The same for an input made at half rate: x (at the input's scale, as a
// block's pairs summed would be) is scaled to the lines' in place.
inline void run_field_half(Voice& v, i32* x, bool drive, i32* out, i32 level) {
    if (!v.fon) return;
    if (drive) field_scale(x, v.fin);
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
        if (env < (1 << 20)) { env = 0; break; }   // (66 dB under a full click: done)
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
// links weigh, they lift and slap down. Each slap is a click through the
// links' band, lo..hi: high-passed at its foot and low-passed at its top,
// all at half the sample rate. amount: Q31 x 2 (0..2); links: Q16, tight to
// loose (how often a lifted link slaps); lrel: log2 s, how long they keep
// slapping; lo, hi: log2 Hz of the links' band.
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
    v.c_hpk = onepole_k(lo + octave);       // (the half rate's coefficients)
    v.c_lpk = onepole_k(hi + octave);
    // at the loudness the resonant band (a state-variable band across lo..hi,
    // high-passed an octave under it and low-passed twice at hi) gave the slaps
    const i32 dl = hi - lo;
    const i32 lg = mul16(lo, Q16(1.73527)) - mul16(hi, Q16(1.18484)) + mul16(mul16(dl, dl), Q16(0.191346)) - Q16(5.80208);
    v.c_amount = multiply(v.c_amount, frac2(lg < 0 ? lg : 0));
    v.c_l1 = v.c_l2 = 0;
    v.c_hp.s = 0;
}
// The chain's passes (noinline, their state in locals: together they spill).
// The dice: a slap, 0.4..1 of U / 16, either way, when they fall under the
// chance; thrown every other half-rate sample (the one between gets none), a
// second slap taking what passes certainty (both: any do).
template <bool both>
__attribute__((noinline)) inline u32 chain_dice(u32 seed, i32 chance2, i32 extra, i32* hit) {
    for (u32 n = 0; n < half / 2; ++n) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        const i32 a = (U / 40) + static_cast<i32>((seed >> 8) & 0x3ffffu) * 64;
        // (without branches: s0, s1 flip a's sign; m0, m1 keep it or not)
        const i32 s0 = static_cast<i32>(seed & 1u) - 1;
        const i32 m0 = -static_cast<i32>(static_cast<i32>(seed >> 1) < chance2);
        i32 h = ((a ^ s0) - s0) & m0;
        if (both) {
            const i32 s1 = static_cast<i32>((seed >> 1) & 1u) - 1;
            const i32 m1 = -static_cast<i32>(static_cast<i32>((seed << 7) >> 1) < extra);
            h += ((a ^ s1) - s1) & m1;
        }
        hit[0] = h;
        hit[1] = 0;
        hit += 2;
    }
    return seed;
}
// The slaps through the band, at the chain's gain, added to out at the full
// rate (each half-rate sample, and between two their mean), or (into_half)
// to a half-rate block.
__attribute__((noinline)) inline void chain_band_half(const i32* x, i32* out, i32 hk, i32 lk, i32 gain, i32& hs_, i32& lp_) {
    i32 hs = hs_, lp = lp_;
    for (u32 n = 0; n < half; ++n) {
        const i32 xi = *x++;
        hs += multiply(xi - hs, hk);
        lp += multiply(xi - hs - lp, lk);
        *out++ += multiply(lp, gain) << 7;
    }
    hs_ = hs;
    lp_ = lp;
}
__attribute__((noinline)) inline i32 chain_band(const i32* x, i32* out, i32 hk, i32 lk, i32 gain, i32& hs_, i32& lp_, i32 prev) {
    i32 hs = hs_, lp = lp_;
    for (u32 n = 0; n < half; ++n) {
        const i32 xi = *x++;
        hs += multiply(xi - hs, hk);
        lp += multiply(xi - hs - lp, lk);
        const i32 y = multiply(lp, gain) << 7;
        out[0] += (prev + y) >> 1;
        out[1] += y;
        prev = y;
        out += 2;
    }
    hs_ = hs;
    lp_ = lp;
    return prev;
}
// How far the sheet moves this block (its largest |disp|), for a model that
// knows it without a pass over the block (a ramp's: the larger end).
inline i32 chain_reach(i32 most, i32 d) { d = d < 0 ? -d : d; return d > most ? d : most; }
inline void chain_most(Voice& v, i32 most, i32* out, i32 scale = -1, bool into_half = false);
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
// most: how far the sheet moves this block (near U at its loudest); scale
// (Q31), if given, takes its level that far down and doubles it.
inline void chain_most(Voice& v, i32 most, i32* out, i32 scale, bool into_half) {
    // How far past their weight the sheet lifts the links this block, 0..U,
    // held by a leaky integrator (x4).
    i32 c = most - v.c_thr;
    c = c < 0 ? 0 : c >= U ? U - 1 : c;
    const i32 env0 = v.c_env;
    i32 env1 = (c << 4) + multiply(env0 - (c << 4), v.c_rel);   // Q29
    if (env1 > (3 << 28)) env1 = 3 << 28;
    v.c_env = env1;
    if (!env0 && !env1 && !v.c_hp.s && !v.c_l1 && !v.c_l2) return;
    // A slap with probability p * env a sample, each 0.4..1 as hard, through the band.
    const i32 m = multiply(v.c_p, (env0 + env1) >> 1);   // (Q29)
    i32 gain = multiply(v.c_amount, env1) << 2;   // Q29 x Q29: of U / 16 hits
    if (scale >= 0) gain = multiply(gain, scale) << 1;
    i32 hs = v.c_hp.s, lp = v.c_l1;
    u32 seed = v.seed ? v.seed : 1u;
    // the dice every fourth sample at four times the chance; past certainty a
    // second slap takes the rest
    const i32 c4 = m >= (one >> 4) ? one : m << 4;
    const i32 extra = m < (one >> 4) ? 0 : m >= (one >> 3) ? one : (m - (one >> 4)) << 4;
    i32 hits[half];
    seed = extra ? chain_dice<true>(seed, c4, extra, hits) : chain_dice<false>(seed, c4, extra, hits);
    i32 prev = 0;
    if (into_half) chain_band_half(hits, out, v.c_hpk, v.c_lpk, gain, hs, lp);
    else prev = chain_band(hits, out, v.c_hpk, v.c_lpk, gain, hs, lp, v.c_l2);
    if (!env1 && absolute(hs) + absolute(lp) + absolute(prev) < (1 << 10)) hs = lp = prev = 0;
    v.c_l1 = lp;
    v.c_l2 = prev;
    v.c_hp.s = hs;
    v.seed = seed;
}

// ------------------------------------------------------------ the swaying sheet
// The sheet swinging on its chains: a comb whose delay swings 0.25..1.65 ms.
// CRASH runs it at half the sample rate on its ring (the strike's click
// passes it by); FLICK at the full rate on the whole hat.
inline void setup_swing(Voice& v, i32 depth, i32 lrate) {
    v.sw_depth = depth;
    if (!depth) return;
    v.sw_inc = increment(lrate) << 6;   // a block's worth
    v.sw_phase = v.seed;
    v.sw_wet = multiply(Q31(0.9), q31_root(depth));
    v.sw_turn = multiply(q31(depth), Q31(0.65));
    for (u32 i = 0; i <= swing_keep; ++i) v.sw_buf[i] = 0;
}
// The comb and the turn in one pass over count samples (noinline, its state
// in locals). The delay is under keep - 1 samples, so the pass keeps the
// last keep (in order) and works in a straight line: those kept, then the
// block's own.
template <u32 count, u32 keep>
__attribute__((noinline)) inline void swing_run(i32* kept, i32* x, i32 d, i32 step, i32 wet, i32 g, i32 gs) {
    i32 line[keep + count];
    for (u32 n = 0; n < keep; ++n) line[n] = kept[n];
    i32* at = line + keep;
    for (u32 n = 0; n < count; ++n) {
        const i32 xi = *x;
        *at = xi;
        const i32* back = at - (d >> 8);
        const i32 s0 = back[0];
        const i32 delayed = s0 + multiply(back[-1] - s0, (d & 0xff) << 23);
        *x++ = multiply(xi + multiply(delayed, wet), g);
        ++at;
        d += step;
        g += gs;
    }
    for (u32 n = 0; n < keep; ++n) kept[n] = line[count + n];
}
// The comb at half rate on a full-rate block: the delayed path is made from
// the block's pairs (each pair's mean), and comes back between and on them;
// the dry path and the turn stay at the full rate.
__attribute__((noinline)) inline void swing_wet(i32* kept, i32* x, i32 d, i32 step, i32 wet, i32 g, i32 gs) {
    const u32 keep = swing_keep;
    i32 line[keep + half];
    for (u32 n = 0; n < keep; ++n) line[n] = kept[n];
    i32* at = line + keep;
    i32 prev = kept[keep];   // the last wet value
    for (u32 n = 0; n < half; ++n) {
        const i32 x0 = x[0], x1 = x[1];
        *at = (x0 >> 1) + (x1 >> 1);
        const i32* back = at - (d >> 8);
        const i32 s0 = back[0];
        const i32 y = multiply(s0 + multiply(back[-1] - s0, (d & 0xff) << 23), wet);
        x[0] = multiply(x0 + ((prev + y) >> 1), g);
        g += gs;
        x[1] = multiply(x1 + y, g);
        g += gs;
        prev = y;
        x += 2;
        ++at;
        d += step;
    }
    for (u32 n = 0; n < keep; ++n) kept[n] = line[half + n];
    kept[keep] = prev;
}
// y: the block (at the full rate, its delayed path at half rate) or its 32
// half-rate samples (at_half), swung in place.
template <bool at_half>
inline void run_swing(Voice& v, i32* y) {
    if (!v.sw_depth) return;
    // The delay (samples, Q8) at the block's start and end: (0.25 + 1.4 depth swing) ms.
    const i32 depth = v.sw_depth;   // Q16
    const i32 a = (1 << 15) + (sine(v.sw_phase) >> 16);   // 0..65535
    v.sw_phase += v.sw_inc;
    const i32 b = (1 << 15) + (sine(v.sw_phase) >> 16);
    i32 d = 2822 + multiply(15805, multiply(depth << 15, a << 15));
    i32 d1 = 2822 + multiply(15805, multiply(depth << 15, b << 15));
    // and the sheet turning away as it swings: up to 9 dB quieter at the far end of its swing
    const i32 turn = v.sw_turn;
    const i32 g = one - multiply(turn, a << 15);
    const i32 g1 = one - multiply(turn, b << 15);
    d >>= 1;   // (in half-rate samples)
    d1 >>= 1;
    if (at_half) swing_run<half, swing_keep>(v.sw_buf, y, d, (d1 - d) >> 5, v.sw_wet, g, (g1 - g) >> 5);
    else swing_wet(v.sw_buf, y, d, (d1 - d) >> 5, v.sw_wet, g, (g1 - g) >> 6);
}
}
