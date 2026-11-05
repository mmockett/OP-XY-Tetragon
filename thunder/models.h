#pragma once
// THUNDER's twelve models: each a way of playing the one sheet (voice.h).
// setup_* runs once as a note starts, from the key's eight controls (c[], Q16
// units 0..65535) and its TUNE (log2 units); block_* renders 64 samples into
// out, at U for the prototype's "1". The mappings follow the prototype
// (the concept's proto.py) in log2 units: its expmap(a, b, t) is mix(L(a), L(b), t).
#include "voice.h"

namespace thunder {
enum Model : u32 { BOOM, SLAP, POP, FLICK, TOM, CRASH, CLANK, GRIT, HOP, ZAP, BOW, SHAKE, model_count };

// A mallet's beater, felt (0) to a steel rod (1): its contact time (log2 s) and click (Q16).
inline i32 beater_time(i32 c) { return mix(L(6e-3), L(0.08e-3), c); }
inline i32 beater_click(i32 c) { return c > 32768 ? (c - 32768) * 2 : 0; }
// The sheet's fundamental moves the most air: its whole face pumps as one,
// while each higher mode's regions swing against each other and largely
// cancel (a plate's radiation efficiency below its coincidence frequency).
// The higher modes' share of the sound, upper (Q31), against the fundamental.
inline void radiate(Voice& v, i32 upper) {
    for (u32 k = 1; k < v.nmodes; ++k) v.mode[k].gw = multiply(v.mode[k].gw, upper);
}
// Samples from log2 seconds.
inline u32 samples(i32 lsec) { return pow2(lsec + lsr); }

// The block's working buffers.
struct Work { i32 exc[frames], fin[frames], body[frames], aux[frames]; };

inline void clear(i32* x) { for (unsigned i = 0; i < frames; ++i) x[i] = 0; }

// ============================================================ BOOM: kick
// A felt mallet on a tight sheet. CAMBER makes a hard hit start sharp and sag
// in pitch as it rings (geometric hardening); GLIDE sets how fast.
inline void setup_boom(Voice& v, const i32* c, i32 tune) {
    const i32 lf0 = mix(L(30), L(105), c[0]) + tune;
    const i32 lt0 = mix(L(1.3), L(0.025), c[1]);   // DAMP: a long boom to a dead thud
    const i32 tension = mix(Q16(0.35), Q16(1.0), c[4]);
    const i32 slack = 65535 - tension;
    setup_modes(v, lf0, tension, mul16(c[3], Q16(0.85)), lt0, mix(Q16(0.5), Q16(1.0), tension),
                mix(Q16(0.5), Q16(1.3), tension), 8, beater_time(c[2]));
    radiate(v, mix(Q31(0.9), Q31(0.45), c[4]));   // a slack plate clangs; a taut head is all fundamental
    const i32 c12 = mix(c[5], sq16(c[5]), Q16(0.2));   // CAMBER^1.2
    v.bend = (c12 * 7) << 11;                            // 3.5 CAMBER^1.2, Q28
    v.bend_env = one;
    v.bend_k = block_decay(mix(L(0.006), L(0.14), c[6]));
    const i32 lt60 = lt0 + L(0.8);
    setup_field(v, lf0 + L(5), slack, lt60 > L(0.08) ? lt60 : L(0.08), c[2]);
    // (0.02 + 0.5 slack^1.5) (0.4 + SPOT), as Q31 x 2
    v.flevel = multiply(Q31(0.01) + (power16(slack, Q16(1.5)) << 13), Q31(0.2) + (c[3] << 14)) << 1;
    if (v.flevel < Q31(0.008)) v.fon = 0;   // a taut sheet's high modes, 40 dB under: not worth running
    // the beater's knock on the head: a felt thud (300 to 600 Hz, 5 ms) to a steel tick
    click(v, (Q16(0.35) + mul16(beater_click(c[2]), Q16(0.25))) << 15, beater_click(c[2]), mix(L(300), L(2500), c[2]), mix(L(600), L(16000), c[2]),
          mix(L(0.005), L(0.0006), c[2]));
    setup_chain(v, Q16(0.45), Q16(0.35), L(0.04), L(1800), L(6000), c[7]);
    strike(v, beater_time(c[2]), U);
    v.s[0] = U;   // the field's impulse
}
inline void block_boom(Voice& v, Work& w, i32* out) {
    glide_modes(v);
    excite(v, w.exc);
    clear(w.fin);
    clear(out);
    w.fin[0] = v.s[0];
    v.s[0] = 0;
    run_modes(v, w.exc, out);
    for (unsigned i = 0; i < frames; ++i) w.body[i] = out[i];
    add_click(v, out);
    run_field(v, w.fin, out, v.flevel);
    run_chain(v, w.body, out);
}

// ============================================================ SLAP: snare
// A tight sheet with a chain held across it as snare wires (CHAIN, LINKS) and
// the sheet's own ring under it (METAL).
inline void setup_slap(Voice& v, const i32* c, i32 tune) {
    // a tighter head rings higher (half an octave over TENSION's travel)
    const i32 lf0 = mix(L(110), L(330), c[0]) + mix(-Q16(0.5), Q16(0.1), c[4]) + tune;
    const i32 lt0 = mix(L(0.45), L(0.035), c[1]);
    const i32 tension = mix(Q16(0.2), Q16(1.0), c[4]);
    setup_modes(v, lf0, tension, Q16(0.05) + mul16(power16(c[3], Q16(0.6)), Q16(0.9)), lt0, Q16(0.6), Q16(0.4), 8, beater_time(c[2]));
    radiate(v, Q31(0.67));
    v.bend = mix(Q16(0.7), Q16(0.1), c[4]) << 12;   // a slack head sags 0.7 over 6 ms, a tight one 0.1: crisp, not a boing
    v.bend_env = one;
    v.bend_k = block_decay(L(0.006));
    // (never quite equal lines, and a smeared input: a crisp ring, not a boing)
    setup_field(v, lf0 + L(3.5), Q16(0.45) + mul16(65535 - tension, Q16(0.55)), lt0 + log16(mix(Q16(1.5), Q16(8.0) - 1, power16(c[7], Q16(0.6))) >> 3) + L(8.0),
                Q16(0.4) + mul16(c[2], Q16(0.5)), 1);
    v.flevel = multiply(Q31(0.9), q31_root(c[7]));    // 1.8 sqrt(METAL), Q31 x 2
    if (!c[7]) v.fon = 0;
    // The wires: LINKS loosens them, slower to settle and lower.
    const i32 lrel = mix(L(0.012), L(0.25), c[6]);
    // the wires: a few from the first notch, more of them, louder, denser and longer as CHAIN rises
    setup_chain(v, Q16(1.4), mix(Q16(0.15), Q16(1.0), c[6]), lrel, mix(L(3000), L(1300), c[6]), L(6000), c[5] ? Q16(0.15) + mul16(c[5], Q16(0.85)) : 0);
    // and they fall away with (1 + 3 CHAIN) rel + 30 ms: more wires buzz on longer
    const i32 tw = mul16(exp16(lrel), Q16(1.0) + mul16(c[5], Q16(3.0))) + Q16(0.03);
    v.env[0].set(one, block_decay(log16(tw)));
    click(v, (Q16(0.15) + mul16(beater_click(c[2]), Q16(0.8))) << 15, beater_click(c[2]));
    strike(v, beater_time(c[2]), U);
    v.s[0] = U;
}
inline void block_slap(Voice& v, Work& w, i32* out) {
    glide_modes(v);
    excite(v, w.exc);
    clear(w.fin);
    clear(w.aux);
    clear(out);
    w.fin[0] = v.s[0];
    v.s[0] = 0;
    run_modes(v, w.exc, out);
    for (unsigned i = 0; i < frames; ++i) w.body[i] = out[i];
    add_click(v, out);
    run_field(v, w.fin, out, v.flevel);
    run_chain(v, w.body, w.aux);
    i32 e;
    const i32 step = v.env[0].step(e);
    for (unsigned i = 0; i < frames; ++i, e += step) out[i] += multiply(w.aux[i], e) << 1;
}

// ============================================================ POP: clap, snap
// An oil-canned dent snapping through: each snap a burst of crackle through
// the dent's band, then a tail, over the sheet's ring.
inline void pop_schedule(Voice& v, u32 now) {
    const u32 r = lcg(v.seed);
    const i32 jitter = multiply(v.k[6], static_cast<i32>(r >> 1) - Q31(0.5));   // SCATTER
    const i32 gap = v.k[5] + multiply(v.k[5], jitter);
    v.at = now + static_cast<u32>(gap > 32 ? gap : 32);
}
inline void setup_pop(Voice& v, const i32* c, i32 tune) {
    const i32 lband = mix(L(650), L(2800), c[0]) + tune;
    const i32 ltail = mix(L(0.35), L(0.025), c[1]);
    v.left = 1 + static_cast<u32>((c[2] * 6 + 32768) >> 16);          // POPS: 1..7
    const i32 hard = c[4];
    v.k[0] = decay(mix(L(0.009), L(0.0006), hard));                   // a snap's burst: a soft pap to a click
    v.k[1] = decay(ltail);                                             // the tail
    v.k[5] = static_cast<i32>(samples(mix(L(0.004), L(0.032), c[3])));   // SPREAD
    v.k[6] = mix(Q31(0.08), Q31(1.4), c[7]);                           // SCATTER: timing
    v.k[7] = mix(Q31(0.05), Q31(0.7), c[7]);                           // and level
    v.k[12] = q31(sq16(c[7]));                                         // and how often a snap's crackle is new
    v.k[2] = svf_f(increment(lband + mul16(hard, Q16(0.6))));
    v.k[3] = damping30(mix(Q16(0.56), Q16(0.9), hard));   // Q over 0.5: its 1 / Q fits
    // the sheet's ring under the snaps (METAL)
    const i32 lt = ltail + octave > L(0.1) ? ltail + octave : L(0.1);
    setup_field(v, lband - octave, Q16(0.8), lt + log16(mix(Q16(1.0), Q16(4.0) - 1, c[5]) >> 2) + 2 * octave, Q16(0.6), 2);
    v.flevel = q31(power16(c[5], Q16(1.5)));   // METAL^1.5: a hint of ring low down, a long metal tail at the top
    if (!c[5]) v.fon = 0;   // no METAL: no ring to run
    v.k[8] = onepole_k(L(400));
    v.k[9] = onepole_k(lband + L(3) < L(14000) ? lband + L(3) : L(14000));
    v.k[10] = onepole_k(L(150));
    v.k[11] = multiply(q31(sq16(hard)), Q31(0.7));   // a hard snap's click
    setup_chain(v, Q16(0.9), Q16(0.6), L(0.06), L(2500), L(6000), c[6]);
    v.s[0] = v.s[1] = 0;
    v.at = 0;
    v.waiting = 1;
    v.cseed = 0x9e3779b9u;   // the snaps' crackle the same every hit, so a clap peaks the same; SCATTER varies it
}
inline void block_pop(Voice& v, Work& w, i32* out) {
    i32 burst = v.s[0], tail = v.s[1];
    clear(w.fin);
    clear(w.aux);
    const bool pops = v.left && v.at < v.t + frames;
    if (pops || burst > (1 << 14) || tail > (1 << 14)) {
        u32 seed = v.cseed ? v.cseed : 1u;
        const i32 kb = v.k[0], kt = v.k[1];
        for (unsigned i = 0; i < frames; ++i) {
            if (pops && v.left && v.t + i == v.at) {
                const u32 r = lcg(v.seed);   // SCATTER's dice: the timing, level and crackle of each snap
                if (static_cast<i32>(r >> 1) < v.k[12]) seed = (r >> 3) | 1u;
                burst = Q31(0.75) + multiply(v.k[7], static_cast<i32>(r >> 1) - Q31(0.5)) + (v.left == 1 ? Q31(0.1) : 0);
                w.fin[i] = (v.left & 1u) ? multiply(U, v.k[11]) : -multiply(U, v.k[11]);
                if (--v.left) pop_schedule(v, v.t + i);
                else tail = Q31(0.25);
            }
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            w.exc[i] = (multiply(static_cast<i32>(seed) >> 4, (burst >> 1) + (tail >> 1)) << 1) + w.fin[i];
            burst = multiply(burst, kb);
            tail = multiply(tail, kt);
        }
        v.cseed = seed;
    } else {
        burst = tail = 0;
        clear(w.exc);
    }
    v.s[0] = burst;
    v.s[1] = tail;
    v.waiting = v.left != 0;
    // dry: the snaps through the dent's band, twice
    {
        const i32 f = v.k[2], q = v.k[3];
        i32 al = v.sv[0].lp, ab = v.sv[0].bp, bl = v.sv[1].lp, bb = v.sv[1].bp;
        for (unsigned i = 0; i < frames; ++i) {
            al += multiply(f, ab);
            ab += multiply(f, w.exc[i] - al - (multiply(q, ab) << 1));
            const i32 y = multiply(q, ab) << 1;
            bl += multiply(f, bb);
            bb += multiply(f, y - bl - (multiply(q, bb) << 1));
            out[i] = multiply(q, bb) << 2;
        }
        v.sv[0].lp = al; v.sv[0].bp = ab; v.sv[1].lp = bl; v.sv[1].bp = bb;
    }
    // the sheet's ring under them (METAL), band-limited
    run_field(v, w.exc, w.aux, Q31(0.5));
    if (v.fon) {
        const i32 k8 = v.k[8], k9 = v.k[9], lv = v.flevel;
        i32 h0 = v.op[0].s, h1 = v.op[4].s, l0 = v.op[1].s;
        for (unsigned i = 0; i < frames; ++i) {
            h0 += multiply(w.aux[i] - h0, k8);
            const i32 a = w.aux[i] - h0;
            h1 += multiply(a - h1, k8);
            l0 += multiply(a - h1 - l0, k9);
            out[i] += multiply(l0, lv) << 1;
        }
        v.op[0].s = h0; v.op[4].s = h1; v.op[1].s = l0;
    }
    if (v.c_amount) {
        for (unsigned i = 0; i < frames; ++i) w.body[i] = v.op[2].low(absolute(w.exc[i]), v.k[10]) << 2;   // the sheet's lift
        run_chain(v, w.body, out);
    }
}

// ============================================================ FLICK: hats
// A fingernail (NAIL) flicks a corner, or a brush drags (SCRAPE): the sheet's
// dense high modes ring until the hand damps them.
inline void setup_flick(Voice& v, const i32* c, i32 tune) {
    const i32 lhp = mix(L(2600), L(6000), c[0]) + tune;
    const i32 ltau = mix(L(1.1), L(0.022), c[1]);
    const i32 nail = c[2];
    // the flick: L = 1.5..60 ms (+8 samples), a fall of L/3 or, past 0.3, a sin^2 window
    const i32 lflick = mix(L(0.0015), L(0.06), c[7]);
    v.u[0] = samples(lflick) + 8;
    v.u[1] = c[7] < Q16(0.3) ? 0 : 1;
    v.k[0] = decay(log16(exp16(lflick) + Q16(8.0 / 44100)) - L(3));
    v.u[2] = 0xffffffffu / v.u[0];   // note-on: the window's increment
    v.s[0] = one;
    const i32 lbase = log16(mix(Q16(1200.0 / 1024), Q16(300.0 / 1024), c[3])) + L(1024) + mix(L(0.35), L(2.0), c[0]) + tune;
    setup_field(v, lbase, mix(Q16(1.0), Q16(0.4), c[4]), log16(mul16(exp16(ltau), Q16(6)) + Q16(0.05)),
                mix(Q16(0.45), Q16(0.9), nail), 2);
    const i32 lcut = lhp + log16(mix(Q16(1.0), Q16(0.45), c[3])) + mul16(q31_root(nail) >> 15, Q16(0.8));   // a coin's edge rings brighter
    v.k[1] = onepole_k(lcut);
    v.k[2] = onepole_k(lhp - L(1.25));
    // the hand: the sheet rings on (falling with six times DAMP's time) until
    // the hand lands, at 0.9 of it, then damps it in a sixth of it: a closed
    // hat holds its body, then stops
    v.env[0].set(one, block_decay(ltau + L(6)));
    v.k[5] = static_cast<i32>(samples(ltau + L(0.9)) >> 6) + 1;   // blocks until the hand lands
    v.k[6] = block_decay(ltau - L(6));
    v.k[4] = mix(Q31(0.9), Q31(0.5), c[4]);                   // the densest modes, as a sizzle: more on a slack sheet
    v.k[3] = mix(Q31(0.045), Q31(0.075), nail);                  // and a noisy sizzle among them, the sheet's forest above them: more off a coin
    v.env[3].set(one, block_decay(ltau - L(0.17)));               // which fade under the hand, over six times DAMP's time
    // the sizzle, the body of the hat: six of the sheet's modes (its 2nd to
    // 7th at this TENSION), each a clang rich in overtones, three and a half
    // octaves under the band, so their overtones crowd it (a dense metal, not
    // a hiss)
    {
        const i32 tt = clamp(c[4], 0, 65535) * 32;
        const u32 idx = static_cast<u32>(tt) >> 16;
        const i32 fr = tt & 0xffff;
        const i32 lsz = lhp - Q16(3.5);
        for (u32 j = 0; j < 6; ++j) {
            const i32 r = mix(tables::mode_ratio[idx][j + 1] << 4, tables::mode_ratio[idx + (idx < 32)][j + 1] << 4, fr);
            v.u[4 + j] = increment(lsz + r);
            v.u[10 + j] = lcg(v.seed);
        }
    }
    v.s[2] = v.s[3] = 0;
    v.env[1].set(mix(Q31(0.04), Q31(0.6), q31_root(nail) >> 15), block_decay(mix(L(0.001), L(0.006), nail)));   // the nail's tick: a soft pad to a coin's edge
    setup_swing(v, c[6], mix(L(3), L(7), c[6]));
    v.env[2].set(one, block_decay(ltau - octave));             // what lifts the chain
    setup_chain(v, Q16(0.02), Q16(0.8), log16(mul16(exp16(ltau), Q16(0.8)) + Q16(0.03)), L(3000), L(6000), c[5]);   // a sizzle chain, light: the flick is quiet
}
inline void block_flick(Voice& v, Work& w, i32* out) {
    clear(w.aux);
    u32 seed = v.seed ? v.seed : 1u;
    const bool flicking = v.u[0] != 0;
    if (flicking) {
        for (unsigned i = 0; i < frames; ++i) {
            i32 e = 0;
            if (v.u[0]) {
                if (v.u[1]) {
                    e = (one - sine(v.u[3] + 0x40000000u)) >> 1;
                    v.u[3] += v.u[2];
                } else {
                    e = v.s[0];
                    v.s[0] = multiply(v.s[0], v.k[0]);
                }
                --v.u[0];
            }
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            w.exc[i] = multiply(static_cast<i32>(seed) >> 4, e);
        }
    } else {
        clear(w.exc);
    }
    run_field(v, w.exc, w.aux, Q31(0.5));
    i32 hand, tick, fizz;
    if (v.k[5] && !--v.k[5]) v.env[0].factor = v.k[6];
    const i32 hs = v.env[0].step(hand), ts = v.env[1].step(tick), zs = v.env[3].step(fizz);
    const i32 hand0 = hand;
    const i32 k1 = v.k[1], k2 = v.k[2], kz = v.k[4];
    i32 h0 = v.op[0].s, h1 = v.op[1].s;
    for (unsigned i = 0; i < frames; ++i, hand += hs) {
        // the ring, high-passed twice, as the hand damps it
        h0 += multiply(w.aux[i] - h0, k1);
        const i32 a = w.aux[i] - h0;
        h1 += multiply(a - h1, k1);
        out[i] = multiply(a - h1, multiply(hand, Q31(0.6)));
    }
    v.op[0].s = h0;
    v.op[1].s = h1;
    if (flicking || tick > (1 << 16)) {
        // the nail's tick: the flick itself, high-passed
        i32 h2 = v.op[2].s;
        for (unsigned i = 0; i < frames; ++i, tick += ts) {
            h2 += multiply(w.exc[i] - h2, k2);
            out[i] += multiply(w.exc[i] - h2, tick);
        }
        v.op[2].s = h2;
    }
    if (multiply(fizz, hand0) > (1 << 16)) {
        // the densest modes: the sizzle, under the hand, high-passed three times
        i32 h4 = v.op[4].s, h5 = v.op[5].s, h6 = v.op[3].s;
        u32 p0 = v.u[10], p1 = v.u[11], p2 = v.u[12], p3 = v.u[13], p4 = v.u[14], p5 = v.u[15];
        const u32 i0 = v.u[4], i1 = v.u[5], i2 = v.u[6], i3 = v.u[7], i4 = v.u[8], i5 = v.u[9];
        i32 hd = hand0;
        const i32 kn = v.k[3];
        for (unsigned i = 0; i < frames; ++i, fizz += zs, hd += hs) {
            p0 += i0; p1 += i1; p2 += i2; p3 += i3; p4 += i4; p5 += i5;
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            const i32 n = ((static_cast<i32>(p0) >> 31) + (static_cast<i32>(p1) >> 31) + (static_cast<i32>(p2) >> 31) +
                           (static_cast<i32>(p3) >> 31) + (static_cast<i32>(p4) >> 31) + (static_cast<i32>(p5) >> 31)) * (U / 3 * 4) + U * 4 +   // each square +-1: no step at the start
                          multiply(static_cast<i32>(seed) >> 1, kn);
            h4 += multiply(n - h4, k1);
            const i32 a = n - h4;
            h5 += multiply(a - h5, k1);
            const i32 b = a - h5;
            h6 += multiply(b - h6, k1);
            out[i] += multiply(multiply(b - h6, kz), multiply(fizz, hd));
        }
        v.op[4].s = h4;
        v.op[5].s = h5;
        v.op[3].s = h6;
        v.u[10] = p0; v.u[11] = p1; v.u[12] = p2; v.u[13] = p3; v.u[14] = p4; v.u[15] = p5;
    }
    v.seed = seed;
    if (v.c_amount) {
        i32 lift;
        const i32 ls = v.env[2].step(lift);
        for (unsigned i = 0; i < frames; ++i, lift += ls) w.body[i] = multiply(noise(v.seed) >> 4, lift) << 1;
        run_chain(v, w.body, out);
    }
    {
        // the pickup's own roll-off: a double zero at the top, so little fizzes above 12 kHz
        i32 last = v.s[2], last2 = v.s[3];
        for (unsigned i = 0; i < frames; ++i) { const i32 x = out[i]; out[i] = (x >> 2) + (last >> 1) + (last2 >> 2); last2 = last; last = x; }
        v.s[2] = last;
        v.s[3] = last2;
    }
    run_swing(v, out);
    // the hat is quiet after its high-passes: brought up to the others' working level
    for (unsigned i = 0; i < frames; ++i) out[i] <<= 2;
}

// ============================================================ TOM: toms
// A stick on a sheet pulled taut, voiced as a tom: the head's modes at their
// full size, a shell ringing an octave over the fundamental (a tom's shell and
// air reinforce the head's second partial, and a harmonic octave carries the
// pitch on a small speaker), the head driven into a soft clip on the strike,
// which crowds its harmonics into the midrange, and the stick's knock on top.
// ROLL bounces the stick into a flam, a ruff, a roll.
inline void setup_tom(Voice& v, const i32* c, i32 tune) {
    const i32 lf0 = mix(L(55), L(420), c[0]) + tune;
    const i32 lt0 = mix(L(1.3), L(0.07), c[1]);
    const i32 tension = mix(Q16(0.45), Q16(1.0), c[4]);
    setup_modes(v, lf0, tension, mul16(c[3], Q16(0.85)), lt0, Q16(0.5), Q16(0.35), 8, beater_time(c[2]));
    // (its higher modes fall off and die away more gently than a kick's, so a
    // stick's strike reaches the midrange). The shell: one more mode, an
    // octave over the head, as large as the fundamental, ringing 0.6 of its
    // time; it glides with the head
    if (v.nmodes > 7) v.nmodes = 7;
    {
        Mode& sh = v.mode[v.nmodes++];
        const i32 lf = lf0 + octave;
        sh.inc = increment(lf < L(6800.0) ? lf : L(6800.0));
        sh.f = svf_f(sh.inc);
        sh.r = expneg(3 * octave - (lt0 + L(0.6)) - lsr);
        sh.gw = multiply(v.mode[0].gw, Q31(1.0 / 2.0)) << 1;
        sh.lp = sh.bp = 0;
    }
    v.k[7] = Q31(2.0 / 4.0);   // the head's drive into its soft clip, Q31 of 4
    v.bend = mul16(c[5], Q16(2.4)) << 12;
    v.bend_env = one;
    v.bend_k = block_decay(mix(L(0.04), L(0.09), c[5]));
    setup_field(v, lf0 + L(4), 65535 - tension, lt0 + L(0.6), c[2]);
    v.flevel = mix(Q31(0.062), Q31(0.015), c[4]);   // 0.03..0.12 as the sheet slackens, Q31 x 2
    if (v.flevel < Q31(0.02)) v.fon = 0;   // a taut drumhead's high modes, too faint to run
    v.k[0] = beater_time(c[2]);
    // the stick's knock: a felt tap (600 Hz to 3 kHz, 4 ms) to a steel tick
    v.k[1] = (Q16(0.15) + mul16(beater_click(c[2]), Q16(0.4))) << 15;
    v.k[3] = beater_click(c[2]);
    v.k[4] = mix(L(600), L(2500), c[2]);
    v.k[5] = mix(L(3000), L(16000), c[2]);
    v.k[6] = mix(L(0.004), L(0.0006), c[2]);
    // ROLL: 1 + 7 ROLL strikes, the last one as loud as its fraction; each
    // 0.82 of the last gap after 45 ms and 0.62..0.74 as loud.
    const i32 n = 65536 + c[6] * 7;
    v.left = static_cast<u32>(n >> 16) - 1 + ((n & 0xffff) ? 1 : 0);
    v.k[2] = (n & 0xffff) ? (n & 0xffff) << 15 : one;   // the last strike's share
    v.samp = one;
    v.samp_k = mix(Q31(0.62), Q31(0.74), c[6]);
    v.sgap = static_cast<i32>(samples(L(0.045)));
    v.sgap_k = Q31(0.82);
    v.at = static_cast<u32>(v.sgap);
    v.waiting = v.left != 0;
    setup_chain(v, Q16(0.9), Q16(0.4), L(0.05), L(1800), L(6000), c[7]);
    strike(v, v.k[0], U);
    click(v, v.k[1], v.k[3], v.k[4], v.k[5], v.k[6]);
    v.s[0] = U;
}
inline void block_tom(Voice& v, Work& w, i32* out) {
    glide_modes(v);
    clear(w.fin);
    w.fin[0] = v.s[0];
    v.s[0] = 0;
    if (v.left && v.at < v.t + frames) {
        for (unsigned i = 0; i < frames; ++i) {
            if (v.left && v.t + i == v.at) {
                v.samp = multiply(v.samp, v.samp_k);
                const i32 a = v.left == 1 ? multiply(v.samp, v.k[2]) : v.samp;
                strike(v, v.k[0], multiply(U, a));
                click(v, multiply(v.k[1], a), v.k[3], v.k[4], v.k[5], v.k[6]);
                w.fin[i] = multiply(U, a);
                v.bend_env = a > v.bend_env ? a : v.bend_env;
                v.sgap = multiply(v.sgap, v.sgap_k);
                v.at += static_cast<u32>(v.sgap);
                --v.left;
            }
            w.exc[i] = pulse(v);
        }
    } else {
        excite(v, w.exc);
    }
    v.waiting = v.left != 0;
    clear(out);
    run_modes(v, w.exc, out);
    for (unsigned i = 0; i < frames; ++i) w.body[i] = out[i];
    if (v.k[7]) {
        // the head driven hard: a soft clip, z - 4 z^3 / 27, flat at |z| = 1.5
        const i32 g = v.k[7];
        for (unsigned i = 0; i < frames; ++i) {
            const i32 z = clamp(multiply(out[i], g), -(3 << 24), 3 << 24) << 5;   // Q30 of U, |z| <= 1.5
            const i32 z2 = multiply(z, z) << 1, z3 = multiply(z2, z) << 1;
            out[i] = (z - multiply(z3, Q31(4.0 / 27.0))) >> 3;
        }
    }
    add_click(v, out);
    run_field(v, w.fin, out, v.flevel);
    run_chain(v, w.body, out);
}

// ============================================================ CRASH: cymbals
// A slack sheet: its modes spread into a dense, even forest. SWELL lets the
// energy bloom from the low modes into the highs, as a gong does.
inline void setup_crash(Voice& v, const i32* c, i32 tune) {
    const i32 lf0 = mix(L(110), L(650), c[0]) + tune;
    const i32 lt60 = mix(L(4.5), L(0.25), c[1]);
    const i32 tension = mul16(c[4], Q16(0.6));
    setup_modes(v, lf0, tension, Q16(0.3) + mul16(c[3], Q16(0.7)), lt60 - 2 * octave, Q16(0.6), Q16(0.5), 6, beater_time(c[2]), 0);   // its fundamental is high-passed away
    v.bend = 1 << 26;
    v.bend_env = one;
    v.bend_k = block_decay(L(0.08));
    setup_field(v, lf0 + L(1.8), 65535 - tension, lt60 + octave / 2, Q16(1.0), 1);
    {
        // a crash's loop is brighter than any other sheet's, so its wash rings on
        const i32 a = frac2(2 * mix(L(0.08), L(0.03), c[2]));
        v.fk = one - a;
        v.fin = static_cast<i32>(pow2(55 * octave - log2q(static_cast<u32>(v.fk))));
    }
    v.k[0] = onepole_k(lf0 + L(1.5));                                    // the body's high-pass
    v.k[1] = onepole_k(mix(L(500), L(2000), c[2]) + mix(-octave, Q16(0.6), c[3]));   // the ring's (twice): struck near the bell, darker
    v.k[2] = multiply(mix(Q31(0.12), Q31(0.9), c[4]), mix(Q31(0.99), Q31(0.3), c[3]));   // the body's share, less toward the edge
    v.env[0].set(one, block_decay(lt60 + log16(mix(Q16(1.0), Q16(0.12), c[4]))));
    // SWELL: noise into the high modes along (t / peak) e^(1 - t / peak)
    v.k[3] = block_decay(mix(L(0.03), L(0.45), c[5]));
    v.k[4] = multiply(q31(c[5]), Q31(0.02 * 2.718 * 1.7 * 3));   // the wash swelling up after it
    v.s[1] = one;   // the swell's two leaky stages
    v.s[2] = 0;
    click(v, (Q16(0.1) + mul16(beater_click(c[2]), Q16(0.3))) << 15, beater_click(c[2]), L(2500), 0, L(0.003));
    setup_swing(v, c[6], mix(L(1.5), L(4), c[6]));
    v.env[1].set(one, block_decay(lt60 - octave));
    v.u[0] = increment(lf0);
    v.u[1] = 0;
    setup_chain(v, Q16(0.2), Q16(0.7), L(0.15), L(2500), L(7000), c[7]);
    strike(v, beater_time(c[2]), U);
    v.s[0] = U / 3;   // the strike into the wash: a third, so the hit doesn't spike over the wash
}
inline void block_crash(Voice& v, Work& w, i32* out) {
    glide_modes(v);
    // the swell: s1 falls, s2 rises toward it, (t / p) e^(-t / p)
    const i32 s1 = v.s[1], s2 = v.s[2];
    const i32 r = v.k[3];
    v.s[1] = multiply(s1, r);
    v.s[2] = multiply(s2, r) + multiply(s1, one - r);
    i32 sw = multiply(s2, v.k[4]);
    const i32 sws = (multiply(v.s[2], v.k[4]) - sw) >> 6;
    excite(v, w.exc);
    clear(w.body);
    clear(w.aux);
    if (sw || sws) {
        u32 seed = v.seed ? v.seed : 1u;
        for (unsigned i = 0; i < frames; ++i, sw += sws) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            w.fin[i] = multiply(static_cast<i32>(seed) >> 4, sw);
        }
        v.seed = seed;
    } else {
        clear(w.fin);
    }
    w.fin[0] += v.s[0];
    v.s[0] = 0;
    run_modes(v, w.exc, w.body);
    run_field(v, w.fin, w.aux, Q31(0.5));
    i32 damp;
    const i32 ds = v.env[0].step(damp);
    const i32 k0 = v.k[0], k1 = v.k[1], share = v.k[2];
    i32 h0 = v.op[0].s, h2 = v.op[2].s, h3 = v.op[3].s;
    for (unsigned i = 0; i < frames; ++i, damp += ds) {
        h0 += multiply(w.body[i] - h0, k0);
        h2 += multiply(w.aux[i] - h2, k1);
        const i32 a = w.aux[i] - h2;
        h3 += multiply(a - h3, k1);
        out[i] = multiply(w.body[i] - h0, share) + multiply((a - h3) << 1, damp);
    }
    v.op[0].s = h0;
    v.op[2].s = h2;
    v.op[3].s = h3;
    add_click(v, out);
    run_swing(v, out);
    if (v.c_amount) {
        // what lifts the chain: the sheet's swing at its fundamental, falling away
        i32 lift;
        const i32 ls = v.env[1].step(lift);
        u32 ph = v.u[1];
        const u32 inc = v.u[0];
        for (unsigned i = 0; i < frames; ++i, lift += ls) {
            w.body[i] = (ph & 0x80000000u) ? -lift : lift;
            ph += inc;
        }
        v.u[1] = ph;
        run_chain(v, w.body, out);
    }
}

