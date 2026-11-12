#pragma once
// Syahi's fixed-point helpers. Everything here is integer: the OP-XY's
// Blackfin has no floating point and no divide instruction, and the module
// links no 64-bit helpers. Pitches and times are Q16 log2 values (65536 an
// octave), signals Q31. Constants written as real numbers (hz, q16, ...) are
// constexpr and folded by the compiler, never evaluated on the device.
#include <opxy/q31.hpp>
#include "tables.h"

namespace syahi {
using opxy::i16;
using opxy::i32;
using opxy::u32;
using opxy::u8;
using opxy::q31::multiply;

const i32 one = 0x7fffffff;
const i32 half_scale = 0x40000000;   // the SDK's output ceiling
const i32 octave = 65536;
const double sample_rate = 44100.0;

// ---- compile-time constants
constexpr double c_series(double y, double y2, int k) { return k > 61 ? 0.0 : y / k + c_series(y * y2, y2, k + 2); }
constexpr double c_ln_mantissa(double x) { return 2.0 * c_series((x - 1) / (x + 1), ((x - 1) / (x + 1)) * ((x - 1) / (x + 1)), 1); }
constexpr double c_log2(double x) {
    return x >= 2.0 ? 1.0 + c_log2(x / 2.0) : x < 1.0 ? c_log2(x * 2.0) - 1.0 : c_ln_mantissa(x) / 0.6931471805599453;
}
constexpr i32 c_round(double x) { return static_cast<i32>(x < 0 ? x - 0.5 : x + 0.5); }
// log2 of a real number, Q16.
constexpr i32 lg(double x) { return c_round(c_log2(x) * 65536.0); }
// A real number in Q16, Q15, Q31.
constexpr i32 q16(double x) { return c_round(x * 65536.0); }
constexpr i32 q15(double x) { return c_round(x * 32768.0); }
constexpr i32 q31(double x) { return x >= 1.0 ? 0x7fffffff : c_round(x * 2147483648.0); }
// log2 of the phase increment for 1 Hz: 2^32 / 44100.
constexpr i32 increment_per_hz = lg(4294967296.0 / 44100.0);

static inline i32 clamp(i32 value, i32 low, i32 high) { return value < low ? low : value > high ? high : value; }
static inline i32 positive(i32 value) { return value < 0 ? 0 : value; }
static inline i32 absolute(i32 value) { return value < 0 ? -value : value; }

// A stored control, 0..127, as a Q31 fraction.
static inline i32 fraction(u32 raw) { return raw >= 127 ? one : static_cast<i32>(raw * 16909320u); }
// low + c (high - low), c a Q31 fraction; low and high in any format.
static inline i32 lin(i32 c, i32 low, i32 high) { return low + multiply(c, high - low); }

// 2^(x / 65536) as an integer, for x in [0, 31 octaves); below 0 is 0 (or
// rounding), from 31 octaves saturates.
static inline u32 exp2i(i32 x) {
    if (x < 0) return 0;
    const u32 whole = static_cast<u32>(x) >> 16;
    if (whole >= 31) return 0x7fffffffu;
    const u32 index = (static_cast<u32>(x) >> 8) & 255u;
    const i32 frac = static_cast<i32>((static_cast<u32>(x) & 255u) << 23);
    const i32 m = tables::exp2[index] + multiply(tables::exp2[index + 1] - tables::exp2[index], frac);   // Q30
    return whole >= 30 ? static_cast<u32>(m) << (whole - 30) : static_cast<u32>(m) >> (30 - whole);
}
// 2^(x / 65536) in Q31 for x <= 0 (1 at 0).
static inline i32 exp2q(i32 x) {
    if (x >= 0) return one;
    if (x <= -31 * octave) return 0;
    const u32 v = exp2i(x + 31 * octave);
    return v > 0x7fffffffu ? one : static_cast<i32>(v);
}
// A phase increment for a pitch in log2 Hz (Q16), capped at 30 octaves of phase.
static inline u32 increment(i32 log2_hz) { return exp2i(clamp(log2_hz + increment_per_hz, 0, 31 * octave - 1)); }

// The per-block multiplier for a ring that falls 60 dB in 2^(log2_seconds) seconds.
static inline i32 per_block(i32 log2_seconds) {
    // 64 samples fall 9.966 * 64 / (44100 T) octaves: its log2 is this constant less log2 T.
    constexpr i32 base = lg(9.965784 * 64.0 / 44100.0);
    const i32 drop = static_cast<i32>(exp2i(clamp(base - log2_seconds + 16 * octave, 0, 30 * octave)));   // octaves, Q16
    return exp2q(-drop);
}
// The per-sample multiplier for a one-pole that settles in 2^(log2_seconds) s
// (its time constant), as 1 - e^(-1 / (fs tau)), Q31.
static inline i32 one_pole(i32 log2_seconds) {
    constexpr i32 base = lg(1.0 / 44100.0);
    const i32 x = static_cast<i32>(exp2i(clamp(base - log2_seconds + 16 * octave, 0, 20 * octave)));   // 1 / (fs tau), Q16
    // 1 - e^-x = 1 - 2^(-x / ln 2)
    return one - exp2q(-multiply(x, q31(0.5 / 0.6931471805599453)) * 2);
}

// Sine of a 32-bit phase, Q31, from 2048 steps (no interpolation: spurs
// under -66 dB, as the modes and rings read it a sample at a time).
const u32 sine_shift = 21;
static inline i32 sine(u32 phase) { return static_cast<i32>(tables::sine[phase >> sine_shift]) << 16; }
// The same, interpolated (error under -100 dB): for coefficients set once a
// note, which the table's steps would put out of tune.
static inline i32 sine_fine(u32 phase) {
    const u32 i = phase >> sine_shift;
    const i32 f = static_cast<i32>((phase >> 5) & 0xffffu);   // the step's fraction, Q16
    const i32 a = tables::sine[i];
    return (a << 16) + (tables::sine[i + 1] - a) * f;
}

// White noise, xorshift32, full scale.
static inline i32 noise(u32& seed) {
    u32 x = seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    seed = x;
    return static_cast<i32>(x);
}

// A deterministic per-hit mix of a serial: the variation HUMAN plays and the
// screen draws.
static inline u32 hash(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// A state-variable filter (topology-preserving): low, band and high from one
// input. Its coefficients come from a table of cutoffs an eighth of an octave
// apart, one table per Q class. Feed it at most an eighth of full scale.
enum QClass { q_butter, q_wires, q_tone, q_clap, q_ring, q_drum };
struct Svf {
    i32 ic1, ic2;
    void reset() { ic1 = ic2 = 0; }
};
struct SvfCoefficients {
    i32 a1, a2, a3, k;   // k is half of 1 / Q
    void set(QClass q, i32 log2_hz) {
        constexpr i32 low = lg(16.0);
        const i32 index = clamp((log2_hz - low + 4096) >> 13, 0, tables::fc_steps - 1);
        a1 = tables::svf[q][index][0];
        a2 = tables::svf[q][index][1];
        a3 = tables::svf[q][index][2];
        k = tables::svf_k[q];
    }
};
// One step: v1 is the band output, v2 the low.
static inline void svf_step(Svf& s, const SvfCoefficients& c, i32 x, i32& band, i32& low) {
    const i32 v3 = x - s.ic2;
    const i32 v1 = multiply(c.a1, s.ic1) + multiply(c.a2, v3);
    const i32 v2 = s.ic2 + multiply(c.a2, s.ic1) + multiply(c.a3, v3);
    s.ic1 = 2 * v1 - s.ic1;
    s.ic2 = 2 * v2 - s.ic2;
    band = v1;
    low = v2;
}
// A band-pass with unity gain at its peak: k * band.
static inline i32 svf_bandpass(Svf& s, const SvfCoefficients& c, i32 x) {
    i32 band, low;
    svf_step(s, c, x, band, low);
    return multiply(c.k, band) * 2;
}
static inline i32 svf_lowpass(Svf& s, const SvfCoefficients& c, i32 x) {
    i32 band, low;
    svf_step(s, c, x, band, low);
    return low;
}
// Chamberlin's state-variable filter: three multiplies, for cutoffs well under
// a fifth of the sample rate (the ring resonators, the wash's high-pass). f is
// sin(pi fc / fs), damping half of 1 / Q, both Q31.
struct Chamberlin {
    i32 low, band;
    i32 f, damping;
    void set(QClass q, i32 log2_hz);
    i32 bandpass(i32 x) {
        low += multiply(f, band) * 2;
        const i32 high = x - low - multiply(damping, band) * 2;
        band += multiply(f, high) * 2;
        return multiply(damping, band) * 2;
    }
    i32 highpass(i32 x) {
        low += multiply(f, band) * 2;
        const i32 high = x - low - multiply(damping, band) * 2;
        band += multiply(f, high) * 2;
        return high;
    }
};
static inline i32 svf_highpass(Svf& s, const SvfCoefficients& c, i32 x) {
    i32 band, low;
    svf_step(s, c, x, band, low);
    return x - multiply(c.k, band) * 2 - low;
}
}

namespace syahi {
// log2 of a positive integer, Q16 (a value of 0 counts as 1).
static inline i32 log2u(u32 v) {
    if (!v) return 0;
    // Normalise in five steps: shift the top set bit to bit 31.
    i32 whole = 31;
    if (!(v & 0xffff0000u)) { v <<= 16; whole -= 16; }
    if (!(v & 0xff000000u)) { v <<= 8; whole -= 8; }
    if (!(v & 0xf0000000u)) { v <<= 4; whole -= 4; }
    if (!(v & 0xc0000000u)) { v <<= 2; whole -= 2; }
    if (!(v & 0x80000000u)) { v <<= 1; whole -= 1; }
    // v is now 1.m in Q31: index the mantissa's top eight bits.
    const u32 index = (v >> 23) & 255u;
    const i32 frac = static_cast<i32>((v >> 7) & 0xffffu) << 15;
    const i32 m = tables::log2[index] + multiply(tables::log2[index + 1] - tables::log2[index], frac);
    return whole * octave + m;
}
// log2(1 + x) for x in Q16 (x >= 0), Q16.
static inline i32 log2_1p(i32 x) { return log2u(static_cast<u32>(positive(x)) + 65536u) - 16 * octave; }
// value * 2^shift, clamped first: no branch (MIN and MAX), for the sample
// loops. It saturates 2^shift - 1 short of full scale, where shl reaches it.
static inline i32 shl_clamp(i32 value, u32 shift) {
    const i32 limit = one >> shift;
    const i32 v = value > limit ? limit : value;
    return (v < -limit ? -limit : v) * (static_cast<i32>(1) << shift);
}
// value * 2^shift, saturating, for 0 <= shift < 31.
static inline i32 shl(i32 value, u32 shift) {
    const i32 limit = one >> shift;
    if (value > limit) return one;
    if (value < -limit) return -one;
    return value * (static_cast<i32>(1) << shift);
}
}

namespace syahi {
// The integer square root.
static inline u32 isqrt(u32 x) {
    u32 root = 0;
    for (u32 bit = 1u << 30; bit; bit >>= 2) {
        if (x >= root + bit) { x -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
    }
    return root;
}
// The square root of a Q31 fraction: more travel near 0, for amounts that grow from nothing.
static inline i32 root(i32 c) { return c <= 0 ? 0 : static_cast<i32>(isqrt(static_cast<u32>(c)) * 46341u); }
}

namespace syahi {
// x^n for a Q31 x, by squaring.
static inline i32 power(i32 x, u32 n) {
    i32 r = one;
    while (n) {
        if (n & 1u) r = multiply(r, x);
        x = multiply(x, x);
        n >>= 1;
    }
    return r;
}
// 2^31 / n for a segment of n samples (1..64): a level's step a sample.
static inline i32 per_sample(i32 span, u32 n) {
    static const i32 reciprocal[65] = {
#define R(n) static_cast<i32>(0x7fffffffu / (n))
        0, R(1), R(2), R(3), R(4), R(5), R(6), R(7), R(8), R(9), R(10), R(11), R(12), R(13), R(14), R(15), R(16),
        R(17), R(18), R(19), R(20), R(21), R(22), R(23), R(24), R(25), R(26), R(27), R(28), R(29), R(30), R(31), R(32),
        R(33), R(34), R(35), R(36), R(37), R(38), R(39), R(40), R(41), R(42), R(43), R(44), R(45), R(46), R(47), R(48),
        R(49), R(50), R(51), R(52), R(53), R(54), R(55), R(56), R(57), R(58), R(59), R(60), R(61), R(62), R(63), R(64),
#undef R
    };
    return n == 1 ? span : multiply(span, reciprocal[n]);
}
}
