#pragma once
// CURIO's fixed-point toolkit, shared by the sixteen specimens. Signals are
// Q28 (1.0 is 1 << 28, so a sum has headroom); gains and decays are Q31;
// controls are Q12, 0..4095; pitches are log2 of hertz in Q12 and times are
// microseconds. No floating point, no 64-bit arithmetic and no division in
// any per-sample path: logarithms and powers of two come from tables, a few
// times a note.
#include <opxy/audio.hpp>
#include <opxy/q31.hpp>
#include "tables.h"

namespace curio {
using opxy::i32;
using opxy::u32;

namespace dsp {
const i32 one = 1 << 28;   // a signal's unit
const i32 full = 0x7fffffff;
const u32 frames = opxy::block_frames;

inline i32 mul(i32 a, i32 b) { return opxy::q31::multiply(a, b); }
// Branch hints for the sample loops, so a part that sounds keeps to the
// straight path: an untaken branch costs a cycle, a taken one five and a
// mispredicted one nine.
#define CURIO_LIKELY(x) __builtin_expect(!!(x), 1)
#define CURIO_UNLIKELY(x) __builtin_expect(!!(x), 0)
// x << 1 kept in a data register: left alone, the Blackfin compiler folds a
// doubling and the add after it into the address unit, moving both through
// P registers and stalling on them. The SDK's saturating add of x to itself
// stays in the data unit; it is x << 1 for every |x| < 2^30, which a
// resonator's feedback never leaves.
inline i32 twice(i32 x) { return opxy::q31::add(x, x); }
inline i32 clamp(i32 v, i32 low, i32 high) { return v < low ? low : v > high ? high : v; }
inline i32 lo(i32 a, i32 b) { return a < b ? a : b; }
inline i32 hi(i32 a, i32 b) { return a > b ? a : b; }
// A Q28 signal times a Q28 level (the product within +-8).
inline i32 lev(i32 x, i32 level) { return mul(x, level) << 3; }
// A Q28 signal times a Q16 gain below 128.
inline i32 gn(i32 x, i32 gain_q16) { return mul(x, gain_q16 << 8) << 7; }
// A Q12 control's share of v (v any i32).
inline i32 sc(i32 v, i32 c) { return mul(v, c << 19); }
// a + (b - a) * c for a Q12 control, in whatever units a and b are.
inline i32 lerp(i32 a, i32 b, i32 c) { return a + sc(b - a, c); }

// 2^(x / 4096) as a whole number, x in 0 .. 31 * 4096; below 0 it is 0.
static __attribute__((noinline)) inline u32 ex2(i32 x) {
    if (x < 0) return 0;
    if (x >= 31 * 4096) return 0x7fffffffu;
    const u32 o = static_cast<u32>(x) >> 12, f = static_cast<u32>(x) & 4095u, i = f >> 4, r = f & 15u;
    const u32 a = table::exp2[i], b = table::exp2[i + 1];
    return (a + (((b - a) * r) >> 4)) >> (30 - o);
}
// log2(v) in Q12, for v >= 1 (0 gives -32 octaves).
static __attribute__((noinline)) inline i32 lg2(u32 v) {
    if (!v) return -(32 << 12);
    i32 o = 31;
    while (!(v & 0x80000000u)) { v <<= 1; --o; }
    const u32 i = (v >> 23) & 255u, r = (v >> 15) & 255u;
    const u32 a = table::log2[i], b = table::log2[i + 1];
    return (o << 12) + static_cast<i32>(a + (((b - a) * r) >> 8));
}
// A Q12 value c (0..4096) raised to the power e (Q12): x^e as Q12.
inline i32 pw(i32 c, i32 e) {
    if (c <= 0) return 0;
    const i32 l = lg2(static_cast<u32>(c)) - (12 << 12);   // <= 0
    return static_cast<i32>(ex2((l * (e >> 2) >> 10) + (24 << 12)) >> 12);
}
// 2^(x / 4096) in Q16, for octaves x in -16..14.
inline i32 oct(i32 x) { return static_cast<i32>(ex2(x + (16 << 12))); }

// Pitch: log2 of hertz in Q12. hz(30) is 30 Hz; hz(35, 10) is 3.5 Hz.
inline i32 hz(u32 whole, u32 per = 1) { return lg2(whole) - (per > 1 ? lg2(per) : 0); }
// The phase increment of a pitch, capped below 20 kHz.
inline u32 inc(i32 lp) { return lp >= 58520 ? 1948000000u : ex2(lp + 67877); }
// A per-sample decay, Q31: exp(-1 / (tau * 44100)) for tau in microseconds.
static __attribute__((noinline)) inline i32 decay(u32 tau_us) {
    if (tau_us < 100) tau_us = 100;
    const u32 d = ex2(145421 - lg2(tau_us));
    return d >= 0x7fffffffu ? 0 : static_cast<i32>(0x7fffffffu - d);
}
// The same from a time to fall 60 dB.
inline i32 decay60(u32 t60_us) { return decay(static_cast<u32>(mul(static_cast<i32>(t60_us), 310879450))); }   // / 6.9078
// Samples in a time (microseconds), as a block's work: 44100 per second.
inline u32 samples(u32 us) { return static_cast<u32>(mul(static_cast<i32>(us), 94702624)); }   // * 0.0441
// A gain from decibels in Q8 (below 0): Q31.
inline i32 db(i32 db_q8) { return static_cast<i32>(ex2(31 * 4096 + (db_q8 * 2721 >> 10))); }   // log2(10) / 20

// sin and cos of w = 2 pi inc / 2^32, Q30, accurate at low pitches too
// (Taylor series on half the angle, then doubled), for coefficients.
inline i32 mq(i32 a, i32 b) { return mul(a, b) << 1; }   // Q30 product
inline void sincos(u32 increment, i32& s, i32& c) {
    if (increment > 0x72000000u) increment = 0x72000000u;   // 19.6 kHz: past it the half angle's square overflows
    const i32 u = 1 << 30;
    const i32 h = mul(static_cast<i32>(increment), 1686629713);   // w / 2, Q30
    const i32 h2 = mq(h, h);
    i32 t = u - mq(h2, 14913080);                 // 1 - h2/72
    t = u - mq(mq(h2, 25565282), t);              // 1/42
    t = u - mq(mq(h2, 53687091), t);              // 1/20
    t = u - mq(mq(h2, 178956971), t);             // 1/6
    const i32 sh = mq(h, t);
    t = u - mq(h2, 19173961);                     // 1 - h2/56
    t = u - mq(mq(h2, 35791394), t);              // 1/30
    t = u - mq(mq(h2, 89478485), t);              // 1/12
    t = u - mq(mq(h2, 536870912), t);             // 1/2
    const i32 ch = t;
    s = mq(sh, ch) << 1;                          // sin w = 2 sin h cos h
    c = u - (mq(sh, sh) << 1);                    // cos w = 1 - 2 sin^2 h
}

// White noise from a 32-bit xorshift: Q28, about unit RMS.
inline i32 noise(u32& state) {
    u32 x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return static_cast<i32>(x) >> 2;
}
// A uniform draw 0..4095 (Q12).
inline i32 draw(u32& state) { noise(state); return static_cast<i32>(state >> 20); }
// A draw -4096..4095 (Q12), for jitter.
inline i32 jitter(u32& state) { return noise(state) >> 17; }

// A sine of a phase: Q31.
inline i32 sine(u32 phase) { return static_cast<i32>(table::sine[phase >> 20]) << 16; }

// A two-pole resonator: y = 2 r cos w y1 - r^2 y2 + x. A strike rings it as a
// decaying sine of the struck amplitude; an input passes at unity at its peak.
struct Res { i32 y1, y2, x1, x2; };
struct ResK { i32 c, r2, s, g, b; };   // r cos w, r^2 / 2, sin w (all Q31), input gains Q31
inline void tune(ResK& k, u32 increment, i32 r) {
    i32 s, c;
    sincos(increment, s, c);
    k.c = mul(r, c) << 1;
    k.r2 = mul(r, r) >> 1;
    k.s = s << 1;
    // Unity peak gain for an input: 2 (1 - r) sin w.
    k.g = mul(full - r, s) << 2;
    // A band-pass's (zeros at 0 and half the rate): unity peak gain at 1 - r.
    k.b = full - r;
}
inline i32 ring(Res& z, const ResK& k, i32 x) {
    const i32 y = ((mul(k.c, z.y1) - mul(k.r2, z.y2)) << 1) + x;
    z.y2 = z.y1;
    z.y1 = y;
    return y;
}
// The resonator as a band-pass, y = ... + (1 - r) (x - x2): no response at
// 0 Hz, as the prototype's band noise has none.
inline i32 bp(Res& z, const ResK& k, i32 x) {
    const i32 in = mul(x, k.b);
    const i32 y = ((mul(k.c, z.y1) - mul(k.r2, z.y2)) << 1) + in - z.x2;
    z.x2 = z.x1;
    z.x1 = in;
    z.y2 = z.y1;
    z.y1 = y;
    return y;
}
// Strikes a resonator with amplitude a (Q28): it rings as a r^n sin((n + 1) w).
inline void strike(Res& z, const ResK& k, i32 a) { z.y1 += mul(a, k.s); }
// A free resonator (no input) rung through n samples and added into y: the
// same samples as ring(z, k, 0) each sample, its state held in registers,
// two samples a turn so the last two outputs trade places.
__attribute__((always_inline)) inline void ring_free(Res& z, const ResK& k, i32* y, u32 n) {
    i32 a = z.y1, b = z.y2;   // the last output and the one before
    const i32 c = k.c, r = k.r2;
    if (n & 1) {
        const i32 v = (mul(c, a) - mul(r, b)) << 1;
        b = a;
        a = v;
        *y++ += v;
    }
    for (u32 j = 0; j < (n >> 1); ++j) {
        b = (mul(c, a) - mul(r, b)) << 1;
        y[0] += b;
        a = (mul(c, b) - mul(r, a)) << 1;
        y[1] += a;
        y += 2;
    }
    z.y1 = a;
    z.y2 = b;
}
static __attribute__((noinline)) inline void ring_into(Res& z, const ResK& k, i32* y, u32 n) { ring_free(z, k, y, n); }
// A band-pass (bp) through n samples of x, each output o handed with its
// place in y to emit(y[j], o): two samples a turn so the states trade
// places (a hardware loop), the odd one first.
template <class Emit> __attribute__((always_inline)) inline void bp_run(Res& z, const ResK& k, const i32* x, i32* y, u32 n, Emit emit) {
    i32 y1 = z.y1, y2 = z.y2, x1 = z.x1, x2 = z.x2;
    const i32 c = k.c, r2 = k.r2, b = k.b;
    if (n & 1) {
        const i32 in = mul(*x++, b);
        const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = o;
        emit(*y++, o);
    }
    for (u32 h = 0; h < (n >> 1); ++h) {
        const i32 i0 = mul(x[0], b);
        y2 = twice(mul(c, y1) - mul(r2, y2)) + i0 - x2;
        x2 = i0;
        emit(y[0], y2);
        const i32 i1 = mul(x[1], b);
        y1 = twice(mul(c, y2) - mul(r2, y1)) + i1 - x1;
        x1 = i1;
        emit(y[1], y1);
        x += 2;
        y += 2;
    }
    z.y1 = y1;
    z.y2 = y2;
    z.x1 = x1;
    z.x2 = x2;
}
// A resonator driven by in(y[j]) through n samples, each output handed to
// emit(y[j], o), the same way.
template <class In, class Emit> __attribute__((always_inline)) inline void drive_run(Res& z, const ResK& k, i32* y, u32 n, In in, Emit emit) {
    i32 y1 = z.y1, y2 = z.y2;
    const i32 c = k.c, r2 = k.r2;
    if (n & 1) {
        const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in(*y);
        y2 = y1;
        y1 = o;
        emit(*y++, o);
    }
    for (u32 h = 0; h < (n >> 1); ++h) {
        y2 = twice(mul(c, y1) - mul(r2, y2)) + in(y[0]);
        emit(y[0], y2);
        y1 = twice(mul(c, y2) - mul(r2, y1)) + in(y[1]);
        emit(y[1], y1);
        y += 2;
    }
    z.y1 = y1;
    z.y2 = y2;
}
// A bank of free resonators in one call, each rung in turn.
static __attribute__((noinline)) inline void ring_bank(Res* z, const ResK* k, u32 count, i32* y, u32 n) {
    for (u32 m = 0; m < count; ++m) ring_free(z[m], k[m], y, n);
}

// A one-pole low-pass's coefficient for a pitch: 1 - exp(-w), Q31.
inline i32 pole(i32 lp) {
    const u32 i = inc(lp);
    // w log2(e) in Q12: inc * 2 pi / 2^32 * 1.4427 * 4096.
    const i32 y = mul(static_cast<i32>(i >> 1), 37131);
    const u32 e = ex2(31 * 4096 - y);
    return static_cast<i32>(0x7fffffffu - (e > 0x7fffffffu ? 0x7fffffffu : e));
}

// A soft clip: tanh, for x Q28 at unit drive; its output is Q28.
inline i32 tanh(i32 x) {
    const i32 sg = x >> 31;           // -1 below zero: (v ^ sg) - sg is v or -v without a branch
    const i32 m = (x ^ sg) - sg;
    if (CURIO_UNLIKELY(m >= (4 << 28))) return ((table::tanh[256] << 13) ^ sg) - sg;
    const u32 i = static_cast<u32>(m) >> 22, r = (static_cast<u32>(m) >> 14) & 255u;
    const i32 a = table::tanh[i], b = table::tanh[i + 1];
    // (b - a) r as a fractional multiply, exact here (the product is under
    // 2^18): a cycle where an integer multiply takes three.
    const i32 y = (a << 13) + (mul((b - a) << 16, static_cast<i32>(r) << 15) << 5);
    return (y ^ sg) - sg;
}
}
}

