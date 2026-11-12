#pragma once
// The RING drawer: GLASS, BELL JAR, CHALK and ROAR.
#include "tick.h"

namespace curio {
using namespace dsp;

// -------------------------------------------------------------------- GLASS
// A struck wine glass, detuned by its water level: four shell modes, each a
// beating doublet (a real glass is never quite round), damped and lowered by
// the water; a wet finger's swell; a second glass chinked against it.
struct Glass {
    u32 rng, t;
    i32 c[8], tune;
    u32 ph[8], incs[8];
    i32 v[4], k[4], amp[4];   // each mode's level at the block's start, its decay over a block
    i32 click_gain;
    Res clickr, crackr, second[4];
    ResK clickk, crackk, secondk[4];
    Env click, crk;
    u32 ph90, ph42, ph55;
    i32 slosh, slosh_env, slosh_k;
    Tone rub;
    i32 rub_amp;
    u32 chink_at;
    i32 chink_amp, sec_amp[4];
    i32 hlast;   // the last half-rate sample, for drawing the next block up

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        hlast = 0;
        // The rim: 1500 Hz for a thin glass down to 495 for a goblet; water
        // lowers it up to 42%.
        const i32 lpe = hz(1500) - sc(C(1.6), c[3]) + tune;
        const i32 w2 = mul(c[0] << 19, c[0] << 19);                       // water^2, Q31
        const i32 lp1 = lpe + lg2(static_cast<u32>((F(1.0) - mul(F(0.42), w2)) >> 19)) - (12 << 12);
        static const i32 lr[4] = {0, 3742, 5891, 7413};                     // 1, 1.885, 2.711, 3.507
        static const i32 lk[4] = {0, 4096, 6492, 8192};                     // log2(k + 1)
        i32 t1 = 350000 + sc(4000000, pw(c[2], C(1.5)));
        t1 = mul(t1, F(1.0) - sc(F(0.4), c[0]));
        t1 = mul(t1, F(1.0) - sc(F(0.6), c[6]));
        const i32 aex = C(2.2) - sc(C(2.2), c[1]);
        // The crack splits each mode by 0.7..14.7 Hz a mode number.
        const u32 split = static_cast<u32>(97392 * 7 / 10) + static_cast<u32>(sc(97392 * 14, c[6]));
        for (int m = 0; m < 4; ++m) {
            incs[2 * m] = inc(lp1 + lr[m]);
            incs[2 * m + 1] = incs[2 * m] + split * static_cast<u32>(m + 1);
            ph[2 * m] = ph[2 * m + 1] = 0;
            const u32 t60 = ex2(lg2(static_cast<u32>(t1)) - (lk[m] * 717 >> 10));   // / (k + 1)^0.7
            k[m] = decay60(t60);
            for (int b = 0; b < 6; ++b) k[m] = mul(k[m], k[m]);   // a block's worth
            amp[m] = static_cast<i32>(ex2((28 << 12) - (lk[m] * (aex >> 2) >> 10)));   // (k + 1)^-aex, Q28; the doublet halves it
            v[m] = amp[m];
            // The shell rings at half the rate (two samples a step): a mode
            // over 10 kHz is left out.
            if (incs[2 * m] > 974000000u) v[m] = amp[m] = 0;
            // The second glass, a major third up: 0.6 of the decays and levels.
            const u32 i2 = static_cast<u32>(mul(static_cast<i32>(incs[2 * m] >> 1), F(1.26 / 2.0)) << 2);
            dsp::tune(secondk[m], i2 < 974000000u ? i2 << 1 : 0, decay60(static_cast<u32>(mul(static_cast<i32>(t60), F(0.3)))));
            second[m] = Res();
            sec_amp[m] = mul(amp[m], F(0.6));
        }
        // The strike: a tick of band noise, brighter for a glass rod.
        const i32 lpc = hz(3000) + lg2(static_cast<u32>(4096 + c[1])) - (12 << 12);
        band(clickk, lpc, C(1.0));
        click_gain = band_gain(lpc, C(1.0));
        clickr = Res();
        click.v = U(0.25) + sc(U(0.6), c[1]);
        click.k = decay(1000);
        // The crack's buzz.
        band_half(crackk, hz(4000), C(0.7));
        crackr = Res();
        crk.v = sc(U(0.2 * 0.7071), c[6]);   // its band at half the rate passes sqrt 2 more of the noise
        crk.k = decay(30000);   // at half the rate
        ph90 = ph42 = ph55 = 0;
        slosh = sc(F(0.05), c[5]);
        slosh_env = full;
        slosh_k = decay(800000);
        // The wet finger.
        rub.ph = 0;
        rub.inc = incs[0];
        rub.v = rub.a = 0;
        rub_amp = sc(U(0.7), c[4]);
        if (rub_amp) rub.hit(rub_amp, decay(static_cast<u32>(300000 + sc(750000, c[4]))), decay(static_cast<u32>(75000 + sc(200000, c[4]))));   // at half the rate
        chink_at = c[7] > 0 ? samples(static_cast<u32>(20000 + sc(60000, c[7]))) : 0;
        chink_amp = sc(U(1.4), c[7]);
    }
    bool waiting() const { return chink_at && t <= chink_at; }
    // The block in passes, each with its state in registers, split at the chink.
    // A shell mode's beating doublet, two sines a split apart: their sum is
    // one sine at their mean times the cosine of half their difference, a
    // beat of a few hertz that moves only a little in a block. So each mode
    // is one sine whose level (its decay, its beat and the water's slosh)
    // is planned at the block's ends and glides between them.
    __attribute__((noinline)) void doublet_pass(int m, i32* y, u32 na, u32 nb, i32 vib) {
        u32 pa = ph[2 * m], pb = ph[2 * m + 1];
        const u32 dn = nb - na, hd0 = (pb - pa) >> 1, hd1 = hd0 + (frames / 2) * dn;
        u32 pc = pa + hd0;
        const i32 v0 = v[m], v1 = mul(v0, k[m]);
        i32 l0 = mul(v0, sine(hd0 + 0x40000000u)), l1 = mul(v1, sine(hd1 + 0x40000000u));
        if (slosh) {
            // The moving water swells and damps the ring as it sloshes.
            l0 += mul(l0, vib) * 6;
            l1 += mul(l1, vib) * 6;
        }
        const i32 dl = (l1 - l0) >> 5;
        i32 l = l0;
        const u32 nc2 = na + nb;   // two samples a step
        for (u32 j = 0; j < frames / 2; ++j) {
            pc += nc2;
            l += dl;
            y[j] += mul(l, sine(pc));
        }
        ph[2 * m] = pa + frames * na;
        ph[2 * m + 1] = pb + frames * nb;
        v[m] = v1;
    }
    // The strike's tick of band noise, at the full rate while it sounds.
    __attribute__((noinline)) void click_pass(i32* y) {
        i32 cv = click.v, y1 = clickr.y1, y2 = clickr.y2, x1 = clickr.x1, x2 = clickr.x2;
        const i32 ck = click.k, c = clickk.c, r2 = clickk.r2, b = clickk.b, g = click_gain;
        u32 r = rng;
        for (u32 j = 0; j < frames; ++j) {
            const i32 in = mul(noise(r), b);
            const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = o;
            cv = mul(cv, ck);
            y[j] += lev(gn(o, g), cv);
        }
        click.v = cv;
        clickr.y1 = y1;
        clickr.y2 = y2;
        clickr.x1 = x1;
        clickr.x2 = x2;
        rng = r;
    }
    // The crack's buzz, gated at 90 Hz, at half the rate.
    __attribute__((noinline)) void crack_pass(i32* y) {
        i32 cv = crk.v, y1 = crackr.y1, y2 = crackr.y2, x1 = crackr.x1, x2 = crackr.x2;
        u32 p = ph90, r = rng;
        const i32 ck = crk.k, c = crackk.c, r2 = crackk.r2, g = crackk.b;   // a band-pass: no response at 0 Hz
        for (u32 j = 0; j < frames / 2; ++j) {
            p += 2 * 8765280u;   // 90 Hz
            const i32 in = mul(noise(r), g);
            const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = o;
            if (static_cast<i32>(p) >= 0) y[j] += lev(o, cv);
            cv = mul(cv, ck);
        }
        crk.v = cv;
        ph90 = p;
        rng = r;
        crackr.y1 = y1;
        crackr.y2 = y2;
        crackr.x1 = x1;
        crackr.x2 = x2;
    }
    void run(i32* out) {
        // The water's slosh, once a block.
        ph42 += 64u * 409045u;   // 4.2 Hz
        for (int b = 0; b < 64; ++b) slosh_env = mul(slosh_env, slosh_k);
        const i32 vib = mul(mul(slosh, sine(ph42)), slosh_env);
        ph55 += 64u * 535653u;
        const i32 rubvib = vib + mul(F(0.002), sine(ph55));
        u32 now[8];
        for (int o = 0; o < 8; ++o) now[o] = incs[o] + static_cast<u32>(mul(static_cast<i32>(incs[o]), vib));
        const u32 rub_inc = rub.inc + static_cast<u32>(mul(static_cast<i32>(rub.inc), rubvib));
        // The shell, the finger, the crack and the second glass at half the
        // rate; the strike's tick at the full rate.
        i32 h[frames / 2];
        for (u32 i = 0; i < frames / 2; ++i) h[i] = 0;
        for (int m = 0; m < 4; ++m)
            if (v[m]) doublet_pass(m, h, now[2 * m], now[2 * m + 1], vib);
        // Once under its floor a part stays there.
        if (crk.v > 4096) crack_pass(h);
        if (rub_amp) tone_into(rub, h, frames / 2, rub_inc << 1);
        // The second glass rings from its chink.
        if (chink_at) {
            const u32 d = chink_at - t;
            if (d < frames) {
                for (int m = 0; m < 4; ++m) dsp::strike(second[m], secondk[m], lev(sec_amp[m], chink_amp));
                ring_bank(second, secondk, 4, h + (d >> 1), frames / 2 - (d >> 1));
            } else if (t >= chink_at) {
                ring_bank(second, secondk, 4, h, frames / 2);
            }
        }
        up_onto(h, out, hlast);
        if (click.v > 4096) click_pass(out);
        t += frames;
    }
};

