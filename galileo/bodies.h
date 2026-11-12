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

// r^16: a per-sample decay's grain.
inline i32 per_grain(i32 r) {
    for (int i = 0; i < 4; ++i) r = multiply(r, r);
    return r;
}

// A membrane: up to three partials on one phase trajectory whose pitch rises
// with the strike (sweep) and falls back.
struct Skin {
    u32 ph[3];
    i32 ratio[3];      // Q28
    i32 share[3];      // each partial's strike, Q15
    Energy e[3];
    u32 inc, sweep_inc;
    i32 s, sr;         // the sweep envelope, Q31, and its decay a grain (16 samples)
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
    bool quiet() const { return e[0].a < 64 && (parts < 2 || e[1].a < 64) && (parts < 3 || e[2].a < 64); }
};
// Sets a skin: base log2 Hz, the sweep's depth (Q16 ratio of the base), its
// time constant's decay, the partials' ratios (Q28), decays and shares (Q15).
inline void skin(Skin& k, i32 log2_hz, i32 depth16, i32 sweep_r, u32 parts, const i32* ratios, const i32* decays,
                 const i32* shares, i32 vp) {
    k.inc = dsp::increment(log2_hz);
    k.sweep_inc = static_cast<u32>(multiply(static_cast<i32>(k.inc >> 1), (depth16 > 0 ? depth16 : 0) << 12)) << 4;
    k.sr = per_grain(sweep_r);
    k.parts = parts;
    k.vp = vp;
    for (u32 i = 0; i < parts; ++i) {
        k.ratio[i] = ratios[i];
        k.e[i].r = per_grain(decays[i]);   // a grain's (16 samples), as the skin runs
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
// late: the strike is due that many samples after now. A mode is linear, so
// the ring it starts then is started now already carried back: its sine at
// (1 - late) and -late steps of its pitch. From `late` on it rings exactly as
// if struck then (but for the decay over those few samples); before, it
// sounds a fraction of a millisecond early.
// g (Q28, 0 for none): the mode's level, put into the strike (a mode is
// linear) so it needs no scaling as it rings.
inline void ring(Struck& s, i32 v15, i32 amplitude15, u32 late = 0, i32 g28 = 0) {
    const i32 c = s.soft + dsp::m15(s.hard - s.soft, v15);
    i32 v = dsp::m15(amplitude15, c) << 13;
    if (g28) v = gain(v, g28);
    if (!late) {
        s.m.strike(v);
        return;
    }
    // Carried back, the ring is r^-late as loud: about 1 + late (1 - r^2) / 2.
    u32 lift = late * (static_cast<u32>(dsp::one - s.m.a2) >> 5);
    if (lift > 0x07ffffffu) lift = 0x07ffffffu;
    const i32 vl = v + multiply(v, static_cast<i32>(lift << 4));
    s.m.y1 = dsp::sat_add(s.m.y1, multiply(vl, dsp::sine(s.m.increment * (1u - late))));
    s.m.y2 = dsp::sat_add(s.m.y2, -multiply(vl, dsp::sine(s.m.increment * late)));
}
// The contact's width (Q8 samples) for a hardness h (Q31): (1.4 - 1.2 h)(1.25 - 0.6 v) ms.
inline void widths(i32 h, u32& w0, u32& w1) {
    i32 base = GQ16(1.4) - multiply(GQ16(1.2), h);                 // ms, Q16
    if (base < GQ16(0.02)) base = GQ16(0.02);
    const u32 samples_q8 = static_cast<u32>((base * 11290) >> 16);  // ms * 44.1 samples, Q8
    w0 = (samples_q8 * 5) >> 2;            // * 1.25
    w1 = (samples_q8 * 13) >> 4;           // * 0.65 (1.25 - 0.6)
}

// The grid impacts strike on (samples, a whole number of skin grains). A
// fill's strikes retune what may still ring (the next head up), so where they
// land sets how it carries on: a fill (its heads need whole grains) keeps 16.
// A tine retunes a set of modes too, but on the 32-sample grid: its new
// strike still rings from its 8-sample step, and only a ringing set's tail
// takes the new pitch up to 24 samples (0.5 ms) early.
const u32 step = 32, fill_step = 16;
inline u32 step_of(u32 model) { return model == kit::fill ? fill_step : step; }
// A pending strike the body schedules itself: a clap's reflections, a ratchet's teeth.
// They gather on the grid ahead, a word a 32-sample cell, 64 cells (46 ms) round
// a ring: the cell's force (Q15, the ghosts' together), its strongest
// one's force, and that one's 8-sample step in the cell (bits 30..31).
const u32 later_cells = 64;

struct Body {
    u32 model;
    i32 p[4];          // the controls prepared, Q31
    u32 used;          // a tine's steps whose contacts are kept (tine_contact)
    u32 seed;
    u32 count;         // strikes so far
    u32 now;           // samples since the note
    Skin skin[2];
    Struck mode[7];
    dsp::Band band[3];
    dsp::Pole pole[4];
    i32 pc[4];
    dsp::Click click[2];
    i32 cg[2];         // the clicks' gains, Q28
    Energy env[4];
    u32 sq[8], sqi[8];
    i32 g[8];          // mixing gains, Q28
    i32 k[4];          // model specifics
    u32 later[later_cells];   // the ghosts ahead, by cell (a tine keeps its contacts here)
    u32 ready;         // model, p and the coefficients prepare() set hold
    u32 due;           // cells ahead holding a ghost
    i32 tuned[2];      // a tine set's pitch as last tuned (log2 Hz), so a repeat isn't tuned again
    i32 held;          // the skins' last half-rate sample (expand)
};

// The hat's and the ride's square banks (ratios to the base, Q28), and a
// clap's early reflections: 11, 19 and 31 ms, at a half, a third and a quarter.
const i32 hat_ratio[6] = {GQ28(1), GQ28(1.37), GQ28(1.61), GQ28(1.83), GQ28(2.27), GQ28(2.61)};
const i32 ride_ratio[8] = {GQ28(1), GQ28(1.19), GQ28(1.43), GQ28(1.71), GQ28(2.03), GQ28(2.38), GQ28(2.93), GQ28(3.41)};
const u16 room_delay[3] = {485, 838, 1367};
const u16 room_level[3] = {GQ15(0.5), GQ15(0.35), GQ15(0.25)};

// A ratchet's tooth q (from 1) as prepare() lays it out: when (samples after
// the stroke, high half) and how hard (Q15, low half). Eleven at most, kept in
// words the ratchet has no other use for.
inline i32& tooth(Body& b, u32 i) { return i < 8 ? reinterpret_cast<i32&>(b.sqi[i]) : b.g[2 + (i - 8)]; }

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
    b.ready = 1;
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
        // The wires' band: broad (0.59 to 1.7 of its centre, a one-pole high-pass
        // and low-pass), from a dark 1.4 kHz to a bright 4.5 kHz, which keeps the
        // noise out of the head's low end.
        const i32 wires = dsp::mix(q16(1400), q16(4500), p3);
        b.pc[0] = dsp::pole(wires - OCT(1.0 / 0.59));
        b.pc[1] = dsp::pole(wires + OCT(1.7));
        prepare_click(b.click[0], 500, 3000);                               // the stick, and the
        b.cg[0] = GQ28(0.2);
        prepare_click(b.click[1], 2600, 3000);                              // crack, through one high-pass
        b.cg[1] = GQ28(0.05) + multiply(GQ28(0.45), p3);
        b.g[0] = GQ28(1.1) - multiply(GQ28(0.45), p1);                       // head
        b.g[1] = multiply(GQ28(1.75 * 1.35), p1);                            // wires (the band's 2.6 dB back)
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
        // The slap's ring: a resonator the noise drives (no zeros), its input
        // scaled by 2 sin w for unit gain at 1.8 kHz, and by the slap's gain.
        b.band[0].tune(q16(1800), OCT(1.2));
        b.g[1] = multiply(GQ28(1.4), p1);
        b.band[0].b = multiply(multiply(b.band[0].b, dsp::sine(dsp::increment(q16(1800)))) * 2, b.g[1] << 2) << 1;
        prepare_click(b.click[0], 400, 3000);
        b.cg[0] = GQ28(0.2);
        b.g[0] = GQ28(1) - multiply(GQ28(0.4), p1);
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
        // The bank sounds at half the rate (squares, band and high-pass), so its
        // corners are tuned an octave up of the full rate's.
        b.band[0].tune(q16(3800) + dsp::octave, OCT(0.7));
        b.pc[0] = dsp::pole(q16(1800) + dsp::octave);
        // The wash's band: 3.2 to 9.4 kHz around 5.5 (a one-pole high-pass and low-pass).
        b.pc[2] = dsp::pole(q16(3250));
        b.pc[3] = dsp::pole(q16(9350));
        u32 w0, w1;
        widths(GQ31(0.8), w0, w1);
        const i32 lbell = lb + OCT(2.1);
        // The bell's ping and its overtone two octaves up. (A third, 2.76 octaves
        // up at 10-20 kHz, reached the ear 35 dB under the ping through the
        // stick's contact: left out.)
        tune(b.mode[0], lbell, dsp::decay(tau + OCT(0.7)), 32767, w0, w1);
        tune(b.mode[1], lbell + OCT(2.0), dsp::decay(tau + OCT(0.45)), GQ15(0.4), w0, w1);
        b.g[0] = multiply(GQ28(0.6 * 1.35), p3);
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
        b.band[0].tune(lf + OCT(2.2) + dsp::octave, OCT(0.8));   // at half the rate (run_bell)
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
        // What every strike's set shares: its modes' decays and the contact's
        // widths; each scale step's contacts are worked out once a note (tine_contact).
        b.pc[0] = dsp::decay(tau);
        b.pc[1] = dsp::decay(tau + OCT(0.3));
        b.pc[2] = dsp::decay(tau + OCT(0.12));
        {
            u32 w0, w1;
            widths(p3, w0, w1);
            b.pc[3] = static_cast<i32>(w0 | w1 << 16);
        }
        b.used = 0;
        prepare_click(b.click[0], 300, 3000);
        b.cg[0] = multiply(GQ28(0.12), p3);
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
        {
            // A stroke drags the pawl over the teeth: 4 ms a tooth, closing up;
            // each tooth a little harder. Laid out once: when and how hard.
            const u32 teeth = static_cast<u32>(b.k[0]);
            const u32 span = 176 * teeth;
            for (u32 q = 1; q < teeth; ++q) {
                const i32 x = static_cast<i32>((q * table::reciprocal[teeth]) >> 1);       // q / teeth, Q15
                const u32 at = (span * static_cast<u32>(dsp::power(x, GQ16(1.3)))) >> 15;
                const i32 rise = GQ15(0.55) + dsp::m15(GQ15(0.45), static_cast<i32>((q * table::reciprocal[teeth - 1]) >> 1));
                tooth(b, q - 1) = static_cast<i32>(at << 16 | static_cast<u32>(rise));
            }
        }
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

// Whether a body's prepared coefficients can serve another note: its strikes
// don't retune it (a fill sets each head, a clap's and a shot's bursts and a
// tine's modes are set per strike).
inline bool keeps(u32 model) {
    return model != kit::fill && model != kit::clap && model != kit::shaker && model != kit::tine;
}
// Clears everything that rings or counts, keeping what prepare() set: a fresh
// note on a body prepared for the same model and controls.
inline void clear_state(Body& b) {
    b.used = b.count = b.now = 0;
    for (u32 i = 0; i < 2; ++i) {
        Skin& k = b.skin[i];
        for (u32 j = 0; j < 3; ++j) { k.ph[j] = 0; k.e[j].a = 0; }
        k.s = 0;
    }
    for (u32 i = 0; i < 7; ++i) b.mode[i].m.y1 = b.mode[i].m.y2 = 0;
    for (u32 i = 0; i < 3; ++i) b.band[i].reset();
    for (u32 i = 0; i < 4; ++i) b.pole[i].s = 0;
    for (u32 i = 0; i < 2; ++i) b.click[i].e = b.click[i].hp.s = 0;
    for (u32 i = 0; i < 4; ++i) b.env[i].a = 0;
    for (u32 i = 0; i < 8; ++i) b.sq[i] = 0;
    for (u32 i = 0; i < later_cells; ++i) b.later[i] = 0;
    b.due = 0;
    b.tuned[0] = b.tuned[1] = 0;
    b.held = 0;
}

// ----------------------------------------------------------------- strike --
inline void add_ghost(Body& b, u32 at, i32 v) {
    if (v <= 0) return;
    // Never a lap round the ring or more ahead (a ghost is at most 44 ms off).
    if (at - b.now >= (later_cells - 1) * step) at = b.now + (later_cells - 1) * step - 1;
    u32& w = b.later[(at / step) % later_cells];
    const i32 had = static_cast<i32>(w & 0x7fffu), top = static_cast<i32>((w >> 15) & 0x7fffu);
    if (!had) ++b.due;
    const u32 fine = (at % step) >> 3;
    w = v > top ? static_cast<u32>(dsp::together(had, v)) | static_cast<u32>(v) << 15 | fine << 30
                : static_cast<u32>(dsp::together(had, v)) | (w & ~0x7fffu);
}
// Samples from b.now (a cell's start) to the next cell holding a ghost, if it is
// within this cell or the next; else all ones.
inline u32 ghost_due(const Body& b) {
    if (!b.due) return 0xffffffffu;
    const u32 c = b.now / step;
    if (b.later[c % later_cells]) return 0;
    if (b.later[(c + 1) % later_cells]) return step;
    return 0xffffffffu;
}
// How hard a stroke reads: its force, 12% more or less at random, so no two
// strokes of a roll or a bounce sound alike (the contact, the pitch's rise).
inline i32 touch(const Body& b, i32 v15) {
    const u32 h = (b.count * 2654435761u) ^ (b.seed & 0xffffu);
    const i32 t = dsp::m15(v15, 28835 + static_cast<i32>((h >> 19) & 8191u));   // 0.88 .. 1.13
    return t > 32767 ? 32767 : t;
}
inline void strike_modes(Body& b, u32 count, i32 v15, i32 vp, i32 tone15, u32 late, i32 g28 = 0) {
    const i32 a = dsp::power(v15, vp);
    for (u32 i = 0; i < count; ++i) ring(b.mode[i], tone15, a, late, g28);
}

// A tine's contacts (Q15, soft and hard, for its three modes) at scale step
// `steps` (0..14) of its key, worked out at the step's first strike in a note
// and kept, so a run over the same frets divides only once a step. Kept in
// the ghost slots a tine has no use for; b.used marks the steps done.
inline const u16* tine_contact(Body& b, u32 steps, i32 lf) {
    u16* c = reinterpret_cast<u16*>(b.later) + steps * 6;
    if (!(b.used & (1u << steps))) {
        b.used |= 1u << steps;
        const u32 w = static_cast<u32>(b.pc[3]), w0 = w & 0xffffu, w1 = w >> 16;
        const i32 at[3] = {lf, lf + OCT(2.756), lf + OCT(5.404)};
        for (u32 k = 0; k < 3; ++k) {
            if (k == 2 && at[k] >= dsp::log2_cap_hz) { c[4] = c[5] = 0; continue; }   // no glint there
            const u32 inc = dsp::increment(at[k]);
            c[2 * k] = static_cast<u16>(dsp::contact(inc, w0));
            c[2 * k + 1] = static_cast<u16>(dsp::contact(inc, w1));
        }
    }
    return c;
}
// tune() with the contacts given.
inline void tine_tune(Struck& s, i32 log2_hz, i32 r, i32 share, u32 soft, u32 hard) {
    s.m.tune(dsp::increment(log2_hz), r);
    s.soft = dsp::m15(share, static_cast<i32>(soft));
    s.hard = dsp::m15(share, static_cast<i32>(hard));
}
// An impact at sample b.now + late (the struck modes ring from then on, the
// rest strikes at b.now); ghost: a strike the body scheduled itself.
inline void strike(Body& b, i32 v15, u32 kind, u32 step, u32 n, bool ghost = false, u32 late = 0) {
    using namespace kit;
    if (v15 <= 0) return;
    ++b.count;
    const i32 vt = touch(b, v15);
    switch (b.model) {
    case kick:
        b.skin[0].strike(v15, vt);
        strike_modes(b, 2, v15, GQ16(1.3), vt, late, b.g[0]);
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
        strike_modes(b, 3, v15, GQ16(1.1), vt, late);
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
        strike_modes(b, 2, v15, GQ16(1.1), vt, late, b.g[1]);
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
        // Retuning a set only when its pitch moves.
        const i32 key = lf ^ (b.k[2] << 3) ^ (h >> 7);
        if (b.tuned[set ? 1 : 0] != key) {
            b.tuned[set ? 1 : 0] = key;
            const u16* c = tine_contact(b, steps, lf);
            tine_tune(b.mode[set], lf, b.pc[0], 32767, c[0], c[1]);
            tine_tune(b.mode[set + 1], lf + OCT(2.756), b.pc[1], dsp::m15(GQ15(0.32), GQ15(0.3) + (h >> 16)), c[2], c[3]);
            // The tine's glint, 5.4 octaves up: over 0.45 of the rate it would only
            // sit at the cap (and its contact leaves it 30 dB under): left out there.
            const i32 glint = lf + OCT(5.404);
            if (glint < dsp::log2_cap_hz) {
                tine_tune(b.mode[set + 2], glint, b.pc[2], dsp::m15(GQ15(0.15), h >> 16), c[4], c[5]);
            } else {
                Struck& off = b.mode[set + 2];
                off.m.y1 = off.m.y2 = 0;
                off.soft = off.hard = 0;
            }
        }
        const i32 a = dsp::power(v15, GQ16(1.1));
        for (u32 i = 0; i < 3; ++i) ring(b.mode[set + i], vt, a, late);
        b.click[0].strike(v15);
        break;
    }
    case clock: {
        const u32 set = (b.count & 1) ? 0 : 2;   // tick, tock, tick...
        const i32 a = dsp::power(v15, GQ16(1.2));
        ring(b.mode[set], vt, a, late);
        ring(b.mode[set + 1], vt, a, late);
        const i32 c = dsp::power(v15, GQ16(1.1));
        for (u32 i = 4; i < 7; ++i) ring(b.mode[i], vt, c, late, b.g[0]);
        b.click[0].strike(v15);
        break;
    }
    case block:
        strike_modes(b, 3, v15, GQ16(1.1), vt, late);
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
                add_ghost(b, b.now + late + ((room_delay[i] * static_cast<u32>(b.k[0])) >> 16),
                          dsp::m15(dsp::m15(v15, room_level[i]), b.k[1] >> 16));
        }
        break;
    }
    case shaker:
        b.env[0].r = b.k[2] + multiply(b.k[3] - b.k[2], vt << 16);           // harder: a shorter tick
        b.env[0].a += v15 << 13;
        if (b.env[0].a > (3 << 28)) b.env[0].a = 3 << 28;
        b.env[1].strike(dsp::m15(v15, GQ15(0.6)));
        strike_modes(b, 2, v15 >> 1, GQ16(1.0), vt >> 1, late, b.g[0]);
        break;
    case ratchet:
        if (!ghost) {
            // A stroke drags the pawl over the teeth (laid out by prepare()).
            const u32 teeth = static_cast<u32>(b.k[0]);
            for (u32 q = 1; q < teeth; ++q) {
                const u32 t = static_cast<u32>(tooth(b, q - 1));
                add_ghost(b, b.now + late + (t >> 16), dsp::m15(v15, static_cast<i32>(t & 0xffffu)));
            }
            b.skin[0].strike(v15, vt);
            v15 = dsp::m15(v15, GQ15(0.55));
        }
        strike_modes(b, 3, v15, GQ16(1.1), touch(b, v15), late, b.g[0]);
        b.click[0].strike(v15);
        break;
    default: break;
    }
    (void)kind;
}
// Plays the ghosts of the cell at b.now as one strike (their forces together),
// its modes from the strongest one's 8-sample step.
__attribute__((noinline)) void strike_ghosts(Body& b, u32 n) {
    u32& w = b.later[(b.now / step) % later_cells];
    const i32 v = static_cast<i32>(w & 0x7fffu);
    const u32 late = (w >> 30) << 3;
    w = 0;
    if (!v) return;
    --b.due;
    if (b.model == kit::ratchet) {
        // Teeth: what strike() does for one, without its dispatch.
        ++b.count;
        strike_modes(b, 3, v, GQ16(1.1), touch(b, v), late, b.g[0]);
        b.click[0].strike(v);
    } else {
        strike(b, v, 0, 0, n, true, late);
    }
}

