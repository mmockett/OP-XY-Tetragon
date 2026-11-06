#pragma once
// The THUMP drawer: BALL, CASK, STONE and DRIP.
#include "dsp.h"

namespace curio {
using namespace dsp;

// A swept sine with its own level and a quick attack: the level is v - a,
// two decays apart, as 1 - exp(-t / attack) times exp(-t / tau).
struct Tone {
    u32 ph, inc;
    i32 v, vk, a, ak;
    i32 level() {
        v = mul(v, vk);
        // The attack is over in a few ms: then it costs nothing.
        if (a) { a = mul(a, ak); if (a < 64 && a > -64) a = 0; }
        return v - a;
    }
    // Adds a strike of amplitude g (Q28) with decay k and attack decay ka.
    void hit(i32 g, i32 k, i32 ka) { v += g; a += g; vk = k; ak = mul(k, ka); }
};

// --------------------------------------------------------------------- BALL
// A ball dropped on a stretched hide, bouncing to rest: each bounce strikes
// the skin again, softer and sooner, its pitch sweeping down from the blow.
struct Ball {
    u32 rng, t, next, k;
    i32 a, e, gap, t0;
    i32 c[8], tune;
    // tone 0 is the drop's own strike, tone 1 the bounces (each restarts it).
    Tone tone[2];
    i32 sweep[2], sdi[2];
    u32 inc0, inc_edge[3], incf, inc190, incp;
    i32 sweepk;
    u32 ph1, ph2, ph3, phf, ph190, php;
    Env edge, fl, clk, ping;
    i32 clk_lp, clk_a;
    i32 tau_us, attk;
    i32 drive, undrive, follow, follow_a, buzz_gain;
    Res buzz;
    ResK buzzk;