// ----------------------------------------------------------------- BELL JAR
// Boyle's demonstration: an electric bell rung inside a glass jar as the air
// is pumped out. The thinner the air, the fainter and shorter the bell and
// the more of it is only the hammer's tick; the jar itself hums.
struct BellJar {
    u32 rng, t;
    i32 c[8], tune;
    u32 n, k, next, per;
    Res mode[4], jar, base;
    ResK modek[4], jark, basek;
    i32 amp[4], clk_amp, base_amp, jar_amp, dry;
    Env clk;
    i32 clk_lp, clk_a;

    void stroke(i32 a) {
        for (int m = 0; m < 4; ++m) dsp::strike(mode[m], modek[m], lev(amp[m], a));
        clk.v += lev(clk_amp, a);
        dsp::strike(base, basek, lev(base_amp, a));
    }
    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        n = 1 + static_cast<u32>(c[0] * 11 >> 12);
        per = static_cast<u32>(44100 * 16) / static_cast<u32>(16 * 16 + sc(160, c[0]));   // 1 / (16 + 10 strokes) s
        const i32 lpb = hz(450) + sc(C(1.8), c[1]) + tune;
        // Ratios between a bell's and a gong's: 1, 1.48, 2.11, 2.94 to 1, 2, 2.76, 5.4.
        static const i32 bell[4] = {4096, 6062, 8643, 12042}, gong[4] = {4096, 8192, 11305, 22118};
        const i32 air = c[2];
        const i32 t1 = mul(80000 + sc(1400000, pw(c[4], C(1.4))), F(1.0) - sc(F(0.7), air));
        const i32 t60[4] = {t1, mul(t1, F(0.7 / 2)) * 2 + sc(t1, c[5]), mul(t1, F(0.4)), mul(t1, F(0.25))};
        const i32 a4[4] = {U(1.0), U(0.85) - sc(U(0.4), c[3]), U(0.2) + sc(U(0.5), c[3]), U(0.1) + sc(U(0.5), c[3])};
        // Thin air: fainter (1 - 0.92 air^1.2), and high-passed at 200..2700 Hz.
        const i32 thin = U(1.0) - sc(U(0.92), pw(air, C(1.2)));
        const i32 lhp = lg2(static_cast<u32>(200 + sc(2500, air)));
        for (int m = 0; m < 4; ++m) {
            const i32 ratio = bell[m] + sc(gong[m] - bell[m], c[5]);
            const i32 lp = lpb + lg2(static_cast<u32>(ratio)) - (12 << 12);
            dsp::tune(modek[m], inc(lp < 57000 ? lp : 57000), decay60(static_cast<u32>(t60[m] > 20000 ? t60[m] : 20000)));
            mode[m] = Res();
            // A first-order high-pass's gain at the mode: x / sqrt(1 + x^2), x = f / fc.
            const i32 lx = lp - lhp;
            const i32 hpg = lx > (6 << 12) ? U(1.0) : static_cast<i32>(ex2((28 << 12) + lx - (lg2(static_cast<u32>((1 << 12) + (ex2(2 * lx + (12 << 12)) > 0x7fffffu ? 0x7fffff : ex2(2 * lx + (12 << 12))))) - (12 << 12)) / 2));
            amp[m] = lev(lev(a4[m], thin), air > 0 ? hpg : U(1.0));
        }
        clk_amp = U(0.1) + sc(U(2.0), c[3]);
        clk.v = 0;
        clk.k = decay(800);
        clk_lp = 0;
        clk_a = pole(hz(1500));
        dsp::tune(basek, inc(hz(180)), decay(15000));
        base = Res();
        base_amp = sc(U(1.4), c[7]);
        // The jar: its air's resonance at 420 Hz, fed by the bell.
        jar_amp = sc(U(0.8), c[6]);
        if (jar_amp) {
            const i32 comb = U(1.0) + sc(U(2.9), mul(c[6] << 19, F(0.75)) >> 19);   // the comb's gain at 420 Hz
            jar_amp = lev(jar_amp, comb);
        }
        dsp::tune(jark, inc(hz(420) + tune), decay(1989));   // a 160 Hz band
        jar = Res();
        dry = U(1.0) - sc(U(0.4), c[6]);
        k = 0;
        stroke(U(1.0));
        k = 1;
        next = n > 1 ? per : 0;
    }
    bool waiting() const { return k < n; }
    // The block in passes, each with its state in registers, split at the
    // hammer's strokes.
    // The hammer's tick.
    __attribute__((noinline)) void click_pass(i32* y, u32 n) {
        i32 v = clk.v, lp = clk_lp;
        const i32 k = clk.k, a = clk_a;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            v = mul(v, k);
            const i32 ck = lev(noise(r), v);
            lp += mul(a, ck - lp);
            y[j] += ck - lp;
        }
        clk.v = v;
        clk_lp = lp;
        rng = r;
    }
    // The jar: its air's resonance at 420 Hz, fed by the bell.
    __attribute__((noinline)) void jar_pass(i32* y, u32 n) {
        const i32 g = jark.g, d = dry, ja = jar_amp;
        drive_run(jar, jark, y, n, [g](i32 x) { return mul(x, g); },
                  [d, ja](i32& to, i32 o) { to = lev(to, d) + lev(o, ja); });
    }
    void run(i32* out) {
        for (u32 i = 0; i < frames; ++i) out[i] = 0;
        const u32 t0 = t;
        for (u32 i = 0;;) {
            // The next stroke in this block.
            u32 e = frames;
            if (k < n) { const u32 d = next - t0; if (d < e && d >= i) e = d; }
            if (e > i) {
                const u32 m = e - i;
                ring_bank(mode, modek, 4, out + i, m);
                click_pass(out + i, m);
                ring_into(base, basek, out + i, m);
                if (jar_amp) jar_pass(out + i, m);
            }
            i = e;
            if (i >= frames) break;
            t = t0 + i;
            if (k < n && t == next) next_stroke();
        }
        t = t0 + frames;
    }
    __attribute__((noinline)) void next_stroke() {
        stroke(U(0.7) + sc(U(0.1), draw(rng)));
        ++k;
        next += per;
    }
};

