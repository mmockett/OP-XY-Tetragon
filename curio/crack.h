#pragma once
// The CRACK drawer: ICE, SHEET, SPARK and CORK.
#include "thump.h"

namespace curio {
using namespace dsp;

// The chance of an event each sample at a rate in hertz, as a u32 threshold.
inline u32 per_sample(u32 rate_hz) { return rate_hz * 97392u; }
// Heavy-tailed noise for cracks and crackle: n |n| (spikier than white, the
// same RMS within a few percent).
inline i32 spiky(u32& rng) {
    const i32 n = noise(rng);
    return mul(n, n < 0 ? -n : n) << 2;
}

// ---------------------------------------------------------------------- ICE
// Lake ice cracking under load: each crack is a crisp snap that sends a
// dispersive chirp across the sheet (the "pew": high notes arrive first),
// over the thump of the lake beneath and the crunch of the surface.
struct Ice {
    u32 rng, t;
    i32 c[8], tune;
    // The chirps: a falling sine and a band of noise riding it, three in turn.
    struct Chirp { u32 ph, f, df; i32 u, v, a; Res z; ResK k; i32 hi_inc, lo_inc; };
    Chirp ch[2];
    u32 turn, newest;
    Res zr;
    ResK zk;
    i32 uk, chirp_amp, chk, chak, zing, zgate;
    // Cracks: the snap's envelope; the shards and the far shore's echoes.
    Env crack;
    i32 crack_k, crack_amp, crack_lp, crack_a;
    u32 shards, next_shard, echo, next_echo;
    i32 shard_amp, spread_scale;
    // The lake under the ice.
    Tone body;
    Env blip;
    i32 body_amp;
    // The surface: band noise and its crunch of micro-cracks.
    Res nz;
    ResK nzk;
    Env hiss, grain;
    i32 hiss_amp, hiss_gain, grain_rate;
    u32 grain_p;
    i32 drive, undrive;