    void strike_at(i32 amp) {
        // The skin's pitch rises a little with each bounce at an edge strike.
        const i32 kk = static_cast<i32>(k < 9 ? k : 9);
        const i32 up = sc(F(0.1), c[6]) * kk;
        const u32 fk = inc0 + static_cast<u32>(mul(static_cast<i32>(inc0), up));
        const u32 s = k ? 1 : 0;
        Tone& o = tone[s];
        o.inc = fk;
        if (!k) {
            inc_edge[0] = static_cast<u32>(mul(static_cast<i32>(fk), F(1.59 / 2))) << 1;
            inc_edge[1] = static_cast<u32>(mul(static_cast<i32>(fk), F(2.14 / 4))) << 2;
            inc_edge[2] = static_cast<u32>(mul(static_cast<i32>(fk), F(2.65 / 4))) << 2;
            incf = static_cast<u32>(mul(static_cast<i32>(fk), F(2.3 / 4))) << 2;
        }
        // The sweep: (0.8 + 4 drop) a^0.7, from the skin's pitch.
        const i32 depth = mul(C(0.8) + sc(C(4.0), c[0]), pw(amp >> 19, C(0.7)) << 19);   // Q12
        sdi[s] = mul(static_cast<i32>(fk), depth << 15) << 4;
        sweep[s] = full;
        // tk = tau (0.35 + 0.65 a) (1 + 0.3 floor)
        i32 tk = mul(tau_us, F(0.35) + mul(F(0.65), amp));
        tk += sc(mul(tk, F(0.3)), c[5]);
        const i32 dk = decay(static_cast<u32>(tk));
        if (s) o.v = o.a = 0;   // a bounce restarts its tone
        o.hit(mul(amp, U(1.0) - sc(U(0.45), c[6])), dk, attk);
        edge.v += mul(amp, U(0.05) + sc(U(0.8), c[6]));
        if (!k) edge.k = decay(static_cast<u32>(mul(tk, F(0.5) - sc(F(0.3), c[6]))));
        fl.v += mul(amp, sc(U(0.9), c[5]));
        clk.v += mul(amp, U(0.25) + sc(U(1.6), c[3]));
        if (c[3] > C(0.45)) ping.v += mul(amp, sc(U(1.1), c[3] - C(0.45)));
    }
    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        k = 0;
        ph1 = ph2 = ph3 = phf = ph190 = php = 0;
        for (int s = 0; s < 2; ++s) {
            tone[s].ph = 0;
            tone[s].inc = 0;
            tone[s].v = tone[s].a = 0;
            tone[s].vk = tone[s].ak = 0;
            sweep[s] = sdi[s] = 0;
        }
        edge.v = fl.v = clk.v = ping.v = 0;
        clk_lp = 0;
        buzz = Res();
        follow = 0;
        // The skin: 30..120 Hz.
        inc0 = inc(hz(30) + 2 * c[2] + tune);
        tau_us = 60000 + sc(1100000, pw(c[4], C(1.6)));
        attk = decay(1200);
        sweepk = decay(22000);
        fl.k = decay(static_cast<u32>(60000 + sc(250000, c[5])));
        inc190 = inc(hz(190));
        incp = inc(hz(3150) + tune);
        ping.k = decay(60000);
        clk.k = decay(static_cast<u32>(800 + sc(6000, 4095 - c[3])));
        clk_a = pole(lg2(static_cast<u32>(250 + sc(12000, mul(c[3], c[3] << 19)))));
        // Grit: a tanh drive and a buzz of the snare chain.
        drive = (1 << 24) + sc(7 << 24, pw(c[7], C(1.5)));   // Q24: 1 + 7 grit^1.5, gradual
        {
            const i32 g = drive >> 12;                      // Q12, 1..8
            const i32 th = tanh(g << 16);                   // Q28
            undrive = static_cast<i32>((static_cast<u32>(1) << 30) / static_cast<u32>(th >> 14));   // Q16
        }
        follow_a = pole(hz(30));
        band(buzzk, hz(3200), C(1.2));
        buzz_gain = mul(band_gain(hz(3200), C(1.2)) << 12, sc(F(0.25), c[7]));   // Q28 per unit noise
        // The bounces: restitution e = 0.85 bounce^0.8, the first gap 0.07 + 0.22 drop s.
        e = mul(F(0.85), pw(c[1], C(0.8)) << 19);
        gap = static_cast<i32>(samples(static_cast<u32>(70000 + sc(220000, c[0]))));
        a = full;
        t0 = 0;
        next = 0;
        schedule();
        strike_at(full);
    }
    void schedule() {
        // As the prototype: t0 += g; a *= e; g *= max(e, 0.55); stop when
        // a < 0.05, g < 10 ms or t0 > 1.6 s.
        if (e <= F(0.02)) { next = 0; return; }
        t0 += gap;
        a = mul(a, e);
        gap = mul(gap, e > F(0.55) ? e : F(0.55));
        if (a < F(0.05) || gap < 441 || t0 > 70560) { next = 0; return; }
        next = static_cast<u32>(t0);
    }
    bool waiting() const { return next != 0; }
    // The block in passes, each with its state in registers, split at the
    // bounces; the noise is drawn in the same order as ever.
    // The drop's tone (it never rests), and how far its sweep has risen.
    __attribute__((noinline)) void drop_pass(i32* y, i32* rise, u32 n) {
        Tone& o = tone[0];
        u32 ph = o.ph;
        const u32 in = o.inc;
        i32 v = o.v, a = o.a, sw = sweep[0];
        const i32 vk = o.vk, ak = o.ak, swk = sweepk, d = sdi[0];
        for (u32 j = 0; j < n; ++j) {
            sw = mul(sw, swk);
            const i32 r = mul(sw, d);
            ph += in + static_cast<u32>(r);
            v = mul(v, vk);
            if (a) { a = mul(a, ak); if (a < 64 && a > -64) a = 0; }
            y[j] += mul(v - a, sine(ph));
            rise[j] = r;
        }
        o.ph = ph;
        o.v = v;
        o.a = a;
        sweep[0] = sw;
    }
    // The bounces' tone: it costs nothing once it has died away.
    __attribute__((noinline)) void bounce_pass(i32* y, u32 n) {
        Tone& o = tone[1];
        u32 ph = o.ph;
        const u32 in = o.inc;
        i32 v = o.v, a = o.a, sw = sweep[1];
        const i32 vk = o.vk, ak = o.ak, swk = sweepk, d = sdi[1];
        for (u32 j = 0; j < n; ++j) {
            if (v < 4096 && v > -4096) { v = a = 0; continue; }
            sw = mul(sw, swk);
            ph += in + static_cast<u32>(mul(sw, d));
            v = mul(v, vk);
            if (a) { a = mul(a, ak); if (a < 64 && a > -64) a = 0; }
            y[j] += mul(v - a, sine(ph));
        }
        o.ph = ph;
        o.v = v;
        o.a = a;
        sweep[1] = sw;
    }
    // The skin's edge modes, risen with the drop's sweep.
    __attribute__((noinline)) void edge_pass(i32* y, const i32* rise, u32 n) {
        u32 p1 = ph1, p2 = ph2, p3 = ph3;
        const u32 i1 = inc_edge[0], i2 = inc_edge[1], i3 = inc_edge[2];
        i32 ev = edge.v;
        const i32 ek = edge.k;
        for (u32 j = 0; j < n; ++j) {
            if (ev > 4096) {
                const i32 r = rise[j];   // adding nothing where it is 0
                p1 += i1 + (static_cast<u32>(mul(r, F(1.59 / 2))) << 1);
                p2 += i2 + (static_cast<u32>(mul(r, F(2.14 / 4))) << 2);
                p3 += i3 + (static_cast<u32>(mul(r, F(2.65 / 4))) << 2);
                ev = mul(ev, ek);
                y[j] += mul(ev, (sine(p1) >> 2) + mul(sine(p2), F(0.7 / 4)) + mul(sine(p3), F(0.4 / 4))) << 2;
            }
        }
        ph1 = p1;
        ph2 = p2;
        ph3 = p3;
        edge.v = ev;
    }
    // The floor's ring and the ping of the ball.
    __attribute__((noinline)) void floor_pass(i32* y, u32 n) {
        u32 pf = phf, p190 = ph190;
        const u32 inf = incf, in190 = inc190;
        i32 fv = fl.v;
        const i32 fk = fl.k;
        for (u32 j = 0; j < n; ++j) {
            if (fv > 4096) {
                pf += inf;
                p190 += in190;
                fv = mul(fv, fk);
                y[j] += mul(fv, (sine(pf) >> 1) + mul(sine(p190), F(0.3))) << 1;
            }
        }
        phf = pf;
        ph190 = p190;
        fl.v = fv;
    }
    __attribute__((noinline)) void ping_pass(i32* y, u32 n) {
        u32 pp = php;
        const u32 ip = incp;
        i32 pv = ping.v;
        const i32 pk = ping.k;
        for (u32 j = 0; j < n; ++j) {
            if (pv > 4096) {
                pp += ip;
                pv = mul(pv, pk);
                y[j] += mul(pv, sine(pp));
            }
        }
        php = pp;
        ping.v = pv;
    }
    // The click, and the buzz's noise drawn after it.
    template <bool grit> __attribute__((noinline)) void click_pass(i32* y, i32* nb, u32 n) {
        i32 cv = clk.v, lp = clk_lp;
        const i32 ck = clk.k, ca = clk_a;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            if (cv > 4096 || lp > 4096 || lp < -4096) {
                cv = mul(cv, ck);
                const i32 c = lev(noise(r), cv);
                lp += mul(ca, c - lp);
                y[j] += lp;
            }
            if (grit) nb[j] = noise(r);
        }
        clk.v = cv;
        clk_lp = lp;
        rng = r;
    }
    // Grit: a tanh drive and a buzz of the snare chain.
    __attribute__((noinline)) void grit_pass(i32* out, const i32* nb, u32 n) {
        i32 f = follow, y1 = buzz.y1, y2 = buzz.y2, x1 = buzz.x1, x2 = buzz.x2;
        const i32 fa = follow_a, dr = drive, ud = undrive << 14, bg = buzz_gain;
        const i32 c = buzzk.c, r2 = buzzk.r2, b = buzzk.b;
        for (u32 j = 0; j < n; ++j) {
            i32 y = out[j];
            const i32 m = y < 0 ? -y : y;
            f += mul(fa, m - f);
            const i32 d = mul(y, dr);                    // y g / 128
            const i32 lim = 1 << 23;                     // g y > 4
            const i32 s = d >= lim ? U(0.99933) : d <= -lim ? -U(0.99933) : tanh(d << 7);
            y = mul(s, ud) << 1;
            const i32 in = mul(nb[j], b);
            const i32 o = ((mul(c, y1) - mul(r2, y2)) << 1) + in - x2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = o;
            y += mul(mul(o, bg) << 3, (f < one ? f : one - 1) << 3);
            out[j] = y;
        }
        follow = f;
        buzz.y1 = y1;
        buzz.y2 = y2;
        buzz.x1 = x1;
        buzz.x2 = x2;
    }
    void run(i32* out) {
        for (u32 i = 0; i < frames; ++i) out[i] = 0;
        i32 rise[frames], nb[frames];
        const u32 t0 = t;
        for (u32 i = 0;;) {
            // The next bounce in this block.
            u32 e = frames;
            if (next) { const u32 d = next - t0; if (d < e && d >= i) e = d; }
            if (e > i) {
                const u32 n = e - i;
                drop_pass(out + i, rise + i, n);
                bounce_pass(out + i, n);
                edge_pass(out + i, rise + i, n);
                floor_pass(out + i, n);
                ping_pass(out + i, n);
                if (c[7] > 0) {
                    click_pass<true>(out + i, nb + i, n);
                    grit_pass(out + i, nb + i, n);
                } else {
                    click_pass<false>(out + i, nb + i, n);
                }
            }
            i = e;
            if (i >= frames) break;
            t = t0 + i;
            if (next && t == next) {
                ++k;
                strike_at(a);
                schedule();
            }
        }
        t = t0 + frames;
    }
};

