#pragma once
// The TICK drawer: CICADA, RATCHET, POD and MATCH.
#include "crack.h"

namespace curio {
using namespace dsp;

// The peak of rate-r impulses (unit size) through a resonator of bandwidth bw,
// about 3.5 sigma: 1 / that, Q16, so a particle sum reads about 1 at its
// loudest. sigma^2 = (r / 44100) (pi bw / 44100) times the impulses' mean square.
inline i32 particle_norm(u32 rate, u32 bw, i32 square_q12) {
    // log2 of sigma^2 in Q12.
    const i32 l = lg2(rate) + lg2(bw) + 6764 - 2 * 63193 + lg2(static_cast<u32>(square_q12)) - (12 << 12);   // pi; 44100^2
    const i32 lsig = l >> 1;
    i32 lpeak = lsig + 7403;   // * 3.5
    // Sparse strikes peak at one strike's ring instead: 1.5 x 1.6 (2 pi bw / 44100).
    const i32 lone = lg2(bw) + 10860 - 63193 + 5174;
    if (lone > lpeak) lpeak = lone;
    return static_cast<i32>(ex2((16 << 12) - lpeak));
}

// ------------------------------------------------------------------- CICADA
// A cicada's tymbal buckling rib by rib: each rib's click rings the hollow
// abdomen's two resonances, over the sizzle of the train; a wing flicks;
// a swarm of others joins in on a second shell of their own pitch.
struct Cicada {
    u32 rng, t;
    i32 c[8], tune;
    u32 n, cicadas;
    u32 start_at[6], kk[6], next_at[6];
    i32 period;         // samples per click, Q8
    i32 lcf[2];         // each shell's pitch (the main, the swarm's)
    Res shell[4];       // the main's two, the swarm's two
    ResK shellk[4];
    i32 q;
    Env env[2];
    i32 tone_amp, sizzle_amp, tone_norm, sizzle_norm;
    Res hi, lo, wingr;
    ResK hik, lok, wingk;
    i32 hi_gain, lo_gain, wing_gain, band_gain_now, last_pos, sizzle_level;
    Env wing, wingatt, tick;
    i32 tick_lp, tick_a;
    i32 chirp, len;