// ============================================================ CLANK: cowbell, rim
// The frame's iron shackle: a free bar, bent by SHAPE into a stirrup, whose
// partials draw together into a two-tone clank. SHEET rings the sheet it
// hangs from; BUZZ loosens it on its pin, so it chatters.
inline void setup_clank(Voice& v, const i32* c, i32 tune) {
    static const i32 bar[4] = {Q16(1.0), Q16(2.756), Q16(5.404), Q16(8.933)};   // a free-free bar
    static const i32 stirrup[4] = {Q16(1.0), Q16(1.47), Q16(2.09), Q16(2.71)};
    const i32 lf = mix(L(260), L(2100), c[0]) + tune;
    const i32 lt0 = mix(L(1.4), L(0.03), c[1]);
    const i32 sp = c[3];
    const i32 w[4] = {Q16(1.0) - mul16(sp, Q16(0.75)), Q16(0.25) + mul16(sp, Q16(0.9)), Q16(0.08) + mul16(sp, Q16(0.9)),
                      Q16(0.03) + mul16(sp, Q16(0.7))};
    i32 sum = 0;
    u32 n = 0;
    i32 lr[4];
    for (u32 k = 0; k < 4; ++k) {
        lr[k] = log16(mix(bar[k], stirrup[k], c[4]));
        if (lf + lr[k] > L(6800.0)) break;
        sum += mul16(w[k], contact_share(lf + lr[k], mix(L(6e-3), L(0.08e-3), c[2])) >> 15);
        ++n;
    }
    v.nmodes = n;
    const i32 lsum = log16(sum);
    for (u32 k = 0; k < n; ++k) {
        Mode& m = v.mode[k];
        const i32 lfk = lf + lr[k];
        m.inc = increment(lfk);
        m.f = svf_f(m.inc);
        m.r = expneg(3 * octave - (lt0 - mul16(Q16(0.4), lr[k])) - lsr);
        m.gw = static_cast<i32>(pow2(log16(w[k]) - lsum + 31 * octave));
        if (m.gw <= 0) m.gw = one;
        m.lp = m.bp = 0;
    }
    setup_field(v, lf + L(0.35), Q16(0.9), mix(L(0.2), L(1.5), c[5]), Q16(0.6));
    v.flevel = multiply(Q31(0.6), q31(c[5]));
    if (!c[5]) v.fon = 0;   // no METAL: no ring to run
    v.k[0] = onepole_k(L(300));
    v.env[0].set(one, block_decay(L(0.01)));
    v.k[1] = beater_time(c[2]);
    v.k[2] = (Q16(0.2) + mul16(beater_click(c[2]), Q16(0.5))) << 15;
    click(v, v.k[2]);
    setup_chain(v, Q16(1.4), Q16(0.7), L(0.08), L(1800), L(6000), c[6]);
    // BUZZ: re-strikes every 30..7 ms, each 0.3..0.8 as hard, 0..10 of them
    v.left = static_cast<u32>((c[7] * 10 + 32768) >> 16);
    v.samp = one;
    v.samp_k = mix(Q31(0.35), Q31(0.8), c[7]);
    v.sgap = static_cast<i32>(samples(mix(L(0.03), L(0.007), c[7])));
    v.sgap_k = Q31(0.93);
    v.at = static_cast<u32>(v.sgap);
    v.waiting = v.left != 0;
    strike(v, v.k[1], U);
}
inline void block_clank(Voice& v, Work& w, i32* out) {
    if (v.left && v.at < v.t + frames) {
        for (unsigned i = 0; i < frames; ++i) {
            if (v.left && v.t + i == v.at) {
                v.samp = multiply(v.samp, v.samp_k);
                strike(v, v.k[1], multiply(U, v.samp));
                click(v, multiply(v.k[2], v.samp));
                v.sgap = multiply(v.sgap, v.sgap_k);
                v.at += static_cast<u32>(v.sgap > 64 ? v.sgap : 64);
                --v.left;
            }
            w.exc[i] = pulse(v);
        }
    } else {
        excite(v, w.exc);
    }
    clear(w.body);
    clear(w.aux);
    v.waiting = v.left != 0;
    run_modes(v, w.exc, w.body);
    i32 e;
    const i32 es = v.env[0].step(e);
    for (unsigned i = 0; i < frames; ++i, e += es) w.fin[i] = multiply(w.body[i], e);
    run_field(v, w.fin, w.aux, Q31(0.5));
    if (v.fon) {
        for (unsigned i = 0; i < frames; ++i) out[i] = w.body[i] + multiply(v.op[0].high(w.aux[i], v.k[0]), v.flevel);
    } else {
        for (unsigned i = 0; i < frames; ++i) out[i] = w.body[i];
    }
    add_click(v, out);
    run_chain(v, w.body, out);
}