// --------------------------------------------------------------------- CASK
// A knuckle on a cooper's cask: the air above the liquid booms as a
// Helmholtz resonator, the staves knock, the hoops ring, the cellar answers.
struct Cask {
    u32 rng, t, tap;
    i32 c[8], tune;
    Tone air;
    u32 inca;
    Env blip, bend, slosh_on, clk, thud;
    i32 bung, slosh_depth, slosh_am, air_amp;
    u32 ph55;
    Res wood[4], hoop[3];
    ResK woodk[4], hoopk[3];
    i32 wood_amp, hoop_amp, clk_lp, clk_a, thud_lp, thud_a;
    i32 attk;

    void strike(i32 g, bool echo) {
        // A delayed copy of the air's ring adds only its share in phase with it.
        const i32 ga = echo ? mul(g, sine(air.ph + 0x40000000u)) : g;
        air.hit(mul(air_amp, ga), air.vk, attk);
        static const i32 wa[4] = {F(0.5), F(0.3), F(0.2), F(0.1)};
        for (int m = 0; m < 4; ++m) dsp::strike(wood[m], woodk[m], mul(mul(wood_amp, wa[m]), g));
        static const i32 ha[3] = {F(1.0), F(0.6), F(0.3)};
        const i32 hg = echo ? mul(g, F(0.5)) : g;
        for (int m = 0; m < 3; ++m) dsp::strike(hoop[m], hoopk[m], mul(mul(hoop_amp, ha[m]), hg));
        clk.v += mul(U(0.4) + sc(U(2.2), c[2]), echo ? mul(g, F(0.4)) : g);
        thud.v += mul(sc(U(0.8), c[1]), g);
    }
    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        tap = 0;
        air.ph = 0;
        air.v = air.a = 0;
        ph55 = 0;
        clk.v = thud.v = 0;
        clk_lp = thud_lp = 0;
        // The air: 40..197 Hz, ringing longer the emptier the cask.
        const i32 lpa = hz(40) + sc(C(2.3), c[0]) + tune;
        inca = inc(lpa);
        const i32 ta = 40000 + sc(900000, pw(4095 - c[1], C(2.2)));
        air.vk = decay(static_cast<u32>(ta));
        attk = decay(2000);
        air_amp = U(1.2) - sc(U(0.5), c[2]);
        blip.v = full;
        blip.k = decay(12000);
        bend.v = full;
        bend.k = decay(80000);
        bung = sc(F(0.35), c[4]);
        slosh_on.v = full;
        slosh_on.k = decay(40000);
        slosh_depth = sc(F(0.12), c[6]);
        slosh_am = sc(F(0.5), c[6]);
        // The staves: four modes from 3.1 (1 + stave) times the air.
        const i32 lps = lpa + lg2(31) - lg2(10) + lg2(static_cast<u32>(4096 + c[3])) - (12 << 12);
        static const i32 ratio[4] = {0, 3236, 5667, 8042};   // log2(1, 1.73, 2.61, 3.9) Q12
        const i32 st = 50000 + sc(400000, c[3]);
        const u32 t60[4] = {static_cast<u32>(st), static_cast<u32>(st), 60000u, 40000u};
        for (int m = 0; m < 4; ++m) {
            tune_res(woodk[m], lps + ratio[m], t60[m]);
            wood[m] = Res();
        }
        wood_amp = U(0.2) + sc(U(1.4), c[3]);
        static const i32 hoops[3] = {1130, 1742, 2810};
        static const u32 ht[3] = {600000, 400000, 250000};
        for (int m = 0; m < 3; ++m) {
            tune_res(hoopk[m], hz(static_cast<u32>(hoops[m])) + tune, ht[m]);
            hoop[m] = Res();
        }
        hoop_amp = sc(U(1.2), c[5]);
        clk.k = decay(static_cast<u32>(6000 - sc(5000, c[2])));
        clk_a = pole(lg2(static_cast<u32>(500 + sc(9000, pw(c[2], C(1.5))))));
        thud.k = decay(40000);
        thud_a = pole(hz(220));
        strike(full, false);
    }
    static void tune_res(ResK& k, i32 lp, u32 t60) { dsp::tune(k, inc(lp), decay60(t60)); }
    bool waiting() const { return c[7] > 0 && tap < 4; }
    // The block in passes, each with its state in registers, split at the
    // cellar's taps.
    // The air's pitch each sample: the knock's blip, the bung's bend, the slosh.
    __attribute__((noinline)) void pitch_pass(u32* fb, u32 n, i32 vib) {
        const u32 in = inca;
        i32 bv = blip.v, ev = bend.v, sv = slosh_on.v;
        const i32 bk = blip.k, ek = bend.k, sk = slosh_on.k, bg = bung;
        const bool slosh = slosh_depth != 0;
        for (u32 j = 0; j < n; ++j) {
            u32 f = in;
            if (bv > (1 << 20)) { bv = mul(bv, bk); f += static_cast<u32>(mul(static_cast<i32>(in), mul(F(0.35), bv))); }
            if (bg) { ev = mul(ev, ek); f -= static_cast<u32>(mul(static_cast<i32>(f), mul(bg, full - ev))); }
            if (slosh) {
                sv = mul(sv, sk);
                f += static_cast<u32>(mul(static_cast<i32>(f), vib));
            }
            fb[j] = f;
        }
        blip.v = bv;
        bend.v = ev;
        slosh_on.v = sv;
    }
    // The air's boom, swelling with the slosh.
    template <bool am_on> __attribute__((noinline)) void air_pass(i32* y, const u32* fb, u32 n, i32 am) {
        u32 ph = air.ph;
        i32 v = air.v, a = air.a;
        const i32 vk = air.vk, ak = air.ak;
        for (u32 j = 0; j < n; ++j) {
            ph += fb[j];
            v = mul(v, vk);
            if (a) { a = mul(a, ak); if (a < 64 && a > -64) a = 0; }
            i32 s = mul(v - a, sine(ph));
            if (am_on) s += mul(s, am);
            y[j] += s;
        }
        air.ph = ph;
        air.v = v;
        air.a = a;
    }
    // The knuckle's click and the thud, from one noise.
    __attribute__((noinline)) void knock_pass(i32* y, u32 n) {
        i32 cv = clk.v, tv = thud.v, cl = clk_lp, tl = thud_lp;
        const i32 ck = clk.k, tk = thud.k, ca = clk_a, ta = thud_a;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            if (cv > 4096 || tv > 4096) {
                const i32 x = noise(r);
                cv = mul(cv, ck);
                cl += mul(ca, lev(x, cv) - cl);
                tv = mul(tv, tk);
                tl += mul(ta, lev(x, tv) - tl);
            } else {
                cl -= cl >> 6;
                tl -= tl >> 6;
            }
            y[j] += cl + tl;
        }
        clk.v = cv;
        thud.v = tv;
        clk_lp = cl;
        thud_lp = tl;
        rng = r;
    }
    void run(i32* out) {
        static const u32 at[4] = {1014, 1632, 2690, 3925};       // 23, 37, 61, 89 ms
        static const i32 gain[4] = {F(0.64), F(0.48), F(0.35), F(0.2)};   // 1.6 times the prototype's: it read faintly
        // The slosh: 5.5 Hz, once a block.
        ph55 += 64u * 535653u;   // 5.5 Hz
        const i32 swing = mul(sine(ph55), full - slosh_on.v);
        const i32 vib = mul(slosh_depth, swing);
        const i32 am = mul(slosh_am, sine(ph55 + 683565275u));   // phase +1 rad
        for (u32 i = 0; i < frames; ++i) out[i] = 0;
        u32 fb[frames];
        const u32 t0 = t;
        for (u32 i = 0;;) {
            // The cellar's next tap in this block.
            u32 e = frames;
            if (c[7] > 0 && tap < 4) { const u32 d = at[tap] - t0; if (d < e && d >= i) e = d; }
            if (e > i) {
                const u32 n = e - i;
                // Each part costs nothing once it is still or switched off.
                pitch_pass(fb + i, n, vib);
                if (slosh_am) air_pass<true>(out + i, fb + i, n, am);
                else air_pass<false>(out + i, fb + i, n, am);
                for (int m = 0; m < 4; ++m) ring_into(wood[m], woodk[m], out + i, n);
                if (hoop_amp) for (int m = 0; m < 3; ++m) ring_into(hoop[m], hoopk[m], out + i, n);
                knock_pass(out + i, n);
            }
            i = e;
            if (i >= frames) break;
            t = t0 + i;
            if (c[7] > 0 && tap < 4 && t == at[tap]) { strike(sc(gain[tap], c[7]), true); ++tap; }
        }
        t = t0 + frames;
    }
};