// -------------------------------------------------------------------- run --
// Each part runs a whole stretch of samples in its own tight loop, its state in
// locals, writing or adding into a buffer: cheaper on the Blackfin than one loop
// that steps every part each sample. A part with nothing ringing is skipped.

// A skin's pitch sweep and its partials' levels are planned every 16 samples
// and ramp straight between: the pitch follows its curve to a sixteenth of a
// step, and a partial costs a phase, two ramps and a sine a sample.
const u32 grain = 16;
// The sweep's curve, ~ s^1.3.
inline i32 sweep_curve(i32 s) { return multiply(s, GQ31(0.6) + multiply(s, GQ31(0.4))); }
// An interpolated sine of a 32-bit phase (dsp::sine, its fraction by shifts).
inline i32 sine_of(u32 x) {
    const i32* t = table::sine + (x >> 23);
    const i32 lo = t[0];
    return lo + multiply(t[1] - lo, static_cast<i32>((x << 9) >> 1));
}
// A skin runs at half the rate: its partials are all under about 3 kHz, swept
// or not, so a grain is 8 of its samples, each two of the block's, and the
// sum comes back to the full rate by straight lines between them (expand).
const u32 hgrain = grain / 2;
// One partial over m half-rate samples (m a whole number of half grains): its
// increment at each grain's edge (inc[0] at the start, full-rate, scaled by
// ratio when given; SWEEP when they move), its level decaying by r16 a
// grain, heard at gain g (Q28, 0 for none). Writes out, or adds to it (ADD);
// swing gets |sine| (SWING).
template <bool ADD, bool SWING, bool SWEEP>
__attribute__((noinline)) void partial_run(u32& phase, i32& level, i32 r16, const u32* __restrict inc, i32 ratio28,
                                           i32 g28, i32* __restrict out, i32* __restrict swing, u32 m) {
    u32 p = phase;
    i32 a = level;
    u32 now = (ratio28 ? times(inc[0], ratio28) : inc[0]) << 1;
    for (u32 g = 0; g < m; g += hgrain) {
        const u32 next = SWEEP ? (ratio28 ? times(inc[g / hgrain + 1], ratio28) : inc[g / hgrain + 1]) << 1 : now;
        const i32 step = static_cast<i32>(next - now) >> 3;
        const i32 end = multiply(a, r16);
        i32 l = g28 ? gain(a, g28) : a;
        const i32 dl = ((g28 ? gain(end, g28) : end) - l) >> 3;
        i32* __restrict o = out + g;
        for (u32 i = 0; i < hgrain; ++i) {
            if (SWEEP) now += static_cast<u32>(step);
            p += now;
            l += dl;
            const i32 v = sine_of(p);
            const i32 y = multiply(v, l);
            if (ADD) o[i] += y;
            else o[i] = y;
            if (SWING) swing[g + i] = v < 0 ? -v : v;
        }
        now = next;
        a = end;
    }
    phase = p;
    level = a;
}
template <bool ADD, bool SWING>
inline void partial(bool sweep, u32& phase, i32& level, i32 r16, const u32* inc, i32 ratio28, i32 g28, i32* out, i32* swing,
                    u32 m) {
    if (sweep) partial_run<ADD, SWING, true>(phase, level, r16, inc, ratio28, g28, out, swing, m);
    else partial_run<ADD, SWING, false>(phase, level, r16, inc, ratio28, g28, out, swing, m);
}
// A skin at half the rate: writes m samples (a whole number of half grains),
// or adds them (add), at gain g (Q28) if given. swing, if given, gets |sine|
// of the fundamental (Q31), at the half rate too, for the snare's wires. A
// partial that has died away only keeps its phase.
inline void skin_half(Skin& k, i32* __restrict out, u32 m, i32* __restrict swing = 0, i32 g28 = 0, bool add = false) {
    u32 inc[64 / grain + 1];
    const u32 grains = m / hgrain;
    i32 s = k.s;
    if (s > 4194304 && k.sweep_inc) {                       // sweeping (above 0.002)
        const i32 sr16 = k.sr;
        const u32 base = k.inc, sweep = k.sweep_inc;
        inc[0] = base + static_cast<u32>(multiply(static_cast<i32>(sweep), sweep_curve(s)));
        for (u32 g = 1; g <= grains; ++g) {
            s = multiply(s, sr16);
            inc[g] = base + static_cast<u32>(multiply(static_cast<i32>(sweep), sweep_curve(s)));
        }
        k.s = s;
    } else {
        k.s = 0;
        for (u32 g = 0; g <= grains; ++g) inc[g] = k.inc;
    }
    if (!k.parts) {
        if (!add) for (u32 i = 0; i < m; ++i) out[i] = 0;
        return;
    }
    const bool sweeping = k.s != 0;
    if (swing) partial<false, true>(sweeping, k.ph[0], k.e[0].a, k.e[0].r, inc, 0, g28, out, swing, m);
    else if (add) partial<true, false>(sweeping, k.ph[0], k.e[0].a, k.e[0].r, inc, 0, g28, out, 0, m);
    else partial<false, false>(sweeping, k.ph[0], k.e[0].a, k.e[0].r, inc, 0, g28, out, 0, m);
    for (u32 part = 1; part < k.parts; ++part) {
        if (k.e[part].a < 4096) {                           // under -95 dBFS
            k.e[part].a = 0;
            u32 p = k.ph[part];
            for (u32 g = 0; g < grains; ++g) {
                const u32 a = times(inc[g], k.ratio[part]), b = times(inc[g + 1], k.ratio[part]);
                p += a * grain + static_cast<u32>(static_cast<i32>(b - a) >> 4) * (grain * (grain + 1) / 2);
            }
            k.ph[part] = p;
            continue;
        }
        partial<true, false>(sweeping, k.ph[part], k.e[part].a, k.e[part].r, inc, k.ratio[part], g28, out, 0, m);
    }
}
// m half-rate samples h back to the full rate: each one, after the midpoint
// between it and the one before (last, kept across stretches). Written, or
// added (ADD).
template <bool ADD>
__attribute__((noinline)) void expand(const i32* __restrict h, u32 m, i32& last, i32* __restrict out) {
    i32 y0 = last;
    for (u32 i = 0; i < m; ++i) {
        const i32 y = h[i];
        const i32 mid = (y0 >> 1) + (y >> 1);
        if (ADD) {
            out[2 * i] += mid;
            out[2 * i + 1] += y;
        } else {
            out[2 * i] = mid;
            out[2 * i + 1] = y;
        }
        y0 = y;
    }
    last = y0;
}
// A click's noise burst, high-passed, added at gain g (Q28).
__attribute__((noinline)) void click_run(dsp::Click& c, u32& seed, i32* __restrict out, u32 n, i32 g28) {
    i32 e = c.e, h = c.hp.s;
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
// r^8: a per-sample decay over 8 samples.
inline i32 per_eight(i32 r) {
    for (int i = 0; i < 3; ++i) r = multiply(r, r);
    return r;
}
// A click under unity gain (g < 1, Q28): the gain rides on the burst's level,
// so the sample needs no scaling (n a multiple of 8).
__attribute__((noinline)) void click_soft_run(dsp::Click& c, u32& seed, i32* __restrict out, u32 n, i32 g28) {
    i32 s = multiply(c.e, g28 << 3), h = c.hp.s;
    const i32 r = c.r, cc = c.c;
    u32 x = seed;
    for (u32 i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        s = multiply(s, r);
        const i32 in = multiply(static_cast<i32>(x), s) * 2;
        h += multiply(in - h, cc);
        out[i] = dsp::sat_add(out[i], in - h);
    }
    seed = x;
    // Its own level, decayed the same way 8 samples at a time.
    const i32 r8 = per_eight(r);
    i32 e = c.e;
    for (u32 g = 0; g < n; g += 8) e = multiply(e, r8);
    if (e < 16 && h < 4096 && h > -4096) e = h = 0;
    c.e = e;
    c.hp.s = h;
}
// Idle, it costs nothing.
inline void click_add(dsp::Click& c, u32& seed, i32* __restrict out, u32 n, i32 g28) {
    if (!c.e && !c.hp.s) return;
    if (g28 < (1 << 28)) click_soft_run(c, seed, out, n, g28);
    else click_run(c, seed, out, n, g28);
}
// Two clicks on one burst of noise and one high-pass (a's), each at its own
// width and gain (Q28, under 1), added: n a multiple of 8.
__attribute__((noinline)) void click_pair_run(dsp::Click& a, dsp::Click& b, u32& seed, i32* __restrict out, u32 n,
                                              i32 ga28, i32 gb28) {
    i32 sa = multiply(a.e, ga28 << 3), sb = multiply(b.e, gb28 << 3), h = a.hp.s;
    const i32 ra = a.r, rb = b.r, c = a.c;
    u32 x = seed;
    for (u32 i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        sa = multiply(sa, ra);
        sb = multiply(sb, rb);
        const i32 in = multiply(static_cast<i32>(x), sa + sb) * 2;
        h += multiply(in - h, c);
        out[i] = dsp::sat_add(out[i], in - h);
    }
    seed = x;
    // Their own levels, decayed the same way 8 samples at a time.
    const i32 ra8 = per_eight(ra), rb8 = per_eight(rb);
    i32 ea = a.e, eb = b.e;
    for (u32 g = 0; g < n; g += 8) { ea = multiply(ea, ra8); eb = multiply(eb, rb8); }
    if (ea < 16 && eb < 16 && h < 4096 && h > -4096) ea = eb = h = 0;
    a.e = ea;
    b.e = eb;
    a.hp.s = h;
}
inline void click_pair_add(dsp::Click& a, dsp::Click& b, u32& seed, i32* __restrict out, u32 n, i32 ga28, i32 gb28) {
    if (a.e || b.e || a.hp.s) click_pair_run(a, b, seed, out, n, ga28, gb28);
}
// Saturating a - b, for b never -2^31 (a product with a factor under 1).
inline i32 sat_sub(i32 a, i32 b) { return dsp::sat_add(a, -b); }
// dsp::resonate.
inline i32 resonate(i32 a1, i32 y1, i32 a2, i32 y2) { return sat_sub(dsp::twice(multiply(a1, y1)), multiply(a2, y2)); }
// One struck mode added into out (n even), each step two samples so the
// state swaps without moves.
__attribute__((noinline)) void modes_one(dsp::Mode& m0, i32* __restrict out, u32 n) {
    i32 p1 = m0.y1, p2 = m0.y2;
    const i32 a1 = m0.a1, a2 = m0.a2;
    for (u32 i = 0; i < n; i += 2) {
        p2 = resonate(a1, p1, a2, p2);
        out[i] = dsp::sat_add(out[i], p2);
        p1 = resonate(a1, p2, a2, p1);
        out[i + 1] = dsp::sat_add(out[i + 1], p1);
    }
    m0.y1 = p1; m0.y2 = p2;
}
// Struck modes, added (n even), each at the level its strikes gave it. A mode
// that has died away is cleared, so it is skipped.
inline void modes_add(Struck* m, u32 count, i32* __restrict out, u32 n) {
    for (u32 k = 0; k < count; ++k) {
        dsp::Mode& o = m[k].m;
        if (!o.y1 && !o.y2) continue;
        modes_one(o, out, n);
        if (o.y1 < 256 && o.y1 > -256 && o.y2 < 256 && o.y2 > -256) o.y1 = o.y2 = 0;
    }
}
// Noise under an envelope (noise std about 1.15 units), written.
__attribute__((noinline)) void noise_env(Energy& env, u32& seed, i32* __restrict out, u32 n) {
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
// A band-pass in place (n even), two samples a turn so the state swaps without moves.
__attribute__((noinline)) void band_run(dsp::Band& f, i32* __restrict io, u32 n) {
    i32 x1 = f.x1, x2 = f.x2, y1 = f.y1, y2 = f.y2;
    const i32 b = f.b, a1 = f.a1, a2 = f.a2;
    for (u32 i = 0; i < n; i += 2) {
        const i32 xa = io[i];
        y2 = multiply(b, xa - x2) + multiply(a1, y1) * 2 - multiply(a2, y2);
        x2 = xa;
        io[i] = y2;
        const i32 xb = io[i + 1];
        y1 = multiply(b, xb - x1) + multiply(a1, y2) * 2 - multiply(a2, y1);
        x1 = xb;
        io[i + 1] = y1;
    }
    f.x1 = x1; f.x2 = x2; f.y1 = y1; f.y2 = y2;
}
__attribute__((noinline)) void high_run(dsp::Pole& p, i32 c, i32* __restrict io, u32 n) {
    i32 s = p.s;
    for (u32 i = 0; i < n; ++i) { s += multiply(io[i] - s, c); io[i] -= s; }
    p.s = s;
}
__attribute__((noinline)) void low_run(dsp::Pole& p, i32 c, i32* __restrict io, u32 n) {
    i32 s = p.s;
    for (u32 i = 0; i < n; ++i) { s += multiply(io[i] - s, c); io[i] = s; }
    p.s = s;
}
__attribute__((noinline)) void add_gain(i32* __restrict out, const i32* __restrict in, i32 g28, u32 n) {
    for (u32 i = 0; i < n; ++i) out[i] += gain(in[i], g28);
}
inline bool idle(const Energy& e) { return e.a < 64; }
// A one-pole high-pass of in, added into out at gain g (Q28).
__attribute__((noinline)) void high_add(dsp::Pole& p, i32 c, const i32* __restrict in, i32* __restrict out, i32 g28, u32 n) {
    i32 h = p.s;
    for (u32 i = 0; i < n; ++i) {
        const i32 y = in[i];
        h += multiply(y - h, c);
        out[i] += gain(y - h, g28);
    }
    p.s = h;
}
// A band-pass of io (in place), then a one-pole high-pass, added into out at gain g (Q28).
inline void band_high_add(dsp::Band& f, dsp::Pole& p, i32 c, i32* __restrict io, i32* __restrict out, i32 g28, u32 n) {
    band_run(f, io, n);
    high_add(p, c, io, out, g28, n);
}
// Noise under an envelope driving a two-pole resonator (f's a1 and a2, its
// input scaled by f.b), added into out (n even): two samples a turn, so the
// state swaps without moves.
__attribute__((noinline)) void noise_ring_add(Energy& env, u32& seed, dsp::Band& f, i32* __restrict out, u32 n) {
    i32 a = env.a, y1 = f.y1, y2 = f.y2;
    const i32 r = env.r, b = f.b, a1 = f.a1, a2 = f.a2;
    u32 x = seed;
    for (u32 i = 0; i < n; i += 2) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        a = multiply(a, r);
        y2 = multiply(b, multiply(static_cast<i32>(x) >> 2, a) * 8) + multiply(a1, y1) * 2 - multiply(a2, y2);
        out[i] += y2;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        a = multiply(a, r);
        y1 = multiply(b, multiply(static_cast<i32>(x) >> 2, a) * 8) + multiply(a1, y2) * 2 - multiply(a2, y1);
        out[i + 1] += y1;
    }
    seed = x;
    env.a = a;
    if (a < 64 && y1 < 4096 && y1 > -4096 && y2 < 4096 && y2 > -4096) y1 = y2 = 0;
    f.y1 = y1;
    f.y2 = y2;
}
// r^n: a per-sample decay over n samples.
inline i32 decay_over(i32 r, u32 n) {
    i32 y = dsp::one;
    for (; n; n >>= 1) {
        if (n & 1) y = multiply(y, r);
        r = multiply(r, r);
    }
    return y;
}
// Noise under an envelope (swelling with a half-rate swing if SWELL, as
// 0.55 + 0.45 swing, which it overwrites) through a one-pole high-pass at ch and a one-pole
// low-pass at cl, a broad band, added into out at gain g (Q28, under 2.5): one
// pass. The gain rides on the envelope's level; the level itself is decayed
// once, over the stretch, at the end.
template <bool SWELL>
__attribute__((noinline)) void noise_hl_add(Energy& env, u32& seed, i32* __restrict swing, dsp::Pole& hp, dsp::Pole& lp,
                                            i32 ch, i32 cl, i32* __restrict out, i32 g28, u32 n) {
    i32 ag = multiply(env.a, g28) << 3, h = hp.s, l = lp.s;
    const i32 r = env.r, r2 = multiply(r, r);
    u32 x = seed;
    if (SWELL) {
        // The level each two samples (the swing's rate) first, in place of the
        // swing; then the noise, two samples a turn.
        const u32 m = n >> 1;
        for (u32 j = 0; j < m; ++j) {
            swing[j] = multiply(ag, GQ31(0.55) + multiply(GQ31(0.45), swing[j]));
            ag = multiply(ag, r2);
        }
        for (u32 i = 0; i < n; ++i) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            const i32 v = multiply(static_cast<i32>(x) >> 2, swing[i >> 1]) * 8;
            h += multiply(v - h, ch);
            l += multiply(v - h - l, cl);
            out[i] += l;
        }
    } else {
        for (u32 i = 0; i < n; ++i) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            ag = multiply(ag, r);
            const i32 v = multiply(static_cast<i32>(x) >> 2, ag) * 8;
            h += multiply(v - h, ch);
            l += multiply(v - h - l, cl);
            out[i] += l;
        }
    }
    seed = x;
    const i32 a = multiply(env.a, decay_over(r, n));
    if (a < 64 && l < 4096 && l > -4096) h = l = 0;
    env.a = a;
    hp.s = h;
    lp.s = l;
}