// ============================================================ GRIT: shakers
// Beads or shot poured onto the sheet: each grain a tick on the sheet's high
// modes. BEAD is how hard the grains are, GRAINS how many, SWELL whether the
// pour starts hard or swells, POUR how long it runs, METAL how much the sheet rings.
inline void setup_grit(Voice& v, const i32* c, i32 tune) {
    const i32 lband = mix(L(1800), L(6000), c[0]) + tune;
    const i32 hard = c[2];
    const i32 lpour = mix(L(0.05), L(0.5), c[7]);
    const i32 pour = exp16(lpour);
    const i32 pk = mix(Q16(0.004), mul16(pour, Q16(0.9)), c[4]);
    v.u[0] = samples(log16(pk));                       // the peak
    v.u[1] = samples(lpour);                           // the pour's end
    v.k[0] = block_decay(mix(L(0.2), L(0.012), c[1])); // its fall after the pour (DAMP)
    v.k[1] = block_decay(log16(mul16(pour, Q16(0.7)) + Q16(0.01)));   // its fall within the pour (when it starts hard)
    // GRAINS: grains a second at the peak, as a chance a sample (Q31)
    v.k[2] = static_cast<i32>(pow2(mix(L(40), L(1200), c[3]) - lsr + 32 * octave));
    // a grain: a tick of the high modes near band x 0.6..1.5, ringing 2.5..0.25 ms
    v.k[3] = lband + log16(mix(Q16(0.6), Q16(1.5), hard));
    v.k[4] = mix(L(0.006), L(0.0008), hard);
    v.k[5] = mix(Q31(0.15), Q31(0.04), hard);            // the grain's own rasp
    v.k[6] = decay(mix(L(0.0016), L(0.0002), hard));
    v.k[7] = onepole_k(lband - octave);
    v.s[0] = 0;   // the pour's envelope, Q31
    v.s[1] = 0;   // a grain's rasp
    v.u[2] = v.u[3] = 0;
    // METAL: the sheet's ring under the grains, ringing longer as it rises
    setup_field(v, lband + L(0.3), Q16(0.6), mix(L(0.7), L(0.04), c[1]) + mul16(c[5], Q16(1.6)), Q16(0.8));
    v.flevel = multiply(Q31(0.85), q31(c[5]));
    if (!c[5]) v.fon = 0;   // no METAL: no ring to run
    v.k[8] = onepole_k(L(1500));
    setup_chain(v, Q16(0.9), Q16(0.8), L(0.05), L(1800), L(6000), c[6]);
    v.waiting = 1;
}
inline void grit_grain(Voice& v, u32 which, i32 level) {
    const u32 r = lcg(v.seed);
    // one of three bead sizes (each within 3%): a rattle of ticks, not a hiss
    static const i32 size[4] = {-Q16(0.32), 0, Q16(0.27), Q16(0.55)};
    const i32 lf = v.k[3] + size[(r >> 12) % 3u == 0 ? 0 : (r >> 12) & 3u] + static_cast<i32>((r >> 20) & 0x7ffu) - 1024;
    Svf& s = v.sv[which & 1];
    v.k[10 + (which & 1)] = svf_f(increment(lf));
    v.k[12 + (which & 1)] = svf_q(lf, v.k[4]);
    // as loud as the pour is now, each grain 0.3..1 as hard
    const i32 a = multiply(level, Q31(0.3) + static_cast<i32>((r >> 9) & 0x3fffffu) * 256) >> 4;
    s.bp += (r & 1u) ? a : -a;
    v.s[1] = U;
}
inline void block_grit(Voice& v, Work& w, i32* out) {
    // The pour's envelope at the block's end: a rise as (t / peak)^1.5, a fall
    // within the pour (fast when SWELL starts it hard), then DAMP's fall.
    const u32 t = v.t + frames;
    const i32 e0 = v.s[0];
    i32 e1;
    if (t < v.u[0]) {
        const i32 x = static_cast<i32>((t << 16) / v.u[0]);   // once a block
        e1 = static_cast<i32>(pow2(mul16(log16(x), Q16(1.5)) + 31 * octave - 1));
    } else if (t < v.u[1]) {
        e1 = v.u[3] ? multiply(e0, v.k[1]) : Q31(0.999);
        v.u[3] = 1;
    } else {
        e1 = multiply(e0, v.k[0]);
    }
    v.s[0] = e1;
    v.waiting = t < v.u[1];
    const i32 es = (e1 - e0) >> 6;
    i32 e = e0;
    i32 rasp = v.s[1];
    for (unsigned i = 0; i < frames; ++i, e += es) {
        const u32 r = lcg(v.seed);
        i32 hit = 0;
        if ((r >> 1) < static_cast<u32>(multiply(v.k[2], e))) {
            grit_grain(v, v.u[2]++, e);
            hit = (r & 2u) ? (e >> 4) : -(e >> 4);
        }
        const i32 a = v.sv[0].band(0, v.k[10], v.k[12]);
        const i32 b = v.sv[1].band(0, v.k[11], v.k[13]);
        const i32 n = v.op[0].high(multiply(noise(v.seed) >> 4, rasp), v.k[7]);
        rasp = multiply(rasp, v.k[6]);
        out[i] = a + b + multiply(n, v.k[5]);
        w.fin[i] = hit >> 2;
        w.body[i] = e >> 4;
        w.aux[i] = 0;
    }
    v.s[1] = rasp;
    run_field(v, w.fin, w.aux, Q31(0.5));
    for (unsigned i = 0; i < frames; ++i) out[i] += multiply(v.op[1].high(w.aux[i], v.k[8]), v.flevel) << 1;
    run_chain(v, w.body, out);
}

