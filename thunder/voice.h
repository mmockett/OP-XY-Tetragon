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

// A low mode of the sheet: a Chamberlin resonator scaled so that it sounds at
// its weight: the force enters its band state times its weight (gw, Q31), and
// its band state is its sound.
struct Mode { i32 lp, bp, f, r, gw; u32 inc; };   // r: its decay over 8 samples, Q31
const u32 field_samples = 2048;
const u32 line_samples = 512;   // each of the four lines' ring
const u32 swing_samples = 128;

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
    u32 floop;                // an all-pass in the first line's loop
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
    u32 sw_phase, sw_inc, sw_pos;
    // each model's own coefficients and states
    i32 k[24];
    i32 s[13];                // (13 keeps the voice its declared size)
    u32 u[16];
    Svf sv[4];
    OnePole op[6];
    Ramp env[4];
    i16 sw_buf[swing_samples];
    i16 buf[field_samples];
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
// CAMBER: every mode's pitch rises by bend * env, which falls away each block.
inline void glide_modes(Voice& v) {
    if (!v.bend) return;
    const i32 m28 = (1 << 28) + multiply(v.bend, v.bend_env);   // 1 + bend env, Q28
    for (u32 k = 0; k < v.nmodes; ++k) {
        Mode& m = v.mode[k];
        const u32 inc = static_cast<u32>(multiply(static_cast<i32>(m.inc >> 1), m28)) << 4;
        m.f = svf_f(inc);
    }
    v.bend_env = multiply(v.bend_env, v.bend_k);
}
// Runs the modes over a block of force x, adding their sound to out. Each
// mode is a lossless state-variable pair (its exact pitch, f = 2 sin(pi fc /
// SR)), decayed every 8 samples; two modes share a pass. A mode that has
// rung down under about -100 dB with nothing driving it is stopped: fixed
// point would otherwise keep it circling a few steps above zero.
inline void run_pair(Mode& a, Mode& b, const i32* x, i32* out, bool drive) {
    i32 al = a.lp, ab = a.bp, bl = b.lp, bb = b.bp;
    const i32 af = a.f, bf = b.f, ag = a.gw, bg = b.gw;
    for (unsigned s = 0; s < frames; s += 8) {
        i32 touch = 0;
        if (drive) for (unsigned i = s; i < s + 8; ++i) touch |= x[i];
        if (touch) {
            for (unsigned i = s; i < s + 8; ++i) {
                al += multiply(af, ab);
                ab += multiply(x[i], ag) - multiply(af, al);
                bl += multiply(bf, bb);
                bb += multiply(x[i], bg) - multiply(bf, bl);
                out[i] += ab + bb;
            }
        } else {
            for (unsigned i = s; i < s + 8; ++i) {
                al += multiply(af, ab);
                ab -= multiply(af, al);
                bl += multiply(bf, bb);
                bb -= multiply(bf, bl);
                out[i] += ab + bb;
            }
        }
        al = multiply(al, a.r);
        ab = multiply(ab, a.r);
        bl = multiply(bl, b.r);
        bb = multiply(bb, b.r);
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
inline void run_modes(Voice& v, const i32* x, i32* out) {
    i32 drive = 0;
    for (unsigned i = 0; i < frames; ++i) drive |= x[i];
    // an odd mode out is paired with a silent one
    Mode spare = {0, 0, 0, one, 0, 0};
    for (u32 k = 0; k < v.nmodes; k += 2) {
        Mode& a = v.mode[k];
        Mode& b = k + 1 < v.nmodes ? v.mode[k + 1] : spare;
        if (!drive && !a.lp && !a.bp && !b.lp && !b.bp) continue;
        run_pair(a, b, x, out, drive != 0);
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
const unsigned half = frames / 2;
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
    for (u32 i = 0; i < field_samples; ++i) v.buf[i] = 0;
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
// One line's writes for samples from..to: (pa + sign pb) g / 2, rounded
// toward zero, plus xsign x.
template <int sign, int xsign>
inline void write_line(i16* o, const i32* pa, const i32* pb, const i32* x, i32 g, unsigned from, unsigned to) {
    for (unsigned i = from; i < to; ++i) {
        i32 h = multiply(sign > 0 ? pa[i] + pb[i] : pa[i] - pb[i], g);
        h = ((h + ((h >> 31) & 0x7fff)) >> 15) + (xsign > 0 ? x[i] : -x[i]);
        o[i] = static_cast<i16>(clamp(h, -32768, 32767));
    }
}
// Runs the network over a block of input, adding level (Q31, x2) of its sound to
// out. Every line is at least half a block long, so a block's reads only see
// what earlier blocks wrote: each line is read and low-passed in a pass of its
// own, then the four are mixed and written back.
inline void run_field(Voice& v, const i32* in, i32* out, i32 level) {
    if (!v.fon) return;
    i32 drive = 0;
    for (unsigned i = 0; i < frames; ++i) drive |= in[i];
    if (!drive && !v.dpos) {
        // with nothing driving it, stop once it has rung down for longer than
        // its longest line (fixed point would otherwise keep it circling just
        // above zero)
        i32 most = 0;
        for (u32 l = 0; l < 4; ++l) most |= absolute(v.flp[l]);
        v.fquiet = most < (1 << 13) ? v.fquiet + 1 : 0;
        if (v.fquiet > line_samples / half + 1) { v.fon = 0; v.fprev = 0; return; }
    } else {
        v.fquiet = 0;
    }
    // the input at half rate (pairs summed), smeared by two Schroeder
    // all-passes (23 and 41 samples, g 0.6)
    i32 x[half];
    const i32 gin = v.fin;
    for (unsigned i = 0; i < half; ++i) {
        const i32 p = (in[2 * i] >> 1) + (in[2 * i + 1] >> 1);
        x[i] = p ? multiply(p, gin) << 8 : 0;
    }
    if (v.fdiff > 1) --v.fdiff;
    if ((v.fdiff > 1 && drive) || v.dpos) {
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
        v.dpos = (v.fdiff <= 1 || !drive) && most < (1 << 12) ? 0u : (pa | pb << 8 | 1u << 16);
        if (!v.dpos) for (u32 i = 0; i < 64; ++i) v.dbuf[i] = 0;
    }
    // each line, read and low-passed
    i32 r[4][half];
    const u32 mask = line_samples - 1, w = v.fw;
    const i32 k = v.fk;
    for (u32 l = 0; l < 4; ++l) {
        const i16* ring = v.buf + l * line_samples;
        i32 lp = v.flp[l];
        u32 at = w - v.fd[l];
        i32* o = r[l];
        for (unsigned i = 0; i < half; ++i) {
            lp += multiply((static_cast<i32>(ring[at & mask]) << 14) - lp, k);
            o[i] = lp;
            ++at;
        }
        v.flp[l] = lp;
    }
    // the first line passes an all-pass (g 0.5) in its loop, so its echoes
    // never line up: a crisp forest, not a comb
    if (v.floop) {
        i32* o = r[0];
        i32* ap = v.abuf;
        u32 p = v.apos;
        for (unsigned i = 0; i < half; ++i) {
            const i32 y = ap[p] - (o[i] >> 1);
            const i32 t = o[i] + (y >> 1);
            ap[p] = t - (t >> 12);   // a little loss, so it cannot circle for ever
            o[i] = y;
            if (++p == 37) p = 0;
        }
        v.apos = p;
    }
    // mixed: the four lines' sum is heard (back at the full rate, each
    // half-rate sample and the step from the last); the Hadamard pairs go back in
    i32 prev = v.fprev;
    i32* r0 = r[0];
    i32* r1 = r[1];
    i32* r2 = r[2];
    i32* r3 = r[3];
    for (unsigned i = 0; i < half; ++i) {
        const i32 a0 = r0[i] + r1[i], a1 = r0[i] - r1[i], a2 = r2[i] + r3[i], a3 = r2[i] - r3[i];
        const i32 y = multiply(a0 - a3, level);   // l0 + l1 - l2 + l3: with the inputs' signs, no resonance at DC
        out[2 * i] += prev + y;
        out[2 * i + 1] += y << 1;
        prev = y;
        r0[i] = a0;
        r1[i] = a1;
        r2[i] = a2;
        r3[i] = a3;
        x[i] >>= 14;
    }
    v.fprev = prev;
    // and each line is written: halved, with the loop's gain, rounded toward
    // zero so the loop can only lose energy, plus the input (+ + + -)
    const i32 g = v.fg;
    const u32 at = w & mask, run = line_samples - at < half ? line_samples - at : half;
    i16* b = v.buf;
    write_line<1, 1>(b + at, r0, r2, x, g, 0, run);
    write_line<1, 1>(b + line_samples + at, r1, r3, x, g, 0, run);
    write_line<-1, 1>(b + 2 * line_samples + at, r0, r2, x, g, 0, run);
    write_line<-1, -1>(b + 3 * line_samples + at, r1, r3, x, g, 0, run);
    if (run < half) {
        write_line<1, 1>(b + at - line_samples, r0, r2, x, g, run, half);
        write_line<1, 1>(b + at, r1, r3, x, g, run, half);
        write_line<-1, 1>(b + line_samples + at, r0, r2, x, g, run, half);
        write_line<-1, -1>(b + 2 * line_samples + at, r1, r3, x, g, run, half);
    }
    v.fw = w + half;
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
inline i32 pulse(Voice& v) {
    if (!v.pinc) return 0;
    const i32 h = (one >> 1) - (sine(v.pphase + 0x40000000u) >> 1);   // (1 - cos) / 2, never past i32
    const u32 next = v.pphase + v.pinc;
    if (next < v.pphase) v.pinc = 0;
    v.pphase = next;
    return multiply(v.pamp, h);
}
// The mallet's force over a block: only while it touches the sheet.
inline void excite(Voice& v, i32* x) {
    if (!v.pinc) {
        for (unsigned i = 0; i < frames; ++i) x[i] = 0;
        return;
    }
    for (unsigned i = 0; i < frames; ++i) x[i] = pulse(v);
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
inline i32 click_sample(Voice& v);
// Adds the click to a block, while there is one.
inline void add_click(Voice& v, i32* out) {
    if (!v.cenv) return;
    for (unsigned i = 0; i < frames; ++i) out[i] += click_sample(v);
}
inline i32 click_sample(Voice& v) {
    if (!v.cenv) return 0;
    i32 n = v.chp.high(noise(v.cseed) >> 4, v.chk);
    if (v.clk) n = v.clp2.low(v.clp.low(n, v.clk), v.clk) << 2;
    v.cenv = multiply(v.cenv, v.ck);
    if (v.cenv < (1 << 16)) v.cenv = 0;
    return multiply(n, v.cenv) << 1;
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
// disp: the sheet's motion (near U at its loudest); adds the rattle to out.
inline void run_chain(Voice& v, const i32* disp, i32* out) {
    if (!v.c_amount) return;
    // How far past their weight the sheet lifts the links this block, 0..U,
    // held by a leaky integrator (x4).
    i32 most = 0;
    for (unsigned i = 0; i < frames; ++i) {
        const i32 d = disp[i] < 0 ? -disp[i] : disp[i];
        most = d > most ? d : most;
    }
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
    for (unsigned i = 0; i < frames; i += 2) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        // a slap, 0.4..1 of U / 16, either way, when the dice fall under the chance
        const i32 a = (U / 40) + static_cast<i32>((seed >> 8) & 0x3ffffu) * 64;
        const i32 hit = static_cast<i32>(seed >> 1) < chance2 ? ((seed & 1u) ? a : -a) : 0;
        lp += multiply(f, bp);
        i32 hp = hit - lp - (multiply(q, bp) << 1);
        bp += multiply(f, hp);
        hs += multiply(bp - hs, hk);
        l1 += multiply(bp - hs - l1, lk);
        l2 += multiply(l1 - l2, lk);
        out[i] += multiply(l2, gain) << 7;
        const i32 hit2 = static_cast<i32>((seed << 7) >> 1) < extra ? ((seed & 2u) ? a : -a) : 0;
        lp += multiply(f, bp);
        hp = hit2 - lp - (multiply(q, bp) << 1);
        bp += multiply(f, hp);
        hs += multiply(bp - hs, hk);
        l1 += multiply(bp - hs - l1, lk);
        l2 += multiply(l1 - l2, lk);
        out[i + 1] += multiply(l2, gain) << 7;
    }
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
    for (u32 i = 0; i < swing_samples; ++i) v.sw_buf[i] = 0;
}
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
    const u32 mask = swing_samples - 1;
    u32 pos = v.sw_pos;
    i16* buf = v.sw_buf;
    for (unsigned i = 0; i < frames; ++i) {
        buf[pos] = static_cast<i16>(clamp(x[i] >> 14, -32768, 32767));
        const u32 back = pos - (static_cast<u32>(d) >> 8);
        const i32 s0 = buf[back & mask], s1 = buf[(back - 1) & mask];
        const i32 delayed = (s0 << 8) + (s1 - s0) * (d & 0xff);   // Q8 of the stored scale
        x[i] += (delayed * wet) >> 10;                           // back to signal: << 14 >> 8 >> 16
        pos = (pos + 1) & mask;
        d += step;
    }
    v.sw_pos = pos;
    // and the sheet turning away as it swings: up to 9 dB quieter at the far end of its swing
    const i32 turn = multiply(q31(depth), Q31(0.65));
    i32 g = one - multiply(turn, a << 15);
    const i32 gs = ((one - multiply(turn, b << 15)) - g) >> 6;
    for (unsigned i = 0; i < frames; ++i, g += gs) x[i] = multiply(x[i], g);
}
}
