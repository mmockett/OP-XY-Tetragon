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
    i32 v[4], k[4], amp[4];
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

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
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
            amp[m] = static_cast<i32>(ex2((28 << 12) - (lk[m] * (aex >> 2) >> 10)));   // (k + 1)^-aex, Q28; the doublet halves it
            v[m] = amp[m];
            // The second glass, a major third up: 0.6 of the decays and levels.
            dsp::tune(secondk[m], static_cast<u32>(mul(static_cast<i32>(incs[2 * m] >> 1), F(1.26 / 2.0)) << 2), decay60(static_cast<u32>(mul(static_cast<i32>(t60), F(0.6)))));
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
        band(crackk, hz(4000), C(0.7));
        crackr = Res();
        crk.v = sc(U(0.2), c[6]);
        crk.k = decay(60000);
        ph90 = ph42 = ph55 = 0;
        slosh = sc(F(0.05), c[5]);
        slosh_env = full;
        slosh_k = decay(800000);
        // The wet finger.
        rub.ph = 0;
        rub.inc = incs[0];
        rub.v = rub.a = 0;
        rub_amp = sc(U(0.7), c[4]);
        if (rub_amp) rub.hit(rub_amp, decay(static_cast<u32>(600000 + sc(1500000, c[4]))), decay(static_cast<u32>(150000 + sc(400000, c[4]))));
        chink_at = c[7] > 0 ? samples(static_cast<u32>(20000 + sc(60000, c[7]))) : 0;
        chink_amp = sc(U(1.4), c[7]);
    }
    bool waiting() const { return chink_at && t <= chink_at; }
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
        for (u32 i = 0; i < frames; ++i, ++t) {
            i32 y = 0;
            for (int m = 0; m < 4; ++m) {
                ph[2 * m] += now[2 * m];
                ph[2 * m + 1] += now[2 * m + 1];
                v[m] = mul(v[m], k[m]);
                y += mul(v[m], (sine(ph[2 * m]) >> 1) + (sine(ph[2 * m + 1]) >> 1));
            }
            // The moving water swells and damps the ring as it sloshes.
            if (slosh) y += mul(y, vib) * 6;
            {
            }
            const i32 n = noise(rng);
            if (click.v > 4096) y += lev(gn(bp(clickr, clickk, n), click_gain), click.step());
            if (crk.v > 4096) {
                ph90 += 8765280u;   // 90 Hz
                const i32 h = bp(crackr, crackk, n);
                if (static_cast<i32>(ph90) >= 0) y += lev(h, crk.v);
                crk.step();
            }
            if (rub_amp) {
                rub.ph += rub_inc;
                y += mul(rub.level(), sine(rub.ph));
            }
            if (chink_at && t == chink_at)
                for (int m = 0; m < 4; ++m) dsp::strike(second[m], secondk[m], lev(sec_amp[m], chink_amp));
            if (chink_at && t >= chink_at)
                for (int m = 0; m < 4; ++m) y += ring(second[m], secondk[m], 0);
            out[i] = y;
        }
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
    void run(i32* out) {
        for (u32 i = 0; i < frames; ++i, ++t) {
            if (k < n && t == next) {
                stroke(U(0.7) + sc(U(0.1), draw(rng)));
                ++k;
                next += per;
            }
            i32 y = ring(mode[0], modek[0], 0) + ring(mode[1], modek[1], 0) + ring(mode[2], modek[2], 0) + ring(mode[3], modek[3], 0);
            const i32 ck = lev(noise(rng), clk.step());
            clk_lp += mul(clk_a, ck - clk_lp);
            y += ck - clk_lp;
            y += ring(base, basek, 0);
            if (jar_amp) {
                const i32 j = ring(jar, jark, mul(y, jark.g));
                y = lev(y, dry) + lev(j, jar_amp);
            }
            out[i] = y;
        }
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
    i32 att, attk;
    u32 ph_ch, inc_ch;
    i32 chatter;
    Env dust, fall;
    i32 drift;

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
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
        ph_ch = 0;
        inc_ch = static_cast<u32>(97392 * 20) + static_cast<u32>(sc(97392 * 40, c[6]));
        chatter = sc(F(0.8), c[6]);
        drift = 0;
    }
    bool waiting() const { return t < len; }
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
        for (u32 i = 0; i < frames; ++i, ++t) {
            i32 y = 0;
            i32 drive = 0;
            if (t < len + fade) {
                glide = glide + glide_step < F(0.8) ? glide + glide_step : F(0.8);
                ph += f;
                // The parabola of the phase, then its difference: a sawtooth with little aliasing.
                const i32 xph = static_cast<i32>(ph) >> 1;            // -1..1 as Q30
                const i32 par = mul(xph, xph);                         // Q29
                const i32 d = par - last;
                last = par;
                const i32 saw = (d >> 9) * sc_;
                lp1 += mul(lpk, saw - lp1);
                lp2 += mul(lpk, lp1 - lp2);
                const i32 wave = lp2 + mul(lp2, F(0.57));   // the fundamental at 1, as the prototype's sum
                att = mul(att, attk);
                i32 env = full - att;
                if (t > len) env = mul(env, static_cast<i32>((len + fade - t) * fade_step));
                env = mul(mul(env, gate), fall.step());
                const i32 s = lev(wave, env >> 3);
                drive = mul(s, slatek.g);
                y = s >> 1;
                if (grit_amp) y += lev(lev(gn(bp(gritr, gritk, noise(rng)), grit_gain), env >> 3), grit_amp);
            }
            // The slate rings from the tap and the squeal, 1.2 times.
            y += mul(ring(slate, slatek, drive), F(0.9)) * 2;
            if (dust.v > 4096 && t * 10 > len * 7) y += lev(gn(bp(dustr, dustk, noise(rng)), dust_gain), t > len ? dust.step() : dust.v);
            out_lp += mul(out_a, y - out_lp);
            out[i] = out_lp;
        }
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
        bp_gain = band_gain(lpc, q);
        roar = windr = Res();
        len = samples(static_cast<u32>(300000 + sc(2200000, c[4])));
        // It fades over the last 0.1 + 0.2 L of its L seconds.
        fade = samples(static_cast<u32>(100000)) + len / 5;
        len -= fade;
        att = full;
        attk = decay(static_cast<u32>(20000 + sc(600000, c[3])));
        throb = c[5];
        flut_ph = 0;
        // The slat's flutter sits three octaves under its hum, so the two
        // make one pitched drone that follows TUNE.
        flut_inc = inc(lpc - C(3.0));
        hum_ph = 0;
        hum_inc = inc(lpc);
        hum_amp = U(0.35);
        flut_swing = sc(F(0.3), c[2]);
        buzz_amp = sc(U(0.6), c[6]);
        buzz_lp = 0;
        buzz_a = pole(hz(1200));
        band(windk, hz(900), C(0.5));
        wind_gain = band_gain(hz(900), C(0.5));
        wind_amp = sc(U(0.4), c[7]);
    }
    bool waiting() const { return t < len; }
    void run(i32* out) {
        // The turn, once a block: the band swings about its pitch.
        rot += 64u * rot_inc;
        const i32 s = sine(rot);
        const i32 m = mul(swing, s);
        const i32 lp = lpc + lg2(static_cast<u32>((full >> 1) + (m >> 1)) >> 19) - (11 << 12);   // log2(1 + swing sin)
        band(roark, lp, q);
        hum_inc = inc(lp);
        // The pulse: (1 - throb) + throb |sin(pi r t)|^1.5, twice a turn's half.
        const i32 hs = sine(rot >> 1);
        const i32 ah = hs < 0 ? -hs : hs;
        const i32 p15 = mul(ah, static_cast<i32>(ex2((31 << 12) + (lg2(static_cast<u32>(ah >> 1 > 0 ? ah >> 1 : 1)) - (30 << 12)) / 2)));   // |s| sqrt|s|
        const i32 am = full - mul(throb << 19, full - p15);
        const u32 fl = flut_inc + static_cast<u32>(mul(static_cast<i32>(flut_inc), mul(flut_swing, s)));
        const u32 fade_step = 0x7fffffffu / fade;   // the fade's slope, once a block
        for (u32 i = 0; i < frames; ++i, ++t) {
            if (t >= len + fade) { out[i] = 0; continue; }
            att = mul(att, attk);
            i32 env = full - att;
            if (t > len) env = mul(env, static_cast<i32>((len + fade - t) * fade_step));
            const i32 n = noise(rng);
            i32 y = mul(gn(bp(roar, roark, n), bp_gain), F(0.39));
            hum_ph += hum_inc;
            y += lev(sine(hum_ph) >> 3, hum_amp);
            if (buzz_amp) {
                flut_ph += fl;
                const i32 sw = static_cast<i32>(flut_ph) >> 3;   // a sawtooth, Q28
                buzz_lp += mul(buzz_a, sw - buzz_lp);
                y += lev(buzz_lp, buzz_amp);
            }
            if (wind_amp) y += lev(gn(bp(windr, windk, n), wind_gain), wind_amp);
            out[i] = mul(mul(y, am), env);
        }
    }
};
}