// ============================================================ HOP: a dropped bearing
// A ball dropped on the sheet bounces lower and faster until it rests: DROP is
// how high it falls from, LOSS how much each bounce takes.
inline void setup_bounce(Voice& v, const i32* c, i32 tune) {
    const i32 lf0 = mix(L(180), L(1300), c[0]) + tune;
    const i32 lt0 = mix(L(0.6), L(0.02), c[1]);
    setup_modes(v, lf0, c[6], Q16(0.3) + mul16(c[3], Q16(0.6)), lt0, Q16(0.6), Q16(0.3), 8, beater_time(c[2]));
    setup_field(v, lf0 + octave, 65535 - c[6], log16(mul16(exp16(lt0), Q16(1.5)) + Q16(0.02)), Q16(0.5) + (c[2] >> 1));
    v.flevel = Q31(0.125);
    v.k[0] = beater_time(c[2]);
    v.k[1] = (Q16(0.3) + mul16(beater_click(c[2]), Q16(0.6))) << 15;
    const i32 le = log16(mix(Q16(0.88), Q16(0.55), c[5]));   // LOSS: restitution e
    v.sgap_k = frac2(le);
    v.samp_k = frac2(mul16(le, Q16(0.9)));
    v.sgap = static_cast<i32>(samples(mix(L(0.06), L(0.38), c[4])));   // DROP
    v.samp = one;
    v.at = static_cast<u32>(v.sgap);
    v.left = 1;
    v.waiting = 1;
    v.k[2] = onepole_k(L(150));
    setup_chain(v, Q16(0.9), Q16(0.6), L(0.06), L(1800), L(6000), c[7]);
    strike(v, v.k[0], U);
    click(v, v.k[1]);
    v.s[0] = U;
}
inline void block_bounce(Voice& v, Work& w, i32* out) {
    clear(w.fin);
    w.fin[0] = v.s[0];
    v.s[0] = 0;
    if (v.left && v.at < v.t + frames) {
        for (unsigned i = 0; i < frames; ++i) {
            if (v.left && v.t + i == v.at) {
                v.samp = multiply(v.samp, v.samp_k);
                const i32 a = v.samp;
                strike(v, v.k[0], multiply(U, a));
                click(v, multiply(v.k[1], a));
                w.fin[i] = multiply(U, a);
                v.sgap = multiply(v.sgap, v.sgap_k);
                v.at += static_cast<u32>(v.sgap);
                if (v.sgap < static_cast<i32>(samples(L(0.004))) || v.samp < Q31(0.01)) v.left = 0;
            }
            w.exc[i] = pulse(v);
        }
    } else {
        excite(v, w.exc);
    }
    v.waiting = v.left != 0;
    clear(out);
    run_modes(v, w.exc, out);
    add_click(v, out);
    run_field(v, w.fin, out, v.flevel);
    if (v.c_amount) {
        for (unsigned i = 0; i < frames; ++i) w.body[i] = v.op[0].low(absolute(out[i]), v.k[2]) << 1;
        run_chain(v, w.body, out);
    }
}