    void tune_shell(int s, i32 lp) {
        // The shell rings for q / (pi f): its decay from its quality.
        const u32 tau = ex2(lg2(static_cast<u32>(q)) - (12 << 12) - lp + 74875);
        dsp::tune(shellk[s * 2], inc(lp), decay(tau));
        dsp::tune(shellk[s * 2 + 1], inc(lp + 2276), decay(static_cast<u32>(mul(static_cast<i32>(tau), F(0.68)))));   // 1.47
    }
    __attribute__((noinline)) void click(u32 who) {
        const u32 k = kk[who];
        const int s = who ? 1 : 0;
        // The train falls up to 1.2 octaves by its last rib.
        const i32 fall = n > 1 ? static_cast<i32>(static_cast<u32>(sc(C(1.2), chirp)) * k / (n - 1)) : sc(C(0.6), chirp);
        tune_shell(s, lcf[s] - fall);
        const i32 a = mul(full - static_cast<i32>(static_cast<u32>(F(0.45) >> 8) * k / n << 8), F(0.8) + sc(F(0.2), draw(rng)));
        const i32 g = who ? mul(a, F(0.7)) : a;
        dsp::strike(shell[s * 2], shellk[s * 2], mul(lev(tone_amp, tone_norm), g));
        dsp::strike(shell[s * 2 + 1], shellk[s * 2 + 1], mul(mul(lev(tone_amp, tone_norm), F(0.6)), g));
        // The swarm's clicks feed the same envelope, an eighth of a unit a click:
        // a long tail piles up to about 0.8 n of them, which a whole unit would
        // overflow; a swarm's pile stops at seven units.
        const i32 add = mul(one >> 3, g);
        env[0].v = env[0].v < 7 * one - add ? env[0].v + add : 7 * one;
        tick.v += mul(U(0.18), g);
        ++kk[who];
        if (kk[who] < n) {
            // Each rib a little early or late, +-6% of the period.
            const i32 dp = mul(period, mul(jitter(rng) << 19, F(0.06)));
            const i32 at = static_cast<i32>(start_at[who]) + (period * static_cast<i32>(kk[who]) >> 8) + (dp >> 8);
            next_at[who] = at > static_cast<i32>(t) ? static_cast<u32>(at) : t + 1;
        }
    }
    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        n = 1 + static_cast<u32>(sc(31, pw(c[0], C(1.5))));
        const i32 lrate = hz(120) + sc(C(3.0), c[1]);
        const u32 rate = ex2(lrate + (8 << 12));   // Q8 hertz
        period = static_cast<i32>((static_cast<u32>(44100) << 8) / (rate >> 8 ? rate >> 8 : 1));   // Q8 samples
        // The shell's two modes (1 and 1.47) stay under 18 kHz at any TUNE.
        lcf[0] = dsp::lo(hz(3500) + sc(C(1.6), c[2]) + tune, hz(12000));
        lcf[1] = dsp::lo(lcf[0] + mul(jitter(rng), F(0.3)), hz(12000));
        q = C(2.0) + sc(C(28.0), pw(c[3], C(1.5)));
        chirp = c[5];
        for (int s = 0; s < 4; ++s) shell[s] = Res();
        tune_shell(0, lcf[0]);
        tune_shell(1, lcf[1]);
        // The tail: 12..412 ms.
        const i32 tl = 12000 + sc(400000, pw(c[4], C(1.7)));
        env[0].v = env[1].v = 0;
        env[0].k = env[1].k = decay(static_cast<u32>(tl));
        // The train's length, for the sizzle's slide.
        len = static_cast<i32>(static_cast<u32>(period) * n >> 8) + 1323;
        // The prototype scales tone and sizzle each to a peak of 1: the
        // tone's ring by the overlap of rings, the sizzle by its envelope's
        // pile-up over the train (1 / (1 - exp(-1 / (tl rate))), at most n).
        const i32 ring_us = static_cast<i32>(ex2(lg2(static_cast<u32>(q)) - (12 << 12) - lcf[0] + 74875));
        const i32 per_us = (period * 91 >> 10) + 1;                           // Q8 samples to us
        const i32 overlap = static_cast<i32>((static_cast<u32>(ring_us) << 8) / static_cast<u32>(per_us));   // Q8
        tone_norm = mul(static_cast<i32>(((static_cast<u32>(1) << 30) / static_cast<u32>((1 << 14) + (overlap << 6))) << 12), F(0.77));
        i32 pile = static_cast<i32>(static_cast<u32>(tl) / static_cast<u32>(per_us)) + 1;
        if (pile > static_cast<i32>(n)) pile = static_cast<i32>(n);
        if (pile < 1) pile = 1;
        // A pile of p clicks reaches about 0.8 p; noise peaks near 3.5 sigma.
        sizzle_norm = static_cast<i32>(static_cast<u32>(U(1.2)) / static_cast<u32>(pile));
        tone_amp = U(0.25) + sc(U(0.6), c[3]);
        sizzle_amp = U(1.0) - sc(U(0.45), c[3]);
        band(hik, lcf[0] + 826, C(0.9));      // 1.15
        band(lok, lcf[0] - 3533, C(0.9));     // 0.55
        hi_gain = band_gain(lcf[0] + 826, C(0.9));
        band_gain_now = 0;
        last_pos = -1;
        sizzle_level = lev(sizzle_amp, sizzle_norm) << 3;
        lo_gain = band_gain(lcf[0] - 3533, C(0.9));
        hi = lo = wingr = Res();
        band(wingk, hz(2500), C(0.7));
        wing_gain = band_gain(hz(2500), C(0.7));
        wing.v = wingatt.v = sc(U(0.5), c[6]);
        wing.k = decay(80000);
        wingatt.k = mul(wing.k, decay(10000));
        tick.v = 0;
        tick.k = decay(300);
        tick_lp = 0;
        tick_a = pole(hz(3000));
        // The swarm: up to five more, starting within 30 ms.
        cicadas = 1 + static_cast<u32>(c[7] * 5 >> 12);
        for (u32 w = 0; w < 6; ++w) {
            kk[w] = 0;
            start_at[w] = w ? samples(static_cast<u32>(draw(rng) * 30000 >> 12)) + 1 : 0;
            next_at[w] = start_at[w];
        }
    }
    bool waiting() const {
        for (u32 w = 0; w < cicadas; ++w)
            if (kk[w] < n) return true;
        return false;
    }
    void run(i32* out) {
        // The sizzle slides from the high band to the low one as the train falls.
        const i32 pos = n > 1 ? mul(t >= static_cast<u32>(len) ? full : static_cast<i32>((static_cast<u32>(t) << 15) / static_cast<u32>(len)) << 16, chirp << 19)
                              : sc(F(0.5), chirp);
        // One band slides from 1.15 to 0.55 of the shell (the prototype
        // crossfaded two), retuned once a block while the train falls.
        if (pos != last_pos || !band_gain_now) {
            const i32 lp = lcf[0] + 826 - mul(4359, pos);
            band(hik, lp, C(0.9));
            band_gain_now = band_gain(lp, C(0.9));
            last_pos = pos;
        }
        for (u32 i = 0; i < frames; ++i) out[i] = 0;
        const u32 t0 = t;
        for (u32 i = 0;;) {
            // The next rib's click of any cicada in this block.
            u32 e = frames;
            for (u32 w = 0; w < cicadas; ++w)
                if (kk[w] < n) { const u32 d = next_at[w] - t0; if (d < e && d >= i) e = d; }
            if (e > i) span(out + i, e - i);
            i = e;
            if (i >= frames) break;
            t = t0 + i;
            clicks();
        }
        t = t0 + frames;
    }
    // Each cicada whose rib clicks at t.
    __attribute__((noinline)) void clicks() {
        for (u32 w = 0; w < cicadas; ++w)
            if (kk[w] < n && t == next_at[w]) click(w);
    }
    // n samples between clicks, each part in its own pass with its state in
    // registers; the noise is drawn in the same order as ever.
    __attribute__((noinline)) void span(i32* out, u32 n) {
        i32 sb[frames], wb[frames];
        ring_into(shell[0], shellk[0], out, n);
        ring_into(shell[1], shellk[1], out, n);
        if (cicadas > 1) {
            ring_into(shell[2], shellk[2], out, n);
            ring_into(shell[3], shellk[3], out, n);
        }
        noise_pass(out, sb, wb, n);
        sizzle_pass(out, sb, n);
        if (wing.v > 4096) wing_pass(out, wb, n);
    }
    // The tick, and the noise for the sizzle and the wing in draw order.
    __attribute__((noinline)) void noise_pass(i32* y, i32* sb, i32* wb, u32 n) {
        i32 ev = env[0].v, tv = tick.v, lp = tick_lp, wv = wing.v;
        const i32 ek = env[0].k, tk = tick.k, ta = tick_a, wk = wing.k;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            ev = mul(ev, ek);
            if (ev > 4096) sb[j] = noise(r);
            if (tv > 4096) {
                tv = mul(tv, tk);
                const i32 x = lev(noise(r), tv);
                lp += mul(ta, x - lp);
                y[j] += x - lp;
            }
            if (wv > 4096) {
                wv = mul(wv, wk);
                wb[j] = noise(r);
            }
        }
        tick.v = tv;
        tick_lp = lp;
        rng = r;
    }
    // The sizzle, driven into a soft clip: denser and less peaky, so a hat
    // carries its loudness under the kit's ceiling.
    __attribute__((noinline)) void sizzle_pass(i32* y, const i32* sb, u32 n) {
        i32 ev = env[0].v, y1 = hi.y1, y2 = hi.y2, x1 = hi.x1, x2 = hi.x2;
        const i32 ek = env[0].k, c = hik.c, r2 = hik.r2, b = hik.b, bg = band_gain_now, sl = sizzle_level;
        for (u32 j = 0; j < n; ++j) {
            ev = mul(ev, ek);
            if (ev > 4096) {
                const i32 in = mul(sb[j], b);
                const i32 o = ((mul(c, y1) - mul(r2, y2)) << 1) + in - x2;
                x2 = x1;
                x1 = in;
                y2 = y1;
                y1 = o;
                const i32 sz = clamp(lev(lev(gn(o, bg), ev), sl), -U(1.9), U(1.9));
                y[j] += tanh(sz << 1) >> 1;
            }
        }
        env[0].v = ev;
        hi.y1 = y1;
        hi.y2 = y2;
        hi.x1 = x1;
        hi.x2 = x2;
    }
    // The wing's flick.
    __attribute__((noinline)) void wing_pass(i32* y, const i32* wb, u32 n) {
        i32 wv = wing.v, av = wingatt.v, y1 = wingr.y1, y2 = wingr.y2, x1 = wingr.x1, x2 = wingr.x2;
        const i32 wk = wing.k, ak = wingatt.k, c = wingk.c, r2 = wingk.r2, b = wingk.b, g = wing_gain;
        for (u32 j = 0; j < n; ++j) {
            if (wv > 4096) {
                wv = mul(wv, wk);
                av = mul(av, ak);
                const i32 in = mul(wb[j], b);
                const i32 o = ((mul(c, y1) - mul(r2, y2)) << 1) + in - x2;
                x2 = x1;
                x1 = in;
                y2 = y1;
                y1 = o;
                y[j] += lev(gn(o, g), wv - av);
            }
        }
        wing.v = wv;
        wingatt.v = av;
        wingr.y1 = y1;
        wingr.y2 = y2;
        wingr.x1 = x1;
        wingr.x2 = x2;
    }
};