    void snap(i32 g, bool echo_hit) {
        // A crack and its chirp.
        crack.v += mul(crack_amp, echo_hit ? mul(g, F(0.4)) : g);
        Chirp& h = ch[turn];
        newest = turn;
        turn ^= 1u;   // two in turn: a third rarely sounded over them
        h.ph = 0;
        h.u = full;
        h.f = static_cast<u32>(h.lo_inc + h.hi_inc);
        h.df = 0;
        h.v = h.a = mul(mul(chirp_amp, g), F(0.75));
        h.z = Res();
        if (echo_hit) {
            body.hit(mul(body_amp, g), body.vk, decay(1000));
            hiss.v += lev(mul(hiss_amp, mul(g, F(0.5))), hiss_gain << 12);
        }
    }
    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        turn = 0;
        newest = 0;
        zr = Res();
        echo = 0;
        const i32 sheet = c[1];
        // The chirp falls from fhi to flo as 1 / (1 + t / tc)^2.
        const i32 fhi = 10000 - sc(7000, sheet), flo = 120 + sc(400, 4095 - sheet);
        const u32 hi = inc(lg2(static_cast<u32>(fhi)) + tune), lo_ = inc(lg2(static_cast<u32>(flo)) + tune);
        const i32 tc_us = 8000 + sc(80000, sheet);
        uk = static_cast<i32>(ex2(145421 - lg2(static_cast<u32>(tc_us))));   // 1 / (tc 44100), Q31
        for (int s = 0; s < 2; ++s) {
            ch[s].v = ch[s].a = 0;
            ch[s].u = 0;
            ch[s].hi_inc = static_cast<i32>(hi - lo_);
            ch[s].lo_inc = static_cast<i32>(lo_);
            ch[s].z = Res();
        }
        const i32 tch = 20000 + sc(310000, pw(c[4], C(1.6)));
        chk = decay(static_cast<u32>(tch));
        chak = mul(chk, decay(800));
        chirp_amp = U(0.08) + sc(U(0.7), c[3]);
        zgate = mul(chirp_amp, F(0.75 / 16));
        zing = band_gain(hz(2000), C(8.0));
        // The snap: 4 ms of high-passed, spiky noise.
        crack.v = 0;
        crack.k = decay(static_cast<u32>(1800 - sc(800, c[0])));
        crack_amp = U(0.8) + sc(U(1.6), c[0]);
        crack_lp = 0;
        crack_a = pole(hz(1200));
        // The lake: 150..260 Hz with a blip of pitch.
        body.ph = 0;
        body.v = body.a = 0;
        body.inc = inc(lg2(static_cast<u32>(300 - sc(170, sheet))) + tune);
        body.vk = decay(static_cast<u32>(20000 + sc(80000, c[6])));
        body_amp = U(0.35) + sc(U(1.3), c[6]);
        body.hit(body_amp, body.vk, decay(1000));
        blip.v = full;
        blip.k = decay(10000);
        // The surface: band noise 2.5..7.5 kHz, broader when warm.
        const i32 lpn = lg2(static_cast<u32>(2500 + sc(5000, c[5]))) + C(0.4) - sc(C(0.8), sheet);   // thick ice is darker
        const i32 q = C(0.6) + sc(C(1.2), c[5]);
        band(nzk, lpn, q);
        hiss_gain = band_gain(lpn, q);
        nz = Res();
        hiss_amp = mul(U(0.5), F(1.0) - sc(F(0.6), c[3]));
        hiss.v = lev(hiss_amp, hiss_gain << 12);   // the band's loudness gain carried in the level
        hiss.k = decay(static_cast<u32>(mul(25000 + sc(200000, pw(c[4], C(1.3))), F(0.5) + sc(F(0.5), sheet))));   // and rings longer
        // The crunch: micro-cracks in the surface, more and crisper when cold.
        grain.v = 0;
        grain.k = decay(250);
        grain_p = per_sample(static_cast<u32>(600 + sc(3400, c[5])));
        // Shards: one crack to a crackle of eight, each 0.8 of the last.
        shards = static_cast<u32>(c[2] * 7 >> 12);
        shard_amp = full;
        next_shard = shards ? samples(static_cast<u32>(mul(8000 + (draw(rng) * 30000 >> 12), F(1.0) - sc(F(0.5), c[2])) << 0)) : 0;
        spread_scale = F(0.6 * 0.5) + sc(F(0.8 * 0.5), sheet);   // half of (0.6 + 0.8 sheet)
        next_echo = samples(static_cast<u32>(mul(70000, spread_scale) << 1));
        drive = (1 << 24) + sc(25165824, c[0]);   // Q24: 1 + 1.5 load
        {
            const i32 th = tanh((drive >> 12) << 16);
            undrive = static_cast<i32>((static_cast<u32>(1) << 30) / static_cast<u32>(th >> 14));
        }
        snap(full, false);
    }
    bool waiting() const { return shards > 0 || (c[7] > 0 && echo < 4); }
    // The block in passes, each with its state in registers, split at the
    // shards and echoes; the noise is drawn in the same order as ever.
    // A chirp into y; the newest also leaves its level where its band of
    // noise sounds, and zero where it rests (it sounds only well above zero).
    template <bool band> __attribute__((noinline)) void chirp_pass(Chirp& h, i32* y, i32* lz, u32 n) {
        u32 ph = h.ph, f = h.f;
        const u32 df = h.df;
        i32 v = h.v, a = h.a;
        const i32 k = chk, ka = chak, gate = zgate;
        for (u32 j = 0; j < n; ++j) {
            const bool zon = v - a > gate;   // under -24 dB of its start the band is lost under the tone: it rests
            ph += f;
            f += df;
            v = mul(v, k);
            if (CURIO_UNLIKELY(a)) { a = mul(a, ka); if (a < 64) a = 0; }
            const i32 level = v - a;   // three quarters of the chirp's level (start)
            y[j] += mul(level, sine(ph));
            if (band) lz[j] = zon ? level : 0;
        }
        h.ph = ph;
        h.f = f;
        h.v = v;
        h.a = a;
    }
    // The snap, and the noise for the band and the surface in draw order.
    template <bool cracking> __attribute__((noinline)) void noise_pass(i32* y, const i32* lz, i32* zn, i32* hn, opxy::u8* gt, u32 n) {
        i32 cv = crack.v, lp = crack_lp, hv = hiss.v;
        const i32 ck = crack.k, ca = crack_a, hk = hiss.k;
        const u32 gp = grain_p;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            if (cracking && cv > 4096) {
                cv = mul(cv, ck);
                const i32 cr = lev(spiky(r), cv);
                lp += mul(ca, cr - lp);
                y[j] += cr - lp;
            }
            if (CURIO_LIKELY(lz[j])) zn[j] = noise(r);
            if (CURIO_LIKELY(hv > 4096)) {
                hv = mul(hv, hk);
                gt[j] = r < gp;
                hn[j] = noise(r);
            }
        }
        crack.v = cv;
        crack_lp = lp;
        rng = r;
    }
    // The band of noise riding the newest chirp's pitch.
    __attribute__((noinline)) void band_pass(i32* y, const i32* lz, const i32* zn, u32 n) {
        i32 y1 = zr.y1, y2 = zr.y2, x1 = zr.x1, x2 = zr.x2;
        const i32 c = zk.c, r2 = zk.r2, b = zk.b, g = zing;
        for (u32 j = 0; j < n; ++j) {
            const i32 l = lz[j];
            if (CURIO_LIKELY(l)) {
                const i32 in = mul(zn[j], b);
                const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
                x2 = x1;
                x1 = in;
                y2 = y1;
                y1 = o;
                y[j] += lev(mul(gn(o, g), F(0.35 / 0.75)), l);
            }
        }
        zr.y1 = y1;
        zr.y2 = y2;
        zr.x1 = x1;
        zr.x2 = x2;
    }
    // Load drives the crack and the chirps (quiet samples pass the curve's
    // straight foot); the lake and the surface stay clean beneath them, so
    // the crack stands out of the body.
    __attribute__((noinline)) void drive_pass(const i32* x, i32* out, u32 n) {
        const i32 dr = drive, ud = undrive << 14;
        for (u32 j = 0; j < n; ++j) {
            const i32 d = mul(x[j], dr);
            const i32 s = d < 400000 && d > -400000 ? d << 7 : d >= (1 << 23) ? U(0.99933) : d <= -(1 << 23) ? -U(0.99933) : tanh(d << 7);
            out[j] = mul(s, ud) << 1;
        }
    }
    // The lake.
    __attribute__((noinline)) void body_pass(i32* y, u32 n) {
        u32 ph = body.ph;
        const u32 in = body.inc;
        i32 v = body.v, a = body.a, bv = blip.v;
        const i32 vk = body.vk, ak = body.ak, bk = blip.k;
        if (v <= 4096) return;   // until the next echo
        for (u32 j = 0; j < n; ++j) {
            if (CURIO_LIKELY(v > 4096)) {
                ph += in;
                if (CURIO_UNLIKELY(bv > (1 << 24))) { bv = mul(bv, bk); ph += static_cast<u32>(mul(static_cast<i32>(in), mul(F(0.6), bv))); }
                v = mul(v, vk);
                if (CURIO_UNLIKELY(a)) { a = mul(a, ak); if (a < 64 && a > -64) a = 0; }
                y[j] += mul(v - a, sine(ph));
            }
        }
        body.ph = ph;
        body.v = v;
        body.a = a;
        blip.v = bv;
    }
    // The surface: noise with a crunch of micro-cracks.
    __attribute__((noinline)) void hiss_pass(i32* y, const i32* hn, const opxy::u8* gt, u32 n) {
        i32 hv = hiss.v, gv = grain.v;
        const i32 hk = hiss.k, gk = grain.k;
        i32 y1 = nz.y1, y2 = nz.y2, x1 = nz.x1, x2 = nz.x2;
        const i32 c = nzk.c, r2 = nzk.r2, b = nzk.b;
        if (hv <= 4096) return;   // until the next echo
        for (u32 j = 0; j < n; ++j) {
            if (CURIO_LIKELY(hv > 4096)) {
                if (gt[j]) gv = one;
                hv = mul(hv, hk);
                i32 level = hv;
                if (gv) { gv = mul(gv, gk); level += lev(level, gv); if (gv < 4096) gv = 0; }
                const i32 in = mul(hn[j], b);
                const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
                x2 = x1;
                x1 = in;
                y2 = y1;
                y1 = o;
                y[j] += lev(o, level);
            }
        }
        hiss.v = hv;
        grain.v = gv;
        nz.y1 = y1;
        nz.y2 = y2;
        nz.x1 = x1;
        nz.x2 = x2;
    }
    // n samples between events: the chirps (the newest carries a band of
    // noise riding its pitch), the snap and the noise, the band, the drive,
    // the lake and the surface.
    __attribute__((noinline)) void span(i32* out, u32 n) {
        i32 x[frames], lz[frames], zn[frames], hn[frames];
        opxy::u8 gt[frames];
        for (u32 j = 0; j < n; ++j) x[j] = 0;
        for (u32 s = 0; s < 2; ++s) {
            Chirp& h = ch[s];
            const bool live = h.v || h.a;
            if (s == newest) {
                if (live) chirp_pass<true>(h, x, lz, n);
                else for (u32 j = 0; j < n; ++j) lz[j] = 0;
            } else if (live) {
                chirp_pass<false>(h, x, lz, n);
            }
        }
        // The snap, once under its floor, stays there until the next crack.
        if (crack.v > 4096) noise_pass<true>(x, lz, zn, hn, gt, n);
        else noise_pass<false>(x, lz, zn, hn, gt, n);
        band_pass(x, lz, zn, n);
        drive_pass(x, out, n);
        body_pass(out, n);
        hiss_pass(out, hn, gt, n);
    }
    // A shard or an echo at t.
    __attribute__((noinline)) void events() {
        static const u32 delay[4] = {70000, 130000, 210000, 300000};
        static const i32 gain[4] = {F(0.8), F(0.6), F(0.4), F(0.25)};
        if (shards && t == next_shard) {
            --shards;
            shard_amp = mul(shard_amp, F(0.88));
            snap(shard_amp, false);
            next_shard = shards ? t + samples(static_cast<u32>(mul(8000 + (draw(rng) * 30000 >> 12), F(1.0) - sc(F(0.5), c[2])) << 0)) : 0;
        }
        if (c[7] > 0 && echo < 4 && t == next_echo) {
            snap(sc(gain[echo], c[7]), true);
            ++echo;
            if (echo < 4) next_echo = samples(static_cast<u32>(mul(static_cast<i32>(delay[echo]), spread_scale) << 1));
        }
    }
    void run(i32* out) {
        // Each chirp's pitch for the block: u = 1 / (1 + t / tc) steps on in
        // four quarters, the pitch glides between the ends (a ramp a sample),
        // and its noise band follows. A chirp gone quiet stops.
        for (int s = 0; s < 2; ++s) {
            Chirp& h = ch[s];
            if (!h.v && !h.a) continue;
            if (h.v < 16384 && h.v > -16384) { h.v = h.a = 0; continue; }
            const u32 f0 = static_cast<u32>(h.lo_inc + mul(h.hi_inc, mul(h.u, h.u)));
            for (int q = 0; q < 4; ++q) h.u -= mul(mul(h.u, h.u), uk) << 4;
            const u32 f1 = static_cast<u32>(h.lo_inc + mul(h.hi_inc, mul(h.u, h.u)));
            h.f = f0;
            h.df = static_cast<u32>(static_cast<i32>(f1 - f0) >> 6);
            if (s == static_cast<int>(newest)) tune_band(zk, f0);
        }
        const u32 t0 = t;
        for (u32 i = 0;;) {
            // The next shard or echo in this block.
            u32 e = frames;
            if (shards) { const u32 d = next_shard - t0; if (d < e && d >= i) e = d; }
            if (c[7] > 0 && echo < 4) { const u32 d = next_echo - t0; if (d < e && d >= i) e = d; }
            if (e > i) span(out + i, e - i);
            i = e;
            if (i >= frames) break;
            t = t0 + i;
            events();
        }
        t = t0 + frames;
    }
    static void tune_band(ResK& k, u32 f) {
        // A band of quality 8 at the chirp's pitch.
        i32 s, c;
        sincos(f, s, c);
        const i32 r = full - mul(static_cast<i32>(f), F(3.14159265 / 16.0));   // 1 - pi f / (8 rate)
        k.c = mul(r, c) << 1;
        k.r2 = mul(r, r) >> 1;
        k.s = s << 1;
        k.g = mul(full - r, s) << 2;
        k.b = full - r;
    }
};