// ============================================================ ZAP: the laser
// Bending waves are dispersive: highs outrun lows. Heard from a pickup at the
// far corner, a tap arrives as a falling chirp, then its echoes off the edges.
inline void setup_zap(Voice& v, const i32* c, i32 tune) {
    v.k[0] = mix(L(1400), L(6000), c[0]) + tune;      // the top
    const i32 ltau = mix(L(0.8), L(0.03), c[1]);
    const i32 lsweep = mix(L(0.015), L(0.3), c[2]);
    v.u[0] = 1 + static_cast<u32>((c[3] * 5 + 32768) >> 16);   // the chirp and ECHOS
    v.k[1] = mix(L(4), L(120), c[4]);                 // DEPTH: the fall, log2
    v.k[2] = mix(Q16(0.45), Q16(2.4), c[7]);          // CURVE: u^p
    v.k[3] = block_decay(ltau);
    // each echo e starts at 1.4 e sweep, lasts sweep (1 + 0.6 e) and is 0.55^e as loud
    for (u32 e = 0; e < 6; ++e) {
        const i32 lsw = lsweep + log16(Q16(1.0) + static_cast<i32>(e) * Q16(0.6));
        v.u[1 + e] = e ? samples(lsweep + log16(static_cast<i32>(e) * Q16(1.4))) : 0;   // start
        v.u[8 + e] = samples(lsw);                                                     // length
        v.k[4 + e] = frac2(static_cast<i32>(e) * L(0.65));                              // level
        // after its sweep it fades with 0.4 sw + 150..4 ms (DAMP)
        v.k[10 + e] = block_decay(log16(mul16(exp16(lsw), Q16(0.4)) + exp16(mix(L(0.15), L(0.004), c[1]))));   // DAMP lets it ring on
        v.k[16 + e] = 0;   // its envelope now
    }
    v.s[0] = v.s[1] = v.s[2] = v.s[3] = v.s[4] = v.s[5] = 0;   // phases
    setup_field(v, v.k[0] - 2 * octave, Q16(0.7), log16(mix(Q16(0.15), Q16(1.2), c[5])), Q16(0.6));
    v.flevel = multiply(Q31(0.55), q31(c[5]));
    if (!c[5]) v.fon = 0;   // no METAL: no ring to run
    v.env[0].set(one, block_decay(L(0.02)));
    setup_chain(v, Q16(0.6), Q16(0.6), L(0.05), L(1800), L(6000), c[6]);
    v.waiting = 1;
}
inline void block_zap(Voice& v, Work& w, i32* out) {
    clear(out);
    const u32 t = v.t;
    u32 last = 0;
    for (u32 e = 0; e < v.u[0]; ++e) {
        const u32 start = v.u[1 + e];
        if (t + frames <= start) continue;
        last = e + 1;
        const u32 te0 = t > start ? t - start : 0, te1 = t + frames - start;
        // the chirp's pitch at the block's start and end: top * depth^-(u^p), u = te / sweep, to 1.5
        u32 inc[2];
        for (u32 j = 0; j < 2; ++j) {
            const u32 te = j ? te1 : te0;
            const u32 len = v.u[8 + e];
            u32 u16v = (te << 8) / (len ? len : 1);   // once a block
            u16v = u16v > 384 ? 384 : u16v;          // Q8, to 1.5
            const i32 uu = static_cast<i32>(u16v) << 8;   // Q16
            const i32 shaped = uu <= 65536 ? power16(uu, v.k[2]) : uu;
            inc[j] = increment(v.k[0] - mul16(v.k[1], shaped));
        }
        // its level: a 1.5 ms attack (its first block), the DAMP fall, and after the sweep a faster fall
        i32 e0 = v.k[16 + e], e1;
        if (start >= t) {
            e0 = 0;
            e1 = v.k[4 + e];
        } else {
            e1 = multiply(e0, v.k[3]);
            if (te1 > v.u[8 + e]) e1 = multiply(e1, v.k[10 + e]);
        }
        v.k[16 + e] = e1;
        const i32 es = (e1 - e0) >> 6;
        const i32 is = static_cast<i32>(inc[1] - inc[0]) >> 6;
        u32 ph = static_cast<u32>(v.s[e]);
        u32 in = inc[0];
        i32 lev = e0;
        const u32 skip = start > t ? start - t : 0;
        for (unsigned i = skip; i < frames; ++i) {
            ph += in;
            in += static_cast<u32>(is);
            out[i] += multiply(sine(ph) >> 4, lev);
            lev += es;
        }
        v.s[e] = static_cast<i32>(ph);
    }
    v.waiting = last < v.u[0];
    i32 g;
    const i32 gs = v.env[0].step(g);
    for (unsigned i = 0; i < frames; ++i, g += gs) {
        w.fin[i] = multiply(out[i], g);
        w.aux[i] = 0;
        w.body[i] = out[i];
    }
    run_field(v, w.fin, w.aux, Q31(0.5));
    for (unsigned i = 0; i < frames; ++i) out[i] += multiply(w.aux[i], v.flevel) << 1;
    run_chain(v, w.body, out);
}