// ------------------------------------------------------------------ RATCHET
// A watchman's rattle: a toothed wheel flicking a wooden slat. Each tooth
// rings the slat's three modes; a spring tings, the box knocks.
struct Ratchet {
    u32 rng, t;
    i32 c[8], tune;
    u32 n, k, next, due;
    i32 per, slow_step, gap;
    Res slat[3], spring, box;
    ResK slatk[3], springk, boxk;
    Env tick;
    i32 tick_lp, tick_a;
    i32 spring_amp, box_amp;

    void tooth(i32 a) {
        static const i32 sa[3] = {U(1.0), U(0.5), U(0.3)};
        for (int m = 0; m < 3; ++m) dsp::strike(slat[m], slatk[m], lev(sa[m], a));
        tick.v += lev(U(0.6), a);
        dsp::strike(spring, springk, lev(spring_amp, a));
        dsp::strike(box, boxk, lev(box_amp, a));
    }
    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        n = 1 + static_cast<u32>(c[0] * 15 >> 12);
        // 10..80 teeth a second.
        per = static_cast<i32>(samples(static_cast<u32>(static_cast<u32>(1000000) / static_cast<u32>(10 + sc(70, c[1])))));
        const i32 lps = lg2(static_cast<u32>(900 + sc(3100, c[2]))) + tune;
        const i32 t60 = 20000 + sc(80000, 4095 - c[3]);
        static const i32 lr[3] = {0, 4950, 8755};   // 1, 2.31, 4.4
        static const i32 tm[3] = {F(1.0) >> 1, F(0.6) >> 1, F(0.4) >> 1};
        for (int m = 0; m < 3; ++m) {
            dsp::tune(slatk[m], inc(lps + lr[m]), decay60(static_cast<u32>(mul(t60, tm[m]) << 1)));
            slat[m] = Res();
        }
        dsp::tune(springk, inc(hz(5400) + tune), decay(50000));
        dsp::tune(boxk, inc(hz(330)), decay(50000));
        spring = box = Res();
        spring_amp = sc(U(1.0), c[5]);
        box_amp = sc(U(0.6), c[6]);
        tick.v = 0;
        tick.k = decay(600);
        tick_lp = 0;
        tick_a = pole(hz(2000));
        // Slowing: a ritardando, each gap up to 4 times the first by the last
        // tooth (the prototype's grew without bound; kept musical here).
        slow_step = n > 1 ? static_cast<i32>(static_cast<u32>(lg2(static_cast<u32>(4096 + sc(C(3.0), c[4]))) - (12 << 12)) / (n - 1)) : 0;
        gap = per;
        k = 0;
        tooth(U(1.0));
        k = 1;
        due = static_cast<u32>(gap);
        next = n > 1 ? jittered(due) : 0;
    }
    u32 jittered(u32 at) {
        // Worn teeth land up to 4 ms off.
        const i32 j = mul(jitter(rng), sc(F(353.0 / 4096.0), c[7]) << 12);   // up to 8 ms
        const i32 w = static_cast<i32>(at) + j;
        return w > static_cast<i32>(t) ? static_cast<u32>(w) : t + 1;
    }
    bool waiting() const { return k < n; }
    void run(i32* out) {
        for (u32 i = 0; i < frames; ++i, ++t) {
            if (k < n && t == next) {
                const i32 a = mul(U(1.0) - static_cast<i32>(static_cast<u32>(U(0.3) >> 8) * k / n << 8),
                                  full - mul(sc(F(0.9), c[7]), draw(rng) << 19));
                tooth(a);
                ++k;
                gap = (per >> 4) * static_cast<i32>(ex2(static_cast<i32>(k) * slow_step + (16 << 12)) >> 4) >> 8;
                if (gap > 66150) gap = 66150;
                due += static_cast<u32>(gap);
                next = k < n ? jittered(due) : 0;
            }
            i32 y = ring(slat[0], slatk[0], 0) + ring(slat[1], slatk[1], 0) + ring(slat[2], slatk[2], 0);
            y += ring(spring, springk, 0) + ring(box, boxk, 0);
            const i32 tk = lev(noise(rng), tick.step());
            tick_lp += mul(tick_a, tk - tick_lp);
            y += tk - tick_lp;
            out[i] = y;
        }
    }
};