// -------------------------------------------------------------------- SHEET
// A thunder sheet: a hung iron sheet struck, then shaken. Twelve of its
// modes ring from the blow and every shake; the frame rattles; the energy
// cascades upward into a hiss of high modes as it rings; a roll of thunder.
// Twelve modes stand for the prototype's 44, weighted for those they replace.
struct Sheet {
    static const i32 ROLL = 4;   // a roll's stroke rings the modes 16 times its size of the blow's share (as loud as the noise did)
    u32 rng, t;
    i32 c[8], tune;
    Res mode[16];
    ResK modek[16];
    i32 gain[16], amp[16];
    u32 incs[16];
    u32 modes;
    Env burst, clank;
    i32 burst_lp, burst_a, clank_lp, clank_a;
    u32 lump_at[15];
    i32 lump_amp[15];
    u32 lumps, lump;
    // The blow's own noise, the same every hit: 2 ms of noise rings the low
    // modes by chance, which swung the hit's level by 12 dB from hit to hit.
    u32 brng;
    // The rattle and the cascade.
    i32 follow, follow_a, slow, slow_a, sum;
    static i32 pow64(i32 k) { for (int i = 0; i < 6; ++i) k = mul(k, k); return k; }
    Res rat;
    ResK ratk;
    i32 rat_amp, rat_gain, sheen_amp;
    u32 ph61, phth;
    u32 inc_th;
    // The thunder.
    Env thunder, thatt;
    i32 th_lp, th_a;

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        const i32 lpb = hz(130) - sc(C(1.6), c[0]) + tune;
        const i32 th = 150000 + sc(3200000, pw(c[3], C(1.6)));
        const i32 dex = -C(0.6) + sc(C(0.5), c[5]);
        const i32 aex = -C(1.5) + sc(C(1.3), c[5]);
        // exp(-rel / (4 + 60 beater)): a soft mallet leaves the high modes still.
        const i32 roll = C(4.0) + sc(C(60.0), c[6]);
        const u32 inv_roll = (static_cast<u32>(1) << 24) / static_cast<u32>(roll);   // Q12 of 1 / roll, Q24 / Q12
        i32 sum = 0;
        modes = 0;
        for (int m = 0; m < 12; ++m) {
            const i32 lr = lg2(table::sheet_ratio[m]) - (12 << 12);
            const i32 lp = lpb + lr;
            if (lp > 56300) break;   // 14 kHz
            const u32 t60 = ex2(lg2(static_cast<u32>(th)) + (lr * (dex >> 2) >> 10));
            incs[m] = inc(lp);
            dsp::tune(modek[m], incs[m], decay60(t60 < 30000 ? 30000 : t60));
            // amplitude rel^aex exp(-rel / roll), Q28
            const i32 rel = static_cast<i32>(table::sheet_ratio[m]);                  // Q12
            const i32 e = rel * static_cast<i32>(inv_roll) >> 12;                     // rel / roll, Q12
            amp[m] = static_cast<i32>(ex2((28 << 12) + (lr * (aex >> 2) >> 10) - ((e * 5909) >> 12)));
            amp[m] = mul(amp[m], static_cast<i32>(table::sheet_weight[m]) << 17) << 2;
            sum += amp[m] >> 4;   // Q24
            mode[m] = Res();
            ++modes;
        }
        // The prototype scales its sheet to a peak of 1: about the modes' sum.
        // The blow's noise rings the modes far harder than its impulse: the
        // prototype's peak grows about 1 + 3 b with the burst's level b.
        const i32 blow = U(0.2) + sc(U(2.0), c[6]);
        sum = mul(sum, (U(1.0) + 2 * blow) >> 4) << 7;   // 3 b in the prototype: an iron rod now strikes louder
        if (sum < (1 << 21)) sum = 1 << 21;
        const i32 normq = static_cast<i32>(((static_cast<u32>(1) << 31) / static_cast<u32>(sum >> 9)) << 12);   // Q28
        // At half level: the sheet's loudest blows and shakes ring far over unity.
        for (u32 m = 0; m < modes; ++m) gain[m] = mul(lev(amp[m], normq), modek[m].s) >> 1;
        // The blow: an impulse and 20 ms of noise, louder for an iron rod.
        burst.v = U(0.2) + sc(U(2.0), c[6]);
        // The beater's blow: a felt mallet's is dull, an iron rod's bright.
        burst_lp = 0;
        burst_a = pole(hz(300) + sc(C(5.5), c[6]));
        burst.k = decay(2000);
        brng = 0x2545f491u;
        // The blow rings each mode from its own phase (the prototype's), so
        // they don't all peak together and the low cluster beats as it did.
        for (u32 m = 0; m < modes; ++m) {
            const i32 at = lev(amp[m], normq) >> 1;
            const u32 phase = table::sheet_phase[m];
            mode[m].y1 += mul(at, sine(phase));
            mode[m].y2 += mul(at, sine(phase - incs[m]));
            amp[m] = at;   // each mode's share of a blow, for the roll's strokes
        }
        // Shakes in the first 40..490 ms, then the roll's strokes.
        lumps = 0;
        const u32 shakes = static_cast<u32>(c[1] * 6 >> 12), strokes = static_cast<u32>(c[7] * 9 >> 12);
        const i32 span = 40000 + sc(450000, c[1]);
        for (u32 k = 0; k < strokes; ++k) {
            // An even roll, 75 ms a stroke, each a few ms off: it keeps time.
            lump_at[lumps] = samples(static_cast<u32>((k + 1) * 75000 + (jitter(rng) * 3000 >> 12)));
            // A roll's stroke strikes the modes outright (marked negative): it
            // costs nothing, where a shake's noise drives every mode a while.
            lump_amp[lumps] = -mul(U(0.7), static_cast<i32>(ex2((31 << 12) - static_cast<i32>(k) * 960)));   // 0.85^k
            ++lumps;
        }
        for (u32 k = 0; k < shakes; ++k) {
            lump_at[lumps] = samples(static_cast<u32>(sc(span, draw(rng)))) + 1;
            lump_amp[lumps] = U(0.3) + sc(U(0.4), draw(rng));
            ++lumps;
        }
        // In time order.
        for (u32 a = 1; a < lumps; ++a)
            for (u32 b = a; b > 0 && lump_at[b - 1] > lump_at[b]; --b) {
                const u32 tt = lump_at[b]; lump_at[b] = lump_at[b - 1]; lump_at[b - 1] = tt;
                const i32 aa = lump_amp[b]; lump_amp[b] = lump_amp[b - 1]; lump_amp[b - 1] = aa;
            }
        lump = 0;
        follow = slow = 0;
        // The followers' poles per block: 1 - (1 - a)^64.
        follow_a = full - pow64(full - pole(hz(40)));
        slow_a = full - pow64(full - pole(hz(6)));
        sum = 0;
        band(ratk, hz(4200), C(0.9));
        rat_gain = band_gain(hz(4200), C(0.9));
        rat_amp = U(0.15) + sc(U(1.1), c[2]);
        rat = Res();
        // The cascade: the ringing feeds a hiss of the sheet's high modes.
        // A soft mallet barely starts the cascade; an iron rod does.
        sheen_amp = sc(U(0.1) + sc(U(0.5), c[5]), C(0.25) + sc(C(0.75), c[6]));
        clank.v = sc(U(0.3), mul(c[6] << 19, c[6] << 19) >> 19);
        clank.k = decay(700);
        clank_lp = 0;
        clank_a = pole(hz(3000));
        ph61 = phth = 0;
        inc_th = static_cast<u32>(97392 * 5) + static_cast<u32>(sc(97392 * 3, c[4]));
        thunder.v = sc(U(0.45 * 8), c[4]);
        thunder.k = decay(static_cast<u32>(250000 + sc(600000, c[4])));
        thatt.v = thunder.v;
        thatt.k = mul(thunder.k, decay(40000));
        th_lp = 0;
        th_a = pole(hz(160));
    }
    bool waiting() const { return lump < lumps; }
    // The block in passes, each with its state held in registers, the same
    // sums sample for sample as ringing every mode each sample.
    // The blow's noise for n samples (zero once it has died away); whether
    // any sample sounds.
    __attribute__((noinline)) bool blow_pass(i32* xb, u32 n) {
        i32 v = burst.v, lp = burst_lp;
        const i32 k = burst.k, a = burst_a;
        u32 r = brng;
        i32 any = 0;
        if (v <= 4096) {   // until the next shake
            for (u32 j = 0; j < n; ++j) xb[j] = 0;
            return false;
        }
        for (u32 j = 0; j < n; ++j) {
            i32 x = 0;
            if (CURIO_LIKELY(v > 4096)) {
                v = mul(v, k);
                lp += mul(a, lev(noise(r), v) - lp);
                x = lp * 2;
            }
            xb[j] = x;
            any |= x;
        }
        burst.v = v;
        burst_lp = lp;
        brng = r;
        return any != 0;
    }
    // A ringing mode through n samples, driven by x when drive, added into y:
    // two samples a turn, so the last two outputs trade places, not move.
    template <bool drive> __attribute__((noinline)) static void ring_live(Res& z, const ResK& k, i32 g, const i32* x, i32* y, u32 n) {
        i32 a = z.y1, b = z.y2;   // the last output and the one before
        const i32 c = k.c, r = k.r2;
        if (n & 1) {
            i32 v = twice(mul(c, a) - mul(r, b));
            if (drive) v += lev(*x++, g);
            b = a;
            a = v;
            *y++ += v;
        }
        for (u32 j = 0; j < (n >> 1); ++j) {
            b = twice(mul(c, a) - mul(r, b));
            if (drive) b += lev(x[0], g);
            y[0] += b;
            a = twice(mul(c, b) - mul(r, a));
            if (drive) a += lev(x[1], g);
            y[1] += a;
            x += 2;
            y += 2;
        }
        z.y1 = a;
        z.y2 = b;
    }
    // A resting mode rings only where the blow drives it.
    __attribute__((noinline)) static void ring_rest(Res& z, const ResK& k, i32 g, const i32* x, i32* y, u32 n) {
        i32 y1 = z.y1, y2 = z.y2;
        const i32 c = k.c, r = k.r2;
        for (u32 j = 0; j < n; ++j) {
            const i32 xj = x[j];
            if (CURIO_LIKELY(xj)) {
                const i32 v = twice(mul(c, y1) - mul(r, y2)) + lev(xj, g);
                y2 = y1;
                y1 = v;
                y[j] += v;
            }
        }
        z.y1 = y1;
        z.y2 = y2;
    }
    // The beater's clank and the block's level; and the noise the rattle
    // and the thunder share, drawn after the clank's as before.
    template <bool clanking> __attribute__((noinline)) void clank_pass(i32* yb, i32* nb, u32 n) {
        i32 cv = clank.v, lp = clank_lp, s = sum;
        const i32 k = clank.k, a = clank_a;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            i32 y = yb[j];
            if (clanking && cv > 4096) {
                // The beater's own clank on the iron, brighter and louder for a rod.
                cv = mul(cv, k);
                const i32 ck = lev(noise(r), cv);
                lp += mul(a, ck - lp);
                y += ck - lp;
            }
            s += (y < 0 ? -y : y) >> 6;
            yb[j] = y;
            nb[j] = noise(r);
        }
        clank.v = cv;
        clank_lp = lp;
        sum = s;
        rng = r;
    }
    // The frame's rattle and the cascade share one band of noise (two
    // samples a turn, so the states trade places).
    __attribute__((noinline)) void rattle_pass(const i32* nb, i32* yb, i32 renv) {
        i32 y1 = rat.y1, y2 = rat.y2, x1 = rat.x1, x2 = rat.x2;
        const i32 c = ratk.c, r2 = ratk.r2, b = ratk.b;
        for (u32 j = 0; j < frames / 2; ++j) {
            const i32 i0 = mul(nb[0], b);
            y2 = twice(mul(c, y1) - mul(r2, y2)) + i0 - x2;
            x2 = i0;
            yb[0] += lev(y2, renv);
            const i32 i1 = mul(nb[1], b);
            y1 = twice(mul(c, y2) - mul(r2, y1)) + i1 - x1;
            x1 = i1;
            yb[1] += lev(y1, renv);
            nb += 2;
            yb += 2;
        }
        rat.y1 = y1;
        rat.y2 = y2;
        rat.x1 = x1;
        rat.x2 = x2;
    }
    // The thunder: the shared noise low-passed, throbbing.
    __attribute__((noinline)) void thunder_pass(const i32* nb, i32* yb, i32 throb) {
        i32 tv = thunder.v, ta = thatt.v, lp = th_lp;
        const i32 tk = thunder.k, tak = thatt.k, a = th_a;
        for (u32 j = 0; j < frames; ++j) {
            if (CURIO_LIKELY(tv > 4096)) {
                tv = mul(tv, tk);
                ta = mul(ta, tak);
                lp += mul(a, nb[j] - lp);
                yb[j] += mul(lev(lp, tv - ta), throb);
            }
        }
        thunder.v = tv;
        thatt.v = ta;
        th_lp = lp;
    }
    void run(i32* out) {
        ph61 += 64u * 5940910u;   // 61 Hz
        const i32 gate = static_cast<i32>(ph61) >= 0 ? F(1.0) : F(0.2);   // (0.6 + 0.4 square) / 1.0
        phth += 64u * inc_th;
        const i32 throb = F(0.55) + mul(F(0.45), sine(phth));
        // The rattle and cascade follow the ringing once a block (40 Hz and
        // 6 Hz followers of the last block's mean level).
        follow += mul(follow_a, sum - follow);
        slow += mul(slow_a, follow - slow);
        sum = 0;
        const i32 renv = lev(lev(mul(follow, gate), rat_amp) + lev(slow, sheen_amp), rat_gain << 12);
        // The modes still ringing (a quiet one rests until the next blow).
        bool on[16];
        for (u32 m = 0; m < modes; ++m) {
            const i32 e = (mode[m].y1 < 0 ? -mode[m].y1 : mode[m].y1) + (mode[m].y2 < 0 ? -mode[m].y2 : mode[m].y2);
            on[m] = e > 8192 || burst.v > 4096;
            if (!on[m]) mode[m].y1 = mode[m].y2 = 0;
        }
        for (u32 i = 0; i < frames; ++i) out[i] = 0;
        // First the blow, split at the shakes and the roll's strokes,
        i32 xb[frames];
        u32 strike_i[16];
        i32 strike_a[16];
        u32 strikes = 0;
        bool blow = false;
        for (u32 i = 0;;) {
            u32 e = frames;
            if (lump < lumps) {
                const u32 d = lump_at[lump] - t;
                if (d < frames && d >= i) e = d;
            }
            if (e > i && blow_pass(xb + i, e - i)) blow = true;
            i = e;
            if (i >= frames) break;
            while (lump < lumps && t + i == lump_at[lump]) {
                const i32 a = lump_amp[lump];
                if (a >= 0) burst.v += a;
                else if (strikes < 16) { strike_i[strikes] = i; strike_a[strikes] = a; ++strikes; }
                ++lump;
            }
        }
        // then the modes between the roll's strokes: those still ringing,
        // driven by the blow if it sounds (lev(0, g) is 0); a resting mode
        // rings only while the blow drives it,
        u32 live[16], n = 0;
        for (u32 m = 0; m < modes; ++m) {
            if (on[m]) { live[n++] = m; continue; }
            u32 i = 0;
            for (u32 s = 0; s <= strikes; ++s) {
                const u32 end = s < strikes ? strike_i[s] : frames;
                if (blow && end > i) ring_rest(mode[m], modek[m], gain[m], xb + i, out + i, end - i);
                i = end;
                if (s < strikes) dsp::strike(mode[m], modek[m], mul(amp[m], -strike_a[s] << 3) << ROLL);
            }
        }
        for (u32 s = 0, i = 0; s <= strikes; ++s) {
            const u32 end = s < strikes ? strike_i[s] : frames;
            if (end > i)
                for (u32 q = 0; q < n; ++q) {
                    const u32 m = live[q];
                    if (blow) ring_live<true>(mode[m], modek[m], gain[m], xb + i, out + i, end - i);
                    else ring_live<false>(mode[m], modek[m], gain[m], xb + i, out + i, end - i);
                }
            i = end;
            // A roll's stroke strikes the modes outright.
            if (s < strikes)
                for (u32 q = 0; q < n; ++q) {
                    const u32 m = live[q];
                    dsp::strike(mode[m], modek[m], mul(amp[m], -strike_a[s] << 3) << ROLL);
                }
        }
        // then the clank, the rattle and the thunder.
        i32 nb[frames];
        for (u32 s = 0, i = 0;;) {
            const u32 end = s < strikes ? strike_i[s] : frames;
            if (end > i) {
                // The clank, once under its floor, stays there until the next stroke.
                if (clank.v > 4096) clank_pass<true>(out + i, nb + i, end - i);
                else clank_pass<false>(out + i, nb + i, end - i);
            }
            i = end;
            if (s >= strikes) break;
            while (s < strikes && strike_i[s] == i) clank.v += mul(-strike_a[s++], F(0.5));
        }
        t += frames;
        rattle_pass(nb, out, renv);
        if (thunder.v > 4096) thunder_pass(nb, out, throb);
    }
};