// ============================================================ BOW: the singing sheet
// A bow drawn across the bottom edge: stick and slip drive one of the sheet's
// modes (SPOT picks it) and its neighbours. PRESS from a pure tone to a squeal
// to a scratch; SPEED how slowly it swells; BEND a hand flexing the sheet.
inline void setup_bow(Voice& v, const i32* c, i32 tune) {
    const i32 lf0 = mix(L(70), L(900), c[0]) + tune;
    const i32 press = c[2];
    const i32 tension = c[5];
    const i32 tt = tension * 32;
    const u32 idx = static_cast<u32>(tt) >> 16;
    const i32 fr = tt & 0xffff;
    u32 kk = static_cast<u32>(mul16(c[3], Q16(4.99)) >> 16);
    if (kk > 4) kk = 4;
    for (u32 j = 0; j < 4; ++j) {
        const u32 m = kk + j;
        const i32 r = mix(tables::mode_ratio[idx][m] << 4, tables::mode_ratio[idx + (idx < 32)][m] << 4, fr);
        v.k[j] = lf0 + r;   // each band's pitch, log2 Hz
    }
    // the sung band's Q: 18 to 3 with PRESS, a third of that with DAMP; the others 12
    v.k[4] = damping30(mul16(mix(Q16(18), Q16(3), press), mix(Q16(1.0), Q16(0.33), c[1])));
    v.k[5] = damping30(mul16(Q16(12), mix(Q16(1.0), Q16(0.4), c[1])));
    v.k[6] = mix(Q31(0.15), Q31(1.0), press);                       // the saw
    v.k[7] = multiply(Q31(0.6), q31(power16(press, Q16(1.5))));     // the scratch
    v.k[8] = multiply(Q31(0.3), mix(Q31(1.0), Q31(0.4), c[1]));      // the pure tone
    // the gesture: swell (SPEED), hold, release (DAMP)
    v.u[0] = samples(mix(L(0.05), L(1.5), c[4]));
    v.u[1] = v.u[0] + samples(mix(L(0.6), L(0.08), c[1]));
    v.k[9] = block_decay(mix(L(1.6), L(0.08), c[1]));
    v.k[10] = mul16(press, Q16(0.36 * 4));                          // the bow's jitter
    v.k[11] = onepole_k(L(40));
    v.k[12] = c[7];                                                  // BEND
    v.s[0] = v.s[1] = 0;
    v.s[2] = 0;   // the gesture's level
    setup_chain(v, Q16(1.2), Q16(0.5), L(0.06), L(1800), L(6000), c[6]);
    v.waiting = 1;
}
inline void block_bow(Voice& v, Work& w, i32* out) {
    const u32 t = v.t;
    // the gesture's level at the block's end
    i32 e0 = v.s[2], e1;
    if (t + frames < v.u[0]) {
        const i32 x = static_cast<i32>(((t + frames) << 10) / v.u[0]);   // once a block
        e1 = (x * x) << 11;
    } else if (t + frames < v.u[1]) {
        e1 = one;
    } else {
        e1 = multiply(e0, v.k[9]);
        if (e1 < Q31(0.0003)) e1 = 0;   // drawn off the sheet
    }
    v.s[2] = e1;
    v.waiting = t + frames < v.u[1];
    // BEND: the hand's vibrato, up to 0.2 octave at 5.2 Hz, after a scoop of up to half an octave from below
    const i32 bend = v.k[12];
    const i32 vib = (mul16(bend, Q16(0.2)) * (sine(t * 506442u) >> 16)) >> 15;
    const i32 scoop = t < 8820u ? mul16(bend, Q16(0.5)) * static_cast<i32>(8820u - t) / 8820 : 0;   // once a block
    const i32 off = vib - scoop;
    const i32 f0 = svf_f(increment(v.k[0] + off)), f1 = svf_f(increment(v.k[1] + off)), f2 = svf_f(increment(v.k[2] + off));
    const u32 inc = increment(v.k[0] + off);
    const i32 es = (e1 - e0) >> 6;
    i32 e = e0;
    // the work in locals, so the loop keeps it in registers
    u32 seed = v.seed ? v.seed : 1u, phase = v.u[2];
    i32 jit = v.op[0].s;
    i32 al = v.sv[0].lp, ab = v.sv[0].bp, bl = v.sv[1].lp, bb = v.sv[1].bp, cl = v.sv[2].lp, cb = v.sv[2].bp;
    i32 ol = v.s[4], ob = v.s[5];   // the pure tone: a lossless oscillator at the sung mode
    if (!ol && !ob) ob = U;
    const i32 q0 = v.k[4], q1 = v.k[5], saw_k = v.k[6], scr = v.k[7], pure = v.k[8], jk = v.k[11], jamt = v.k[10];
    const i32 fo = f0;
    for (unsigned s8 = 0; s8 < frames; s8 += 8) {
        // stick and slip: a sawtooth at the sung mode, its period jittered
        // with PRESS (noise low-passed at 40 Hz, every 8 samples)
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        jit += multiply((static_cast<i32>(seed) >> 4) - jit, jk << 3);
        const u32 step = inc + static_cast<u32>(multiply(static_cast<i32>(inc >> 1), multiply(jit, jamt) << 4) << 1);
        for (unsigned i = s8; i < s8 + 8; ++i, e += es) {
            phase += step;
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            const i32 ex = multiply(multiply(static_cast<i32>(phase) >> 4, saw_k) + multiply(static_cast<i32>(seed) >> 4, scr), e);
            al += multiply(f0, ab);
            ab += multiply(f0, ex - al - (multiply(q0, ab) << 1));
            bl += multiply(f1, bb);
            bb += multiply(f1, ex - bl - (multiply(q1, bb) << 1));
            cl += multiply(f2, cb);
            cb += multiply(f2, ex - cl - (multiply(q1, cb) << 1));
            ol += multiply(fo, ob);
            ob -= multiply(fo, ol);
            out[i] = (multiply(q0, ab) << 1) + (multiply(multiply(q1, bb + cb), Q31(0.3)) << 1) + multiply(multiply(ob >> 4 << 4, e), pure);
        }
    }
    v.seed = seed;
    v.u[2] = phase;
    v.op[0].s = jit;
    v.sv[0].lp = al; v.sv[0].bp = ab; v.sv[1].lp = bl; v.sv[1].bp = bb; v.sv[2].lp = cl; v.sv[2].bp = cb;
    v.s[4] = ol;
    v.s[5] = ob;
    for (unsigned i = 0; i < frames; ++i) w.body[i] = out[i];
    run_chain(v, w.body, out);
}