// -------------------------------------------------------------------- CHALK
// Chalk on a slate: it taps, then sticks and slips, a squeal whose waveform
// sharpens with pressure, filtered by the slate's own ring.
struct Chalk {
    u32 rng, t;
    i32 c[8], tune;
    u32 ph, inc0, len, fade;
    i32 glide_step, glide;
    i32 last;
    i32 lp1, lp2, lpk, out_lp, out_a;
    Res slate, gritr, dustr;
    ResK slatek, gritk, dustk;
    i32 grit_gain, dust_gain, grit_amp;
    i32 att, attk, attk64, fallk64;   // and their falls over a block
    u32 ph_ch, inc_ch;
    i32 chatter;
    Env dust, fall;
    i32 drift;
    u32 grng, drng;   // the grit's and the dust's own noise

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        grng = (x.seed * 2654435761u) | 1u;
        drng = (x.seed * 2246822519u + 0x9e3779b9u) | 1u;
        t = 0;
        ph = 0;
        const i32 lp0 = hz(500) + sc(C(2.5), c[0]) + tune;
        inc0 = inc(lp0);
        // The stroke lasts 30..330 ms, its last 20 ms fading.
        fade = 882;
        len = samples(static_cast<u32>(30000 + sc(300000, c[4]))) - fade;
        // The squeal falls away over the stroke, as a cuica's note does: it
        // plays as a pitched hit, not a held scrape.
        fall.v = full;
        fall.k = decay(static_cast<u32>(12000 + sc(120000, c[4])));
        glide = 0;
        glide_step = static_cast<i32>(static_cast<u32>(sc(F(0.8) >> 4, c[3])) / len) << 4;   // to 1 + 0.8 glide
        last = 0;
        // The pressure: a sawtooth (DPW) through two poles at 1..16 times the pitch.
        lp1 = lp2 = 0;
        lpk = pole(lp0 + sc(C(4.0), c[1]));
        out_lp = 0;
        out_a = pole(hz(7000));
        const i32 ls = lg2(static_cast<u32>(1500 + sc(3500, c[2])));
        dsp::tune(slatek, inc(ls), decay(354));   // a 900 Hz band
        slate = Res();
        // The tap of the chalk on the slate: it rings the board once.
        dsp::strike(slate, slatek, U(0.6));
        band(gritk, hz(4000), C(0.8));
        grit_gain = band_gain(hz(4000), C(0.8));
        grit_amp = sc(U(0.4), c[5]);
        band(dustk, hz(7000), C(1.0));
        dust_gain = band_gain(hz(7000), C(1.0));
        gritr = dustr = Res();
        dust.v = sc(U(0.3), c[7]);
        dust.k = decay(40000);
        att = full;
        attk = decay(6000);
        attk64 = attk;
        fallk64 = fall.k;
        for (int i = 0; i < 6; ++i) { attk64 = mul(attk64, attk64); fallk64 = mul(fallk64, fallk64); }
        ph_ch = 0;
        inc_ch = static_cast<u32>(97392 * 20) + static_cast<u32>(sc(97392 * 40, c[6]));
        chatter = sc(F(0.8), c[6]);
        drift = 0;
    }
    bool waiting() const { return t < len; }
    // The block in passes, each with its state in registers; the noise is
    // drawn in the same order as ever. The squeal sounds for the first sq
    // samples of the block, until the stroke has faded.
    // The pressure: a sawtooth (DPW) through two poles.
    __attribute__((noinline)) void wave_pass(i32* wb, u32 sq, u32 f, i32 scale) {
        u32 p = ph;
        i32 l = last, a1 = lp1, a2 = lp2;
        const i32 k = lpk;
        for (u32 j = 0; j < sq; ++j) {
            p += f;
            // The parabola of the phase, then its difference: a sawtooth with little aliasing.
            const i32 xph = static_cast<i32>(p) >> 1;            // -1..1 as Q30
            const i32 par = mul(xph, xph);                        // Q29
            const i32 d = par - l;
            l = par;
            const i32 saw = (d >> 9) * scale;
            a1 += mul(k, saw - a1);
            a2 += mul(k, a1 - a2);
            wb[j] = a2 + mul(a2, F(0.57));   // the fundamental at 1, as the prototype's sum
        }
        ph = p;
        last = l;
        lp1 = a1;
        lp2 = a2;
    }
    // The stroke's envelope (an eighth, as the parts take it), planned at
    // the block's two ends and glided between them; the glide a block's step.
    i32 env_at(u32 tt, i32 at, i32 fv, i32 gate, u32 fade_step) const {
        i32 env = full - at;
        if (tt > len) env = tt >= len + fade ? 0 : mul(env, static_cast<i32>((len + fade - tt) * fade_step));
        return mul(mul(env, gate), fv) >> 3;
    }
    __attribute__((noinline)) void env_pass(i32* eb, u32 sq, i32 gate, u32 fade_step) {
        const i32 a1 = mul(att, attk64), f1 = mul(fall.v, fallk64);
        const i32 e0 = env_at(t, att, fall.v, gate, fade_step), e1 = env_at(t + frames, a1, f1, gate, fade_step);
        const i32 de = (e1 - e0) >> 6;
        i32 e = e0;
        for (u32 j = 0; j < sq; ++j) {
            e += de;
            eb[j] = e;
        }
        att = a1;
        fall.v = f1;
        glide = glide + glide_step * static_cast<i32>(frames) < F(0.8) ? glide + glide_step * static_cast<i32>(frames) : F(0.8);
    }
    // The grit: its own noise through a 4 kHz band, following the stroke.
    __attribute__((noinline)) void grit_pass(i32* y, const i32* eb, u32 sq) {
        i32 y1 = gritr.y1, y2 = gritr.y2, x1 = gritr.x1, x2 = gritr.x2;
        u32 r = grng;
        const i32 c = gritk.c, r2 = gritk.r2, b = gritk.b, ga = gn(grit_amp, grit_gain);   // the band's gain and the grit's level as one
        if (sq & 1) {
            const i32 in = mul(noise(r), b);
            const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = o;
            *y++ += lev(lev(o, ga), *eb++);
        }
        for (u32 j = 0; j < (sq >> 1); ++j) {   // two samples a turn: the states trade places
            const i32 i0 = mul(noise(r), b);
            y2 = twice(mul(c, y1) - mul(r2, y2)) + i0 - x2;
            x2 = i0;
            y[0] += lev(lev(y2, ga), eb[0]);
            const i32 i1 = mul(noise(r), b);
            y1 = twice(mul(c, y2) - mul(r2, y1)) + i1 - x1;
            x1 = i1;
            y[1] += lev(lev(y1, ga), eb[1]);
            y += 2;
            eb += 2;
        }
        grng = r;
        gritr.y1 = y1;
        gritr.y2 = y2;
        gritr.x1 = x1;
        gritr.x2 = x2;
    }
    // The squeal, and the slate ringing from the tap and the squeal, 1.2 times.
    __attribute__((noinline)) void slate_pass(i32* y, const i32* wb, const i32* eb, u32 sq) {
        i32 y1 = slate.y1, y2 = slate.y2;
        const i32 c = slatek.c, r2 = slatek.r2, g = slatek.g;
        if (sq & 1) {
            const i32 s = lev(*wb++, *eb++);
            const i32 o = twice(mul(c, y1) - mul(r2, y2)) + mul(s, g);
            y2 = y1;
            y1 = o;
            *y++ += (s >> 1) + mul(o, F(0.9)) * 2;
        }
        for (u32 j = 0; j < (sq >> 1); ++j) {   // two samples a turn: the states trade places
            const i32 s0 = lev(wb[0], eb[0]);
            y2 = twice(mul(c, y1) - mul(r2, y2)) + mul(s0, g);
            y[0] += (s0 >> 1) + mul(y2, F(0.9)) * 2;
            const i32 s1 = lev(wb[1], eb[1]);
            y1 = twice(mul(c, y2) - mul(r2, y1)) + mul(s1, g);
            y[1] += (s1 >> 1) + mul(y1, F(0.9)) * 2;
            y += 2;
            wb += 2;
            eb += 2;
        }
        const u32 rest = frames - sq;
        if (rest & 1) {
            const i32 o = twice(mul(c, y1) - mul(r2, y2));
            y2 = y1;
            y1 = o;
            *y++ += mul(o, F(0.9)) * 2;
        }
        for (u32 j = 0; j < (rest >> 1); ++j) {
            y2 = twice(mul(c, y1) - mul(r2, y2));
            y[0] += mul(y2, F(0.9)) * 2;
            y1 = twice(mul(c, y2) - mul(r2, y1));
            y[1] += mul(y1, F(0.9)) * 2;
            y += 2;
        }
        slate.y1 = y1;
        slate.y2 = y2;
    }
    // Chalk dust falling from the slate, from 0.7 of the stroke: its own
    // noise through a 7 kHz band, fading after the stroke ends.
    template <bool fading> __attribute__((always_inline)) void dust_run(i32* y, u32 n) {
        i32 dv = dust.v, y1 = dustr.y1, y2 = dustr.y2, x1 = dustr.x1, x2 = dustr.x2;
        const i32 dk = dust.k, c = dustk.c, r2 = dustk.r2, b = gn(dustk.b, dust_gain);   // the band's gain taken at its input
        u32 r = drng;
        if (dv <= 4096) return;   // and so it stays (checked once a run: past it the band is far under hearing)
        if (n & 1) {
            const i32 in = mul(noise(r), b);
            const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = o;
            if (fading) dv = mul(dv, dk);
            *y++ += lev(o, dv);
        }
        for (u32 j = 0; j < (n >> 1); ++j) {   // two samples a turn: the states trade places
            const i32 i0 = mul(noise(r), b);
            y2 = twice(mul(c, y1) - mul(r2, y2)) + i0 - x2;
            x2 = i0;
            if (fading) dv = mul(dv, dk);
            y[0] += lev(y2, dv);
            const i32 i1 = mul(noise(r), b);
            y1 = twice(mul(c, y2) - mul(r2, y1)) + i1 - x1;
            x1 = i1;
            if (fading) dv = mul(dv, dk);
            y[1] += lev(y1, dv);
            y += 2;
        }
        drng = r;
        dust.v = dv;
        dustr.y1 = y1;
        dustr.y2 = y2;
        dustr.x1 = x1;
        dustr.x2 = x2;
    }
    __attribute__((noinline)) void dust_pass(i32* y) {
        const u32 d0 = len * 7 / 10 + 1, l1 = len + 1;   // the first samples past 0.7 of the stroke and past its end
        const u32 j0 = d0 > t ? (d0 - t < frames ? d0 - t : frames) : 0;
        u32 j1 = l1 > t ? (l1 - t < frames ? l1 - t : frames) : 0;
        if (j1 < j0) j1 = j0;
        if (j1 > j0) dust_run<false>(y + j0, j1 - j0);
        if (frames > j1) dust_run<true>(y + j1, frames - j1);
    }
    __attribute__((noinline)) void out_pass(i32* y) {
        i32 lp = out_lp;
        const i32 a = out_a;
        for (u32 j = 0; j < frames; ++j) {
            lp += mul(a, y[j] - lp);
            y[j] = lp;
        }
        out_lp = lp;
    }
    void run(i32* out) {
        // The glide and a slow wander, once a block.
        const u32 f = inc0 + static_cast<u32>(mul(static_cast<i32>(inc0), glide)) + static_cast<u32>(mul(static_cast<i32>(inc0), drift));
        drift += mul(jitter(rng) << 12, F(0.0008));
        drift = clamp(drift, -F(0.01), F(0.01));
        // DPW: the parabola's difference times rate / (4 f), as (d >> 9) * 2^38 / inc.
        const i32 sc_ = static_cast<i32>((static_cast<u32>(1) << 30) / (f >> 8));
        ph_ch += 64u * inc_ch;
        const i32 gate = chatter && sine(ph_ch) > F(0.3) ? full - chatter : full;
        const u32 fade_step = 0x7fffffffu / fade;   // the fade's slope, once a block
        const u32 end = len + fade;
        const u32 sq = t < end ? (end - t < frames ? end - t : frames) : 0;
        i32 wb[frames], eb[frames];
        for (u32 i = 0; i < frames; ++i) out[i] = 0;
        if (sq) {
            wave_pass(wb, sq, f, sc_);
            env_pass(eb, sq, gate, fade_step);
        }
        if (grit_amp && sq) grit_pass(out, eb, sq);
        slate_pass(out, wb, eb, sq);
        if (dust.v > 4096) dust_pass(out);
        out_pass(out);
        t += frames;
    }
};

