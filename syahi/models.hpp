#pragma once
// The eight models: eight windows on one core. Each maps its eight controls to
// what the core plays (Spec). Slots 0, 1, 4 and 5 are TUNE, DECAY, FORM and
// SPOT on every model; 2, 3, 6 and 7 are the model's own.
#include "form.hpp"

namespace syahi {
enum Model : u32 { boom, snare, tom, tabla, bar, metal, bounce, wire, model_count };

constexpr i32 q28(double x) { return c_round(x * 268435456.0); }

// What a strike asks of the core. Pitches and times are log2 (Q16) of Hz and
// seconds; gains Q28 (1.0 = 2^28); fractions Q31.
struct Spec {
    i32 pitch;          // the lowest named partial, log2 Hz
    i32 decay;          // log2 s, 60 dB
    i32 contact;        // log2 s: 0.07 ms brass tip .. 5.5 ms felt
    i32 spot;           // 0 centre .. 1 edge
    i32 load;           // Raman's paste
    i32 alpha;          // Q10, or -1 for FORM's own
    i32 tilt;           // Q10: the share of that alpha the model keeps (a cymbal's upper modes ring on)
    i32 bend;           // octaves the struck energy lifts the pitch
    i32 bend_time;      // log2 s, the energy's time constant
    i32 spring;         // Q16: dispersion, highs bend further
    i32 glide;          // octaves the pitch moves after the strike, signed
    i32 glide_time;     // log2 s
    i32 beat;           // Hz (Q16) between a mode and its twin
    i32 swell;          // energy climbing into the upper modes
    i32 tube;           // a resonator under the lowest mode
    i32 bright;         // tilts the strike toward the upper modes (bronze's shimmer)
    i32 node;           // Q31: how far the strike nears the lowest mode's node (a bar's end), taking it away
    i32 head;           // the head's gain, Q28
    u32 count;          // strikes, 1..24
    i32 gap;            // log2 s between the first two
    i32 shrink;         // log2 of each gap's ratio to the last
    i32 rebound;        // log2 of each strike's level to the last's (<= 0)
    i32 jitter;         // Q31: random share of each gap
    i32 last_loud;      // Q31: 0 each strike softer .. 1 the last loudest (a clap)
    i32 noise;          // Q28: brush, palm or clap noise
    i32 noise_length;   // log2 s, the burst's time constant
    i32 noise_attack;   // log2 s
    i32 noise_tone;     // log2 Hz of the noise's band
    QClass noise_q;
    i32 noise_ring;     // Q31: share through the two lowest modes
    i32 noise_air;      // Q31: share of the noise above its band (a crack, a clap's spread)
    i32 tail;           // log2: the last burst's length over the others'
    i32 wires;          // Q28: the rattle's level
    i32 wire_time;      // log2 s
    i32 wire_tone;      // log2 Hz
    i32 wire_top;       // log2 Hz, the rattle's low-pass
    i32 wire_gap;       // Q31: the gap the head must swing past, of its peak
    i32 dense;          // Q28: metal's wash
    i32 body;           // Q28: one resonance below the head
    i32 body_pitch;     // log2 Hz
    i32 body_decay;     // log2 s
    i32 air;            // Q28: the head's push on the room
    i32 air_cycles;     // Q16: how many cycles of 48 Hz it lasts
};

// lo * (hi / lo)^c in log2.
static inline i32 ex(i32 c, i32 log_low, i32 log_high) { return lin(c, log_low, log_high); }

static inline void clear(Spec& s) {
    s.alpha = -1;
    s.tilt = 1024;
    s.bend = s.spring = s.glide = s.beat = s.swell = s.tube = s.bright = s.node = 0;
    s.bend_time = lg(0.03);
    s.glide_time = lg(0.12);
    s.head = q28(1.0);
    s.count = 1;
    s.gap = lg(0.02);
    s.shrink = 0;
    s.rebound = lg(0.92);
    s.jitter = s.last_loud = 0;
    s.noise = 0;
    s.noise_length = lg(0.006);
    s.noise_attack = lg(0.0004);
    s.noise_tone = lg(3000);
    s.noise_q = q_tone;
    s.noise_ring = q31(0.5);
    s.noise_air = 0;
    s.tail = 0;
    s.wires = 0;
    s.wire_time = lg(0.12);
    s.wire_tone = lg(4500);
    s.wire_top = lg(9000);
    s.wire_gap = q31(0.12);
    s.dense = s.body = s.air = 0;
    s.air_cycles = q16(1.5);
    s.body_pitch = lg(50);
    s.body_decay = lg(0.3);
    s.load = 0;
}

// A hard contact (Q31, 0 felt .. 1 brass) as its contact time.
static inline i32 contact(i32 hard) { return lin(one - hard, lg(0.00007), lg(0.0055)); }

// The eight controls (Q31 fractions) as the core's strike.
static inline void spec(u32 model, const i32* c, Spec& s) {
    clear(s);
    // SPOT: the first steps off the centre change the most, as a drum's do.
    s.spot = (c[5] >> 1) + (root(c[5]) >> 1);
    switch (model) {
    case boom:
        s.pitch = ex(c[0], lg(30), lg(110));
        s.decay = ex(c[1], lg(0.12), lg(2.6));
        s.bend = lin(c[2], 0, q16(2.6));
        s.bend_time = log2_1p(lin(c[2], q16(0.018 / 0.001 - 1), q16(0.048 / 0.001 - 1))) + lg(0.001);
        s.contact = contact(lin(c[3], q31(0.1), one));
        // The beater's own sound on the head: a felt thump, a wooden knock, a
        // plastic click, rising in level and pitch and shortening as it hardens.
        s.noise = multiply(c[3], q28(2.5));
        s.noise_length = lin(c[3], lg(0.006), lg(0.0018));
        s.noise_tone = lin(c[3], lg(700), lg(5000));
        s.noise_ring = 0;
        // SHELL: a deeper shell booms louder, longer and lower under the head.
        s.body = multiply(root(c[6]), q28(1.3));
        s.body_decay = log2_1p(lin(c[6], 0, q16(0.7 / 0.08))) + lg(0.08);
        s.body_pitch = s.pitch + lin(c[6], lg(0.86), lg(0.66));
        s.air = multiply(c[7], q28(1.6));
        s.air_cycles = lin(c[7], q16(1.5), q16(4.0));
        break;
    case snare: {
        // STICK's lowest third is a brush: a swept noise that dies as the stick takes over.
        const i32 brush = positive(one - shl(multiply(c[3], q31(0.8)), 2));   // 1 - 3.2 c
        s.pitch = ex(c[0], lg(120), lg(340));
        s.decay = ex(c[1], lg(0.07), lg(0.8));
        // The stick hardens smoothly out of the brush.
        s.contact = contact(lin(positive(multiply(c[3] - q31(0.3), q31(1 / 0.7 / 2)) * 2), q31(0.05), one));
        s.wires = multiply(c[2], q28(1.1));
        s.wire_time = ex(one - c[6], lg(0.05), lg(0.32)) + lg(0.6) + log2_1p(lin(c[1], 0, q16(0.8 / 0.6)));
        s.wire_tone = ex(c[6], lg(1500), lg(5200));
        s.wire_gap = q31(0.08);
        s.wire_top = ex(c[6], lg(5000), lg(11000));
        s.head = lin(c[7], q28(0.15), q28(1.6));
        // The brush; out of it the stick's crack on the head, and past four
        // fifths a rimshot's: a short, bright burst whose air spreads it to the top.
        const i32 stick = positive(c[3] - q31(0.3));
        s.noise = multiply(brush, q28(0.35)) + multiply(stick, q28(4.0)) + multiply(positive(c[3] - q31(0.8)), q28(0.8));
        s.noise_length = log2_1p(lin(brush, q16(0.003 / 0.001 - 1), q16(0.049 / 0.001 - 1))) + lg(0.001);
        s.noise_attack = log2_1p(lin(brush, q16(0.0002 / 0.0001 - 1), q16(0.0204 / 0.0001 - 1))) + lg(0.0001);
        s.noise_tone = lg(3500);
        s.noise_air = multiply(one - brush, q31(0.5));
        s.noise_ring = q31(0.3);
        s.bend = q16(0.25);
        s.bend_time = lg(0.012);
        break;
    }
    case tom: {
        s.pitch = ex(c[0], lg(55), lg(330));
        s.decay = ex(c[1], lg(0.15), lg(2.6));
        s.bend = lin(c[2], 0, q16(1.6));
        s.bend_time = log2_1p(lin(c[2], q16(0.03 / 0.001 - 1), q16(0.08 / 0.001 - 1))) + lg(0.001);
        // SQUEEZE: -1 octave (falls) .. +1 (rises), the talking drum's armpit.
        s.glide = lin(c[3], -q16(1), q16(1));
        s.glide_time = lg(0.16);
        s.contact = contact(lin(c[6], q31(0.05), one));
        // Past half way a stick's tip ticks on the head.
        s.noise = multiply(positive(c[6] - q31(0.3)), q28(7.9));
        s.noise_length = lg(0.003);
        s.noise_tone = lin(c[6], lg(1500), lg(4500));
        s.noise_ring = q31(0.4);
        s.beat = lin(c[7], 0, q16(7));
        s.body = multiply(c[7], q28(0.25));
        s.body_decay = lg(0.4);
        s.body_pitch = s.pitch + lg(0.82);
        break;
    }
    case tabla:
        s.pitch = ex(c[0], lg(90), lg(560));
        s.decay = ex(c[1], lg(0.1), lg(2.2));
        s.load = c[2];
        s.glide = c[3] >> 15;   // up to an octave, Q16
        s.glide_time = lg(0.14);
        s.contact = contact(lin(c[6], q31(0.85), q31(0.7)));
        s.noise = multiply(root(c[6]), q28(1.4));
        s.noise_length = lg(0.0035);
        s.noise_tone = ex(c[6], lg(1800), lg(3600));
        s.noise_ring = q31(0.4);
        // GOURD: a rounder, bigger pot, its Helmholtz bass louder and lower.
        s.body = multiply(root(c[7]), q28(1.5));
        s.body_pitch = clamp(s.pitch + lg(0.38), lg(55), lg(140)) - multiply(c[7], q16(0.6));
        s.body_decay = log2_1p(lin(c[7], q16(0.18 / 0.1 - 1), q16(0.38 / 0.1 - 1))) + lg(0.1);
        s.bend = q16(0.12);
        s.bend_time = lg(0.02);
        break;
    case bar:
        // A bar's SPOT runs evenly from the middle to its end, and its last
        // third nears the lowest mode's node (a fifth of the way in from the
        // end), which takes the note away and leaves the bar's thin overtones.
        s.spot = c[5];
        s.node = clamp(shl(c[5] - q31(0.6), 1), 0, one);   // 0 .. 0.8 over the last two fifths
        s.pitch = ex(c[0], lg(160), lg(1700));
        s.decay = ex(c[1], lg(0.1), lg(3.2));
        s.beat = multiply(multiply(c[2], root(c[2])), q16(8));   // 8 c^1.5 Hz
        s.contact = contact(lin(c[3], q31(0.1), one));
        // A hard mallet ticks on the bar: rubber's knock to brass's ping.
        s.noise = multiply(positive(c[3] - q31(0.25)), q28(3.0));
        s.noise_length = lg(0.0015);
        s.noise_tone = lin(c[3], lg(2500), lg(7000));
        s.noise_ring = 0;
        s.tube = c[6];
        // ALLOY: rosewood's tilt 2.0 to bronze's 0.1 (the prototype's), and
        // bronze strikes brighter as well as ringing longer.
        s.alpha = lin(c[7], 2048, 102);
        s.bright = multiply(multiply(c[7], c[7]), q31(0.9));
        break;
    case metal:
        s.pitch = ex(c[0], lg(160), lg(2600));
        s.decay = ex(c[1], lg(0.035), lg(4.5));
        // A cymbal's upper modes ring nearly as long as its lowest: the shimmer.
        s.tilt = 340;
        s.dense = multiply(c[2], q28(1.1));
        // The coupling takes its energy from the modes: a clean bell at 0, spray at the top.
        s.head = q28(1.0) - multiply(multiply(s.dense, s.dense) << 3, q31(0.45));
        s.contact = contact(lin(c[3], q31(0.1), one));
        s.swell = c[6];
        s.wires = multiply(c[7], q28(0.4));
        s.wire_time = log2_1p(lin(c[1], q16(0.105 / 0.035 - 1), q16(0.455 / 0.035 - 1))) + lg(0.035);
        s.wire_tone = lg(6500);
        s.wire_gap = q31(0.02);
        s.wire_top = lg(12000);
        s.noise = multiply(positive(c[3] - q31(0.7)), q28(0.8));
        s.noise_length = lg(0.002);
        s.noise_tone = lg(8000);
        s.noise_ring = 0;
        break;
    case bounce: {
        s.pitch = ex(c[0], lg(90), lg(1400));
        s.decay = ex(c[1], lg(0.03), lg(1.0));
        s.count = 1 + ((static_cast<u32>(c[2]) >> 16) * 23u + 16384u) / 32768u;
        s.gap = ex(c[3], lg(0.004), lg(0.14));
        s.shrink = lin(c[7], -q16(0.8), q16(0.35));
        // A shrinking stick loses height as it speeds up; an even or slowing one rebounds at 0.92.
        s.rebound = s.shrink < 0 ? multiply(s.shrink, q31(0.6)) : lg(0.92);
        s.contact = contact(lin(c[6], q31(0.9), q31(0.3)));
        s.head = lin(c[6], q28(0.45), q28(0.08));
        s.noise = multiply(c[6], q28(0.55));
        // Flat palms: each hand a sharper burst, the spread of the room above its band.
        s.noise_length = lin(c[6], lg(0.0045), lg(0.0028));
        s.noise_air = multiply(multiply(c[6], c[6]), q31(0.8));
        s.noise_tone = ex(c[0], lg(1000), lg(6000));
        s.noise_q = q_tone;   // a wide band, once more spread by the air
        s.noise_ring = q31(0.25);
        // The last hand rings on as the room's tail: 1 + 30 c1 c6 times as long as
        // a hand, its length kept as the hands sharpen.
        s.tail = log2_1p(multiply(multiply(c[1], c[6]), q31(30.0 / 32)) >> 10) + multiply(c[6], lg(0.0045 / 0.0028));
        s.jitter = multiply(c[6], q31(0.25));
        // Flat palms clap: the last hand is the loudest, eased in over the middle of HANDS.
        s.last_loud = clamp(shl(c[6] - q31(0.3), 1) + (c[6] - q31(0.3)) / 2, 0, one);
        break;
    }
    default:   // wire
        s.pitch = ex(c[0], lg(30), lg(1600));
        s.decay = ex(c[1], lg(0.05), lg(3.2));
        // A harder hammer stretches the wire further: up to 40% more drop.
        s.bend = multiply(lin(c[2], 0, q16(5)), lin(c[7], q31(0.6), one));
        s.bend_time = ex(c[3], lg(0.004), lg(0.5));
        s.spring = multiply(c[6], q31(0.45)) >> 14;   // 0.9 c, Q16, even all the way (the prototype went to 1.6, past where the highs have all left)
        s.contact = contact(lin(c[7], q31(0.1), one));
        // A hard hammer rings the wire's clamp: a click past half way.
        s.noise = multiply(positive(c[7] - q31(0.3)), q28(7.9));
        s.noise_length = lin(c[7], lg(0.003), lg(0.0008));
        s.noise_tone = lin(c[7], lg(2000), lg(9000));
        s.noise_ring = 0;
        break;
    }
}
}