// ============================================================ SHAKE: thunder
// The sheet shaken by its handle: jolts at RATE for RUMBLE seconds, rising to
// and falling from a peak SWELL places; CRACK is the opening clap. Flexing
// the sheet bends its pitch as it rumbles.
inline void shake_jolt(Voice& v) {
    // the jolt after this one, and its level along the storm's shape
    const u32 r = lcg(v.seed);
    const i32 jit = Q31(0.6) + multiply(Q31(0.8), static_cast<i32>(r >> 1));   // 0.6..1.4
    const u32 gap = static_cast<u32>(multiply(static_cast<i32>(v.u[1]), jit));
    v.at += gap > 64 ? gap : 64;
    if (v.at >= v.u[0]) { v.left = 0; return; }
    // the storm's shape at the jolt: (u / p)^0.7 rising, ((1 - u) / (1 - p))^0.7 falling
    const i32 uu = static_cast<i32>((static_cast<u32>(v.at) << 8) / (v.u[0] >> 8));   // at a jolt
    const i32 p = v.k[1];
    const i32 x = uu < p ? static_cast<i32>((static_cast<u32>(uu) << 16) / static_cast<u32>(p > 1 ? p : 1))
                         : static_cast<i32>((static_cast<u32>(65536 - uu) << 16) / static_cast<u32>(65536 - p > 1 ? 65536 - p : 1));
    const i32 shape = power16(x > 65535 ? 65535 : x, Q16(0.7));
    v.samp = multiply(q31(shape), Q31(0.5) + multiply(Q31(0.5), static_cast<i32>(lcg(v.seed) >> 1)));
}
inline void setup_shake(Voice& v, const i32* c, i32 tune) {
    const i32 lf0 = mix(L(18), L(75), c[0]) + tune;
    const i32 lt0 = mix(L(1.8), L(0.15), c[1]);
    setup_modes(v, lf0, c[4], Q16(0.7), lt0, Q16(0.5), Q16(0.4), 6, mix(L(0.008), L(0.0015), c[6]));
    v.u[0] = samples(mix(L(0.3), L(3.5), c[2]));                      // RUMBLE
    v.u[1] = samples(-mix(L(3), L(16), c[3]));                        // RATE: the mean gap
    v.k[1] = mix(Q16(0.02), Q16(0.97), c[7]);                         // SWELL: where it peaks
    const i32 crack = c[6];
    v.k[2] = mix(L(0.008), L(0.0015), crack);                          // a jolt's push (log2 s)
    setup_field(v, lf0 + L(12), 65535 - c[4], lt0, Q16(0.5));
    v.flevel = Q31(0.5);
    v.k[3] = onepole_k(mix(L(900), L(3000), crack));
    v.k[4] = onepole_k(mix(L(200), L(900), crack));
    v.k[5] = onepole_k(L(22));
    v.k[6] = mix(Q31(0.3), Q31(0.6), crack);    // the field's share
    // the wobble: the pitch flexes with the jolts at RATE
    v.u[2] = increment(mix(L(3), L(16), c[3]) - octave) << 6;
    v.u[3] = 0;
    setup_chain(v, Q16(0.14), Q16(0.8), L(0.06), L(900), mix(L(2500), L(5000), c[5]), c[5]);   // a heavier chain rattles brighter
    // CRACK: the first jolt, a sharp clap
    strike(v, mix(L(0.004), L(0.0004), crack), multiply(U, Q31(0.2) + multiply(Q31(0.8), q31(crack))) << 1);
    v.s[0] = multiply(U, Q31(0.2) + multiply(Q31(0.8), q31(crack))) << 1;
    v.at = 0;
    v.left = 1;
    v.samp = 0;
    shake_jolt(v);
    v.waiting = 1;
}
inline void block_shake(Voice& v, Work& w, i32* out) {
    // the wobble: the modes' pitch swings by up to 4% with the storm
    {
        const i32 wob = multiply(sine(v.u[3]), Q31(0.04));
        v.u[3] += v.u[2];
        const i32 m28 = (1 << 28) + (wob >> 3);
        for (u32 k = 0; k < v.nmodes; ++k) {
            Mode& m = v.mode[k];
            m.f = svf_f(static_cast<u32>(multiply(static_cast<i32>(m.inc >> 1), m28)) << 4);
        }
    }
    clear(w.fin);
    w.fin[0] = v.s[0];
    v.s[0] = 0;
    if (v.left && v.at < v.t + frames) {
        for (unsigned i = 0; i < frames; ++i) {
            if (v.left && v.t + i == v.at) {
                v.u[4] ^= 1u;
                const i32 a = v.u[4] ? v.samp : -v.samp;
                strike(v, v.k[2], multiply(U, a));
                w.fin[i] = multiply(U, a);
                shake_jolt(v);
            }
            w.exc[i] = pulse(v);
        }
    } else {
        excite(v, w.exc);
    }
    clear(w.body);
    clear(w.aux);
    v.waiting = v.left != 0;
    run_modes(v, w.exc, w.body);
    for (unsigned i = 0; i < frames; ++i) w.fin[i] = v.op[0].low(w.fin[i], v.k[4]) << 2;
    run_field(v, w.fin, w.aux, Q31(0.5));
    for (unsigned i = 0; i < frames; ++i) {
        const i32 m = v.op[1].low(w.aux[i], v.k[3]);
        out[i] = v.op[2].high(w.body[i] + multiply(m, v.k[6]), v.k[5]);
    }
    run_chain(v, w.body, out);
}