// ---------------------------------------------------------------------- POD
// A dried seed pod rattled in the hand: seeds strike the shell at random, as
// often as the shake is hard, each ringing the shell's three resonances.
struct Pod {
    u32 rng, t;
    i32 c[8], tune;
    Env up, down, bump_up, bump_down;
    u32 bump_at;
    i32 bump_amp, peak_inv;
    u32 rate_p;
    Res r[3], sw, late;
    ResK rk[3], swk, latek;
    i32 norm, dry_lp, dry_a, sw_gain, sw_amp, late_norm;
    u32 late_p;
    Env late_env;
    u32 late_end, stop;

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        const i32 a_us = 4000 + sc(40000, c[1]), d_us = 30000 + sc(220000, c[1]);
        down.v = full;
        down.k = decay(static_cast<u32>(d_us));
        up.v = full;
        up.k = mul(down.k, decay(static_cast<u32>(a_us)));
        // The tilt's second rush of seeds, about 20 ms either side of its time.
        bump_at = samples(static_cast<u32>(90000 + sc(100000, c[1]) - 30000));
        bump_amp = sc(F(0.99), c[5]);   // the pulse below peaks at 0.43 of its size
        bump_up.v = bump_down.v = 0;
        bump_down.k = decay(20000);
        bump_up.k = mul(bump_down.k, decay(12000));
        // The envelope's own peak, for the seeds' rate: (1 - e^-t/a) e^-t/d
        // at its top, about 1 - a / d for a short attack.
        const i32 pk = full - static_cast<i32>((static_cast<u32>(a_us) << 15) / static_cast<u32>(d_us) << 16 >> 0) ;
        const i32 env_peak = pk > F(0.3) ? pk : F(0.3);
        const i32 top = env_peak;
        peak_inv = static_cast<i32>((static_cast<u32>(1) << 30) / static_cast<u32>(top >> 15)) ;   // Q14 of 1 / top
        const u32 rate = static_cast<u32>(500 + sc(6000, c[0]));
        rate_p = per_sample(rate);
        // A bigger pod: a lower, broader shell.
        const i32 lps = lg2(static_cast<u32>(1800 + sc(5000, c[2]))) + tune - sc(C(0.8), c[3]);
        const i32 bwr = C(0.08) + sc(C(0.5), c[3]);   // bandwidth over the pitch, Q12
        static const i32 off[3] = {0, 1685, -2024};   // 1.33, 0.71
        for (int m = 0; m < 3; ++m) {
            // A reson of bandwidth bw rings for 1 / (pi bw).
            const u32 tau = ex2(74875 - (lps + off[m]) - (lg2(static_cast<u32>(bwr)) - (12 << 12)));
            dsp::tune(rk[m], inc(lps + off[m]), decay(tau));
            r[m] = Res();
        }
        const u32 bw = ex2(lps + lg2(static_cast<u32>(bwr)) - (12 << 12));
        norm = mul(particle_norm(rate, bw, C(1.083 * 1.74)), F(0.6)) << 1 >> 1;
        // The shake is over by 0.16 + 0.5 shake s; its last 20 ms fade.
        stop = samples(static_cast<u32>(160000 + sc(500000, c[1])));
        dry_lp = 0;
        dry_a = pole(hz(5000));
        band(swk, lps + 1077, C(0.8));
        sw_gain = band_gain(lps + 1077, C(0.8));
        sw_amp = U(0.1) + sc(U(1.4), c[6]);
        sw = Res();
        // Scatter: stragglers after 50 ms, for up to 0.3 s more.
        dsp::tune(latek, inc(lps - 623), decay(ex2(74875 - (lps - 623) - (lg2(static_cast<u32>(bwr)) - (12 << 12)))));
        late = Res();
        late_p = c[7] > 0 ? per_sample(static_cast<u32>(40 + sc(400, c[7]))) : 0;
        late_env.v = full;
        late_env.k = decay(250000);
        late_end = samples(static_cast<u32>(60000 + sc(500000, c[1]) + sc(300000, c[7])));
        late_norm = sc(U(1.4), c[7]);
    }
    bool waiting() const { return false; }
    void run(i32* out) {
        for (u32 i = 0; i < frames; ++i, ++t) {
            if (t == bump_at && bump_amp) {
                // The pulse (two decays apart) peaks at 0.43 of its size.
                bump_down.v = bump_up.v = bump_amp;
            }
            i32 e = down.step() - (up.v = mul(up.v, up.k));
            // The tilt: a second rush of seeds as the pod is tipped, on top.
            const i32 b = bump_down.step() - (bump_up.v = mul(bump_up.v, bump_up.k));
            if (t + 882 > stop) e = t >= stop ? 0 : mul(e, static_cast<i32>((stop - t) * 2434995u));   // / 882
            // Seeds: a strike when the draw falls under the envelope's share of the rate.
            noise(rng);
            const u32 p = (static_cast<u32>(mul(static_cast<i32>(rate_p >> 1), mul(e, peak_inv << 16) << 1)) << 1) +
                          (static_cast<u32>(mul(static_cast<i32>(rate_p >> 1), b)) << 3);   // up to 1.7 times the shake's rate
            i32 imp = 0;
            if (rng < p) imp = one / 2 + static_cast<i32>(static_cast<u32>(one) >> 12) * (draw(rng));   // 0.5 .. 1.5
            i32 y = ring(r[0], rk[0], mul(imp, rk[0].g)) + mul(ring(r[1], rk[1], mul(imp, rk[1].g)), F(0.7)) +
                    mul(ring(r[2], rk[2], mul(imp, rk[2].g)), F(0.5));
            y = gn(y, norm);
            if (c[4] > 0) {
                dry_lp += mul(dry_a, y - dry_lp);
                y = mul(y, F(1.0) - sc(F(0.6), c[4])) + mul((y - dry_lp), sc(F(0.75), c[4])) * 4;
            }
            y += lev(gn(bp(sw, swk, noise(rng)), sw_gain), lev(sw_amp, (e >> 3) + (b >> 1)));
            if (late_norm && t > 2205 && t < late_end) {
                noise(rng);
                const i32 le = late_env.step();
                const i32 li = rng < late_p ? le >> 3 : 0;
                y += lev(gn(ring(late, latek, mul(li, latek.g)), norm), late_norm);
            }
            out[i] = y;
        }
    }
};