// -------------------------------------------------------------------- STONE
// A ringing stone, a lithophone: a free bar's modes, stretched by the rock.
struct Stone {
    u32 rng, t;
    i32 c[8], tune;
    Res mode[5];
    ResK modek[5];
    Tone stack[2];
    Env chip, dust;
    i32 chip_lp, chip_a, dust_gain;
    Res dustr;
    ResK dustk;
    u32 ph37;
    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        ph37 = 0;
        const i32 lp0 = hz(110) + sc(C(3.0), c[0]) + tune;   // 110..880 Hz
        static const i32 lr[5] = {0, 5991, 9972, 12940, 15312};   // log2(1, 2.756, 5.404, 8.933, 13.34)
        static const i32 lk[5] = {0, 4096, 6492, 8192, 9511};      // log2(k + 1)
        const i32 stretch = C(0.7) + sc(C(0.45), c[2]);
        // Dull also shortens the ring and its chip, as a damp stone's.
        const i32 t1 = mul(120000 + sc(1600000, pw(c[3], C(1.5))), F(1.0) - sc(F(0.5), c[6]));
        const i32 dex = C(0.2) + sc(C(2.4), c[6]) - sc(C(0.4), c[2]);
        const i32 aex = C(3.2) - sc(C(3.0), pw(c[1], C(0.6)));
        for (int m = 0; m < 5; ++m) {
            const i32 lp = lp0 + (lr[m] * (stretch >> 2) >> 10);
            const u32 t60 = ex2(lg2(static_cast<u32>(t1)) - (lk[m] * (dex >> 2) >> 10));
            mode[m] = Res();
            if (lp > 57500) { modek[m].c = modek[m].r2 = modek[m].s = 0; continue; }   // above 0.45 of the rate
            dsp::tune(modek[m], inc(lp), decay60(t60));
            const i32 amp = static_cast<i32>(ex2((28 << 12) - (lk[m] * (aex >> 2) >> 10)));
            dsp::strike(mode[m], modek[m], amp);
        }
        // Stacked stones: two modes at 1.5 and 3.9 times, swelling in over 20 ms.
        const i32 sg = sc(U(1.1), c[5]);
        stack[0].ph = stack[1].ph = 0;
        stack[0].v = stack[0].a = stack[1].v = stack[1].a = 0;
        stack[0].inc = inc(lp0 + 2396);    // 1.5
        stack[1].inc = inc(lp0 + 8042);    // 3.9
        stack[0].hit(sg, decay60(static_cast<u32>(mul(t1, F(0.6)) * 2)), decay(20000));
        stack[1].hit(mul(sg, F(0.3)), decay60(static_cast<u32>(mul(t1, F(0.4)))), decay(20000));
        chip.v = U(0.1) + sc(U(2.5), c[4]);
        chip.k = decay(1200);
        chip_lp = 0;
        chip_a = pole(hz(1500));
        dust.v = sc(U(0.35), c[7]);
        dust.k = decay(static_cast<u32>(mul(t1, F(0.3))));
        band(dustk, hz(2400), C(0.8));
        dust_gain = band_gain(hz(2400), C(0.8));
        dustr = Res();
    }
    bool waiting() const { return false; }
    void run(i32* out) {
        ph37 += 64u * 3603060u;   // 37 Hz, the dust's patter
        const bool on = static_cast<i32>(ph37) >= 0;
        for (u32 i = 0; i < frames; ++i, ++t) {
            i32 y = 0;
            for (int m = 0; m < 5; ++m) y += ring(mode[m], modek[m], 0);
            for (int m = 0; m < 2; ++m) {
                stack[m].ph += stack[m].inc;
                y += mul(stack[m].level(), sine(stack[m].ph));
            }
            const i32 n = noise(rng);
            const i32 ch = lev(n, chip.step());
            chip_lp += mul(chip_a, ch - chip_lp);
            y += ch - chip_lp;
            const i32 d = dust.step();
            if (d > 256) {
                const i32 b = bp(dustr, dustk, n);
                if (on) y += mul(mul(b, dust_gain << 12) << 3, d << 3);
            }
            out[i] = y;
        }
    }
};