// The largest |x| of n samples, unsigned (so |-2^31| is 2^31).
__attribute__((noinline)) u32 peak_of(const i32* __restrict x, u32 n) {
    u32 most = 0;
    for (u32 i = 0; i < n; ++i) {
        const u32 v = static_cast<u32>(x[i]);
        const u32 a = x[i] < 0 ? 0u - v : v;
        most = a > most ? a : most;
    }
    return most;
}
// A soft knee, the body's own limiter: straight up to k, then bending towards
// k + h (units 2^28; inv_h28 is 1 / h in Q28). A hit's first milliseconds give
// up their peak, so its body sounds louder under the same ceiling; softer
// strokes pass untouched.
__attribute__((noinline)) void knee_bend(i32* __restrict io, u32 n, i32 k, i32 h, i32 inv_h28) {
    for (u32 i = 0; i < n; ++i) {
        const i32 x = io[i];
        const i32 a = x < 0 ? -x : x;
        if (a <= k) continue;
        const i32 y = k + multiply(dsp::tanh(gain(a - k, inv_h28)), h);
        io[i] = x < 0 ? -y : y;
    }
}
// The bend's search only where some sample is over the knee.
inline void knee_run(i32* __restrict io, u32 n, i32 k, i32 h, i32 inv_h28) {
    if (peak_of(io, n) > static_cast<u32>(k)) knee_bend(io, n, k, h, inv_h28);
}

