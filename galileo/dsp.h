#pragma once
// GALILEO's fixed-point arithmetic and the parts every body is built from.
//
// Everything is integer: the OP-XY's Blackfin has no floating point, no divide
// instruction and no 64-bit helpers in the module, so there is no `/` or `%` by
// a variable anywhere here (udiv is a loop, used only when a note starts or the
// tempo changes). Inside a voice a signal's unit amplitude is 2^28, a sixteenth
// of full scale, so sums of parts never overflow.
#include <opxy/q31.hpp>
#include "tables.h"

namespace galileo {
using opxy::i32;
using opxy::u32;
using opxy::u16;
using opxy::u8;
namespace dsp {
using opxy::q31::multiply;
const i32 one = 0x7fffffff;      // Q31 1
const i32 amp = 1 << 28;         // a signal's unit amplitude
const i32 octave = 65536;        // log2 values are Q16
const u32 rate = 44100;
// log2 of 44100, Q16, and of the phase increment for 1 Hz (32 - log2 44100).
const i32 log2_rate = 1011122;
const i32 log2_hz_increment = 1086030;

inline i32 clamp(i32 v, i32 lo, i32 hi) { return v < lo ? lo : v > hi ? hi : v; }
inline i32 positive(i32 v) { return v < 0 ? 0 : v; }
// a + (b - a) p, p Q31 0..1.
inline i32 mix(i32 a, i32 b, i32 p) { return a + multiply(b - a, p); }
// Q15 product.
inline i32 m15(i32 a, i32 b) { return (a * b) >> 15; }

// Unsigned division by a shift and subtract: notes and tempo changes only.
inline u32 udiv(u32 n, u32 d) {
    if (!d) return 0xffffffffu;
    u32 quotient = 0, rest = 0;
    for (int i = 31; i >= 0; --i) {
        rest = (rest << 1) | ((n >> i) & 1u);
        if (rest >= d) { rest -= d; quotient |= 1u << i; }
    }
    return quotient;
}
// floor(sqrt(n)).
inline u32 isqrt(u32 n) {
    u32 root = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return root;
}
// log2(n), Q16, n > 0 (0 for 0).
inline i32 log2(u32 n) {
    if (!n) return 0;
    i32 w = 0;
    u32 x = n;
    for (u32 step = 16; step; step >>= 1)
        if (x >= 1u << step) { x >>= step; w += static_cast<i32>(step); }
    const u32 normal = n << (31 - w);                 // leading bit at 31
    const u32 index = (normal >> 23) & 255u;
    const i32 rest = static_cast<i32>((normal >> 15) & 255u);
    const i32 lo = static_cast<i32>(table::log2[index]), hi = static_cast<i32>(table::log2[index + 1]);
    return w * octave + lo + (((hi - lo) * rest) >> 8);
}
// 2^(x / 65536) as an integer, x in Q16, 0 <= x < 32 octaves (clamped).
inline u32 exp2(i32 x) {
    if (x < 0) return 0;
    if (x >= 32 * octave) return 0xffffffffu;
    const u32 whole = static_cast<u32>(x) >> 16;
    const u32 index = (static_cast<u32>(x) >> 8) & 255u;
    const u32 fraction = static_cast<u32>(x) & 255u;
    const u32 lo = table::exp2[index], hi = table::exp2[index + 1];
    const u32 mantissa = lo + (((hi - lo) >> 8) * fraction);   // Q30, 2^30 .. 2^31
    return whole <= 30 ? mantissa >> (30 - whole) : mantissa << (whole - 30);
}
// 2^(x / 65536) in Q31 for x <= 0 (1 at 0).
inline i32 fraction(i32 x) {
    if (x >= 0) return one;
    const u32 v = exp2(x + 31 * octave);
    return v > 0x7fffffffu ? one : static_cast<i32>(v);
}
// The phase increment for 2^(log2_hz / 65536) Hz, capped below 0.45 of the rate.
inline u32 increment(i32 log2_hz) {
    const u32 v = exp2(log2_hz + log2_hz_increment);
    return v > 0x73333333u ? 0x73333333u : v;
}
// log2 Hz for a frequency given in Hz (a constant).
inline i32 hz(u32 f) { return log2(f); }
// Interpolated sine of a 32-bit phase, Q31.
inline i32 sine(u32 phase) {
    const u32 index = phase >> 23;
    const i32 f = static_cast<i32>((phase & 0x7fffffu) << 8);
    const i32 lo = table::sine[index];
    return lo + multiply(table::sine[index + 1] - lo, f);
}
// The per-sample multiplier for a time constant of 2^(log2_samples / 65536) samples.
inline i32 decay(i32 log2_samples) {
    const i32 p = clamp(log2_samples, 0, 22 * octave - 1) * 16;
    const u32 index = static_cast<u32>(p) >> 16;
    const i32 f = static_cast<i32>((static_cast<u32>(p) & 0xffffu) << 15);
    const i32 lo = table::decay[index];
    return lo + multiply(table::decay[index + 1] - lo, f);
}
// log2 of a time constant in samples, from one in seconds given as log2 seconds (Q16).
inline i32 samples(i32 log2_seconds) { return log2_seconds + log2_rate; }
// The multiplier r^64: a block's worth of a per-sample decay r.
inline i32 per_block(i32 r) {
    for (int i = 0; i < 6; ++i) r = multiply(r, r);
    return r;
}
// tanh of x, x in units of 2^28 (|x| up to 4 read from the table, beyond it 1), Q31.
inline i32 tanh(i32 x) {
    const bool negative = x < 0;
    u32 a = static_cast<u32>(negative ? -x : x);
    i32 y;
    if (a >= (4u << 28)) y = table::tanh[256];
    else {
        const u32 index = a >> 22;                    // 256 steps over 0..4
        const i32 f = static_cast<i32>((a & 0x3fffffu) << 9);
        const i32 lo = table::tanh[index];
        y = lo + multiply(table::tanh[index + 1] - lo, f);
    }
    return negative ? -y : y;
}
// A velocity (Q15) raised to a power (Q16), Q15.
// For an exponent from 1 to 2, v^(1+k) is close to v (1 - k + k v): within 0.05
// of full scale, at two multiplies. Exponents outside 1..2 take the exact way.
inline i32 power(i32 v, i32 exponent) {
    if (v <= 0) return 0;
    if (v >= 32767) return 32767;
    if (exponent >= octave && exponent <= 2 * octave) {
        const i32 k = (exponent - octave) >> 1;               // Q15
        return (v * (32768 - k + ((k * v) >> 15))) >> 15;
    }
    const i32 l = log2(static_cast<u32>(v)) - 15 * octave;   // <= 0
    const i32 scaled = multiply(l, exponent << 14) * 2;        // l * exponent (Q16 * Q16 >> 16)
    return fraction(scaled) >> 16;
}
// The exact power, for tables and captions.
inline i32 power_exact(i32 v, i32 exponent) {
    if (v <= 0) return 0;
    if (v >= 32767) return 32767;
    const i32 l = log2(static_cast<u32>(v)) - 15 * octave;
    return fraction(multiply(l, exponent << 14) * 2) >> 16;
}

// xorshift32 noise, full scale.
inline i32 noise(u32& seed) {
    u32 x = seed;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    seed = x;
    return static_cast<i32>(x);
}
// Noise at unit amplitude (2^28).
inline i32 noise_unit(u32& seed) { return noise(seed) >> 3; }

// A decaying amplitude that strikes add energy to: a = sqrt(a^2 + v^2), held
// under a cap. a is in units of 2^28; r the per-sample decay.
struct Energy {
    i32 a, r;
    // sqrt(a^2 + v^2) as max + 3/8 min (within 7%), without a root.
    void strike(i32 v15, i32 cap = 375809638) {   // cap 1.4
        const i32 v = v15 << 13;
        const i32 hi = a > v ? a : v, lo = a > v ? v : a;
        const i32 next = hi + ((lo >> 3) * 3);
        a = next > cap ? cap : next;
    }
    void hold(i32 v15) { const i32 v = v15 << 13; if (v > a) a = v; }
    i32 step() { a = multiply(a, r); return a; }
};

// One-pole filter; c = 1 - exp(-2 pi f / rate).
struct Pole {
    i32 s;
    i32 low(i32 x, i32 c) { s += multiply(x - s, c); return s; }
    i32 high(i32 x, i32 c) { s += multiply(x - s, c); return x - s; }
};
// The one-pole coefficient for a corner at 2^(log2_hz) Hz.
inline i32 pole(i32 log2_hz) {
    // time constant rate / (2 pi f) samples: log2 = log2 rate - log2(2 pi) - log2 f
    return one - decay(log2_rate - 173768 - log2_hz);
}

// A two-pole band-pass with unit gain at its centre: y = b (x - x2) + a1 y1 - a2 y2.
struct Band {
    i32 x1, x2, y1, y2;
    i32 b, a1, a2;   // b Q31, a1 Q30, a2 Q31
    void reset() { x1 = x2 = y1 = y2 = 0; }
    // Centre 2^log2_hz, quality 2^log2_q.
    void tune(i32 log2_hz, i32 log2_q) {
        const u32 w = increment(log2_hz);
        // r = exp(-pi bandwidth / rate): a time constant of rate / (pi bandwidth) samples.
        const i32 r = decay(log2_rate - 108232 - (log2_hz - log2_q));
        a2 = multiply(r, r);
        a1 = multiply(r, sine(w + 0x40000000u));   // r cos w, Q31 = 2 r cos w in Q30
        b = (one - a2) >> 1;
    }
    i32 run(i32 x) {
        const i32 y = multiply(b, x - x2) + multiply(a1, y1) * 2 - multiply(a2, y2);
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// A struck mode: a two-pole resonator rung by adding to its state. y = 2 r cos w y1 - r^2 y2.
struct Mode {
    i32 y1, y2;
    i32 a1, a2, s;   // r cos w (Q31, doubled in use), r^2, sin w
    u32 increment;
    void tune(u32 inc, i32 r) {
        increment = inc;
        a2 = multiply(r, r);
        a1 = multiply(r, sine(inc + 0x40000000u));
        s = sine(inc);
    }
    // A strike of amplitude v (units 2^28).
    void strike(i32 v) { y1 += multiply(v, s); }
    i32 run() {
        const i32 y = multiply(a1, y1) * 2 - multiply(a2, y2);
        y2 = y1; y1 = y;
        return y;
    }
};

// How much of a mode a strike excites: a contact of `width` samples (Q8)
// reaches a mode of increment inc as 1 / (1 + (1.1 f w)^2), Q15. Uses a
// division: prepare it when a body's controls change, never per sample.
inline i32 contact(u32 inc, u32 width_q8) {
    // f w = inc * width / 2^32 (width in samples); in Q16 that is inc * width_q8 >> 24.
    u32 u = static_cast<u32>(multiply(static_cast<i32>(inc >> 1), static_cast<i32>(width_q8 << 7))) << 1;
    u = (u >> 4) * 1126u >> 6;                        // * 1.1 (1126 / 1024)
    if (u > 0xffff0u) u = 0xffff0u;
    const u32 u2 = ((u >> 4) * (u >> 4)) >> 8;        // Q16
    const u32 k = udiv(1u << 31, (1u << 16) + u2);
    return k > 32767u ? 32767 : static_cast<i32>(k);
}

// A burst of noise per strike, decaying over `r`, then high-passed: the contact's click.
struct Click {
    i32 e, r, c;
    Pole hp;
    void strike(i32 v15) { e += v15 << 13; if (e > (3 << 28)) e = 3 << 28; }
    i32 run(u32& seed) {
        if (!e) return hp.high(0, c);
        e = multiply(e, r);
        return hp.high(multiply(noise(seed), e) * 2, c);
    }
};
}
}