// --------------------------------------------------------------------- DRIP
// A tap dripping into a basin: each drop traps a bubble whose note rises
// (Minnaert), the basin rings under it and the tiled room answers.
struct Drip {
    u32 rng, t;
    i32 c[8], tune;
    struct Bubble { u32 ph, inc, f; i32 rise, v, vk, a, ak, span, df; };
    Bubble bub[3];   // two for drops in turn, one for the room's echoes
    u32 turn;
    u32 drops, next_drop, echo, base_t;
    i32 drip_gap;
    i32 lp0, last_ratio, last_amp;
    Res basin[4];
    ResK basink[4];
    Env splash;
    i32 splash_lp, splash_a, basin_amp;
    u32 ph23;
    i32 wob, risek, risek64;

    void drop(i32 amp, i32 ratio_lp, bool echo_drop) {
        Bubble& b = echo_drop ? bub[2] : bub[turn];
        if (!echo_drop) turn ^= 1u;
        b.ph = 0;
        b.inc = inc(lp0 + ratio_lp);
        b.rise = full;
        b.f = b.inc;   // until the next block's glide
        b.df = 0;
        b.span = C(0.15) + sc(C(1.2), c[1]);
        const i32 tau = 25000 + sc(120000, c[4]);
        b.vk = decay(static_cast<u32>(tau));
        b.v = amp >> 3;
        b.a = amp >> 3;
        b.ak = mul(b.vk, decay(1500));
        // The room's echoes are dull (the prototype low-passes them): no splash.
        if (!echo_drop) splash.v += mul(amp, sc(U(1.8), c[5]));
        if (!echo_drop || c[2] > 0) {
            static const i32 ba[4] = {F(1.0), F(0.5), F(0.25), F(0.1)};
            const i32 g = mul(amp, sc(U(0.22), c[2]));
            for (int m = 0; m < 4; ++m) dsp::strike(basin[m], basink[m], mul(g, ba[m]));
        }
    }
    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        turn = 0;
        echo = 0;
        ph23 = 0;
        for (int s = 0; s < 3; ++s) {
            bub[s].v = bub[s].a = bub[s].vk = bub[s].ak = 0;
            bub[s].inc = bub[s].f = 0;
            bub[s].df = 0;
        }
        lp0 = hz(350) + sc(C(2.5), 4095 - c[0]) + tune;   // 350..1980 Hz
        wob = sc(F(0.08), c[6]);   // the prototype's 3% hid under the rise
        risek = decay(20000);
        risek64 = risek;
        for (int i = 0; i < 6; ++i) risek64 = mul(risek64, risek64);   // a block's worth
        // The basin: four modes from 0.62 of the drop's note.
        const i32 lb = lp0 - 2827;   // 0.62
        static const i32 br[4] = {0, 4974, 8263, 10500};   // log2(1, 2.32, 4.05, 5.9)
        const u32 t60[4] = {static_cast<u32>(300000 + sc(2500000, c[2])), static_cast<u32>(200000 + sc(1500000, c[2])), 600000u, 300000u};
        for (int m = 0; m < 4; ++m) {
            dsp::tune(basink[m], inc(lb + br[m]), decay60(t60[m]));
            basin[m] = Res();
        }
        splash.v = 0;
        splash.k = decay(6000);
        splash_lp = 0;
        splash_a = pole(hz(2500));
        drops = static_cast<u32>(c[3] * 5 >> 12);   // more drops from the tap
        base_t = 0;
        // The tap drips steadily, 100 ms apart, down to 55 ms for a faster
        // tap, each drop +-6% off: a figure that holds its time in a groove.
        drip_gap = 100000 - sc(45000, c[3]);
        next_drop = drops ? samples(static_cast<u32>(drip_gap + (drip_gap * jitter(rng) >> 16))) : 0;
        last_ratio = 0;
        last_amp = full;
        drop(full, 0, false);
    }
    bool waiting() const { return drops > 0 || (c[7] > 0 && echo < 6); }
    void run(i32* out) {
        static const u32 at[6] = {1367, 2073, 3219, 4851, 7056, 10143};   // 31 .. 230 ms
        static const i32 eg[6] = {F(0.7), F(0.55), F(0.45), F(0.3), F(0.2), F(0.12)};
        ph23 += 64u * 2239006u;   // 23 Hz wobble
        const i32 vib = mul(wob, sine(ph23));
        // Each bubble's pitch for the block: its rise and wobble at the block's
        // two ends, glided between them a sample at a time.
        for (int s = 0; s < 3; ++s) {
            Bubble& b = bub[s];
            if (!b.v && !b.a) continue;
            const i32 r1 = mul(b.rise, risek64);
            u32 f0 = b.inc + static_cast<u32>(mul(static_cast<i32>(b.inc), mul(full - b.rise, b.span << 18)) << 1);
            u32 f1 = b.inc + static_cast<u32>(mul(static_cast<i32>(b.inc), mul(full - r1, b.span << 18)) << 1);
            f0 += static_cast<u32>(mul(static_cast<i32>(f0), vib));
            f1 += static_cast<u32>(mul(static_cast<i32>(f1), vib));
            b.f = f0;
            b.df = static_cast<i32>(f1 - f0) >> 6;
            b.rise = r1;
        }
        for (u32 i = 0; i < frames; ++i, ++t) {
            if (drops && t == next_drop) {
                --drops;
                last_amp = F(0.5) + mul(F(0.4), draw(rng) << 19);   // 0.5 .. 0.9
                last_ratio = (jitter(rng) * 1229) >> 12;             // +-0.3 octave
                drop(last_amp, last_ratio, false);
                next_drop = drops ? t + samples(static_cast<u32>(drip_gap + (drip_gap * jitter(rng) >> 16))) : 0;
                base_t = t;
                echo = 0;
            }
            if (c[7] > 0 && echo < 6 && t - base_t == at[echo]) {
                drop(mul(mul(last_amp, eg[echo]), c[7] << 19), last_ratio, true);
                ++echo;
            }
            i32 y = 0;
            for (int s = 0; s < 3; ++s) {
                Bubble& b = bub[s];
                if (!b.v && !b.a) continue;
                // A bubble that has died away costs nothing until the next drop.
                if (b.v < 4096 && b.v > -4096) { b.v = b.a = 0; continue; }
                b.ph += b.f;
                b.f += static_cast<u32>(b.df);
                b.v = mul(b.v, b.vk);
                b.a = mul(b.a, b.ak);
                y += mul(b.v - b.a, sine(b.ph));
                if (wob) y += mul(mul(b.v - b.a, sine(b.ph)), vib) * 5;   // the bubble's wobble flutters its level too
            }
            for (int m = 0; m < 4; ++m) y += ring(basin[m], basink[m], 0);
            if (splash.v > 4096) {
                const i32 sp = lev(noise(rng), splash.step());
                splash_lp += mul(splash_a, sp - splash_lp);
                y += sp - splash_lp;
            } else if (splash_lp) {
                splash.v = 0;
                y -= splash_lp;
                splash_lp -= splash_lp >> 4;
                if (splash_lp < 64 && splash_lp > -64) splash_lp = 0;
            }
            out[i] = y;
        }
    }
};
}
