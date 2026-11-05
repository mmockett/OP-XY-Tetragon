#pragma once
// The motion inside a hit, shared by the engine and the screen.
//
// A note opens the gate on a grooved board. The ball rolls down, rings the
// frets hung over the groove, lands on the key's body and bounces to rest; a
// handful of balls does the same, each a little behind the one before. Fret i
// of n rings at T (i/n)^(k/2), where the frets sit at x ~ i^k along the board
// and k = 2 is Galileo's spacing, at which the ball rings them at even times.
// After the landing at T, bounce j flies 2 max(dt, 30 ms) e^j and strikes at
// e^j, until it is under 2.5 ms or -32 dB (Zeno's ball, at rest in finite time).
//
// plan() lays a note's run out once, from the key's controls, its pad fields
// and the tempo. The engine walks it a block at a time (Cursor); the screen
// reads the same plan to place the ball and swing the bells.
#include "dsp.h"

namespace galileo {
namespace motion {
const u32 frets_max = 16, balls_max = 24;
const u32 min_flight = 110;        // 2.5 ms
const u32 min_velocity = 819;      // 0.025, Q15
const u32 flight_floor = 1323;     // 30 ms
const u32 roll_reference = 5512;   // 125 ms: rolls this fast or slower strike at full force

// A key's FEEL pad field (0..15): 0 LOCK; 1..7 swing, 8..14 looseness; 15 FREE.
struct Feel {
    u32 locked;   // tilt picks a division of the beat (else it runs in milliseconds)
    i32 swing;    // the share of a division odd frets come late, Q15
    u32 lax;      // up to this many samples late, at random, for each fret and landing
};
const u16 swing_steps[7] = {2621, 4260, 5571, 6881, 8192, 9503, 10813};   // 8 .. 33 %
const u16 lax_steps[7] = {44, 88, 132, 220, 309, 397, 529};                // 1 .. 12 ms
inline Feel feel(i32 value) {
    Feel f = {1, 0, 0};
    if (value >= 15) f.locked = 0;
    else if (value >= 8) f.lax = lax_steps[value - 8];
    else if (value >= 1) f.swing = swing_steps[value - 1];
    return f;
}
// Locked tilt's eight divisions, in sixteenths (Q12): 1/4, 1/8, 1/8T, 1/16,
// 1/16T, 1/32, 1/32T, 1/64.
const u16 division[8] = {16384, 8192, 5461, 4096, 2731, 2048, 1365, 1024};

// The run every ball follows, and the handful.
struct Ball {
    u32 offset;   // samples behind the first ball
    u16 gain;     // Q15
    u16 e;        // its restitution, Q15
};
struct Run {
    u32 n, m;            // frets, balls
    u32 dt;              // the frets' period, samples
    u32 at[frets_max + 1];   // fret i's time (samples), the landing at[n] = T
    u16 v[frets_max + 1];    // its strike, Q15
    u32 flight;          // 2 max(dt, 30 ms): the first bounce's flight is flight * e
    i32 space;           // the frets' spacing exponent k / 2, Q16 (1 at Galileo's)
    Ball ball[balls_max];
};

// The fret period for a key's tilt (0..127): a division of the sixteenth p16
// (samples) when locked, else 300 ms (level) to 18 ms (steep).
inline u32 period(u32 tilt, const Feel& f, u32 p16) {
    if (f.locked) return (p16 * division[tilt >> 4]) >> 12;
    // log2(0.3 s * 44100) - tilt * log2(1 / 0.06)
    return dsp::exp2(897286 - dsp::multiply(266004, static_cast<i32>(tilt << 24)));
}

// A small deterministic random sequence: the handful's scatter is the same
// on every note of a key, so the screen can draw the balls the engine plays.
struct Random {
    u32 s;
    u32 next() { s = s * 1664525u + 1013904223u; return s >> 8; }   // 24 bits
    i32 unit15() { return static_cast<i32>(next() >> 9); }          // 0..32767
};

// Lays out a note's run. tilt, frets, bounce, balls: the key's motion controls
// (0..127); space: the SPACE pad field (-16..15); p16: a sixteenth in samples.
inline void plan(Run& run, u32 tilt, u32 frets, u32 bounce, u32 balls, i32 space, const Feel& f, u32 p16) {
    tilt = tilt > 127 ? 127 : tilt;
    const u32 n = ((frets > 127 ? 127 : frets) * 16 + 64) >> 7;
    const u32 m = table::ball_count[balls > 127 ? 127 : balls];
    const i32 e = table::restitution[bounce > 127 ? 127 : bounce];
    run.n = n;
    run.m = m;
    const u32 dt = period(tilt, f, p16);
    run.dt = dt;
    const u32 T = n * dt;
    run.flight = 2 * (dt > flight_floor ? dt : flight_floor);
    // A steeper plane strikes harder: -6 dB .. full force at the landing, the
    // same number of dB a step: a soft stroke to an accent (and brighter, as the
    // contact shortens), never so soft that a slow division (a key on 1/8 or
    // 1/4) drops out of the kit.
    const i32 force = static_cast<i32>(dsp::exp2(15 * dsp::octave - 65000 + ((65000 * static_cast<i32>(tilt)) >> 7)));
    const i32 fret_force = 18000;   // -5 dB: a roll under its accent
    // Frets fast enough to pile energy into the body strike softer, by the
    // square root of their period, so a fast roll is no louder than a slow one.
    u32 ratio = dsp::udiv(dt << 15, roll_reference);
    if (ratio > 32768) ratio = 32768;
    i32 even = static_cast<i32>(dsp::isqrt(ratio << 15));   // Q15
    if (even < 13107) even = 13107;
    if (even > 32767) even = 32767;
    // k / 2 = 1 + space / 32: frets at x ~ i^k.
    space = space < -16 ? -16 : space > 15 ? 15 : space;
    run.space = 65536 + space * 2048;
    // A player's hands: a roll alternates sticks, the second a little softer
    // and a touch late, and no two strokes are alike, each a little early or
    // late (up to 5% of the period, at most 2 ms) and a little softer or
    // harder. The same on every note of a key, so the screen rings the bells
    // the engine plays.
    Random hands = {0x51ed270bu ^ (n * 0x9e3779b9u) ^ (dt << 7)};
    u32 nudge = (dt * 3) >> 6;                      // 4.7% of the period
    if (nudge > 88) nudge = 88;                     // 2 ms
    for (u32 i = 0; i < n; ++i) {
        // (i / n)^(k/2), Q16
        u32 share = 0;
        if (i) {
            const i32 l = table::log2_whole[i] - table::log2_whole[n];    // <= 0
            const i32 scaled = dsp::multiply(l, run.space << 14) * 2;     // l * k/2
            share = dsp::exp2(scaled + 16 * dsp::octave);
        }
        run.at[i] = ((T >> 4) * share) >> 12;
        const i32 wobble = hands.unit15() - 16384;                       // -0.5 .. 0.5, Q15
        if (i) run.at[i] += static_cast<u32>(static_cast<i32>(nudge >> 1) + ((wobble * static_cast<i32>(nudge)) >> 15)) +
                            ((i & 1) ? nudge >> 1 : 0) - (nudge >> 1);
        const i32 rising = 12452 + ((16384 * static_cast<i32>(share)) >> 16);   // 0.38 .. 0.88
        const i32 hand = (i & 1) ? 28180 : 32767;                        // the second stick: -1.3 dB
        const i32 touch = 29491 + ((hands.unit15() * 3276) >> 15);       // 0.9 .. 1.0
        // The frets don't follow the plane's force: a roll's loudness is its own,
        // the same at 1/16 as at 1/64; only the landing (the accent) does.
        run.v[i] = static_cast<u16>(dsp::m15(dsp::m15(dsp::m15(dsp::m15(fret_force, rising), even), hand), touch));
    }
    run.at[n] = T;
    run.v[n] = static_cast<u16>(force);
    // Swing: odd frets late by a share of the period, never past the next.
    if (f.swing)
        for (u32 i = 1; i < n; i += 2) {
            const u32 late = run.at[i] + ((dt * static_cast<u32>(f.swing)) >> 15);
            run.at[i] = late < run.at[i + 1] ? late : run.at[i + 1];
        }
    // The handful: spacing, weights from grace notes into the accent (or, for a
    // big handful, at random), and each ball's restitution a little different.
    Random r = {0x2545f491u ^ (m * 0x9e3779b9u)};
    if (m == 1) {
        run.ball[0].offset = 0;
        run.ball[0].gain = 32767;
        run.ball[0].e = static_cast<u16>(e);
        return;
    }
    const u32 d = table::ball_spacing[m], jit = table::ball_scatter[m];
    const i32 loose = table::ball_loose[m];
    u32 sum = 0;   // sum of gains squared, Q22
    i32 gains[balls_max];
    for (u32 b = 0; b < m; ++b) {
        run.ball[b].offset = b ? b * d + ((r.next() & 0xffffu) * jit >> 16) : 0;
        // (b / (m - 1))^1.5, Q15
        const u32 x = (b * table::reciprocal[m - 1]) >> 1;                 // Q15
        const i32 x15 = static_cast<i32>(x > 32767 ? 32767 : x);
        const i32 pow15 = dsp::m15(x15, static_cast<i32>(dsp::isqrt(static_cast<u32>(x15) << 15)));
        const i32 accent = 13763 + dsp::m15(19005, pow15);                // 0.42 + 0.58 x^1.5
        const i32 random = 9830 + dsp::m15(22937, r.unit15());             // 0.3 .. 1
        const i32 g = accent + dsp::m15(random - accent, loose);
        gains[b] = g;
        sum += static_cast<u32>((g >> 4) * (g >> 4));
        // restitution * (1 + 0.07 gauss), at most 0.96
        const i32 gauss = r.unit15() + r.unit15() + r.unit15() + r.unit15() - 65536;   // ~ N(0, 0.577), Q15
        i32 eb = e + dsp::m15(e, dsp::m15(gauss, 3970));                   // 0.07 * sqrt(3)
        eb = eb < 0 ? 0 : eb > 31457 ? 31457 : eb;
        run.ball[b].e = static_cast<u16>(eb);
    }
    // Normalise: the handful together strikes like about 1.15 balls.
    const u32 root = dsp::isqrt(sum);                  // Q11
    const u32 norm = (root * 28494u) >> 15;            // / 1.15
    for (u32 b = 0; b < m; ++b) {
        u32 g = static_cast<u32>(gains[b]);
        if (norm > 2048) g = dsp::udiv(g << 11, norm);
        run.ball[b].gain = static_cast<u16>(g > 32767 ? 32767 : g);
    }
}

// The restitution of ball b's bounce j (from 1): its own, a little more or
// less on each hop (up to 3%), as a real ball's spin and the surface vary, so
// a long bounce never rattles like a machine.
inline u32 hop_e(const Ball& ball, u32 b, u32 j) {
    const u32 h = (j * 2654435761u) ^ (b * 0x85ebca6bu) ^ 0x27d4eb2fu;
    const i32 s = static_cast<i32>((h >> 13) & 2047u) - 1024;           // -1 .. 1, Q10
    const i32 e = ball.e + ((static_cast<i32>(ball.e) * s * 31) >> 20);  // e (1 +- 0.03)
    return static_cast<u32>(e < 0 ? 0 : e > 31457 ? 31457 : e);
}

// An impact: when (samples from the note), how hard (Q15) and what (fret,
// landing or bounce) at which step of the run.
enum Kind : u32 { fret = 0, landing = 1, rebound = 2 };
struct Impact {
    u32 at;
    i32 v;
    u32 kind, step;
};

// One ball's place in its run: the engine's per-ball state, one compare a block.
struct Cursor {
    u32 next;      // its next impact's time, samples from the note
    u16 index;     // 0..n the run; n + 1.. its bounces
    u16 v;         // the next bounce's strike, Q15
    u32 flight;    // the next bounce's flight, samples
    u32 done;
};
// Starts a ball; jitter: the random lateness of its first impact (FEEL lax).
inline void begin(Cursor& c, const Run& run, u32 b, u32 jitter) {
    c.index = 0;
    c.next = run.at[0] + run.ball[b].offset + jitter;
    c.v = 0;
    c.flight = 0;
    c.done = 0;
}
// The ball's due impact, and the ball moved on to the next; jitter delays the
// next fret or landing (not a bounce, which follows physics).
inline Impact take(Cursor& c, const Run& run, u32 b, u32 jitter) {
    const Ball& ball = run.ball[b];
    Impact hit;
    hit.at = c.next;
    if (c.index <= run.n) {
        hit.v = dsp::m15(run.v[c.index], ball.gain);
        hit.kind = c.index < run.n ? fret : landing;
        hit.step = c.index;
        if (c.index == run.n) {
            // Leaving the landing: the first bounce.
            const u32 e1 = hop_e(ball, b, 1);
            c.flight = (run.flight * e1) >> 15;
            c.v = static_cast<u16>(dsp::m15(run.v[run.n], static_cast<i32>(e1)));
        }
        ++c.index;
        if (c.index <= run.n) {
            const u32 at = run.at[c.index] + ball.offset + jitter;
            c.next = at > c.next ? at : c.next;
            return hit;
        }
    } else {
        hit.v = dsp::m15(c.v, ball.gain);
        hit.kind = rebound;
        hit.step = run.n;
        ++c.index;
        const u32 ej = hop_e(ball, b, c.index - run.n);
        c.flight = (c.flight * ej) >> 15;
        c.v = static_cast<u16>(dsp::m15(c.v, static_cast<i32>(ej)));
    }
    if (c.flight < min_flight || c.v < min_velocity || c.index > run.n + 60) {
        c.done = 1;
        c.next = 0xffffffffu;
    } else {
        c.next += c.flight;
    }
    return hit;
}

// Where a ball is at time t (samples from the note), for the screen: on the
// board (along: 0 at the gate, 65536 at the lip), or hopping on the body
// (hop: which bounce from 1, phase Q16 of its flight, height Q16 of the first),
// or at rest. Bounces are replayed from the plan; the screen calls this only for
// the few balls it draws.
struct Place {
    u32 state;    // 0 waiting at the gate, 1 rolling, 2 hopping, 3 at rest
    u32 along;    // Q16
    u32 hop, phase, height;
};
inline Place where(const Run& run, u32 b, u32 t) {
    Place p = {0, 0, 0, 0, 0};
    const Ball& ball = run.ball[b];
    if (t < ball.offset) return p;
    const u32 s = t - ball.offset;
    const u32 T = run.at[run.n];
    if (s < T) {
        // x ~ t^2: (s / T)^2, Q16
        const u32 share = dsp::udiv(s << 8, T >> 8 ? T >> 8 : 1);   // Q16
        const u32 sh = share > 65535 ? 65535 : share;
        p.state = 1;
        p.along = (sh * sh) >> 16;
        return p;
    }
    const u32 e1 = hop_e(ball, b, 1);
    u32 at = T, flight = (run.flight * e1) >> 15, v = static_cast<u32>(dsp::m15(run.v[run.n], static_cast<i32>(e1)));
    u32 height = 65536;   // the first hop's height, relative: e^2j
    for (u32 j = 1; j <= 60; ++j) {
        if (flight < min_flight || v < min_velocity) break;
        if (s < at + flight) {
            p.state = 2;
            p.hop = j;
            p.phase = dsp::udiv((s - at) << 8, flight >> 8 ? flight >> 8 : 1);
            if (p.phase > 65535) p.phase = 65535;
            p.height = height;
            return p;
        }
        at += flight;
        const u32 ej = hop_e(ball, b, j + 1);
        height = (height * ((ej * ej) >> 15)) >> 15;
        flight = (flight * ej) >> 15;
        v = static_cast<u32>(dsp::m15(static_cast<i32>(v), static_cast<i32>(ej)));
    }
    p.state = 3;
    return p;
}
// When the last ball comes to rest (samples from the note).
inline u32 rest(const Run& run) {
    u32 latest = 0;
    for (u32 b = 0; b < run.m; ++b) {
        const Ball& ball = run.ball[b];
        const u32 e1 = hop_e(ball, b, 1);
        u32 at = run.at[run.n] + ball.offset;
        u32 flight = (run.flight * e1) >> 15, v = static_cast<u32>(dsp::m15(run.v[run.n], static_cast<i32>(e1)));
        for (u32 j = 1; j <= 60 && flight >= min_flight && v >= min_velocity; ++j) {
            at += flight;
            const u32 ej = hop_e(ball, b, j + 1);
            flight = (flight * ej) >> 15;
            v = static_cast<u32>(dsp::m15(static_cast<i32>(v), static_cast<i32>(ej)));
        }
        if (at > latest) latest = at;
    }
    return latest;
}
}
}