// -------------------------------------------------------------------- MATCH
// A match drawn along the box: the head stick-slips over the striker, then
// the phosphorus flares, crackles and settles into the wood's flame.
struct Match {
    u32 rng, t;
    i32 c[8], tune;
    u32 ds;
    u32 slip_p;
    Res sc_, phos, fl;
    ResK sck, phosk, flk;
    i32 sc_norm, sc_amp, sc_att, sc_attk;
    Env fl_d, fl_a, puff;
    i32 fl_gain, fl_amp, phos_amp;
    u32 phos_p;
    i32 wood_lp, wood_a, wood_amp, puff_lp, puff_a;

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        ds = samples(static_cast<u32>(20000 + sc(100000, c[3])));
        const u32 rate = static_cast<u32>(400 + sc(1400, c[0]));
        slip_p = per_sample(rate);
        const i32 lph = lg2(static_cast<u32>(1500 + sc(5500, c[2]))) + tune;
        dsp::tune(sck, inc(lph), decay(354));   // a 900 Hz band: the head's squeal reads as a pitch
        sc_ = Res();
        sc_norm = particle_norm(rate, 900, C(1.0));
        sc_amp = U(0.2) + sc(U(1.6), c[0]);
        sc_att = full;
        sc_attk = decay(ds * 11338u / 1000u);   // ds / 2, in us
        fl_d.v = full;
        fl_d.k = decay(static_cast<u32>(40000 + sc(300000, c[1])));   // the prototype cut its tail short; this ends it
        fl_a.v = full;
        fl_a.k = mul(fl_d.k, decay(static_cast<u32>(12000 + sc(50000, c[1]))));
        const i32 lpf = lg2(static_cast<u32>(1800 + sc(4000, c[6])));
        const i32 q = C(0.5) + sc(C(2.0), c[6]);
        band(flk, lpf, q);
        fl_gain = band_gain(lpf, q);
        fl = Res();
        fl_amp = U(0.4) + sc(U(0.8), c[1]);
        phos_p = per_sample(static_cast<u32>(150 + sc(900, c[4])));
        dsp::tune(phosk, inc(hz(3800)), decay(127));
        phos = Res();
        phos_amp = mul(sc(U(5.0), c[4]), particle_norm(static_cast<u32>(150 + sc(900, c[4])), 2500, C(1.0)) << 11) << 4;   // normalized crackle
        wood_lp = 0;
        wood_a = pole(hz(600));
        wood_amp = sc(U(2.5), c[5]);   // the burning wood's roar under the flare
        puff.v = sc(U(3.0), c[7]);
        puff.k = decay(50000);
        puff_lp = 0;
        puff_a = pole(hz(250));
    }
    bool waiting() const { return t < ds; }
    void run(i32* out) {
        for (u32 i = 0; i < frames; ++i, ++t) {
            i32 y = 0;
            if (t < ds) {
                noise(rng);
                const i32 slip = rng < slip_p ? one : 0;
                sc_att = mul(sc_att, sc_attk);
                const i32 s = ring(sc_, sck, mul(slip, sck.g));
                y = lev(gn(s, sc_norm), lev(sc_amp, (full - sc_att) >> 3));
            } else {
                y = ring(sc_, sck, 0);
                y = lev(gn(y, sc_norm), lev(sc_amp, (full - sc_att) >> 3));
                const i32 env = fl_d.step() - (fl_a.v = mul(fl_a.v, fl_a.k));
                const i32 n = noise(rng);
                y += lev(lev(gn(bp(fl, flk, n), fl_gain), env >> 3), fl_amp);
                if (phos_amp) {
                    noise(rng);
                    const i32 cr = rng < phos_p ? lev(noise(rng), env >> 3) : 0;
                    y += lev(ring(phos, phosk, mul(cr, phosk.g)), phos_amp);
                }
                if (wood_amp) {
                    wood_lp += mul(wood_a, n - wood_lp);
                    y += lev(lev(wood_lp, env >> 3), wood_amp);
                }
                if (puff.v > 4096) {
                    puff_lp += mul(puff_a, lev(n, puff.step()) - puff_lp);
                    y += puff_lp;
                }
            }
            out[i] = y;
        }
    }
};
}
