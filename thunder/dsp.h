#pragma once
// THUNDER's fixed-point building blocks. Everything here is integer: the
// OP-XY's Blackfin has no floating point and no divide instruction, so
// frequencies and times are held as log2 values (Q16, 65536 an octave) and
// turned into coefficients with a table of powers of two. Literal constants
// are written through constexpr helpers, which the compiler folds; no
// floating point reaches the device.
#include <opxy/q31.hpp>
#include <opxy/audio.hpp>
#include "tables.h"
#include "units.h"

namespace thunder {
using opxy::i16;
using opxy::i32;
using opxy::i8;
using opxy::u16;
using opxy::u32;
using opxy::u8;
using opxy::q31::multiply;
const unsigned frames = opxy::block_frames;
const i32 one = 0x7fffffff;
const i32 half_scale = 0x40000000;   // the SDK's ceiling for a voice
const i32 octave = 65536;            // log2 values are Q16

// ------------------------------------------------------------ constants
// A literal in Q31 (|x| < 1) and in Q16.
constexpr i32 Q31(double x) { return x >= 1.0 ? one : ct::round(x * 2147483648.0); }
constexpr i32 Q16(double x) { return ct::round(x * 65536.0); }
const i32 lsr = L(44100.0);   // log2 of the sample rate

// ------------------------------------------------------------ arithmetic
inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 absolute(i32 v) { return v < 0 ? (v == -one - 1 ? one : -v) : v; }
// a + (b - a) * t, t a Q16 unit (0..65536).
inline i32 mix(i32 a, i32 b, i32 t) { return a + multiply(b - a, clamp(t, 0, 65535) << 15); }
// A Q16 unit squared, and to the power 1.5.
inline i32 sq16(i32 t) { return static_cast<i32>((static_cast<u32>(t) * static_cast<u32>(t)) >> 16); }

// 2^(l / 65536) as an unsigned integer, for l in [0, 32 octaves): callers
// add the scale they want (31 octaves for a Q31 fraction, and so on).
inline u32 pow2(i32 l) {
    if (l <= 0) return l == 0 ? 1u : 0u;
    if (l >= 32 * octave) return 0xffffffffu;
    const u32 n = static_cast<u32>(l) >> 16, f = static_cast<u32>(l) & 0xffffu;
    const u32 i = f >> 8, r = f & 0xffu;
    const u32 a = tables::exp2[i], b = tables::exp2[i + 1];
    const u32 m = a + (((b - a) * r) >> 8);   // 2^30 .. 2^31
    if (n >= 30) {
        const u32 s = n - 30;
        return s && m >= 0x80000000u ? 0xffffffffu : m << s;
    }
    return m >> (30 - n);
}
// A Q31 fraction 2^(l / 65536), l <= 0; 0 below 2^-31.
inline i32 frac2(i32 l) {
    const u32 v = pow2(l + 31 * octave);
    return v > 0x7fffffffu ? one : static_cast<i32>(v);
}
// log2 of a positive integer, Q16 (note-on work: two searches of the exp2 table).
inline i32 log2q(u32 x) {
    if (!x) return -32 * octave;
    u32 n = 0, y = x;
    while (y >= 2u) { y >>= 1; ++n; }
    const u32 m = n <= 30 ? x << (30 - n) : x >> (n - 30);   // the mantissa, Q30
    u32 lo = 0, hi = 256;
    while (hi - lo > 1) {
        const u32 mid = (lo + hi) >> 1;
        if (tables::exp2[mid] <= m) lo = mid; else hi = mid;
    }
    const u32 a = tables::exp2[lo], d = tables::exp2[lo + 1] - a;
    u32 rl = 0, rh = 256;
    while (rh - rl > 1) {
        const u32 mid = (rl + rh) >> 1;
        if (a + ((d * mid) >> 8) <= m) rl = mid; else rh = mid;
    }
    return static_cast<i32>(n * 65536u + lo * 256u + rl);
}
// exp(-x), Q31, for x = 2^(lx / 65536).
inline i32 expneg(i32 lx) {
    if (lx < -octave) {   // x < 0.5: the series keeps small x exact
        const i32 x = frac2(lx), x2 = multiply(x, x), x3 = multiply(x2, x);
        return one - x + (x2 >> 1) - multiply(x3, Q31(1.0 / 6.0)) + multiply(multiply(x2, x2), Q31(1.0 / 24.0));
    }
    if (lx > 5 * octave) return 0;
    return frac2(-static_cast<i32>(pow2(lx + L(1.4426950408889634) + 16 * octave)));
}
// A phase increment (2^32 a cycle) for a frequency given as log2 Hz.
inline u32 increment(i32 lhz) { return pow2(lhz - lsr + 32 * octave); }
// A per-sample decay, Q31, for a time constant given as log2 seconds: exp(-1 / (tau SR)).
inline i32 decay(i32 ltau) { return expneg(-ltau - lsr); }
// The same decay over a whole block of 64 samples.
inline i32 block_decay(i32 ltau) { return expneg(6 * octave - ltau - lsr); }

// The Chamberlin state-variable filter's frequency coefficient,
// f = 2 sin(pi fc / SR), Q31, for an increment up to about 7 kHz.
inline i32 svf_f(u32 inc) {
    const u32 top = 690000000u;   // 7.08 kHz: f stays under 1
    if (inc > top) inc = top;
    const i32 x = multiply(static_cast<i32>(inc >> 1), Q31(3.14159265358979 / 4.0)) << 2;   // pi * inc / 2^32, Q31
    const i32 x2 = multiply(x, x), x3 = multiply(x2, x), x5 = multiply(x3, x2);
    const i32 s = x - multiply(x3, Q31(1.0 / 6.0)) + multiply(x5, Q31(1.0 / 120.0));
    return s >= Q31(0.5) ? one : s << 1;
}
// Its damping 1 / Q for a mode that rings with time constant tau: 1 / (pi f tau).
inline i32 svf_q(i32 lhz, i32 ltau) {
    const i32 l = -lhz - ltau - L(3.14159265358979);
    return l >= 0 ? Q31(0.97) : clamp(frac2(l), 1, Q31(0.97));
}

// a b / 65536, for |a| < 2^24 and |b| < 2^23.
inline i32 mul16(i32 a, i32 b) { return multiply(a << 7, b << 8); }
// t^e for a Q16 unit t and a Q16 exponent e.
inline i32 power16(i32 t, i32 e) {
    if (t <= 0) return 0;
    if (t >= 65535) return 65535;
    const i32 lt = log2q(static_cast<u32>(t)) - 16 * octave;   // log2 t, <= 0
    return static_cast<i32>(pow2(mul16(lt, e) + 16 * octave));
}
// log2 of a Q16 value (a time, a ratio).
inline i32 log16(i32 t) { return log2q(static_cast<u32>(t > 1 ? t : 1)) - 16 * octave; }
// A Q16 value from its log2.
inline i32 exp16(i32 l) { return static_cast<i32>(pow2(l + 16 * octave)); }
// A Q16 unit as a Q31 fraction.
inline i32 q31(i32 t) { return clamp(t, 0, 65535) << 15; }
// 1 / Q in Q30 for a Q given as Q16.
inline i32 damping30(i32 qq) { return static_cast<i32>(pow2(30 * octave - log16(qq))); }

// ------------------------------------------------------------ noise
// xorshift: shifts and exclusive-ors only (a 32-bit multiply takes several
// cycles on the Blackfin). The state is never zero.
inline u32 lcg(u32& s) {
    u32 x = s ? s : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s = x;
}
inline i32 noise(u32& s) { return static_cast<i32>(lcg(s)); }

// ------------------------------------------------------------ sine
inline i32 sine(u32 phase) {   // Q31
    const u32 i = phase >> 22, r = (phase >> 6) & 0xffffu;
    const i32 a = tables::sine[i], b = tables::sine[i + 1];
    return (a << 16) + (b - a) * static_cast<i32>(r);
}

// ------------------------------------------------------------ envelopes
// An exponential that moves once a block and is ramped through it.
struct Ramp {
    i32 value, factor;   // Q31: now, and its change over a block
    void set(i32 v, i32 f) { value = v; factor = f; }
    // The block's start and step per sample; advances by a block.
    i32 step(i32& start) {
        start = value;
        value = multiply(value, factor);
        return (value - start) >> 6;
    }
};

// ------------------------------------------------------------ filters
// One-pole low-pass and high-pass; k = 1 - exp(-2 pi fc / SR), Q31.
inline i32 onepole_k(i32 lhz) { return one - expneg(lhz + L(6.283185307179586) - lsr); }
struct OnePole {
    i32 s;
    i32 low(i32 x, i32 k) { s += multiply(x - s, k); return s; }
    i32 high(i32 x, i32 k) { s += multiply(x - s, k); return x - s; }
};
// A Chamberlin state-variable filter: low, band and high outputs.
struct Svf {
    i32 lp, bp;
    i32 band(i32 x, i32 f, i32 q) {
        lp += multiply(f, bp);
        const i32 hp = x - lp - multiply(q, bp);
        bp += multiply(f, hp);
        return bp;
    }
    // Damping q in Q30 (up to 2: Q down to 0.5), and the band normalised to a
    // peak gain of one.
    i32 band30(i32 x, i32 f, i32 q30) {
        lp += multiply(f, bp);
        const i32 hp = x - lp - (multiply(q30, bp) << 1);
        bp += multiply(f, hp);
        return multiply(q30, bp) << 1;
    }
    i32 high(i32 x, i32 f, i32 q) {
        lp += multiply(f, bp);
        const i32 hp = x - lp - multiply(q, bp);
        bp += multiply(f, hp);
        return hp;
    }
};
}