// The kick head's saturation: x driven by g (Q28), through a cubic soft clip
// (u - 4/27 u^3, held at 1 from u = 1.5; tanh's shape, a little firmer), at
// the makeup gain (Q28). In place.
__attribute__((noinline)) void kick_drive(i32* __restrict io, u32 n, i32 g28, i32 makeup) {
    const i32 hi = GQ28(1.5), lo = -GQ28(1.5);
    for (u32 i = 0; i < n; ++i) {
        i32 u = gain(io[i], g28);
        u = u > hi ? hi : u < lo ? lo : u;
        const i32 sq = multiply(u << 2, u << 1);                  // u^2, units 2^28
        const i32 y = u - multiply(u, multiply(sq << 2, GQ31(4.0 / 27.0)) << 1);
        io[i] = multiply(y, makeup) << 3;
    }
}
__attribute__((noinline)) inline void run_kick(Body& b, i32* __restrict out, u32 count) {
    i32 h[32];
    skin_half(b.skin[0], h, count / 2);
    expand<false>(h, count / 2, b.held, out);
    modes_add(b.mode, 2, out, count);   // at the attack's level, from their strikes
    kick_drive(out, count, b.k[0], b.k[1]);
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_snare(Body& b, i32* __restrict out, u32 count) {
    i32 h[32], swing[32];
    skin_half(b.skin[0], h, count / 2, swing, b.g[0]);   // the head at its level
    expand<false>(h, count / 2, b.held, out);
    if (!idle(b.env[0]) || b.pole[1].s) {
        // The wires buzz with the head: noise, swelling with the head's swing.
        noise_hl_add<true>(b.env[0], b.seed, swing, b.pole[0], b.pole[1], b.pc[0], b.pc[1], out, b.g[1], count);
    }
    // The stick's click and the crack: one burst of noise, one high-pass.
    click_pair_add(b.click[0], b.click[1], b.seed, out, count, b.cg[0], b.cg[1]);
    knee_run(out, count, GQ28(1.2), GQ28(0.6), GQ28(1 / 0.6));
}
__attribute__((noinline)) inline void run_skins(Body& b, i32* __restrict out, u32 count) {   // tom, fill
    i32 h[32];
    skin_half(b.skin[0], h, count / 2);
    if (b.model == kit::fill) skin_half(b.skin[1], h, count / 2, 0, 0, true);
    expand<false>(h, count / 2, b.held, out);
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_hand(Body& b, i32* __restrict out, u32 count) {
    i32 h[32];
    skin_half(b.skin[0], h, count / 2, 0, b.g[0]);
    expand<false>(h, count / 2, b.held, out);
    if (!idle(b.env[0]) || b.band[0].y1 || b.band[0].y2) noise_ring_add(b.env[0], b.seed, b.band[0], out, count);
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_modes(Body& b, i32* __restrict out, u32 count, u32 modes) {   // rim, block, tine (6), clock (7), ratchet
    for (u32 i = 0; i < count; ++i) out[i] = 0;
    if (b.model == kit::clock) {
        modes_add(b.mode, 4, out, count);
        modes_add(b.mode + 4, 3, out, count);   // the case, at its level from the strikes
    } else if (b.model == kit::ratchet) {
        modes_add(b.mode, 3, out, count);       // at their level from the strikes
    } else {
        modes_add(b.mode, modes, out, count);
    }
    // The shell, at its level, added (silent at 0, but ringing on).
    if (b.model == kit::rim || b.model == kit::ratchet) {
        const i32 level = b.g[b.model == kit::ratchet ? 1 : 0];
        i32 h[32];
        skin_half(b.skin[0], h, count / 2, 0, level);
        if (level) expand<true>(h, count / 2, b.held, out);
    }
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
// Three squares, each +-2^26 by the sign of its phase, plus `offset`: written,
// or added (ADD). Two passes make the hat's six (each pass's registers its own).
template <bool ADD>
__attribute__((noinline)) void squares3(u32* __restrict phase, const u32* __restrict inc, i32 offset, i32* __restrict out, u32 n) {
    u32 p = phase[0], q = phase[1], r = phase[2];
    const u32 d = inc[0], e = inc[1], f = inc[2];
    for (u32 i = 0; i < n; ++i) {
        p += d;
        q += e;
        r += f;
        const i32 v = ((static_cast<i32>(p) >> 31) + (static_cast<i32>(q) >> 31) + (static_cast<i32>(r) >> 31)) * (1 << 26) + offset;
        if (ADD) out[i] += v;
        else out[i] = v;
    }
    phase[0] = p;
    phase[1] = q;
    phase[2] = r;
}
// six squares of 2^25: about 0.75 of unit at most
inline void bank6(u32* __restrict phase, const u32* __restrict inc, i32* __restrict out, u32 n) {
    squares3<false>(phase, inc, 3 << 25, out, n);
    squares3<true>(phase + 3, inc + 3, 3 << 25, out, n);
}
// The hat's metal and hiss under its envelope: (bank g0 + noise g1) a.
__attribute__((noinline)) void hat_mix(Energy& env, u32& seed, i32 g0, i32 g1, i32* __restrict io, u32 n) {
    i32 a = env.a;
    const i32 r = env.r;
    u32 x = seed;
    for (u32 i = 0; i < n; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        a = multiply(a, r);
        io[i] = multiply(multiply(io[i], g0) + multiply(static_cast<i32>(x) >> 3, g1), a) * 8;
    }
    seed = x;
    env.a = a;
}
// Two one-pole high-passes at c, then a zero at Nyquist, no pole, times 4:
// out = (y + last) 4.
__attribute__((noinline)) void hat_finish(dsp::Pole& p1, dsp::Pole& p2, i32 c, dsp::Pole& z, const i32* __restrict in,
                                          i32* __restrict out, u32 n) {
    i32 s1 = p1.s, s2 = p2.s, last = z.s;
    for (u32 i = 0; i < n; ++i) {
        i32 y = in[i];
        s1 += multiply(y - s1, c);
        y -= s1;
        s2 += multiply(y - s2, c);
        y -= s2;
        out[i] = (y + last) * 4;
        last = y;
    }
    p1.s = s1; p2.s = s2; z.s = last;
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
    bank6(b.sq, b.sqi, src, count);
    hat_mix(b.env[0], b.seed, b.g[0], b.g[1], src, count);
    band_run(b.band[0], src, count);
    // Two high-passes, then a zero at Nyquist, no pole: the hiss and the bank's
    // aliases darkened above 10 kHz.
    hat_finish(b.pole[1], b.pole[2], b.pc[1], b.pole[0], src, out, count);
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
__attribute__((noinline)) inline void run_ride(Body& b, i32* __restrict out, u32 count) {
    i32 sh[32];
    const u32 m = count / 2;
    if (!idle(b.env[0]) || b.band[0].y1 || b.band[0].y2) {
        // The bank at half the rate, in two passes of four squares, the second
        // applying the envelope (its decay squared, a step of two samples).
        u32 p0 = b.sq[0], p1 = b.sq[1], p2 = b.sq[2], p3 = b.sq[3];
        const u32 d0 = b.sqi[0] * 2, d1 = b.sqi[1] * 2, d2 = b.sqi[2] * 2, d3 = b.sqi[3] * 2;
        for (u32 i = 0; i < m; ++i) {
            p0 += d0; p1 += d1; p2 += d2; p3 += d3;
            sh[i] = (static_cast<i32>(p0) >> 31) + (static_cast<i32>(p1) >> 31) + (static_cast<i32>(p2) >> 31) +
                    (static_cast<i32>(p3) >> 31);
        }
        b.sq[0] = p0; b.sq[1] = p1; b.sq[2] = p2; b.sq[3] = p3;
        p0 = b.sq[4]; p1 = b.sq[5]; p2 = b.sq[6]; p3 = b.sq[7];
        const u32 d4 = b.sqi[4] * 2, d5 = b.sqi[5] * 2, d6 = b.sqi[6] * 2, d7 = b.sqi[7] * 2;
        i32 a = b.env[0].a;
        const i32 r = multiply(b.env[0].r, b.env[0].r);
        for (u32 i = 0; i < m; ++i) {
            p0 += d4; p1 += d5; p2 += d6; p3 += d7;
            // -count of negative squares, -8..0: the bank is (8 + 2 sum) * 2^25, at 0.8
            const i32 sum = sh[i] + (static_cast<i32>(p0) >> 31) + (static_cast<i32>(p1) >> 31) + (static_cast<i32>(p2) >> 31) +
                            (static_cast<i32>(p3) >> 31);
            a = multiply(a, r);
            sh[i] = multiply((8 + 2 * sum) * GQ28(0.8 / 8), a) * 8;
        }
        b.sq[4] = p0; b.sq[5] = p1; b.sq[6] = p2; b.sq[7] = p3;
        b.env[0].a = a;
        band_run(b.band[0], sh, m);
        high_run(b.pole[0], b.pc[0], sh, m);
        expand<false>(sh, m, b.held, out);
    } else {
        for (u32 k = 0; k < 8; ++k) b.sq[k] += b.sqi[k] * count;
        for (u32 i = 0; i < count; ++i) out[i] = 0;
    }
    if (b.g[0] && (!idle(b.env[1]) || b.pole[2].s))
        noise_hl_add<false>(b.env[1], b.seed, 0, b.pole[3], b.pole[2], b.pc[2], b.pc[3], out, b.g[0], count);
    if (b.g[1]) modes_add(b.mode, 2, out, count);   // the bell, at its level from the strikes
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
// One bell of the pair: two partials (1 and 1.48, the second at 0.8), each a
// pure sine at `pure` plus its sign at `clang` (a sine of the table is
// negative exactly when its phase is past half a turn), under a slow and a
// fast decay whose sum is planned every 16 samples and ramps between. KIND 0
// has both parts, 1 only the pure (no clang), 2 only the clang (no table).
// Each partial at half scale, so their sum (up to 1.8) stays in range. At half
// the rate: m samples, inc the full rate's increment.
template <int KIND>
__attribute__((noinline)) void bell_run(Energy& es, Energy& ef, u32 inc_full, i32 pure, i32 clang, u32* __restrict ph,
                                        i32* __restrict out, u32 m) {
    const u32 inc = inc_full * 2;
    i32 slow = es.a, fast = ef.a;
    const i32 rs16 = per_grain(es.r), rf16 = per_grain(ef.r);
    const u32 inc2 = times(inc, GQ28(1.48));
    u32 p0 = ph[0], p1 = ph[1];
    const i32 a0 = pure >> 1, a1 = multiply(pure, GQ31(0.8)) >> 1;
    const i32 up0 = clang >> 1, down0 = (-clang) >> 1;
    const i32 up1 = multiply(clang, GQ31(0.8)) >> 1, down1 = multiply(-clang, GQ31(0.8)) >> 1;
    const i32 ups = up0 + up1, d0 = down0 - up0, d1 = down1 - up1;
    for (u32 g = 0; g < m; g += hgrain) {
        const i32 s1 = multiply(slow, rs16), f1 = multiply(fast, rf16);
        i32 l = slow + fast;
        const i32 dl = (s1 + f1 - l) >> 3;
        i32* __restrict o = out + g;
        for (u32 i = 0; i < hgrain; ++i) {
            p0 += inc;
            p1 += inc2;
            l += dl;
            i32 wave = 0;
            if (KIND != 2) wave = multiply(sine_of(p0), a0) + multiply(sine_of(p1), a1);
            // The signs, without a branch: a phase's top bit, all ones from
            // half a turn (where the sine is 0, so either reads the same but
            // for its last bit).
            if (KIND != 1) wave += ups + (d0 & (static_cast<i32>(p0) >> 31)) + (d1 & (static_cast<i32>(p1) >> 31));
            o[i] += multiply(wave, l) * 2;
        }
        slow = s1;
        fast = f1;
    }
    ph[0] = p0; ph[1] = p1;
    es.a = slow; ef.a = fast;
}
__attribute__((noinline)) inline void run_bell(Body& b, i32* __restrict out, u32 count) {
    const i32 sq = b.k[2];
    const i32 pure = dsp::one - sq, clang = multiply(GQ31(0.7), sq);
    // The pair and its band at half the rate (the bells under 1.9 kHz, the
    // band under 5.7), back to the full rate by straight lines.
    i32 h[32];
    const u32 m = count / 2;
    for (u32 i = 0; i < m; ++i) h[i] = 0;
    for (u32 w = 0; w < 2; ++w) {
        if (b.env[2 * w].a < 64 && b.env[2 * w + 1].a < 64) continue;
        const u32 inc = static_cast<u32>(w ? b.k[1] : b.k[0]);
        if (!pure) bell_run<2>(b.env[2 * w], b.env[2 * w + 1], inc, pure, clang, b.sq + 2 * w, h, m);
        else if (!clang) bell_run<1>(b.env[2 * w], b.env[2 * w + 1], inc, pure, clang, b.sq + 2 * w, h, m);
        else bell_run<0>(b.env[2 * w], b.env[2 * w + 1], inc, pure, clang, b.sq + 2 * w, h, m);
    }
    band_run(b.band[0], h, m);
    expand<false>(h, m, b.held, out);
    click_add(b.click[0], b.seed, out, count, b.cg[0]);
}
// out 2 + hand g (the bursts' own crack over the cavity's ring), through two
// one-pole high-passes at c.
__attribute__((noinline)) void clap_mix(i32* __restrict out, const i32* __restrict hand, i32 g28, dsp::Pole& p1,
                                        dsp::Pole& p2, i32 c, u32 n) {
    i32 s1 = p1.s, s2 = p2.s;
    for (u32 i = 0; i < n; ++i) {
        i32 y = out[i] * 2 + gain(hand[i], g28);
        s1 += multiply(y - s1, c);
        y -= s1;
        s2 += multiply(y - s2, c);
        out[i] = y - s2;
    }
    p1.s = s1; p2.s = s2;
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
    clap_mix(out, hand, b.g[1], b.pole[0], b.pole[1], b.pc[0], count);
    knee_run(out, count, GQ28(0.6), GQ28(0.3), GQ28(1 / 0.3));
}
// The woodblock: its modes and its click, then a soft knee of its own: the
// click's loudest samples give up about 2 dB of peak (its burst of noise still
// reconstructs about 3 dB over its samples, which is what holds the block's level).
__attribute__((noinline)) inline void run_block(Body& b, i32* __restrict out, u32 count) {
    run_modes(b, out, count, 3);
    knee_run(out, count, GQ28(0.9), GQ28(0.45), GQ28(1 / 0.45));
}
__attribute__((noinline)) void add_eighth(i32* __restrict out, const i32* __restrict in, u32 n) {
    for (u32 i = 0; i < n; ++i) out[i] += in[i] >> 3;
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
    add_eighth(out, grain, count);
    modes_add(b.mode, 2, out, count);   // the shell, at its level from the strikes
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