// ============================================================ dispatch
inline void setup_model(Voice& v, u32 model, const i32* c, i32 tune) {
    switch (model) {
    case BOOM: setup_boom(v, c, tune); break;
    case SLAP: setup_slap(v, c, tune); break;
    case POP: setup_pop(v, c, tune); break;
    case FLICK: setup_flick(v, c, tune); break;
    case TOM: setup_tom(v, c, tune); break;
    case CRASH: setup_crash(v, c, tune); break;
    case CLANK: setup_clank(v, c, tune); break;
    case GRIT: setup_grit(v, c, tune); break;
    case HOP: setup_bounce(v, c, tune); break;
    case ZAP: setup_zap(v, c, tune); break;
    case BOW: setup_bow(v, c, tune); break;
    default: setup_shake(v, c, tune); break;
    }
}
inline void block_model(Voice& v, Work& w, i32* out) {
    switch (v.model) {
    case BOOM: block_boom(v, w, out); break;
    case SLAP: block_slap(v, w, out); break;
    case POP: block_pop(v, w, out); break;
    case FLICK: block_flick(v, w, out); break;
    case TOM: block_tom(v, w, out); break;
    case CRASH: block_crash(v, w, out); break;
    case CLANK: block_clank(v, w, out); break;
    case GRIT: block_grit(v, w, out); break;
    case HOP: block_bounce(v, w, out); break;
    case ZAP: block_zap(v, w, out); break;
    case BOW: block_bow(v, w, out); break;
    default: block_shake(v, w, out); break;
    }
}
}