namespace curio {
namespace dsp {
// Compile-time constants from decimals (folded by the compiler: no float at run time).
constexpr i32 F(double x) { return static_cast<i32>(x >= 1.0 ? 2147483647.0 : x * 2147483648.0); }   // Q31
constexpr i32 U(double x) { return static_cast<i32>(x * 268435456.0); }                                // Q28
constexpr i32 C(double x) { return static_cast<i32>(x * 4096.0); }                                     // Q12

// A cheaper soft clip than tanh for the sample loops: x - 4 x^3 / 27 up to
// 1.5, where it reaches 1 flat, and 1 past it (x Q28, output Q28). Two
// multiplies and a clamp, no table and no branch.
inline i32 soft(i32 x) {
    const i32 m = U(1.5);
    x = x > m ? m : x < -m ? -m : x;
    const i32 x2 = mul(x, x) << 3;                     // Q28
    return x - (mul(mul(x2, x), F(4.0 / 27.0)) << 3);
}

// What a specimen reads at a strike: its eight controls (Q12, VARY's moves
// included), the key's pitch offset (TUNE and VARY, Q12 octaves) and a seed.
struct Ctx {
    i32 c[8];
    i32 tune;
    u32 seed;
};

// A decaying level: v *= k each sample.
struct Env {
    i32 v, k;
    i32 step() { v = mul(v, k); return v; }
};
// Unit-RMS gain for white noise through a resonator of quality q (Q12) at a
// pitch: sqrt(44100 q / (pi f)), Q16.
inline i32 band_gain(i32 lp, i32 q) {
    return static_cast<i32>(ex2(((56431 + lg2(static_cast<u32>(q)) - (12 << 12) - lp) >> 1) + (16 << 12)));
}
// A resonator for noise: centre lp, quality q (Q12); peak gain 1.
static __attribute__((noinline)) inline void band(ResK& k, i32 lp, i32 q) {
    // r = exp(-pi f / (q 44100)): a time constant of q / (pi f).
    const u32 tau_us = ex2(lg2(static_cast<u32>(q)) - (12 << 12) - lp + 74875);   // log2(1e6 / pi) = 18.28
    tune(k, inc(lp), decay(tau_us));
}
// The same for a resonator stepped at half the rate (two samples a step).
static __attribute__((noinline)) inline void band_half(ResK& k, i32 lp, i32 q) {
    const u32 tau_us = ex2(lg2(static_cast<u32>(q)) - (12 << 12) - lp + 74875 - (1 << 12));
    tune(k, inc(lp + (1 << 12)), decay(tau_us));
}
}
}
