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
    i32 tuned[2];       // the pitch each shell is tuned to now
    Res shell[4];       // the main's two, the swarm's two
    ResK shellk[4];
    i32 q, lq, lq9;     // log2 of the shell's quality and of the sizzle band's (0.9), less 12 octaves
    i32 slide, slide_step;   // the sizzle's place along the train (Q31) and its step a block
    Env env[2];
    i32 tone_amp, sizzle_amp, tone_norm, sizzle_norm;
    Res hi, lo, wingr;
    ResK hik, lok, wingk;
    i32 hi_gain, lo_gain, wing_gain, band_gain_now, last_pos, sizzle_level;
    Env wing, wingatt, tick;
    i32 tick_lp, tick_a;
    u32 srng, wrng;     // the sizzle's and the wing's own noise
    // Each cicada's rib quotients and remainders, and their steps a rib.
    u32 fall_k[6], fall_r[6], amp_k[6], amp_r[6];
    u32 fall_q, fall_rq, amp_q, amp_rq;
    i32 chirp, len;
    i32 wlast;   // the wing's last half-rate sample
    i32 swarm_lp;   // the swarm shell's pitch for the next block

    __attribute__((noinline)) void tune_shell(int s, i32 lp) {
        // A train that doesn't fall keeps its shell's tuning from rib to rib.
        if (lp == tuned[s]) return;
        tuned[s] = lp;
        // The shell rings for q / (pi f): its decay from its quality, found
        // from the log of that time without leaving the log (decay's own sum).
        tune_quick(shellk[s * 2], inc(lp), decay_log(lq - lp));
        tune_quick(shellk[s * 2 + 1], inc(lp + 2276), decay_log(lq - lp - 2279));   // 1.47, 0.68 of the time
    }
    // decay(2^(l / 4096) us), from l.
    static i32 decay_log(i32 l) {
        if (l < 27213) l = 27213;   // 100 us, as decay's floor
        const u32 d = ex2(145421 - l);
        return d >= 0x7fffffffu ? 0 : static_cast<i32>(0x7fffffffu - d);
    }
    // dsp::tune with sin and cos read from the sine table and interpolated
    // (to about 3e-5, a fraction of a hertz in the pole) rather than summed
    // from their series: a rib's retune is a quarter of the work.
    static void tune_quick(ResK& k, u32 increment, i32 r) {
        const u32 f = (increment >> 4) & 0xffffu;   // 16 bits between table entries
        const u32 is = increment >> 20, ic = (increment + 0x40000000u) >> 20;
        const i32 s0 = table::sine[is], s1 = table::sine[(is + 1) & 4095u];
        const i32 c0 = table::sine[ic], c1 = table::sine[(ic + 1) & 4095u];
        const i32 sn = (s0 << 15) + ((s1 - s0) * static_cast<i32>(f) >> 1);   // Q30
        const i32 cs = (c0 << 15) + ((c1 - c0) * static_cast<i32>(f) >> 1);
        k.c = mul(r, cs) << 1;
        k.r2 = mul(r, r) >> 1;
        k.s = sn << 1;
        k.g = mul(full - r, sn) << 2;
        k.b = full - r;
    }
    __attribute__((noinline)) void click(u32 who) {
        const int s = who ? 1 : 0;
        // The train falls up to 1.2 octaves by its last rib: fall_k[who] is
        // 1.2 chirp k / (n - 1), carried from rib to rib without a division.
        const i32 fall = n > 1 ? static_cast<i32>(fall_k[who]) : sc(C(0.6), chirp);
        // The main cicada retunes its shell rib by rib; the swarm's shared
        // shell takes the latest swarm rib's pitch once a block.
        if (s) swarm_lp = lcf[1] - fall;
        else tune_shell(0, lcf[0] - fall);
        const i32 a = mul(full - static_cast<i32>(amp_k[who] << 8), F(0.8) + sc(F(0.2), draw(rng)));   // 0.45 k / n
        // The next rib's quotients: q + x / d, the remainders carried.
        fall_k[who] += fall_q;
        fall_r[who] += fall_rq;
        if (fall_r[who] >= n - 1) { fall_r[who] -= n - 1; ++fall_k[who]; }
        amp_k[who] += amp_q;
        amp_r[who] += amp_rq;
        if (amp_r[who] >= n) { amp_r[who] -= n; ++amp_k[who]; }
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
        srng = (x.seed * 2654435761u) | 1u;
        wrng = (x.seed * 2246822519u + 0x9e3779b9u) | 1u;
        t = 0;
        n = 1 + static_cast<u32>(sc(31, pw(c[0], C(1.5))));
        const i32 lrate = hz(120) + sc(C(3.0), c[1]);
        const u32 rate = ex2(lrate + (8 << 12));   // Q8 hertz
        period = static_cast<i32>((static_cast<u32>(44100) << 8) / (rate >> 8 ? rate >> 8 : 1));   // Q8 samples
        // The shell's two modes (1 and 1.47) stay under 18 kHz at any TUNE.
        lcf[0] = dsp::lo(hz(3500) + sc(C(1.6), c[2]) + tune, hz(12000));
        lcf[1] = dsp::lo(lcf[0] + mul(jitter(rng), F(0.3)), hz(12000));
        q = C(2.0) + sc(C(28.0), pw(c[3], C(1.5)));
        lq = lg2(static_cast<u32>(q)) - (12 << 12) + 74875;   // log2 of q / pi, us, less the pitch: each click's shell
        lq9 = lg2(static_cast<u32>(C(0.9))) - (12 << 12);
        chirp = c[5];
        for (int s = 0; s < 4; ++s) shell[s] = Res();
        tuned[0] = tuned[1] = -(1 << 30);   // nothing yet
        tune_shell(0, lcf[0]);
        tune_shell(1, lcf[1]);
        swarm_lp = lcf[1];
        // The tail: 12..412 ms.
        const i32 tl = 12000 + sc(400000, pw(c[4], C(1.7)));
        env[0].v = env[1].v = 0;
        env[0].k = env[1].k = decay(static_cast<u32>(tl));
        // The train's length, for the sizzle's slide.
        len = static_cast<i32>(static_cast<u32>(period) * n >> 8) + 1323;
        slide = 0;
        slide_step = static_cast<i32>((static_cast<u32>(frames) << 15) / static_cast<u32>(len)) << 16;   // a block's share of the train
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
        band_half(wingk, hz(2500), C(0.7));   // the wing at half the rate
        wing_gain = band_gain(hz(2500) + (1 << 12), C(0.7));
        wlast = 0;
        wing.v = wingatt.v = sc(U(0.5), c[6]);
        wing.k = decay(40000);
        wingatt.k = mul(wing.k, decay(5000));
        tick.v = 0;
        tick.k = decay(300);
        tick_lp = 0;
        tick_a = pole(hz(3000));
        // The swarm: up to five more, starting within 30 ms.
        cicadas = 1 + static_cast<u32>(c[7] * 5 >> 12);
        {
            const u32 fx = static_cast<u32>(sc(C(1.2), chirp)), ax = static_cast<u32>(F(0.45) >> 8);
            fall_q = n > 1 ? fx / (n - 1) : 0;
            fall_rq = n > 1 ? fx % (n - 1) : 0;
            amp_q = ax / n;
            amp_rq = ax % n;
        }
        for (u32 w = 0; w < 6; ++w) {
            fall_k[w] = fall_r[w] = amp_k[w] = amp_r[w] = 0;
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
        // The sizzle slides from the high band to the low one as the train
        // falls: its place along the train steps on a block at a time.
        i32 pos;
        if (n > 1) {
            pos = mul(slide, chirp << 19);
            slide = slide < full - slide_step ? slide + slide_step : full;
        } else {
            pos = sc(F(0.5), chirp);
        }
        // One band slides from 1.15 to 0.55 of the shell (the prototype
        // crossfaded two), retuned once a block while the train falls, its
        // quality's log worked out at the strike and its sin and cos read
        // from the sine table.
        if (pos != last_pos || !band_gain_now) {
            const i32 lp = lcf[0] + 826 - mul(4359, pos);
            tune_quick(hik, inc(lp), decay(ex2(lq9 - lp + 74875)));
            band_gain_now = static_cast<i32>(ex2(((56431 + lq9 - lp) >> 1) + (16 << 12)));
            last_pos = pos;
        }
        if (cicadas > 1) tune_shell(1, swarm_lp);
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
        // The sizzle and the wing run over the whole block, the clicks'
        // swells taken from its start: a click's sizzle sounds at most a
        // block (1.5 ms) early, its shell and tick on the sample.
        t = t0;
        sizzle_pass(out, frames);
        if (wing.v > 4096) wing_pass(out);
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
        ring_bank(shell, shellk, cicadas > 1 ? 4 : 2, out, n);
        // The tick, once under its floor, stays there until the next click.
        if (tick.v > 4096) tick_pass(out, n);
    }
    // Each rib's tick.
    __attribute__((noinline)) void tick_pass(i32* y, u32 n) {
        i32 tv = tick.v, lp = tick_lp;
        const i32 tk = tick.k, ta = tick_a;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            if (CURIO_LIKELY(tv > 4096)) {
                tv = mul(tv, tk);
                const i32 x = lev(noise(r), tv);
                lp += mul(ta, x - lp);
                y[j] += x - lp;
            }
        }
        tick.v = tv;
        tick_lp = lp;
        rng = r;
    }
    // The sizzle, driven into a soft clip: denser and less peaky, so a hat
    // carries its loudness under the kit's ceiling.
    __attribute__((noinline)) void sizzle_pass(i32* y, u32 n) {
        i32 ev = env[0].v, y1 = hi.y1, y2 = hi.y2;
        u32 r = srng;
        // The band driven straight (unity at its peak), its loudness gain and
        // the sizzle's level taken at its input, Q28.
        const i32 gq = dsp::lo(lev(gn(hik.g >> 3, band_gain_now), sizzle_level), U(7.9));
        const i32 ek = env[0].k, c = hik.c, r2 = hik.r2;
        if (ev <= 4096) {   // under its floor until the next click: only the envelope moves
            for (u32 j = 0; j < n; ++j) ev = mul(ev, ek);
            env[0].v = ev;
            return;
        }
        for (u32 j = 0; j < n; ++j) {
            ev = mul(ev, ek);
            const i32 o = twice(mul(c, y1) - mul(r2, y2)) + lev(noise(r), gq);
            y2 = y1;
            y1 = o;
            y[j] += soft(lev(o, ev) << 1) >> 1;   // flat from 0.75
        }
        env[0].v = ev;
        srng = r;
        hi.y1 = y1;
        hi.y2 = y2;
    }
    // The wing's flick.
    __attribute__((noinline)) void wing_pass(i32* y) {
        i32 wv = wing.v, av = wingatt.v, y1 = wingr.y1, y2 = wingr.y2;
        u32 r = wrng;
        const i32 wk = wing.k, ak = wingatt.k, c = wingk.c, r2 = wingk.r2, g = gn(wingk.g, wing_gain);   // driven straight, unit RMS
        i32 h[frames / 2];
        for (u32 j = 0; j < frames / 2; ++j) {
            wv = mul(wv, wk);
            av = mul(av, ak);
            const i32 o = twice(mul(c, y1) - mul(r2, y2)) + mul(noise(r), g);
            y2 = y1;
            y1 = o;
            h[j] = lev(o, wv - av);
        }
        wing.v = wv;
        wingatt.v = av;
        wrng = r;
        wingr.y1 = y1;
        wingr.y2 = y2;
        up_into(h, y, wlast);
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
    // The next tooth flicks the slat.
    __attribute__((noinline)) void next_tooth() {
        const i32 a = mul(U(1.0) - static_cast<i32>(static_cast<u32>(U(0.3) >> 8) * k / n << 8),
                          full - mul(sc(F(0.9), c[7]), draw(rng) << 19));
        tooth(a);
        ++k;
        gap = (per >> 4) * static_cast<i32>(ex2(static_cast<i32>(k) * slow_step + (16 << 12)) >> 4) >> 8;
        if (gap > 66150) gap = 66150;
        due += static_cast<u32>(gap);
        next = k < n ? jittered(due) : 0;
    }
    // The tick of each tooth.
    __attribute__((noinline)) void tick_pass(i32* y, u32 n) {
        i32 v = tick.v, lp = tick_lp;
        const i32 tk = tick.k, a = tick_a;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            v = mul(v, tk);
            const i32 x = lev(noise(r), v);
            lp += mul(a, x - lp);
            y[j] += x - lp;
        }
        tick.v = v;
        tick_lp = lp;
        rng = r;
    }
    // The block in passes, each with its state in registers, split at the teeth.
    void run(i32* out) {
        for (u32 i = 0; i < frames; ++i) out[i] = 0;
        const u32 t0 = t;
        for (u32 i = 0;;) {
            u32 e = frames;
            if (k < n) { const u32 d = next - t0; if (d < e && d >= i) e = d; }
            if (e > i) {
                const u32 m = e - i;
                ring_bank(slat, slatk, 3, out + i, m);
                ring_into(spring, springk, out + i, m);
                ring_into(box, boxk, out + i, m);
                tick_pass(out + i, m);
            }
            i = e;
            if (i >= frames) break;
            t = t0 + i;
            if (k < n && t == next) next_tooth();
        }
        t = t0 + frames;
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
    i32 late_k64;   // the stragglers' fall over a block
    i32 dk64, uk64, bdk64, buk64;   // the envelopes' decays over a block
    u32 srng;                       // the swish's own noise

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        srng = (x.seed * 2654435761u) | 1u;
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
        dk64 = down.k;
        uk64 = up.k;
        bdk64 = bump_down.k;
        buk64 = bump_up.k;
        for (int i = 0; i < 6; ++i) {
            dk64 = mul(dk64, dk64);
            uk64 = mul(uk64, uk64);
            bdk64 = mul(bdk64, bdk64);
            buk64 = mul(buk64, buk64);
        }
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
        late_k64 = late_env.k;
        for (int i = 0; i < 6; ++i) late_k64 = mul(late_k64, late_k64);
        // They straggle on up to 0.3 s past the shake (one draw a sample
        // decides seeds and stragglers alike, so under the shake they hide).
        late_end = samples(static_cast<u32>(160000 + sc(500000, c[1]) + sc(300000, c[7])));
        late_norm = sc(U(1.4), c[7]);
    }
    bool waiting() const { return late_p && t < late_end; }
    // The block in passes, each with its state in registers; the noise is
    // drawn in the same order as ever.
    // The shake's envelope and the tilt's pulse.
    __attribute__((noinline)) void env_pass(i32* eb, i32* bb) {
        i32 dv = down.v, uv = up.v, bd = bump_down.v, bu = bump_up.v;
        const i32 dk = down.k, uk = up.k, bdk = bump_down.k, buk = bump_up.k;
        for (u32 j = 0; j < frames; ++j) {
            const u32 tt = t + j;
            // The pulse (two decays apart) peaks at 0.43 of its size.
            if (CURIO_UNLIKELY(tt == bump_at && bump_amp)) bd = bu = bump_amp;
            dv = mul(dv, dk);
            uv = mul(uv, uk);
            i32 e = dv - uv;
            bd = mul(bd, bdk);
            bu = mul(bu, buk);
            bb[j] = bd - bu;
            if (CURIO_UNLIKELY(tt + 882 > stop)) e = tt >= stop ? 0 : mul(e, static_cast<i32>((stop - tt) * 2434995u));   // / 882
            eb[j] = e;
        }
        down.v = dv;
        up.v = uv;
        bump_down.v = bd;
        bump_up.v = bu;
    }
    // Seeds: a strike when the draw falls under the envelope's share of the
    // rate, planned once a block from its middle; a straggler when it falls
    // in the scatter's share just above. One draw a sample.
    __attribute__((noinline)) void seed_pass(const i32* eb, const i32* bb, i32* ib, i32* lb) {
        u32 r = rng;
        const i32 rp = static_cast<i32>(rate_p >> 1), pi = peak_inv << 16;
        const u32 p = (static_cast<u32>(mul(rp, mul(eb[frames / 2], pi) << 1)) << 1) +
                      (static_cast<u32>(mul(rp, bb[frames / 2])) << 3);   // up to 1.7 times the shake's rate
        u32 q = p;
        i32 la = 0;
        if (late_p && t + frames / 2 > 2205 && t + frames / 2 < late_end) {
            q = p + late_p;
            // A straggler rings its own resonance, a little under the shell's.
            la = late_env.v >> 3;
        }
        for (u32 j = 0; j < frames; ++j) {
            noise(r);
            const u32 x = r;
            i32 imp = 0, late = 0;
            if (CURIO_UNLIKELY(x < q)) {
                if (x < p) imp = one / 2 + static_cast<i32>(static_cast<u32>(one) >> 12) * (draw(r));   // 0.5 .. 1.5
                else late = la;
            }
            ib[j] = imp;
            lb[j] = late;
        }
        rng = r;
        if (t + frames > 2205) late_env.v = mul(late_env.v, late_k64);
    }
    // A shell resonance rung by the seeds, weighted into y.
    template <bool weighted> __attribute__((noinline)) static void shell_pass(Res& z, const ResK& k, const i32* ib, i32* y, i32 w) {
        i32 y1 = z.y1, y2 = z.y2;
        const i32 c = k.c, r2 = k.r2, g = k.g;
        for (u32 j = 0; j < frames / 2; ++j) {   // two samples a turn: the states trade places
            y2 = twice(mul(c, y1) - mul(r2, y2)) + mul(ib[0], g);
            y[0] += weighted ? mul(y2, w) : y2;
            y1 = twice(mul(c, y2) - mul(r2, y1)) + mul(ib[1], g);
            y[1] += weighted ? mul(y1, w) : y1;
            ib += 2;
            y += 2;
        }
        z.y1 = y1;
        z.y2 = y2;
    }
    // The seeds' level, and the dry pod's crackle.
    __attribute__((noinline)) void dry_pass(i32* y) {
        const i32 nm = norm;
        if (c[4] > 0) {
            i32 lp = dry_lp;
            const i32 a = dry_a, wet = F(1.0) - sc(F(0.6), c[4]), crisp = sc(F(0.75), c[4]);
            for (u32 j = 0; j < frames; ++j) {
                const i32 v = gn(y[j], nm);
                lp += mul(a, v - lp);
                y[j] = mul(v, wet) + mul((v - lp), crisp) * 4;
            }
            dry_lp = lp;
        } else {
            for (u32 j = 0; j < frames; ++j) y[j] = gn(y[j], nm);
        }
    }
    // The swish of the shake: its level (with the band's gain) glided
    // across the block from its two ends.
    __attribute__((noinline)) void swish_pass(i32* y, const i32* eb, const i32* bb) {
        i32 y1 = sw.y1, y2 = sw.y2, x1 = sw.x1, x2 = sw.x2;
        u32 r = srng;
        const i32 c = swk.c, r2 = swk.r2, b = swk.b, g = sw_gain, amp = sw_amp;
        const i32 l0 = gn(lev(amp, (eb[0] >> 3) + (bb[0] >> 1)), g);
        const i32 l1 = gn(lev(amp, (eb[frames - 1] >> 3) + (bb[frames - 1] >> 1)), g);
        const i32 dl = (l1 - l0) >> 6;
        i32 l = l0;
        for (u32 j = 0; j < frames; j += 2) {   // two samples a turn: the states trade places
            const i32 i0 = mul(noise(r), b);
            y2 = twice(mul(c, y1) - mul(r2, y2)) + i0 - x2;
            x2 = i0;
            y[j] += lev(y2, l);
            l += dl;
            const i32 i1 = mul(noise(r), b);
            y1 = twice(mul(c, y2) - mul(r2, y1)) + i1 - x1;
            x1 = i1;
            y[j + 1] += lev(y1, l);
            l += dl;
        }
        srng = r;
        sw.y1 = y1;
        sw.y2 = y2;
        sw.x1 = x1;
        sw.x2 = x2;
    }
    // Scatter: stragglers after 50 ms, ringing their own resonance.
    __attribute__((noinline)) void late_pass(i32* y, const i32* lb) {
        const i32 nm = norm, ln = late_norm;
        drive_run(late, latek, y, frames, [&lb, this](i32) { return mul(*lb++, latek.g); },
                  [nm, ln](i32& to, i32 o) { to += lev(gn(o, nm), ln); });
    }
    // The shake's envelope and the tilt's pulse planned at the block's ends
    // and glided between them (they move slowly).
    __attribute__((noinline)) void ramp_pass(i32* eb, i32* bb, bool shaking) {
        const i32 e0 = down.v - up.v, b0 = bump_down.v - bump_up.v;
        down.v = mul(down.v, dk64);
        up.v = mul(up.v, uk64);
        bump_down.v = mul(bump_down.v, bdk64);
        bump_up.v = mul(bump_up.v, buk64);
        const i32 e1 = shaking ? down.v - up.v : 0, b1 = bump_down.v - bump_up.v;
        const i32 de = shaking ? (e1 - e0) >> 6 : 0, db = (b1 - b0) >> 6;
        i32 e = shaking ? e0 : 0, b = b0;
        for (u32 j = 0; j < frames; ++j) {
            e += de;
            b += db;
            eb[j] = e;
            bb[j] = b;
        }
    }
    void run(i32* out) {
        i32 eb[frames], bb[frames], ib[frames], lb[frames];
        // Sample by sample only where the tilt's pulse starts or the shake
        // fades out.
        const bool bumping = bump_amp && bump_at - t < frames;
        const bool fading = t + frames - 1 + 882 > stop && t < stop;
        if (bumping || fading) env_pass(eb, bb);
        else ramp_pass(eb, bb, t < stop);
        seed_pass(eb, bb, ib, lb);
        for (u32 i = 0; i < frames; ++i) out[i] = 0;
        shell_pass<false>(r[0], rk[0], ib, out, 0);
        shell_pass<true>(r[1], rk[1], ib, out, F(0.7));
        shell_pass<true>(r[2], rk[2], ib, out, F(0.5));
        dry_pass(out);
        swish_pass(out, eb, bb);
        // Scatter: the stragglers' resonance, while they come and ring on.
        if (late_p && t + frames > 2205 && t < late_end + 4410) late_pass(out, lb);
        t += frames;
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
    // The block in passes, each with its state in registers, split where the
    // draw ends; the noise is drawn in the same order as ever.
    // The draw: the head stick-slips, ringing its squeal.
    __attribute__((noinline)) void draw_pass(i32* y, u32 n) {
        i32 at = sc_att;
        const i32 ak = sc_attk, g = sck.g, nm = sc_norm, amp = sc_amp;
        const u32 p = slip_p;
        u32 r = rng;
        drive_run(sc_, sck, y, n, [&r, p, g](i32) { noise(r); return mul(r < p ? one : 0, g); },
                  [&at, ak, nm, amp](i32& to, i32 o) { at = mul(at, ak); to = lev(gn(o, nm), lev(amp, (full - at) >> 3)); });
        sc_att = at;
        rng = r;
    }
    // The flare's envelope (an eighth, as the parts take it).
    __attribute__((noinline)) void flare_pass(i32* eb, u32 n) {
        i32 dv = fl_d.v, av = fl_a.v;
        const i32 dk = fl_d.k, ak = fl_a.k;
        for (u32 j = 0; j < n; ++j) {
            dv = mul(dv, dk);
            av = mul(av, ak);
            eb[j] = (dv - av) >> 3;
        }
        fl_d.v = dv;
        fl_a.v = av;
    }
    // The flame's noise, and the phosphorus's crackle, in draw order.
    template <bool phos_on> __attribute__((noinline)) void noise_pass(const i32* eb, i32* nb, i32* cb, u32 n) {
        u32 r = rng;
        const u32 p = phos_p;
        for (u32 j = 0; j < n; ++j) {
            nb[j] = noise(r);
            if (phos_on) {
                noise(r);
                cb[j] = CURIO_UNLIKELY(r < p) ? lev(noise(r), eb[j]) : 0;
            }
        }
        rng = r;
    }
    // The head's squeal ringing on after the draw.
    __attribute__((noinline)) void squeal_pass(i32* y, u32 n) {
        const i32 nm = sc_norm, lv = lev(sc_amp, (full - sc_att) >> 3);
        drive_run(sc_, sck, y, n, [](i32) { return 0; }, [nm, lv](i32& to, i32 o) { to = lev(gn(o, nm), lv); });
    }
    __attribute__((noinline)) void flame_pass(i32* y, const i32* eb, const i32* nb, u32 n) {
        const i32 g = fl_gain, amp = fl_amp;
        bp_run(fl, flk, nb, y, n, [&eb, g, amp](i32& to, i32 o) { to += lev(lev(gn(o, g), *eb++), amp); });
    }
    __attribute__((noinline)) void phos_pass(i32* y, const i32* cb, u32 n) {
        const i32 g = phosk.g, amp = phos_amp;
        drive_run(phos, phosk, y, n, [&cb, g](i32) { return mul(*cb++, g); }, [amp](i32& to, i32 o) { to += lev(o, amp); });
    }
    // The burning wood's roar under the flare, and the puff.
    template <bool wood_on> __attribute__((noinline)) void wood_pass(i32* y, const i32* eb, const i32* nb, u32 n) {
        i32 wl = wood_lp, pl = puff_lp, pv = puff.v;
        const i32 wa = wood_a, amp = wood_amp, pa = puff_a, pk = puff.k;
        for (u32 j = 0; j < n; ++j) {
            const i32 x = nb[j];
            if (wood_on) {
                wl += mul(wa, x - wl);
                y[j] += lev(lev(wl, eb[j]), amp);
            }
            if (pv > 4096) {
                pv = mul(pv, pk);
                pl += mul(pa, lev(x, pv) - pl);
                y[j] += pl;
            }
        }
        wood_lp = wl;
        puff_lp = pl;
        puff.v = pv;
    }
    void run(i32* out) {
        // The draw's samples in this block, then the flame's.
        const u32 d = t < ds ? (ds - t < frames ? ds - t : frames) : 0;
        if (d) draw_pass(out, d);
        if (d < frames) {
            const u32 n = frames - d;
            i32* y = out + d;
            i32 eb[frames], nb[frames], cb[frames];
            flare_pass(eb, n);
            if (phos_amp) noise_pass<true>(eb, nb, cb, n);
            else noise_pass<false>(eb, nb, cb, n);
            squeal_pass(y, n);
            flame_pass(y, eb, nb, n);
            if (phos_amp) phos_pass(y, cb, n);
            if (wood_amp) wood_pass<true>(y, eb, nb, n);
            else if (puff.v > 4096) wood_pass<false>(y, eb, nb, n);
        }
        t += frames;
    }
};
}