// --------------------------------------------------------------------- ROAR
// A bull-roarer: a wooden slat whirled on a cord. It spins as it circles, so
// its roar swings in pitch and pulses twice a turn; the slat flutters.
struct Roar {
    u32 rng, t;
    i32 c[8], tune;
    u32 len, fade, rot, rot_inc, flut_ph, flut_inc;
    i32 lpc, swing, q, bp_gain;
    Res roar, windr;
    ResK roark, windk;
    i32 wind_gain, wind_amp, buzz_amp, buzz_lp, buzz_a, flut_swing;
    i32 att, attk, throb;
    u32 hum_ph, hum_inc;
    i32 hum_amp;
    i32 hlast;   // the last half-rate sample, for drawing the next block up

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        rot = 0;
        rot_inc = static_cast<u32>(97392 * 2) + static_cast<u32>(sc(97392 * 14, c[0]));   // 2..16 turns a second
        lpc = hz(1400) - sc(C(2.2), c[1]) + tune;
        swing = F(0.05) + sc(F(0.4), c[2]);
        q = C(6.0) + sc(C(6.0), 4095 - c[7]);
        // The whole roar is under 11 kHz: it runs at half the rate, two
        // samples a step, drawn back up once a block.
        bp_gain = band_gain(lpc + (1 << 12), q);
        hlast = 0;
        roar = windr = Res();
        len = samples(static_cast<u32>(300000 + sc(2200000, c[4])));
        // It fades over the last 0.1 + 0.2 L of its L seconds.
        fade = samples(static_cast<u32>(100000)) + len / 5;
        len -= fade;
        att = full;
        attk = decay(static_cast<u32>(20000 + sc(600000, c[3])) >> 1);
        throb = c[5];
        flut_ph = 0;
        // The slat's flutter sits three octaves under its hum, so the two
        // make one pitched drone that follows TUNE.
        flut_inc = inc(lpc - C(3.0)) << 1;
        hum_ph = 0;
        hum_inc = inc(lpc) << 1;
        hum_amp = U(0.35);
        flut_swing = sc(F(0.3), c[2]);
        buzz_amp = sc(U(0.6), c[6]);
        buzz_lp = 0;
        buzz_a = pole(hz(1200) + (1 << 12));
        band_half(windk, hz(900), C(0.5));
        wind_gain = band_gain(hz(900) + (1 << 12), C(0.5));
        wind_amp = sc(U(0.4), c[7]);
    }
    bool waiting() const { return t < len; }
    void run(i32* out) {
        // The turn, once a block: the band swings about its pitch.
        rot += 64u * rot_inc;
        const i32 s = sine(rot);
        const i32 m = mul(swing, s);
        const i32 lp = lpc + lg2(static_cast<u32>((full >> 1) + (m >> 1)) >> 19) - (11 << 12);   // log2(1 + swing sin)
        band_half(roark, lp, q);
        hum_inc = inc(lp) << 1;
        // The pulse: (1 - throb) + throb |sin(pi r t)|^1.5, twice a turn's half.
        const i32 hs = sine(rot >> 1);
        const i32 ah = hs < 0 ? -hs : hs;
        const i32 p15 = mul(ah, static_cast<i32>(ex2((31 << 12) + (lg2(static_cast<u32>(ah >> 1 > 0 ? ah >> 1 : 1)) - (30 << 12)) / 2)));   // |s| sqrt|s|
        const i32 am = full - mul(throb << 19, full - p15);
        const u32 fl = flut_inc + static_cast<u32>(mul(static_cast<i32>(flut_inc), mul(flut_swing, s)));
        const u32 fade_step = 0x7fffffffu / fade;   // the fade's slope, once a block
        // The block in passes at half the rate, each with its state in
        // registers, until the roar has faded; then silence.
        const u32 end = len + fade;
        if (t < end) {
            const u32 n = frames / 2;
            i32 h[frames / 2], nb[frames / 2];
            noise_pass(nb, n);
            roar_pass(h, nb, n);
            hum_pass(h, n);
            if (buzz_amp) buzz_pass(h, n, fl);
            if (wind_amp) wind_pass(h, nb, n);
            env_pass(h, n, am, fade_step);
            up_onto(h, out, hlast);
        } else {
            for (u32 i = 0; i < frames; ++i) out[i] = 0;
        }
        t += frames;
    }
    __attribute__((noinline)) void noise_pass(i32* nb, u32 n) {
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) nb[j] = noise(r);
        rng = r;
    }
    // The roar: noise through a band swinging about its pitch.
    __attribute__((noinline)) void roar_pass(i32* y, const i32* nb, u32 n) {
        const i32 g = bp_gain;
        bp_run(roar, roark, nb, y, n, [g](i32& to, i32 o) { to = mul(gn(o, g), F(0.39)); });
    }
    // The hum at the band's pitch.
    __attribute__((noinline)) void hum_pass(i32* y, u32 n) {
        u32 ph = hum_ph;
        const u32 in = hum_inc;
        const i32 a = hum_amp;
        for (u32 j = 0; j < n; ++j) {
            ph += in;
            y[j] += lev(sine(ph) >> 3, a);
        }
        hum_ph = ph;
    }
    // The slat's flutter: a sawtooth, low-passed.
    __attribute__((noinline)) void buzz_pass(i32* y, u32 n, u32 fl) {
        u32 ph = flut_ph;
        i32 lp = buzz_lp;
        const i32 a = buzz_a, amp = buzz_amp;
        for (u32 j = 0; j < n; ++j) {
            ph += fl;
            const i32 sw = static_cast<i32>(ph) >> 3;   // a sawtooth, Q28
            lp += mul(a, sw - lp);
            y[j] += lev(lp, amp);
        }
        flut_ph = ph;
        buzz_lp = lp;
    }
    // The wind in the cord, from the roar's noise.
    __attribute__((noinline)) void wind_pass(i32* y, const i32* nb, u32 n) {
        const i32 g = wind_gain, amp = wind_amp;
        bp_run(windr, windk, nb, y, n, [g, amp](i32& to, i32 o) { to += lev(gn(o, g), amp); });
    }
    // The swell, the fade and the pulse.
    __attribute__((noinline)) void env_pass(i32* y, u32 n, i32 am, u32 fade_step) {
        i32 a = att;
        const i32 k = attk;
        const u32 l = len, end = len + fade;
        u32 tt = t;
        for (u32 j = 0; j < n; ++j, tt += 2) {
            a = mul(a, k);
            i32 env = full - a;
            if (CURIO_UNLIKELY(tt > l)) env = tt >= end ? 0 : mul(env, static_cast<i32>((end - tt) * fade_step));
            y[j] = mul(mul(y[j], am), env);
        }
        att = a;
    }
};
}