// -------------------------------------------------------------------- SPARK
// A Leyden jar discharging across a spark gap: a flurry of sharp cracks,
// each with its shock's snap and a ping of the jar's glass, then the hiss
// of ozone and the arc's mains buzz.
struct Spark {
    u32 rng, t;
    i32 c[8], tune;
    u32 at[11];
    i32 lvl[11];
    u32 n, k, end;
    i32 lpc;
    Res burstr, oz, glass[2];
    ResK bpk, ozk, glassk[2];
    Env burst, snap;
    i32 bak, batt, bgain, ozgain;
    i32 to_k, oz_level, oz_ramp;
    i32 snap_lp, snap_a;
    u32 ph120;
    Env arc;
    i32 arc_lp, arc_a, arc_amp;
    Tone body;
    i32 out_lp, out_a;
    u32 pre;
    i32 glass_amp;
    bool glass_on;

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        k = 0;
        const u32 main = 3 + static_cast<u32>(c[0] * 3 >> 12);
        n = main + static_cast<u32>(c[5] * 5 >> 12);
        const i32 g = 4000 + sc(16000, c[1]);
        lpc = lg2(static_cast<u32>(700 + sc(2600, c[2]))) + tune;
        // The prototype's burst train.
        u32 now = 0;
        for (u32 b = 0; b < n; ++b) {
            at[b] = now;
            const i32 l = b < main ? U(1.0) : U(0.25) + sc(U(0.45), draw(rng));
            lvl[b] = mul(mul(l, F(0.7) + sc(F(0.3), draw(rng))), F(0.35) + sc(F(0.65), c[0]));
            const i32 spread = C(0.4) + sc(sc(C(3.0), c[5]), draw(rng)) + sc(C(0.6), 4095 - c[5]);
            now += samples(static_cast<u32>(g * spread >> 12));
        }
        end = at[n - 1];
        burst.v = 0;
        burst.k = decay(3500);
        bak = mul(burst.k, decay(300));
        batt = 0;
        bgain = band_gain(lpc, C(1.4));
        burstr = Res();
        // The ozone tail, swelling in as the cracks go on.
        const i32 lpo = lpc + 563;   // 1.1
        band(ozk, lpo, C(1.1));
        ozgain = band_gain(lpo, C(1.1));
        oz = Res();
        const i32 to = 22000 + sc(480000, pw(c[3], C(2.0)));
        to_k = decay(static_cast<u32>(to));
        oz_level = 0;
        oz_ramp = end ? static_cast<i32>(static_cast<u32>(U(0.3)) / end) : U(0.3);
        // The snap: a shock's broadband click at each crack.
        snap.v = 0;
        snap.k = decay(250);
        snap_lp = 0;
        snap_a = pole(hz(2500));
        // The jar's glass: two modes, pinged by each crack.
        const i32 lpg = lpc + C(1.45);
        dsp::tune(glassk[0], inc(lpg), decay(25000));
        dsp::tune(glassk[1], inc(lpg + C(0.77)), decay(14000));
        glass[0] = glass[1] = Res();
        glass_amp = U(0.25) + sc(U(0.25), c[2]);
        ph120 = 0;
        arc.v = sc(U(0.6), c[4]);
        arc.k = decay(static_cast<u32>(mul(to, F(0.6)) << 1));
        arc_lp = 0;
        arc_a = pole(hz(1500));
        body.ph = 0;
        body.v = body.a = 0;
        body.inc = inc(hz(210) + tune);
        body.hit(sc(U(1.2), c[6]), decay(50000), decay(2000));
        out_lp = 0;
        out_a = pole(lg2(static_cast<u32>(9000 - sc(7500, c[7]))));
        pre = samples(static_cast<u32>(sc(20000, c[7])));
    }
    bool waiting() const { return k < n; }
    void crack_now() {
        // Each crack's band wanders +-15% about the jar's.
        band(bpk, lpc + mul(jitter(rng), F(0.2)), C(1.4));
        burst.v += lvl[k];
        batt += lvl[k];
        snap.v += mul(lvl[k], F(0.5) + sc(F(0.5), c[0])) << 1;   // more charge, a sharper crack
        for (int m = 0; m < 2; ++m) dsp::strike(glass[m], glassk[m], lev(lvl[k], m ? glass_amp >> 1 : glass_amp));
        ++k;
    }
    // The block in passes, each with its state in registers, split at the
    // cracks; the noise is drawn in the same order as ever.
    // The burst of band noise (it rings on while its band does), and the
    // noise for the snap and the ozone in draw order.
    __attribute__((noinline)) void burst_pass(i32* y, i32* sb, i32* ob, u32 n) {
        i32 bv = burst.v, ba = batt, y1 = burstr.y1, y2 = burstr.y2, x1 = burstr.x1, x2 = burstr.x2;
        i32 sv = snap.v;   // the snap's draws stop as it falls quiet, as its own pass's do
        const i32 bk = burst.k, ak = bak, c = bpk.c, r2 = bpk.r2, b = bpk.b, g = bgain, sk = snap.k;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            if (bv > 4096 || y1 > 4096 || y1 < -4096) {
                bv = mul(bv, bk);
                ba = mul(ba, ak);
                const i32 in = mul(lev(noise(r), bv - ba), b);
                const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
                x2 = x1;
                x1 = in;
                y2 = y1;
                y1 = o;
                y[j] += gn(o, g);
            }
            if (sv > 4096) {
                sv = mul(sv, sk);
                sb[j] = spiky(r);
            }
            ob[j] = noise(r);
        }
        burst.v = bv;
        batt = ba;
        burstr.y1 = y1;
        burstr.y2 = y2;
        burstr.x1 = x1;
        burstr.x2 = x2;
        rng = r;
    }
    // The snap: a shock's broadband click at each crack.
    __attribute__((noinline)) void snap_pass(i32* y, const i32* sb, u32 n) {
        i32 v = snap.v, lp = snap_lp;
        const i32 k = snap.k, a = snap_a;
        if (v <= 4096) { snap_lp = 0; return; }   // until the next crack
        for (u32 j = 0; j < n; ++j) {
            if (CURIO_LIKELY(v > 4096)) {
                v = mul(v, k);
                const i32 sn = lev(sb[j], v);
                lp += mul(a, sn - lp);
                y[j] += sn - lp;
            } else {
                lp = 0;
            }
        }
        snap.v = v;
        snap_lp = lp;
    }
    // Ozone, swelling in as the cracks go on.
    __attribute__((noinline)) void ozone_pass(i32* y, const i32* ob, u32 tt, u32 n) {
        i32 lv = oz_level, y1 = oz.y1, y2 = oz.y2, x1 = oz.x1, x2 = oz.x2;
        const i32 ramp = oz_ramp, k = to_k, c = ozk.c, r2 = ozk.r2, b = ozk.b, g = ozgain;
        const u32 last = end;
        for (u32 j = 0; j < n; ++j, ++tt) {
            if (CURIO_UNLIKELY(tt <= last)) lv += ramp;
            else lv = mul(lv, k);
            const i32 in = mul(ob[j], b);
            const i32 o = twice(mul(c, y1) - mul(r2, y2)) + in - x2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = o;
            y[j] += lev(gn(o, g), lv);
        }
        oz_level = lv;
        oz.y1 = y1;
        oz.y2 = y2;
        oz.x1 = x1;
        oz.x2 = x2;
    }
    // The arc's mains buzz, from half way through the cracks.
    __attribute__((noinline)) void arc_pass(i32* y, u32 tt, u32 n) {
        u32 ph = ph120;
        i32 lp = arc_lp, v = arc.v;
        const i32 a = arc_a, k = arc.k;
        const u32 last = end;
        for (u32 j = 0; j < n; ++j, ++tt) {
            if (CURIO_LIKELY(tt * 2 > last)) {
                ph += 11687059u;   // 120 Hz
                const i32 sq = static_cast<i32>(ph) >= 0 ? one >> 1 : -(one >> 1);
                const i32 hum = sq + (sine(ph * 2) >> 3);
                lp += mul(a, hum - lp);
                if (CURIO_LIKELY(tt > last)) v = mul(v, k);
                y[j] += lev(lp, v);
            }
        }
        ph120 = ph;
        arc_lp = lp;
        arc.v = v;
    }
    // The jar's body.
    __attribute__((noinline)) void body_pass(i32* y, u32 n) {
        u32 ph = body.ph;
        const u32 in = body.inc;
        i32 v = body.v, a = body.a;
        const i32 vk = body.vk, ak = body.ak;
        for (u32 j = 0; j < n; ++j) {
            if (CURIO_LIKELY(v)) {
                ph += in;
                v = mul(v, vk);
                if (CURIO_UNLIKELY(a)) { a = mul(a, ak); if (a < 64 && a > -64) a = 0; }
                y[j] += mul(v - a, sine(ph));
                if (CURIO_UNLIKELY(v < 4096)) v = 0;
            }
        }
        body.ph = ph;
        body.v = v;
        body.a = a;
    }
    // The distance: the whole low-passed.
    __attribute__((noinline)) void dist_pass(i32* y, u32 n) {
        i32 lp = out_lp;
        const i32 a = out_a;
        for (u32 j = 0; j < n; ++j) {
            lp += mul(a, y[j] - lp);
            y[j] = lp;
        }
        out_lp = lp;
    }
    // n samples after the pre-delay, between cracks.
    __attribute__((noinline)) void span(i32* y, u32 tt, u32 n) {
        i32 sb[frames], ob[frames];
        for (u32 j = 0; j < n; ++j) y[j] = 0;
        burst_pass(y, sb, ob, n);
        snap_pass(y, sb, n);
        if (glass_on) {
            ring_into(glass[0], glassk[0], y, n);
            ring_into(glass[1], glassk[1], y, n);
        }
        ozone_pass(y, ob, tt, n);
        if (c[4] > 0) arc_pass(y, tt, n);
        if (body.v) body_pass(y, n);
        if (c[7] > 0) dist_pass(y, n);
    }
    void run(i32* out) {
        // The jar's glass rests once it has rung out, until the next crack.
        glass_on = k < n || (glass[0].y1 > 2048 || glass[0].y1 < -2048 || glass[1].y1 > 2048 || glass[1].y1 < -2048);
        const u32 t0 = t;
        for (u32 i = 0;;) {
            if (t0 + i < pre) {
                // Before the distance's delay: silence.
                u32 e = pre - t0;
                if (e > frames) e = frames;
                for (; i < e; ++i) out[i] = 0;
                if (i >= frames) break;
            }
            const u32 tt = t0 + i - pre;
            // The next crack in this block.
            u32 e = frames;
            if (k < n) { const u32 d = at[k] - tt; if (d < frames - i) e = i + d; }
            if (e > i) span(out + i, tt, e - i);
            i = e;
            if (i >= frames) break;
            const u32 now = t0 + i - pre;
            while (k < n && now == at[k]) crack_now();
        }
        t = t0 + frames;
    }
};

