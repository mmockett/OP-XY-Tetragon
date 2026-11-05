#pragma once
// The bodies the ball lands on: GALILEO's fifteen models.
//
// Each impact (a fret, the landing, a bounce) strikes the voice's one body:
// a short contact, harder and brighter the faster the ball, which adds energy
// to what still rings rather than restarting it. So rolls build up and
// re-strikes never click. The bodies:
//   membranes: one sine whose pitch rises with the strike, and two partials;
//   metal: an inharmonic bank of square waves through a band, or modes;
//   wood, bars and clocks: two or three struck two-pole modes;
//   snare, clap and shaker: a resonant body that noise excites (wires the head
//   shakes, the cavity between two palms, a shell of shot).
// prepare() reads the key's four body controls (Q31) when they change,
// strike() takes an impact, run() writes samples (unit amplitude 2^28).
#include "dsp.h"
#include "kit.h"

namespace galileo {
namespace body {
using dsp::multiply;
using dsp::Energy;
#define GQ28(x) static_cast<i32>((x) * 268435456.0 + 0.5)
#define GQ31(x) static_cast<i32>((x) * 2147483647.0 + 0.5)
#define GQ16(x) static_cast<i32>((x) * 65536.0 + 0.5)
#define GQ15(x) static_cast<i32>((x) * 32768.0 + 0.5)
// log2 of a ratio, Q16 octaves.
#define OCT(x) (dsp::log2(static_cast<u32>(GQ16(x))) - 16 * dsp::octave)

// x * g, g in Q28 (up to 8), held within full scale (three saturating doublings).
inline i32 gain(i32 x, i32 g28) { return dsp::twice(dsp::twice(dsp::twice(multiply(x, g28)))); }
// An increment times a ratio (Q28, below 8).
inline u32 times(u32 inc, i32 ratio28) { return static_cast<u32>(multiply(static_cast<i32>(inc >> 1), ratio28)) << 4; }
// log2 samples of a time constant of ms milliseconds (Q8: 256 is 1 ms).
inline i32 ms(u32 ms_q8) { return dsp::log2(ms_q8 ? ms_q8 : 1) - 8 * dsp::octave + 358007; }
// The per-sample decay for a time constant a + (b - a) p milliseconds.
inline i32 decay_ms(u32 a_q8, u32 b_q8, i32 p) {
    return dsp::decay(ms(static_cast<u32>(dsp::mix(static_cast<i32>(a_q8), static_cast<i32>(b_q8), p))));
}
// log2 Hz: base Hz, up `octaves` (Q16) at p = 1.
inline i32 pitch(u32 base_hz, i32 octaves, i32 p) { return dsp::log2(base_hz) + multiply(octaves, p); }
// A one-pole corner at a + (b - a) p Hz.
inline i32 corner(u32 a, u32 b, i32 p) {
    return dsp::pole(dsp::log2(static_cast<u32>(dsp::mix(static_cast<i32>(a), static_cast<i32>(b), p))));
}
inline i32 q16(u32 hz) { return dsp::log2(hz); }

// A membrane: up to three partials on one phase trajectory whose pitch rises
// with the strike (sweep) and falls back.
struct Skin {
    u32 ph[3];
    i32 ratio[3];      // Q28
    i32 share[3];      // each partial's strike, Q15
    Energy e[3];
    u32 inc, sweep_inc;
    i32 s, sr;         // the sweep envelope, Q31, and its decay
    i32 vp;            // strike exponent, Q16
    u32 parts;
    // v15 the strike's force; tone15 how hard it reads (the force, a little
    // more or less on each stroke).
    void strike(i32 v15, i32 tone15) {
        const i32 v = dsp::power(v15, vp);
        for (u32 k = 0; k < parts; ++k) e[k].strike(dsp::m15(v, share[k]));
        // The pitch rises with the strike: by 0.4 of the depth however soft, all of it at full force.
        const i32 sv = GQ31(0.4) + multiply(GQ31(0.6), tone15 << 16);
        if (sv > s) s = sv;
    }
    // One sample.
    i32 run() {
        s = multiply(s, sr);
        const i32 curve = multiply(s, GQ31(0.6) + multiply(s, GQ31(0.4)));   // ~ s^1.3
        const u32 now = inc + static_cast<u32>(multiply(static_cast<i32>(sweep_inc), curve));
        ph[0] += now;
        i32 y = multiply(dsp::sine(ph[0]), e[0].step());
        for (u32 k = 1; k < parts; ++k) {
            ph[k] += times(now, ratio[k]);
            y += multiply(dsp::sine(ph[k]), e[k].step());
        }
        return y;
    }
    bool quiet() const { return e[0].a < 64 && (parts < 2 || e[1].a < 64) && (parts < 3 || e[2].a < 64); }
};
// Sets a skin: base log2 Hz, the sweep's depth (Q16 ratio of the base), its
// time constant's decay, the partials' ratios (Q28), decays and shares (Q15).
inline void skin(Skin& k, i32 log2_hz, i32 depth16, i32 sweep_r, u32 parts, const i32* ratios, const i32* decays,
                 const i32* shares, i32 vp) {
    k.inc = dsp::increment(log2_hz);
    k.sweep_inc = static_cast<u32>(multiply(static_cast<i32>(k.inc >> 1), (depth16 > 0 ? depth16 : 0) << 12)) << 4;
    k.sr = sweep_r;
    k.parts = parts;
    k.vp = vp;
    for (u32 i = 0; i < parts; ++i) {
        k.ratio[i] = ratios[i];
        k.e[i].r = decays[i];
        k.share[i] = shares[i];
    }
}

// A struck mode with its strike's contact (Q15 at the softest and hardest).
struct Struck {
    dsp::Mode m;
    i32 soft, hard;   // Q15: the strike reaching this mode
};
// Tunes a struck mode: log2 Hz, decay, share (Q15) and the contact's width in
// samples (Q8) at velocity 0 and 1.
inline void tune(Struck& s, i32 log2_hz, i32 r, i32 share, u32 width0, u32 width1) {
    const u32 inc = dsp::increment(log2_hz);
    s.m.tune(inc, r);
    s.soft = dsp::m15(share, dsp::contact(inc, width0));
    s.hard = dsp::m15(share, dsp::contact(inc, width1));
}
inline void ring(Struck& s, i32 v15, i32 amplitude15) {
    const i32 c = s.soft + dsp::m15(s.hard - s.soft, v15);
    s.m.strike(dsp::m15(amplitude15, c) << 13);
}
// The contact's width (Q8 samples) for a hardness h (Q31): (1.4 - 1.2 h)(1.25 - 0.6 v) ms.
inline void widths(i32 h, u32& w0, u32& w1) {
    i32 base = GQ16(1.4) - multiply(GQ16(1.2), h);                 // ms, Q16
    if (base < GQ16(0.02)) base = GQ16(0.02);
    const u32 samples_q8 = static_cast<u32>((base * 11290) >> 16);  // ms * 44.1 samples, Q8
    w0 = (samples_q8 * 5) >> 2;            // * 1.25
    w1 = (samples_q8 * 13) >> 4;           // * 0.65 (1.25 - 0.6)
}

// A pending strike the body schedules itself: a clap's reflections, a ratchet's teeth.
struct Ghost { u32 at; i32 v; };
const u32 ghosts = 32;

struct Body {
    u32 model;
    i32 p[4];          // the controls prepared, Q31
    u32 prepared;
    u32 seed;
    u32 count;         // strikes so far
    u32 now;           // samples since the note
    Skin skin[2];
    Struck mode[7];
    dsp::Band band[3];
    dsp::Pole pole[6];
    i32 pc[6];
    dsp::Click click[2];
    i32 cg[2];         // the clicks' gains, Q28
    Energy env[4];
    u32 sq[8], sqi[8];
    i32 g[8];          // mixing gains, Q28
    i32 k[4];          // model specifics
    Ghost ghost[ghosts];
    u32 pending;
    u32 due;           // the earliest ghost's time, or 0xffffffff for none
    i32 tuned[2];      // a tine set's pitch as last tuned (log2 Hz), so a repeat isn't tuned again
};

// The hat's and the ride's square banks (ratios to the base, Q28), and a
// clap's early reflections: 11, 19 and 31 ms, at a half, a third and a quarter.
const i32 hat_ratio[6] = {GQ28(1), GQ28(1.37), GQ28(1.61), GQ28(1.83), GQ28(2.27), GQ28(2.61)};
const i32 ride_ratio[8] = {GQ28(1), GQ28(1.19), GQ28(1.43), GQ28(1.71), GQ28(2.03), GQ28(2.38), GQ28(2.93), GQ28(3.41)};
const u16 room_delay[3] = {485, 838, 1367};
const u16 room_level[3] = {GQ15(0.5), GQ15(0.35), GQ15(0.25)};

// ---------------------------------------------------------------- prepare --
inline void prepare_click(dsp::Click& c, u32 width_us, u32 hp_hz) {
    c.r = dsp::decay(ms((width_us * 262) >> 10));                   // us -> ms, Q8
    c.c = dsp::pole(dsp::log2(hp_hz));
}
// width in microseconds and the high-pass, from a control.
inline void prepare_click_p(dsp::Click& c, i32 w0, i32 w1, i32 h0, i32 h1, i32 p) {
    const i32 w = dsp::mix(w0, w1, p);
    c.r = dsp::decay(ms(static_cast<u32>((w * 262) >> 10)));   // us -> ms Q8: * 256 / 1000
    c.c = dsp::pole(dsp::log2(static_cast<u32>(dsp::mix(h0, h1, p))));
}

// A click whose width (us) and high-pass move together along a control, the
// corner on a log scale: from a dull thump to a bright tick.
inline void prepare_click_log(dsp::Click& c, i32 w0, i32 w1, u32 h0, u32 h1, i32 p) {
    const i32 w = dsp::mix(w0, w1, p);
    c.r = dsp::decay(ms(static_cast<u32>((w * 262) >> 10)));
    c.c = dsp::pole(dsp::mix(dsp::log2(h0), dsp::log2(h1), p));
}

inline void prepare(Body& b, u32 model, const i32* p) {
    using namespace kit;
    b.model = model;
    for (u32 i = 0; i < 4; ++i) b.p[i] = p[i];
    b.prepared = 1;
    const i32 p0 = p[0], p1 = p[1], p2 = p[2], p3 = p[3];
    switch (model) {
    case kick: {   // tune, punch, decay, beater
        const i32 lf = pitch(38, GQ16(1.25), p0);
        // 35 ms * 43^(decay^1.5): a short, punchy kick low on the travel, a long
        // boom (1.5 s) at its top.
        const i32 d15 = dsp::power_exact(p2 >> 16, GQ16(1.5)) << 16;
        const i32 tau = dsp::samples(OCT(0.035) + multiply(OCT(43), d15));
        const i32 punch = dsp::power_exact(p1 >> 16, GQ16(1.2));
        const i32 ratios[2] = {GQ28(1), GQ28(1.48)};
        const i32 decays[2] = {dsp::decay(tau), dsp::decay(tau + OCT(0.35))};
        const i32 shares[2] = {32767, dsp::m15(GQ15(0.18), 32767 - (p2 >> 16))};
        // Punch: the strike's pitch rise, none to +3 octaves, falling back over
        // 12 to 22 ms, so the deeper drop is heard as one; and the transient
        // (the knock, the thump and the beater's click) from -10 dB to +6 dB.
        skin(b.skin[0], lf, punch * 14, decay_ms(12 * 256, 22 * 256, p1), 2, ratios, decays, shares, GQ16(1.2));
        const i32 attack = GQ28(0.3) + multiply(GQ28(1.8), punch << 16);   // Q28, 1 at the default
        // The shell's knock and the beater's thump: two short modes over the
        // head, where a kick's weight reads on small speakers. A felt beater
        // thumps; a plastic one clicks.
        u32 w0, w1;
        widths(p3, w0, w1);
        tune(b.mode[0], lf + OCT(3.4), decay_ms(48 * 256, 30 * 256, p3), GQ15(0.85), w0, w1);
        tune(b.mode[1], lf + dsp::mix(OCT(5.5), OCT(12), p3), dsp::decay(ms(10 * 256)), GQ15(0.6) - dsp::m15(GQ15(0.35), p3 >> 16), w0, w1);
        // The beater: a felt thump (2.5 ms, dull) to a plastic click (0.15 ms, bright).
        prepare_click_log(b.click[0], 2500, 150, 150, 5000, p3);
        b.cg[0] = gain(GQ28(0.06) + multiply(GQ28(0.9), p3), GQ28(0.4) + multiply(GQ28(1.6), punch << 16));
        b.g[0] = attack;
        // How hard the head drives its saturation: 3 for a short, punchy kick,
        // easing to 1.6 from decay 0.4 to 0.8 so a long boom keeps a clean low
        // end; k[1] makes up its gain, 1 / tanh(drive).
        const i32 over = dsp::clamp(p2 - GQ31(0.4), 0, GQ31(0.4)) * 2;      // 0 .. 0.8, Q31
        b.k[0] = GQ28(3.0) - multiply(GQ28(1.75), over);
        const u32 t = static_cast<u32>(dsp::tanh(b.k[0])) >> 13;              // tanh(drive), Q18
        b.k[1] = static_cast<i32>(dsp::udiv(1u << 31, t ? t : 1) << 15);    // Q28
        break;
    }
    case snare: {  // tune, wires, decay, tone
        const i32 lf = pitch(150, GQ16(1.1), p0);
        const i32 tau = ms(static_cast<u32>(dsp::mix(50 * 256, 300 * 256, p2)));
        const i32 ratios[3] = {GQ28(1), GQ28(1.59), GQ28(2.14)};
        const i32 decays[3] = {dsp::decay(tau), dsp::decay(tau + OCT(0.6)), dsp::decay(tau + OCT(0.45))};
        // The head's upper modes carry the shell's body (300 .. 700 Hz), brighter with tone.
        const i32 shares[3] = {32767, GQ15(0.42) + dsp::m15(GQ15(0.3), p3 >> 16), GQ15(0.22) + dsp::m15(GQ15(0.38), p3 >> 16)};
        skin(b.skin[0], lf, GQ16(0.6), dsp::decay(ms(12 * 256)), 3, ratios, decays, shares, GQ16(1.2));
        b.env[0].r = decay_ms(70 * 256, 370 * 256, p2);                      // the wires
        // The wires' band: broad (Q 0.9), from a dark 1.4 kHz to a bright 4.5 kHz,
        // over a high-pass that keeps the noise out of the head's low end.
        b.band[0].tune(dsp::mix(q16(1400), q16(4500), p3), -OCT(1.1));
        b.pc[0] = dsp::pole(q16(700));
        b.pc[1] = dsp::pole(q16(8500));
        prepare_click(b.click[0], 500, 2500);
        b.cg[0] = GQ28(0.2);
        prepare_click(b.click[1], 2600, 3600);                              // the crack
        b.cg[1] = GQ28(0.05) + multiply(GQ28(0.45), p3);
        b.g[0] = GQ28(1.1) - multiply(GQ28(0.45), p1);                       // head
        b.g[1] = multiply(GQ28(1.75), p1);                                   // wires
        break;
    }
    case tom: case fill: {   // tune, decay, bend (tom) or spread (fill), skin
        const i32 lf = pitch(62, GQ16(2.2), p0);
        const i32 d13 = dsp::power_exact(p1 >> 16, GQ16(1.3)) << 16;           // decay^1.3
        const i32 tau = ms(static_cast<u32>(dsp::mix(120 * 256, 1020 * 256, d13)));
        b.k[0] = tau;
        b.k[1] = lf;
        if (model == tom) {
            const i32 ratios[3] = {GQ28(1), GQ28(1.5), GQ28(1.98)};
            const i32 decays[3] = {dsp::decay(tau + dsp::log2(static_cast<u32>(dsp::mix(GQ16(0.6), GQ16(1.0), p3))) - 16 * dsp::octave),
                                   dsp::decay(tau + dsp::log2(static_cast<u32>(dsp::mix(GQ16(0.35), GQ16(0.75), p3))) - 16 * dsp::octave),
                                   dsp::decay(tau + dsp::log2(static_cast<u32>(dsp::mix(GQ16(0.25), GQ16(0.55), p3))) - 16 * dsp::octave)};
            const i32 shares[3] = {32767, GQ15(0.05) + dsp::m15(GQ15(0.75), p3 >> 16), dsp::m15(GQ15(0.5), p3 >> 16)};
            skin(b.skin[0], lf, GQ16(0.25) + dsp::m15(GQ16(0.9) >> 1, p2 >> 16) * 2, dsp::decay(ms(50 * 256)), 3, ratios, decays, shares, GQ16(1.2));
        } else {
            // A rack of heads: each fret the next head up; set as each strikes.
            b.k[2] = GQ16(1.5 / 12) + multiply(GQ16(3.5 / 12), p2);           // octaves a head
            b.k[3] = p3;
        }
        prepare_click(b.click[0], 700, 1500);
        b.cg[0] = GQ28(0.3);
        break;
    }
    case hand: {   // tune, slap, decay, mute
        const i32 lf = pitch(170, GQ16(1.3), p0) + multiply(OCT(1.12), p3);
        const i32 base = dsp::mix(60 * 256, 560 * 256, p2);
        const i32 tau = ms(static_cast<u32>(multiply(base, GQ31(1) - multiply(GQ31(0.75), p3))));
        const i32 ratios[3] = {GQ28(1), GQ28(1.52), GQ28(2.31)};
        const i32 decays[3] = {dsp::decay(tau), dsp::decay(tau + OCT(0.5)), dsp::decay(tau + OCT(0.3))};
        const i32 shares[3] = {32767, GQ15(0.25) + dsp::m15(GQ15(0.4), p1 >> 16), dsp::m15(GQ15(0.35), p1 >> 16)};
        skin(b.skin[0], lf, GQ16(0.15), dsp::decay(ms(20 * 256)), 3, ratios, decays, shares, GQ16(1.2));
        b.env[0].r = decay_ms(12 * 256, 32 * 256, p1);
        b.band[0].tune(q16(1800), OCT(1.2));
        prepare_click(b.click[0], 400, 3000);
        b.cg[0] = GQ28(0.2);
        b.g[0] = GQ28(1) - multiply(GQ28(0.4), p1);
        b.g[1] = multiply(GQ28(1.4), p1);
        break;
    }
    case rim: {    // tune, wood, decay, body
        const i32 lf = pitch(700, GQ16(1.6), p0);
        const i32 metal = dsp::one - p1;
        const i32 tau = ms(static_cast<u32>(multiply(dsp::mix(12 * 256, 112 * 256, p2) * 4, GQ31(0.25) + multiply(GQ31(0.375), metal))));
        u32 w0, w1;
        widths(GQ31(0.9), w0, w1);
        tune(b.mode[0], lf, dsp::decay(tau), 32767, w0, w1);
        tune(b.mode[1], lf + dsp::log2(static_cast<u32>(GQ16(2.3) + multiply(GQ16(0.4), metal))) - 16 * dsp::octave,
             dsp::decay(tau + OCT(0.6)), GQ15(0.6), w0, w1);
        tune(b.mode[2], lf + dsp::log2(static_cast<u32>(GQ16(3.9) - multiply(GQ16(0.8), metal))) - 16 * dsp::octave,
             dsp::decay(tau + OCT(0.4)), GQ15(0.35), w0, w1);
        const i32 ratios[1] = {GQ28(1)};
        const i32 decays[1] = {decay_ms(60 * 256, 180 * 256, p2)};
        const i32 shares[1] = {32767};
        skin(b.skin[0], lf - OCT(3.7), GQ16(0.3), dsp::decay(ms(10 * 256)), 1, ratios, decays, shares, GQ16(1.0));
        b.g[0] = multiply(GQ28(1.5), p3);
        prepare_click(b.click[0], 300, 2500);
        b.cg[0] = GQ28(0.5);
        break;
    }
    case hat: {    // tune, decay, tone, metal
        const u32 base = dsp::increment(pitch(310, GQ16(1), p0));
        for (u32 i = 0; i < 6; ++i) b.sqi[i] = times(base, hat_ratio[i]);
        b.env[0].r = dsp::decay(dsp::samples(OCT(0.028) + multiply(OCT(26), p1)));
        // Tune moves the metal's band with it, by an octave over its travel.
        const i32 shift = multiply(dsp::octave, p0) - dsp::octave / 2;
        b.band[0].tune(dsp::log2(static_cast<u32>(dsp::mix(4200, 7200, p2))) + shift, OCT(0.9));
        b.pc[0] = dsp::pole(q16(13000));
        b.pc[1] = dsp::pole(dsp::log2(static_cast<u32>(dsp::mix(3000, 5000, p2))) + shift);
        // An open hat rings longer, so it plays softer: its strike about 7 dB
        // under a closed one's, so the two sit level by loudness.
        const i32 open = dsp::one - multiply(GQ31(0.8), p1);
        b.g[0] = multiply(p3, open);                                  // the bank
        b.g[1] = multiply(multiply(GQ31(0.6), dsp::one - multiply(GQ31(0.7), p3)), open);   // the hiss (Q31)
        prepare_click(b.click[0], 300, 6000);
        b.cg[0] = GQ28(0.25);
        break;
    }
    case ride: {   // tune, decay, bell, wash
        const i32 lb = pitch(360, GQ16(0.9), p0);
        const u32 base = dsp::increment(lb);
        for (u32 i = 0; i < 8; ++i) b.sqi[i] = times(base, ride_ratio[i]);
        const i32 tau = dsp::samples(OCT(0.55) + multiply(3 * dsp::octave, p1));   // 0.55 .. 4.4 s
        b.env[0].r = dsp::decay(tau);
        b.env[1].r = dsp::decay(tau + OCT(0.6));
        b.band[0].tune(q16(3800), OCT(0.7));
        b.band[1].tune(q16(5500), OCT(0.9));
        b.pc[0] = dsp::pole(q16(1800));
        b.pc[1] = dsp::pole(q16(10000));
        b.pc[2] = dsp::pole(q16(2800));
        b.pc[3] = dsp::pole(q16(11000));
        u32 w0, w1;
        widths(GQ31(0.8), w0, w1);
        const i32 lbell = lb + OCT(2.1);
        tune(b.mode[0], lbell, dsp::decay(tau + OCT(0.7)), 32767, w0, w1);
        tune(b.mode[1], lbell + OCT(2.0), dsp::decay(tau + OCT(0.45)), GQ15(0.4), w0, w1);
        tune(b.mode[2], lbell + OCT(2.76), dsp::decay(tau + OCT(0.3)), GQ15(0.2), w0, w1);
        b.g[0] = multiply(GQ28(0.6), p3);
        b.g[1] = multiply(GQ28(0.55), p2);
        prepare_click(b.click[0], 300, 5000);
        b.cg[0] = GQ28(0.15);
        break;
    }
    case bell: {   // tune, decay, pair, metal
        const i32 lf = pitch(540, GQ16(1.2), p0);
        const i32 d13 = dsp::power_exact(p1 >> 16, GQ16(1.3)) << 16;           // decay^1.3
        const i32 tau = ms(static_cast<u32>(dsp::mix(50 * 256, 650 * 256, d13)));
        b.k[0] = static_cast<i32>(dsp::increment(lf));
        // The pair's lower bell: 1 .. 7 semitones under, from the first touch of pair.
        b.k[1] = p2 > GQ31(0.02) ? static_cast<i32>(dsp::increment(lf - GQ16(1.0 / 12) - multiply(GQ16(6.0 / 12), p2)))
                                 : b.k[0];
        b.k[2] = p3;
        for (u32 i = 0; i < 2; ++i) {
            b.env[2 * i].r = dsp::decay(tau);
            // decays at 1/tau + 1/12 ms
            b.env[2 * i + 1].r = multiply(dsp::decay(tau), dsp::decay(ms(12 * 256)));
        }
        b.band[0].tune(lf + OCT(2.2), OCT(0.8));
        prepare_click(b.click[0], 300, 3000);
        b.cg[0] = GQ28(0.2);
        break;
    }
    case tine: {   // tune, decay, step, hard
        b.k[0] = pitch(330, GQ16(1.6), p0);
        b.k[1] = (static_cast<u32>(p2) + (1u << 28)) >> 29;          // 0..4 scale steps a fret
        const i32 tau = ms(static_cast<u32>(dsp::mix(250 * 256, 2450 * 256, p1)));
        b.k[2] = tau;
        b.k[3] = p3;
        prepare_click(b.click[0], 300, 3000);
        b.cg[0] = multiply(GQ28(0.12), p3);
        b.pending = 0xffffffffu;   // modes tuned per strike
        break;
    }
    case clock: {  // tune, decay, tock, case
        const i32 lf = pitch(1700, GQ16(1.2), p0);
        const i32 r = decay_ms(6 * 256, 250 * 256, multiply(p1, p1));
        const i32 lo = -GQ16(3.0 / 12) - multiply(GQ16(16.0 / 12), p2);   // the tock 3 .. 19 semitones under
        u32 w0, w1;
        widths(dsp::one, w0, w1);   // a hard tick: little filtering
        tune(b.mode[0], lf, r, 32767, w0, w1);
        tune(b.mode[1], lf + OCT(2.71), r, GQ15(0.5), w0, w1);
        tune(b.mode[2], lf + lo, r, 32767, w0, w1);
        tune(b.mode[3], lf + lo + OCT(2.0), r, GQ15(0.6), w0, w1);         // a woodier second partial
        const i32 lc = pitch(420, GQ16(0.8), p0);
        widths(GQ31(0.8), w0, w1);
        tune(b.mode[4], lc, decay_ms(40 * 256, 540 * 256, p3), 32767, w0, w1);
        tune(b.mode[5], lc + OCT(1.53), decay_ms(30 * 256, 330 * 256, p3), GQ15(0.6), w0, w1);
        tune(b.mode[6], lc + OCT(2.4), decay_ms(20 * 256, 220 * 256, p3), GQ15(0.4), w0, w1);
        b.g[0] = GQ28(0.15) + multiply(GQ28(0.8), p3);
        prepare_click(b.click[0], 200, 4000);
        b.cg[0] = GQ28(0.35);
        break;
    }
    case block: {  // tune, decay, hollow, click
        const i32 lf = pitch(600, GQ16(1.6), p0);
        const i32 base = dsp::mix(20 * 256, 110 * 256, p1);
        const i32 tau = ms(static_cast<u32>(base + multiply(multiply(base, GQ31(0.6)), p2) * 2));   // (1 + 1.2 hollow)
        u32 w0, w1;
        widths(GQ31(0.85), w0, w1);
        tune(b.mode[0], lf, dsp::decay(tau), 32767, w0, w1);
        tune(b.mode[1], lf + dsp::log2(static_cast<u32>(GQ16(2.6) - multiply(GQ16(1.0), p2))) - 16 * dsp::octave,
             dsp::decay(tau - dsp::octave), GQ15(0.45), w0, w1);
        tune(b.mode[2], lf + dsp::log2(static_cast<u32>(GQ16(4.2) - multiply(GQ16(1.3), p2))) - 16 * dsp::octave,
             dsp::decay(tau + OCT(0.3)), GQ15(0.2), w0, w1);
        // A knuckle's knock (1 ms, dull) to a stick's hard click (0.15 ms).
        prepare_click_log(b.click[0], 2000, 100, 300, 6000, p3);
        b.cg[0] = GQ28(0.5) + multiply(GQ28(1.2), p3);
        break;
    }
    case clap: {   // tone, decay, room, snap
        const i32 lf = pitch(900, GQ16(1.25), p0);
        b.band[0].tune(lf, OCT(1.15) + multiply(OCT(2.0) - OCT(1.15), p3)); // the cavity between the palms, Q 1.15 .. 2
        b.env[0].r = decay_ms(10 * 256, 4 * 256, p3);                       // a burst: rises in 0.4 ms
        b.k[2] = decay_ms(16 * 256, 6 * 256, p3);                            // a soft hand's burst
        b.k[3] = decay_ms(7 * 256, 2 * 256, p3);                             // a hard one's
        b.env[1].r = dsp::decay(ms(102));                                    // 0.4 ms
        b.env[2].r = decay_ms(30 * 256, 380 * 256, p1);                      // the room's tail
        b.g[0] = GQ28(0.25) + multiply(GQ28(0.5), p2);
        b.k[0] = static_cast<i32>(dsp::mix(GQ16(0.6), GQ16(1.4), p2));       // the reflections' spread, Q16
        b.k[1] = p2;
        b.pc[0] = corner(500, 1100, p3);
        b.g[1] = GQ28(0.05) + multiply(GQ28(0.25), p3);                       // the bursts' own crack: air over the palms
        break;
    }
    case shaker: { // tone, decay, shell, grit
        const i32 lf = pitch(3000, GQ16(1.6), p0);
        b.env[0].r = decay_ms(77, 2048, multiply(p3, p3));                   // a grain: 0.3 .. 8 ms
        b.k[2] = decay_ms(154, 3072, multiply(p3, p3));                      // a soft grain
        b.k[3] = decay_ms(51, 1024, multiply(p3, p3));                       // a hard one
        b.env[1].r = decay_ms(15 * 256, 400 * 256, multiply(p1, p1));        // the shell's rustle
        b.band[0].tune(lf, OCT(0.5) + multiply(OCT(12), p2));               // Q 0.5 .. 6
        u32 w0, w1;
        widths(GQ31(0.9), w0, w1);
        const i32 r = decay_ms(5 * 256, 60 * 256, p2);
        tune(b.mode[0], lf - OCT(1.25), r, GQ15(0.5), w0, w1);
        tune(b.mode[1], lf + OCT(1.13), r, GQ15(0.3), w0, w1);
        b.g[0] = GQ28(0.1) + multiply(GQ28(1.1), p2);
        b.pc[0] = dsp::pole(q16(1800));
        break;
    }
    case ratchet: { // tune, decay, teeth, wood
        const i32 lf = pitch(900, GQ16(1.4), p0);
        const i32 w = p3, metal = dsp::one - p3;
        const i32 tau = ms(static_cast<u32>(dsp::mix(10 * 256, 70 * 256, p1)));
        u32 w0, w1;
        widths(GQ31(0.9), w0, w1);
        tune(b.mode[0], lf, dsp::decay(tau + dsp::log2(static_cast<u32>(GQ16(1.3) - multiply(GQ16(0.6), w))) - 16 * dsp::octave),
             32767, w0, w1);
        tune(b.mode[1], lf + dsp::log2(static_cast<u32>(GQ16(2.2) + multiply(GQ16(0.7), metal))) - 16 * dsp::octave,
             dsp::decay(tau + OCT(0.6)), GQ15(0.6), w0, w1);
        tune(b.mode[2], lf + dsp::log2(static_cast<u32>(GQ16(3.7) - multiply(GQ16(0.4), w))) - 16 * dsp::octave,
             dsp::decay(tau + OCT(0.4)), GQ15(0.35), w0, w1);
        b.k[0] = 1 + ((static_cast<i32>(p2 >> 16) * 11 + 16384) >> 15);      // teeth a stroke
        b.g[0] = static_cast<i32>(dsp::udiv(static_cast<u32>(GQ28(1.6)), 256 + ((38 * b.k[0]) >> 0)) << 8);   // 1.6 / (1 + .15 teeth)
        const i32 ratios[1] = {GQ28(1)};
        const i32 decays[1] = {decay_ms(30 * 256, 180 * 256, p1)};
        const i32 shares[1] = {32767};
        skin(b.skin[0], lf - OCT(4.5), GQ16(0.1), dsp::decay(ms(10 * 256)), 1, ratios, decays, shares, GQ16(1.0));
        b.g[1] = multiply(GQ28(0.25), w);
        prepare_click(b.click[0], 200, 3000);
        b.cg[0] = GQ28(0.3);
        break;
    }
    default: break;
    }
}

// ----------------------------------------------------------------- strike --
inline void add_ghost(Body& b, u32 at, i32 v) {
    for (u32 i = 0; i < ghosts; ++i)
        if (!b.ghost[i].v) {
            b.ghost[i].at = at;
            b.ghost[i].v = v;
            if (at < b.due) b.due = at;
            return;
        }
}
// The next ghost due, or 0xffffffff.
inline u32 ghost_due(const Body& b) { return b.due; }

// How hard a stroke reads: its force, 12% more or less at random, so no two
// strokes of a roll or a bounce sound alike (the contact, the pitch's rise).
inline i32 touch(const Body& b, i32 v15) {
    const u32 h = (b.count * 2654435761u) ^ (b.seed & 0xffffu);
    const i32 t = dsp::m15(v15, 28835 + static_cast<i32>((h >> 19) & 8191u));   // 0.88 .. 1.13
    return t > 32767 ? 32767 : t;
}
inline void strike_modes(Body& b, u32 count, i32 v15, i32 vp, i32 tone15) {
    const i32 a = dsp::power(v15, vp);
    for (u32 i = 0; i < count; ++i) ring(b.mode[i], tone15, a);
}

// An impact at sample b.now; ghost: a strike the body scheduled itself.
inline void strike(Body& b, i32 v15, u32 kind, u32 step, u32 n, bool ghost = false) {
    using namespace kit;
    if (v15 <= 0) return;
    ++b.count;
    const i32 vt = touch(b, v15);
    switch (b.model) {
    case kick:
        b.skin[0].strike(v15, vt);
        strike_modes(b, 2, v15, GQ16(1.3), vt);
        b.click[0].strike(vt);
        break;
    case snare:
        b.skin[0].strike(v15, vt);
        b.env[0].strike(dsp::power(v15, GQ16(1.4)));
        b.click[0].strike(vt);
        b.click[1].strike(vt);
        break;
    case tom: case hand:
        b.skin[0].strike(v15, vt);
        if (b.model == hand) b.env[0].strike(dsp::power(vt, GQ16(1.5)));
        b.click[0].strike(vt);
        break;
    case fill: {
        // Each fret is the next head up the rack; the landing and its bounces the lowest.
        Skin& s = b.skin[b.count & 1];
        const u32 up = n > step ? n - step : 0;
        const i32 share = n ? static_cast<i32>((up * table::reciprocal[n]) >> 1) : 0;   // Q15
        const i32 lf = b.k[1] + static_cast<i32>(up) * b.k[2];
        const i32 tau = b.k[0] + dsp::log2(static_cast<u32>(GQ16(1) - (dsp::m15(GQ16(0.25) >> 1, share) * 2))) - 16 * dsp::octave;
        const i32 sk = b.k[3];
        const i32 ratios[2] = {GQ28(1), GQ28(1.5)};
        const i32 decays[2] = {dsp::decay(tau + dsp::log2(static_cast<u32>(dsp::mix(GQ16(0.6), GQ16(1.0), sk))) - 16 * dsp::octave),
                               dsp::decay(tau + dsp::log2(static_cast<u32>(dsp::mix(GQ16(0.4), GQ16(0.8), sk))) - 16 * dsp::octave)};
        const i32 shares[2] = {32767, GQ15(0.08) + dsp::m15(GQ15(0.8), sk >> 16)};
        skin(s, lf, GQ16(0.45), dsp::decay(ms(40 * 256)), 2, ratios, decays, shares, GQ16(1.2));
        s.strike(v15, vt);
        b.click[0].strike(vt);
        break;
    }
    case rim:
        strike_modes(b, 3, v15, GQ16(1.1), vt);
        b.skin[0].strike(v15, vt);
        b.click[0].strike(vt);
        break;
    case hat:
        b.env[0].strike(dsp::power(v15, GQ16(1.3)));
        b.click[0].strike(v15);
        break;
    case ride:
        b.env[0].strike(dsp::power(v15, GQ16(1.2)));
        b.env[1].strike(dsp::power(v15, GQ16(1.2)));
        strike_modes(b, 3, v15, GQ16(1.1), vt);
        b.click[0].strike(vt);
        break;
    case bell: {
        const u32 which = (b.k[1] != b.k[0] && (b.count & 1) == 0) ? 1 : 0;   // the pair alternates
        const i32 v = dsp::power(v15, GQ16(1.2));
        b.env[2 * which].strike(dsp::m15(v, GQ15(0.6)));
        b.env[2 * which + 1].strike(dsp::m15(v, GQ15(0.4)));
        b.click[0].strike(v15);
        break;
    }
    case tine: {
        // Each fret a step further up the scale, the landing at the key's pitch.
        const u32 set = (b.count & 1) * 3;
        u32 steps = static_cast<u32>(b.k[1]) * (n > step ? n - step : 0);
        while (steps >= 15) steps -= 15;               // three octaves, then round again
        u32 octaves = 0, degree = steps;
        while (degree >= 5) { degree -= 5; ++octaves; }
        const i32 lf = b.k[0] + static_cast<i32>(octaves) * dsp::octave + table::pentatonic[degree];
        const i32 h = b.k[3];
        // Tuning a set costs divisions (its contact): only when its pitch moves.
        const i32 key = lf ^ (b.k[2] << 3) ^ (h >> 7);
        if (b.tuned[set ? 1 : 0] != key) {
            b.tuned[set ? 1 : 0] = key;
            u32 w0, w1;
            widths(h, w0, w1);
            tune(b.mode[set], lf, dsp::decay(b.k[2]), 32767, w0, w1);
            tune(b.mode[set + 1], lf + OCT(2.756), dsp::decay(b.k[2] + OCT(0.3)),
                 dsp::m15(GQ15(0.32), GQ15(0.3) + (h >> 16)), w0, w1);
            tune(b.mode[set + 2], lf + OCT(5.404), dsp::decay(b.k[2] + OCT(0.12)), dsp::m15(GQ15(0.15), h >> 16), w0, w1);
        }
        const i32 a = dsp::power(v15, GQ16(1.1));
        for (u32 i = 0; i < 3; ++i) ring(b.mode[set + i], vt, a);
        b.click[0].strike(v15);
        break;
    }
    case clock: {
        const u32 set = (b.count & 1) ? 0 : 2;   // tick, tock, tick...
        const i32 a = dsp::power(v15, GQ16(1.2));
        ring(b.mode[set], vt, a);
        ring(b.mode[set + 1], vt, a);
        const i32 c = dsp::power(v15, GQ16(1.1));
        for (u32 i = 4; i < 7; ++i) ring(b.mode[i], vt, c);
        b.click[0].strike(v15);
        break;
    }
    case block:
        strike_modes(b, 3, v15, GQ16(1.1), vt);
        b.click[0].strike(vt);
        break;
    case clap: {
        // Each hand: a burst through the palms' cavity; the room returns it.
        const i32 v = v15 << 13;
        b.env[0].r = b.k[2] + multiply(b.k[3] - b.k[2], vt << 16);           // harder: shorter
        b.env[0].a += v;
        b.env[1].a += v;
        if (b.env[0].a > (3 << 28)) b.env[0].a = 3 << 28;
        if (b.env[1].a > (3 << 28)) b.env[1].a = 3 << 28;
        b.env[2].strike(v15);
        if (!ghost && b.k[1] > 0) {
            for (u32 i = 0; i < 3; ++i)
                add_ghost(b, b.now + ((room_delay[i] * static_cast<u32>(b.k[0])) >> 16),
                          dsp::m15(dsp::m15(v15, room_level[i]), b.k[1] >> 16));
        }
        break;
    }
    case shaker:
        b.env[0].r = b.k[2] + multiply(b.k[3] - b.k[2], vt << 16);           // harder: a shorter tick
        b.env[0].a += v15 << 13;
        if (b.env[0].a > (3 << 28)) b.env[0].a = 3 << 28;
        b.env[1].strike(dsp::m15(v15, GQ15(0.6)));
        strike_modes(b, 2, v15 >> 1, GQ16(1.0), vt >> 1);
        break;
    case ratchet:
        if (!ghost) {
            // A stroke drags the pawl over the teeth: 4 ms a tooth, closing up.
            const u32 teeth = static_cast<u32>(b.k[0]);
            const u32 span = 176 * teeth;
            for (u32 q = 1; q < teeth; ++q) {
                const i32 x = static_cast<i32>((q * table::reciprocal[teeth]) >> 1);       // q / teeth, Q15
                const u32 at = (span * static_cast<u32>(dsp::power(x, GQ16(1.3)))) >> 15;
                const i32 rise = GQ15(0.55) + dsp::m15(GQ15(0.45), static_cast<i32>((q * table::reciprocal[teeth - 1]) >> 1));
                add_ghost(b, b.now + at, dsp::m15(v15, rise));
            }
            b.skin[0].strike(v15, vt);
            v15 = dsp::m15(v15, GQ15(0.55));
        }
        strike_modes(b, 3, v15, GQ16(1.1), touch(b, v15));
        b.click[0].strike(v15);
        break;
    default: break;
    }
    (void)kind;
}
// Plays the ghosts due at b.now.
inline void strike_ghosts(Body& b, u32 n) {
    if (b.due > b.now) return;
    for (u32 i = 0; i < ghosts; ++i)
        if (b.ghost[i].v && b.ghost[i].at <= b.now) {
            const i32 v = b.ghost[i].v;
            b.ghost[i].v = 0;
            strike(b, v, 0, 0, n, true);
        }
    b.due = 0xffffffffu;
    for (u32 i = 0; i < ghosts; ++i)
        if (b.ghost[i].v && b.ghost[i].at < b.due) b.due = b.ghost[i].at;
}

// -------------------------------------------------------------------- run --
// Each part runs a whole stretch of samples in its own tight loop, its state in
// locals, writing or adding into a buffer: cheaper on the Blackfin than one loop
// that steps every part each sample. A part with nothing ringing is skipped.

// A skin: writes n samples. While the strike's pitch sweep lasts, every
// sample follows it; after, each partial runs at its own fixed increment.
// swing, if given, gets |sine| of the fundamental (Q31) for the snare's wires.
inline void skin_block(Skin& k, i32* __restrict out, u32 n, i32* __restrict swing = 0) {
    i32 s = k.s;
    if (s > 4194304 && k.sweep_inc) {                       // sweeping (above 0.002)
        const i32 sr = k.sr;
        u32 p0 = k.ph[0], p1 = k.ph[1], p2 = k.ph[2];
        i32 a0 = k.e[0].a, a1 = k.e[1].a, a2 = k.e[2].a;
        const i32 r0 = k.e[0].r, r1 = k.e[1].r, r2 = k.e[2].r;
        const i32 q1 = k.ratio[1], q2 = k.ratio[2];
        const u32 parts = k.parts, base = k.inc, sweep = k.sweep_inc;
        for (u32 i = 0; i < n; ++i) {
            s = multiply(s, sr);
            const i32 curve = multiply(s, GQ31(0.6) + multiply(s, GQ31(0.4)));   // ~ s^1.3
            const u32 now = base + static_cast<u32>(multiply(static_cast<i32>(sweep), curve));
            p0 += now;
            a0 = multiply(a0, r0);
            const i32 v = dsp::sine(p0);
            i32 y = multiply(v, a0);
            if (swing) swing[i] = v < 0 ? -v : v;
            if (parts > 1) { p1 += times(now, q1); a1 = multiply(a1, r1); y += multiply(dsp::sine(p1), a1); }
            if (parts > 2) { p2 += times(now, q2); a2 = multiply(a2, r2); y += multiply(dsp::sine(p2), a2); }
            out[i] = y;
        }
        k.s = s;
        k.ph[0] = p0; k.ph[1] = p1; k.ph[2] = p2;
        k.e[0].a = a0; k.e[1].a = a1; k.e[2].a = a2;
        return;
    }
    k.s = 0;
    if (!k.parts) { for (u32 i = 0; i < n; ++i) out[i] = 0; return; }
    for (u32 part = 0; part < k.parts; ++part) {
        u32 p = k.ph[part];
        i32 a = k.e[part].a;
        const i32 r = k.e[part].r;
        const u32 inc = part ? times(k.inc, k.ratio[part]) : k.inc;
        if (!part) {
            for (u32 i = 0; i < n; ++i) {
                p += inc;
                a = multiply(a, r);
                const i32 v = dsp::sine(p);
                out[i] = multiply(v, a);
                if (swing) swing[i] = v < 0 ? -v : v;
            }
        } else if (a) {
            for (u32 i = 0; i < n; ++i) {
                p += inc;
                a = multiply(a, r);
                out[i] += multiply(dsp::sine(p), a);
            }
        } else p += inc * n;
        k.ph[part] = p;
        k.e[part].a = a;
    }
}
// A click's noise burst, high-passed, added at gain g (Q28). Idle, it costs nothing.
inline void click_add(dsp::Click& c, u32& seed, i32* __restrict out, u32 n, i32 g28) {
    i32 e = c.e, h = c.hp.s;
    if (!e && !h) return;
    const i32 r = c.r, cc = c.c;
    u32 x = seed;
    for (u32 i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        e = multiply(e, r);
        const i32 in = multiply(static_cast<i32>(x), e) * 2;
        h += multiply(in - h, cc);
        out[i] = dsp::sat_add(out[i], gain(in - h, g28));
    }
    seed = x;
    if (e < 16 && h < 4096 && h > -4096) e = h = 0;
    c.e = e;
    c.hp.s = h;
}
// Struck modes, added. A mode that has died away is cleared, so it is skipped.
inline void modes_add(Struck* m, u32 count, i32* __restrict out, u32 n, i32 g28 = 0) {
    for (u32 k = 0; k < count; ++k) {
        i32 y1 = m[k].m.y1, y2 = m[k].m.y2;
        if (!y1 && !y2) continue;
        const i32 a1 = m[k].m.a1, a2 = m[k].m.a2;
        if (g28) {
            for (u32 i = 0; i < n; ++i) {
                const i32 y = dsp::resonate(a1, y1, a2, y2);
                y2 = y1; y1 = y;
                out[i] = dsp::sat_add(out[i], gain(y, g28));
            }
        } else {
            for (u32 i = 0; i < n; ++i) {
                const i32 y = dsp::resonate(a1, y1, a2, y2);
                y2 = y1; y1 = y;
                out[i] = dsp::sat_add(out[i], y);
            }
        }
        if (y1 < 256 && y1 > -256 && y2 < 256 && y2 > -256) y1 = y2 = 0;
        m[k].m.y1 = y1; m[k].m.y2 = y2;
    }
}
// Noise under an envelope (noise std about 1.15 units), written.
inline void noise_env(Energy& env, u32& seed, i32* __restrict out, u32 n) {
    i32 a = env.a;
    const i32 r = env.r;
    u32 x = seed;
    for (u32 i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        a = multiply(a, r);
        out[i] = multiply(static_cast<i32>(x) >> 2, a) * 8;
    }
    env.a = a;
    seed = x;
}
inline void band_run(dsp::Band& f, i32* __restrict io, u32 n) {
    i32 x1 = f.x1, x2 = f.x2, y1 = f.y1, y2 = f.y2;
    const i32 b = f.b, a1 = f.a1, a2 = f.a2;
    for (u32 i = 0; i < n; ++i) {
        const i32 x = io[i];
        const i32 y = multiply(b, x - x2) + multiply(a1, y1) * 2 - multiply(a2, y2);
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        io[i] = y;
    }
    f.x1 = x1; f.x2 = x2; f.y1 = y1; f.y2 = y2;
}
inline void high_run(dsp::Pole& p, i32 c, i32* __restrict io, u32 n) {
    i32 s = p.s;
    for (u32 i = 0; i < n; ++i) { s += multiply(io[i] - s, c); io[i] -= s; }
    p.s = s;
}
inline void low_run(dsp::Pole& p, i32 c, i32* __restrict io, u32 n) {
    i32 s = p.s;
    for (u32 i = 0; i < n; ++i) { s += multiply(io[i] - s, c); io[i] = s; }
    p.s = s;
}
inline void add_gain(i32* __restrict out, const i32* __restrict in, i32 g28, u32 n) {
    for (u32 i = 0; i < n; ++i) out[i] += gain(in[i], g28);
}
inline bool idle(const Energy& e) { return e.a < 64; }
// A band-pass, then a one-pole high-pass, added into out at gain g (Q28): one pass.
inline void band_high_add(dsp::Band& f, dsp::Pole& p, i32 c, const i32* __restrict in, i32* __restrict out, i32 g28, u32 n) {
    i32 x1 = f.x1, x2 = f.x2, y1 = f.y1, y2 = f.y2, h = p.s;
    const i32 b = f.b, a1 = f.a1, a2 = f.a2;
    for (u32 i = 0; i < n; ++i) {
        const i32 x = in[i];
        const i32 y = multiply(b, x - x2) + multiply(a1, y1) * 2 - multiply(a2, y2);
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        h += multiply(y - h, c);
        out[i] += gain(y - h, g28);
    }
    f.x1 = x1; f.x2 = x2; f.y1 = y1; f.y2 = y2; p.s = h;
}
// Noise under an envelope, scaled per sample by `shape` (Q31) if given, through a
// band-pass and a one-pole high-pass, added into out at gain g: one pass.
inline void noise_band_add(Energy& env, u32& seed, const i32* __restrict shape, dsp::Band& f, dsp::Pole& p, i32 c,
                           i32* __restrict out, i32 g28, u32 n) {
    i32 a = env.a, x1 = f.x1, x2 = f.x2, y1 = f.y1, y2 = f.y2, h = p.s;
    const i32 r = env.r, b = f.b, a1 = f.a1, a2 = f.a2;
    u32 z = seed;
    for (u32 i = 0; i < n; ++i) {
        z ^= z << 13; z ^= z >> 17; z ^= z << 5;
        a = multiply(a, r);
        i32 x = multiply(static_cast<i32>(z) >> 2, a) * 8;
        if (shape) x = multiply(x, shape[i]);
        const i32 y = multiply(b, x - x2) + multiply(a1, y1) * 2 - multiply(a2, y2);
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        h += multiply(y - h, c);
        out[i] += gain(y - h, g28);
    }
    env.a = a; seed = z;
    f.x1 = x1; f.x2 = x2; f.y1 = y1; f.y2 = y2; p.s = h;
}

// A soft knee, the body's own limiter: straight up to k, then bending towards
// k + h (units 2^28; inv_h28 is 1 / h in Q28). A hit's first milliseconds give
// up their peak, so its body sounds louder under the same ceiling; softer
// strokes pass untouched.
inline void knee_run(i32* __restrict io, u32 n, i32 k, i32 h, i32 inv_h28) {
    for (u32 i = 0; i < n; ++i) {
        const i32 x = io[i];
        const i32 a = x < 0 ? -x : x;
        if (a <= k) continue;
        const i32 y = k + multiply(dsp::tanh(gain(a - k, inv_h28)), h);
        io[i] = x < 0 ? -y : y;
    }
}

__attribute__((noinline)) inline void run_kick(Body& b, i32* __restrict out, u32 count) {
    skin_block(b.skin[0], out, count);
    modes_add(b.mode, 2, out, count, b.g[0]);
    const i32 drive = b.k[0], makeup = b.k[1];
    for (u32 i = 0; i < count; ++i) out[i] = multiply(dsp::tanh(gain(out[i], drive)), makeup);
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_snare(Body& b, i32* __restrict out, u32 count) {
    i32 swing[64], w[64];
    skin_block(b.skin[0], out, count, swing);
    for (u32 i = 0; i < count; ++i) out[i] = gain(out[i], b.g[0]);
    if (!idle(b.env[0]) || b.band[0].y1 || b.band[0].y2) {
        // The wires buzz with the head: noise, swelling with the head's swing.
        for (u32 i = 0; i < count; ++i) w[i] = GQ31(0.55) + multiply(GQ31(0.45), swing[i]);
        noise_band_add(b.env[0], b.seed, w, b.band[0], b.pole[0], b.pc[0], out, b.g[1], count);
    }
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
    click_add(b.click[1], b.seed, out, count, b.cg[1]);
    knee_run(out, count, GQ28(1.2), GQ28(0.6), GQ28(1 / 0.6));
}
__attribute__((noinline)) inline void run_skins(Body& b, i32* __restrict out, u32 count) {   // tom, fill
    skin_block(b.skin[0], out, count);
    if (b.model == kit::fill) {
        i32 more[64];
        skin_block(b.skin[1], more, count);
        for (u32 i = 0; i < count; ++i) out[i] += more[i];
    }
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_hand(Body& b, i32* __restrict out, u32 count) {
    skin_block(b.skin[0], out, count);
    for (u32 i = 0; i < count; ++i) out[i] = gain(out[i], b.g[0]);
    if (!idle(b.env[0]) || b.band[0].y1 || b.band[0].y2) {
        i32 sl[64];
        noise_env(b.env[0], b.seed, sl, count);
        band_run(b.band[0], sl, count);
        add_gain(out, sl, b.g[1], count);
    }
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_modes(Body& b, i32* __restrict out, u32 count, u32 modes) {   // rim, block, tine (6), clock (7), ratchet
    for (u32 i = 0; i < count; ++i) out[i] = 0;
    if (b.model == kit::clock) {
        modes_add(b.mode, 4, out, count);
        modes_add(b.mode + 4, 3, out, count, b.g[0]);
    } else if (b.model == kit::ratchet) {
        modes_add(b.mode, 3, out, count, b.g[0]);
    } else {
        modes_add(b.mode, modes, out, count);
    }
    if (b.model == kit::rim || b.model == kit::ratchet) {
        i32 shell[64];
        skin_block(b.skin[0], shell, count);
        add_gain(out, shell, b.g[b.model == kit::ratchet ? 1 : 0], count);
    }
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
// six squares of 2^25: about 0.75 of unit at most
inline void bank(u32* __restrict phase, const u32* __restrict inc, u32 voices, i32* __restrict out, u32 n) {
    // Squares in twos: each adds +-2^25, the sign of its phase.
    for (u32 i = 0; i < n; ++i) out[i] = 0;
    for (u32 k = 0; k + 1 < voices; k += 2) {
        u32 p = phase[k], q = phase[k + 1];
        const u32 d = inc[k], e = inc[k + 1];
        for (u32 i = 0; i < n; ++i) {
            p += d;
            q += e;
            out[i] += ((static_cast<i32>(p) >> 31) + (static_cast<i32>(q) >> 31)) * (1 << 26) + (1 << 26);
        }
        phase[k] = p;
        phase[k + 1] = q;
    }
}
__attribute__((noinline)) inline void run_hat(Body& b, i32* __restrict out, u32 count) {
    if (idle(b.env[0]) && !b.band[0].y1 && !b.band[0].y2 && !b.pole[2].s) {
        for (u32 i = 0; i < count; ++i) out[i] = 0;
        for (u32 k = 0; k < 6; ++k) b.sq[k] += b.sqi[k] * count;
        b.pole[0].s = 0;
        click_add(b.click[0], b.seed, out, count, b.cg[0]);
        return;
    }
    i32 src[64];
    bank(b.sq, b.sqi, 6, src, count);
    i32 a = b.env[0].a;
    const i32 r = b.env[0].r, g0 = b.g[0], g1 = b.g[1];
    u32 x = b.seed;
    for (u32 i = 0; i < count; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        a = multiply(a, r);
        src[i] = multiply(multiply(src[i], g0) + multiply(static_cast<i32>(x) >> 3, g1), a) * 8;
    }
    b.seed = x;
    b.env[0].a = a;
    band_run(b.band[0], src, count);
    high_run(b.pole[1], b.pc[1], src, count);
    high_run(b.pole[2], b.pc[1], src, count);
    // A zero at Nyquist, no pole: the hiss and the bank's aliases darkened above 10 kHz.
    i32 last = b.pole[0].s;
    for (u32 i = 0; i < count; ++i) { out[i] = (src[i] + last) * 4; last = src[i]; }
    b.pole[0].s = last;
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_ride(Body& b, i32* __restrict out, u32 count) {
    i32 sh[64];
    for (u32 i = 0; i < count; ++i) out[i] = 0;
    if (!idle(b.env[0]) || b.band[0].y1 || b.band[0].y2) {
        // The bank in two passes of four squares, the second applying the envelope.
        u32 p0 = b.sq[0], p1 = b.sq[1], p2 = b.sq[2], p3 = b.sq[3];
        const u32 d0 = b.sqi[0], d1 = b.sqi[1], d2 = b.sqi[2], d3 = b.sqi[3];
        for (u32 i = 0; i < count; ++i) {
            p0 += d0; p1 += d1; p2 += d2; p3 += d3;
            sh[i] = (static_cast<i32>(p0) >> 31) + (static_cast<i32>(p1) >> 31) + (static_cast<i32>(p2) >> 31) +
                    (static_cast<i32>(p3) >> 31);
        }
        b.sq[0] = p0; b.sq[1] = p1; b.sq[2] = p2; b.sq[3] = p3;
        p0 = b.sq[4]; p1 = b.sq[5]; p2 = b.sq[6]; p3 = b.sq[7];
        const u32 d4 = b.sqi[4], d5 = b.sqi[5], d6 = b.sqi[6], d7 = b.sqi[7];
        i32 a = b.env[0].a;
        const i32 r = b.env[0].r;
        for (u32 i = 0; i < count; ++i) {
            p0 += d4; p1 += d5; p2 += d6; p3 += d7;
            // -count of negative squares, -8..0: the bank is (8 + 2 sum) * 2^25
            const i32 sum = sh[i] + (static_cast<i32>(p0) >> 31) + (static_cast<i32>(p1) >> 31) + (static_cast<i32>(p2) >> 31) +
                            (static_cast<i32>(p3) >> 31);
            a = multiply(a, r);
            sh[i] = multiply((8 + 2 * sum) * (1 << 25), a) * 8;
        }
        b.sq[4] = p0; b.sq[5] = p1; b.sq[6] = p2; b.sq[7] = p3;
        b.env[0].a = a;
        band_high_add(b.band[0], b.pole[0], b.pc[0], sh, out, GQ28(0.8), count);
    } else for (u32 k = 0; k < 8; ++k) b.sq[k] += b.sqi[k] * count;
    if (b.g[0] && (!idle(b.env[1]) || b.band[1].y1 || b.band[1].y2))
        noise_band_add(b.env[1], b.seed, 0, b.band[1], b.pole[3], b.pc[2], out, b.g[0], count);
    if (b.g[1]) modes_add(b.mode, 3, out, count, b.g[1]);
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_bell(Body& b, i32* __restrict out, u32 count) {
    const i32 sq = b.k[2];
    const i32 pure = dsp::one - sq, clang = multiply(GQ31(0.7), sq);
    for (u32 i = 0; i < count; ++i) out[i] = 0;
    for (u32 w = 0; w < 2; ++w) {
        i32 slow = b.env[2 * w].a, fast = b.env[2 * w + 1].a;
        if (slow < 64 && fast < 64) continue;
        const i32 rs = b.env[2 * w].r, rf = b.env[2 * w + 1].r;
        const u32 inc = static_cast<u32>(w ? b.k[1] : b.k[0]), inc2 = times(inc, GQ28(1.48));
        u32 p0 = b.sq[2 * w], p1 = b.sq[2 * w + 1];
        for (u32 i = 0; i < count; ++i) {
            slow = multiply(slow, rs);
            fast = multiply(fast, rf);
            p0 += inc;
            p1 += inc2;
            const i32 s0 = dsp::sine(p0), s1 = dsp::sine(p1);
            // Each partial at half scale, so their sum (up to 1.8) stays in range.
            const i32 wave = ((multiply(s0, pure) + (s0 < 0 ? -clang : clang)) >> 1) +
                             (multiply(multiply(s1, pure) + (s1 < 0 ? -clang : clang), GQ31(0.8)) >> 1);
            out[i] += multiply(wave, slow + fast) * 2;
        }
        b.sq[2 * w] = p0; b.sq[2 * w + 1] = p1;
        b.env[2 * w].a = slow; b.env[2 * w + 1].a = fast;
    }
    band_run(b.band[0], out, count);
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_clap(Body& b, i32* __restrict out, u32 count) {
    i32 hand[64];
    i32 e0 = b.env[0].a, e1 = b.env[1].a, e2 = b.env[2].a;
    const i32 r0 = b.env[0].r, r1 = b.env[1].r, r2 = b.env[2].r, room = b.g[0];
    u32 x = b.seed;
    for (u32 i = 0; i < count; ++i) {
        e0 = multiply(e0, r0);
        e1 = multiply(e1, r1);
        e2 = multiply(e2, r2);
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const i32 n = static_cast<i32>(x) >> 2;
        hand[i] = multiply(n, e0 - e1) * 8;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        out[i] = hand[i] + gain(multiply(static_cast<i32>(x) >> 2, e2) * 8, room);
    }
    b.seed = x;
    b.env[0].a = e0; b.env[1].a = e1; b.env[2].a = e2;
    band_run(b.band[0], out, count);
    for (u32 i = 0; i < count; ++i) out[i] = out[i] * 2 + gain(hand[i], b.g[1]);
    high_run(b.pole[0], b.pc[0], out, count);
    high_run(b.pole[1], b.pc[0], out, count);
    knee_run(out, count, GQ28(0.6), GQ28(0.3), GQ28(1 / 0.3));
}
// The woodblock: its modes and its click, then a soft knee of its own: the
// click's loudest samples give up about 2 dB of peak (its burst of noise still
// reconstructs about 3 dB over its samples, which is what holds the block's level).
__attribute__((noinline)) inline void run_block(Body& b, i32* __restrict out, u32 count) {
    run_modes(b, out, count, 3);
    knee_run(out, count, GQ28(0.9), GQ28(0.45), GQ28(1 / 0.45));
}
__attribute__((noinline)) inline void run_shaker(Body& b, i32* __restrict out, u32 count) {
    i32 grain[64];
    i32 e0 = b.env[0].a, e1 = b.env[1].a;
    const i32 r0 = b.env[0].r, r1 = b.env[1].r;
    u32 x = b.seed;
    for (u32 i = 0; i < count; ++i) {
        e0 = multiply(e0, r0);
        e1 = multiply(e1, r1);
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        grain[i] = multiply(static_cast<i32>(x) >> 2, e0) * 8;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        out[i] = grain[i] + multiply(multiply(static_cast<i32>(x) >> 2, e1) * 8, GQ31(0.2));
    }
    b.seed = x;
    b.env[0].a = e0; b.env[1].a = e1;
    band_run(b.band[0], out, count);
    for (u32 i = 0; i < count; ++i) out[i] += grain[i] >> 3;
    modes_add(b.mode, 2, out, count, b.g[0]);
    high_run(b.pole[0], b.pc[0], out, count);
    // The loudest grains give up their peak (about 3 dB at the defaults), the
    // pour under them untouched, so the shaker sits level with the kit.
    knee_run(out, count, GQ28(0.16), GQ28(0.13), GQ28(1 / 0.13));
}

inline void run(Body& b, i32* out, u32 count) {
    using namespace kit;
    switch (b.model) {
    case kick: run_kick(b, out, count); break;
    case snare: run_snare(b, out, count); break;
    case tom: case fill: run_skins(b, out, count); break;
    case hand: run_hand(b, out, count); break;
    case rim: run_modes(b, out, count, 3); break;
    case hat: run_hat(b, out, count); break;
    case ride: run_ride(b, out, count); break;
    case bell: run_bell(b, out, count); break;
    case tine: run_modes(b, out, count, 6); break;
    case clock: run_modes(b, out, count, 7); break;
    case block: run_block(b, out, count); break;
    case clap: run_clap(b, out, count); break;
    case shaker: run_shaker(b, out, count); break;
    case ratchet: run_modes(b, out, count, 3); break;
    default: for (u32 i = 0; i < count; ++i) out[i] = 0; break;
    }
    b.now += count;
}
}
}