// --------------------------------------------------------------------- CORK
// A cork drawn from a bottle: a squeak as it turns, the neck's Helmholtz
// pop, the thock of the cork, glugs, and the fizz of what was inside.
struct Cork {
    u32 rng, t;
    i32 c[8], tune;
    u32 pre;
    u32 ph_sq, sq_inc0, sq_step;
    i32 sq_amp, sq_att, sq_attk, sq_lp, sq_a;
    Tone pop, body;
    i32 pop_k0, pop_drop, popk_fall, pop_fall;
    Env thock;
    i32 th_lp, th_a;
    u32 glugs, glug_at, glug;
    Tone gl;
    i32 gl_amp, gl_rise;
    u32 gl_inc;
    u32 fizz_p;
    Res fz;
    ResK fzk;
    i32 fz_hp, fz_a, fz_amp;
    Env fizz;
    bool popped;

    void start(const Ctx& x) {
        for (int i = 0; i < 8; ++i) c[i] = x.c[i];
        tune = x.tune;
        rng = x.seed | 1u;
        t = 0;
        popped = false;
        pre = samples(static_cast<u32>(sc(50000, c[5])));
        const i32 lpf = lg2(static_cast<u32>(350 + sc(900, c[0]))) + tune;
        // The squeak: 2.2 to 3.7 kHz over the twist.
        ph_sq = 0;
        sq_inc0 = inc(hz(2200));
        sq_step = pre ? (inc(hz(3700)) - sq_inc0) / pre : 0;
        sq_amp = sc(U(0.25), c[5]);
        sq_att = sq_amp;
        sq_attk = decay(5000);
        sq_lp = 0;
        sq_a = pole(hz(5000));
        // The pop: the neck's note, bent up from 0.7 of itself.
        pop.ph = 0;
        pop.v = pop.a = 0;
        pop.inc = inc(lpf);
        const i32 tp = 18000 + sc(60000, c[3]);
        pop_fall = full;
        popk_fall = decay(static_cast<u32>(3000 + sc(6000, 4095 - c[1])));
        pop_k0 = decay(static_cast<u32>(tp));
        body.ph = 0;
        body.v = body.a = 0;
        body.inc = pop.inc >> 1;
        thock.v = 0;
        thock.k = decay(1000);
        th_lp = 0;
        th_a = pole(lg2(static_cast<u32>(800 + sc(5000, c[4]))));
        glugs = c[7] > 0 ? 1 + static_cast<u32>(c[7] * 3 >> 12) : 0;
        glug = 0;
        glug_at = pre + samples(60000);
        gl.ph = 0;
        gl.v = gl.a = 0;
        gl_inc = static_cast<u32>(mul(static_cast<i32>(pop.inc), F(0.6)));
        gl_amp = sc(U(0.4), c[7]);
        // The fizz: sparse clicks through a bright resonance.
        fizz_p = per_sample(static_cast<u32>(150 + sc(3000, c[6])));
        const i32 lpz = lg2(static_cast<u32>(5000 + sc(3000, c[6])));
        dsp::tune(fzk, inc(lpz), decay(127));   // a 2.5 kHz band
        fz = Res();
        fz_hp = 0;
        fz_a = pole(hz(3000));
        // Spilling froths even an uncorked still bottle a little.
        fz_amp = sc(U(1.2) + sc(U(1.5), c[6]), c[2] > (c[6] >> 1) ? c[2] : c[6] >> 1);
        fizz.v = fz_amp;
        fizz.k = decay(static_cast<u32>(120000 + sc(400000, c[2])));
    }
    bool waiting() const { return !popped || glug < glugs; }
    // The block in passes, each with its state in registers, split at the
    // pop and the glugs; the noise is drawn in the same order as ever.
    // The squeak as the cork turns: 2.2 to 3.7 kHz over the twist.
    __attribute__((noinline)) void squeak_pass(i32* y, u32 n) {
        u32 ph = ph_sq, tt = t;
        i32 att = sq_att, lp = sq_lp;
        const u32 i0 = sq_inc0, st = sq_step;
        const i32 ak = sq_attk, amp = sq_amp, a = sq_a, edge = mul(sq_amp, F(0.3));
        for (u32 j = 0; j < n; ++j, ++tt) {
            ph += i0 + st * tt;
            att = mul(att, ak);
            const i32 s = mul(amp - att, sine(ph));
            const i32 sq = (s < 0 ? -edge : edge) + s;
            lp += mul(a, sq - lp);
            y[j] += lp;
        }
        ph_sq = ph;
        sq_att = att;
        sq_lp = lp;
    }
    // The neck's pop, bent up from 0.7 of itself.
    __attribute__((noinline)) void pop_pass(i32* y, u32 n) {
        u32 ph = pop.ph;
        const u32 in = pop.inc;
        i32 v = pop.v, a = pop.a, fall = pop_fall;
        const i32 vk = pop.vk, ak = pop.ak, fk = popk_fall;
        for (u32 j = 0; j < n; ++j) {
            fall = mul(fall, fk);
            ph += in - static_cast<u32>(mul(static_cast<i32>(in), mul(F(0.3), fall)));
            v = mul(v, vk);
            if (CURIO_UNLIKELY(a)) { a = mul(a, ak); if (a < 64 && a > -64) a = 0; }
            y[j] += mul(v - a, sine(ph));
        }
        pop.ph = ph;
        pop.v = v;
        pop.a = a;
        pop_fall = fall;
    }
    // The thock of the cork, and the fizz's clicks in draw order.
    template <bool thocking, bool fizzing> __attribute__((noinline)) void noise_pass(i32* y, i32* cb, u32 n) {
        i32 tv = thock.v, lp = th_lp, fv = fizz.v;
        const i32 tk = thock.k, a = th_a, fk = fizz.k;
        const u32 p = fizz_p;
        u32 r = rng;
        for (u32 j = 0; j < n; ++j) {
            if (thocking && CURIO_LIKELY(tv > 4096)) {
                tv = mul(tv, tk);
                lp += mul(a, lev(noise(r), tv) - lp);
                y[j] += lp;
            }
            if (fizzing && CURIO_LIKELY(fv > 4096)) {
                fv = mul(fv, fk);
                noise(r);
                cb[j] = r < p ? noise(r) : 0;
            }
        }
        thock.v = tv;
        th_lp = lp;
        rng = r;
    }
    // A glug, rising (1 + 8 t).
    __attribute__((noinline)) void glug_pass(i32* y, u32 n) {
        u32 ph = gl.ph;
        const u32 in = gl_inc;
        i32 v = gl.v, a = gl.a, rise = gl_rise;
        const i32 vk = gl.vk, ak = gl.ak;
        for (u32 j = 0; j < n; ++j) {
            if (CURIO_LIKELY(v > 4096)) {
                rise += 190;   // 8 / 44100 each sample, Q20
                ph += in + static_cast<u32>(mul(static_cast<i32>(in), rise << 11));
                v = mul(v, vk);
                if (CURIO_UNLIKELY(a)) { a = mul(a, ak); if (a < 64 && a > -64) a = 0; }
                y[j] += mul(v - a, sine(ph));
            }
        }
        gl.ph = ph;
        gl.v = v;
        gl.a = a;
        gl_rise = rise;
    }
    // The fizz: sparse clicks through a bright resonance.
    __attribute__((noinline)) void fizz_pass(i32* y, const i32* cb, u32 n) {
        i32 fv = fizz.v, y1 = fz.y1, y2 = fz.y2, hp = fz_hp;
        const i32 fk = fizz.k, c = fzk.c, r2 = fzk.r2, g = fzk.g, a = fz_a;
        for (u32 j = 0; j < n; ++j) {
            if (CURIO_LIKELY(fv > 4096)) {
                const i32 o = twice(mul(c, y1) - mul(r2, y2)) + mul(cb[j], g);
                y2 = y1;
                y1 = o;
                hp += mul(a, o - hp);
                fv = mul(fv, fk);
                y[j] += lev(o - hp, fv);
            }
        }
        fizz.v = fv;
        fz.y1 = y1;
        fz.y2 = y2;
        fz_hp = hp;
    }
    // n samples between events.
    __attribute__((noinline)) void span(i32* y, u32 n) {
        i32 cb[frames];
        for (u32 j = 0; j < n; ++j) y[j] = 0;
        if (t < pre) squeak_pass(y, n);
        if (popped) {
            pop_pass(y, n);
            tone_at(body, y, n);
        }
        const bool th = popped && thock.v > 4096, fz_on = fizz.v > 4096;
        if (th && fz_on) noise_pass<true, true>(y, cb, n);
        else if (th) noise_pass<true, false>(y, cb, n);
        else if (fz_on) noise_pass<false, true>(y, cb, n);
        if (gl.v > 4096) glug_pass(y, n);
        if (fz_on) fizz_pass(y, cb, n);
    }
    // The pop, or a glug, at t.
    __attribute__((noinline)) void events() {
        if (t >= pre && !popped) {
            popped = true;
            pop_fall = full;
            const i32 g = U(0.6) + sc(U(0.6), c[1]);
            pop.hit(g, pop_k0, decay(600));
            body.hit(sc(U(0.5), c[3]), decay(static_cast<u32>(mul(18000 + sc(60000, c[3]), F(0.75)) << 1)), decay(600));
            thock.v = U(0.1) + sc(U(2.4), c[4]);
        }
        if (glug < glugs && t == glug_at) {
            gl.v = gl.a = 0;
            gl.ph = 0;
            gl.hit(gl_amp, decay(20000), decay(400));
            gl_rise = 0;
            ++glug;
            glug_at += samples(70000);
        }
    }
    void run(i32* out) {
        const u32 t0 = t;
        for (u32 i = 0;;) {
            t = t0 + i;
            events();
            // The next event in this block: the pop, or a glug.
            u32 e = frames;
            if (!popped) { const u32 d = pre - t0; if (d < e) e = d; }
            if (glug < glugs) { const u32 d = glug_at - t0; if (d < e && d > i) e = d; }
            span(out + i, e - i);
            i = e;
            if (i >= frames) break;
        }
        t = t0 + frames;
    }
};
}
