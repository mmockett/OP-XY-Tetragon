#pragma once
// GALILEO's captions: each control's value in real units for the header, and
// the pad fields' captions (SPACE, FEEL) for the stock header and the kit's own.
// Every caption a key can show is measured in the device font by
// tools/check_captions.py.
#include "dsp.h"
#include "kit.h"
#include "motion.h"

namespace galileo {
namespace caption {
using opxy::i32;
using opxy::u32;

// Writes text into out (capacity bytes).
inline void put(char* out, u32 capacity, const char* text) {
    u32 n = 0;
    for (; text[n] && n + 1 < capacity; ++n) out[n] = text[n];
    out[n] = 0;
}
// A whole number (0..99999) after a prefix and before a unit.
inline void number(char* out, u32 capacity, const char* prefix, u32 value, const char* unit) {
    char digits[8];
    u32 n = 0;
    do { digits[n++] = static_cast<char>('0' + (value - (value / 10u) * 10u)); value /= 10u; } while (value && n < 7);
    char text[24];
    u32 at = 0;
    for (u32 i = 0; prefix[i] && at < 10; ++i) text[at++] = prefix[i];
    while (n) text[at++] = digits[--n];
    for (u32 i = 0; unit[i] && at < 22; ++i) text[at++] = unit[i];
    text[at] = 0;
    put(out, capacity, text);
}
// A value with two decimals (hundredths), "0.45".
inline void hundredths(char* out, u32 capacity, u32 h, const char* unit) {
    char text[12];
    const u32 whole = h / 100u, frac = h - whole * 100u;
    u32 at = 0;
    if (whole >= 10) text[at++] = static_cast<char>('0' + whole / 10u);
    text[at++] = static_cast<char>('0' + (whole - (whole / 10u) * 10u));
    text[at++] = '.';
    text[at++] = static_cast<char>('0' + frac / 10u);
    text[at++] = static_cast<char>('0' + (frac - (frac / 10u) * 10u));
    for (u32 i = 0; unit[i] && at < 11; ++i) text[at++] = unit[i];
    text[at] = 0;
    put(out, capacity, text);
}
// A time in milliseconds: "85ms" under a tenth of a second, then "0.41s", from ten seconds "12.9s".
inline void duration(char* out, u32 capacity, u32 ms) {
    if (ms < 100) number(out, capacity, "", ms, "ms");
    else if (ms < 10000) hundredths(out, capacity, ms / 10u, "s");
    else {   // "12.9s"
        char text[8];
        const u32 tenths = ms / 100u, whole = tenths / 10u;
        text[0] = static_cast<char>('0' + whole / 10u);
        text[1] = static_cast<char>('0' + (whole - (whole / 10u) * 10u));
        text[2] = '.';
        text[3] = static_cast<char>('0' + (tenths - whole * 10u));
        text[4] = 's';
        text[5] = 0;
        put(out, capacity, text);
    }
}
// A frequency: "540", "1.2k", "12k".
inline void hertz(char* out, u32 capacity, u32 hz) {
    if (hz < 1000) { number(out, capacity, "", hz, ""); return; }
    if (hz >= 10000) { number(out, capacity, "", (hz + 500) / 1000u, "k"); return; }
    char text[8];
    const u32 tenths = (hz + 50) / 100u;
    text[0] = static_cast<char>('0' + tenths / 10u);
    text[1] = '.';
    text[2] = static_cast<char>('0' + (tenths - (tenths / 10u) * 10u));
    text[3] = 'k';
    text[4] = 0;
    put(out, capacity, text);
}
const char* const note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
// The note nearest 2^(log2_hz) Hz: "G#3".
inline void note(char* out, u32 capacity, i32 log2_hz) {
    // MIDI note = 69 + 12 log2(f / 440); log2 440 = 8.7814 (575501 in Q16)
    const i32 semis = ((log2_hz - 575501) * 12 + (1 << 15)) >> 16;   // rounded
    i32 midi = 69 + semis;
    midi = midi < 0 ? 0 : midi > 127 ? 127 : midi;
    const u32 octave = static_cast<u32>(midi) / 12u;
    const u32 name = static_cast<u32>(midi) - octave * 12u;
    char text[8];
    u32 at = 0;
    for (const char* s = note_names[name]; *s; ++s) text[at++] = *s;
    if (octave == 0) { text[at++] = '-'; text[at++] = '1'; }
    else text[at++] = static_cast<char>('0' + octave - 1);
    text[at] = 0;
    put(out, capacity, text);
}
// 0..100, as the stock header shows amounts.
inline void amount(char* out, u32 capacity, u32 raw) { number(out, capacity, "", (raw * 100u + 63u) / 127u, ""); }

// The division names for locked tilt.
const char* const divisions[8] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"};

// log2 Hz of a model's tune (or tone) control, as bodies.h sets it.
inline i32 tune_hz(u32 model, u32 raw) {
    const i32 p = raw >= 127 ? dsp::one : static_cast<i32>(raw << 24);
    switch (model) {
    case kit::kick: return dsp::log2(38) + dsp::multiply(81920, p);       // 38 Hz, +1.25 oct
    case kit::snare: return dsp::log2(150) + dsp::multiply(72090, p);
    case kit::tom: case kit::fill: return dsp::log2(62) + dsp::multiply(144179, p);
    case kit::hand: return dsp::log2(170) + dsp::multiply(85197, p);
    case kit::rim: return dsp::log2(700) + dsp::multiply(104858, p);
    case kit::hat: return dsp::log2(310) + dsp::multiply(65536, p);
    case kit::ride: return dsp::log2(360) + dsp::multiply(58982, p);
    case kit::bell: return dsp::log2(540) + dsp::multiply(78643, p);
    case kit::tine: return dsp::log2(330) + dsp::multiply(104858, p);
    case kit::clock: return dsp::log2(1700) + dsp::multiply(78643, p);
    case kit::block: return dsp::log2(600) + dsp::multiply(104858, p);
    case kit::clap: return dsp::log2(850) + dsp::multiply(78643, p);
    case kit::shaker: return dsp::log2(3000) + dsp::multiply(104858, p);
    case kit::ratchet: return dsp::log2(900) + dsp::multiply(91750, p);
    default: return dsp::log2(440);
    }
}
// A model's decay control as milliseconds (the main time constant, x 3 for
// what one hears as its length: about -26 dB).
inline u32 decay_ms(u32 model, u32 raw) {
    const u32 p = raw > 127 ? 127 : raw;                    // 0..127
    const u32 p2 = (p * p + 63) / 127u;                     // squared, 0..127
    u32 tau;                                                // ms
    switch (model) {
    case kit::kick:                                                                                  // 35 ms * 43^(p^1.5)
        tau = dsp::exp2(dsp::log2(35) + dsp::multiply(355616, dsp::power_exact(static_cast<i32>(p << 8), 98304) << 16)); break;
    case kit::snare: tau = 50 + (250 * p) / 127u; break;
    case kit::tom: case kit::fill: tau = 120 + (900 * static_cast<u32>(dsp::power_exact(static_cast<i32>(p << 8), 85197))) / 32767u; break;
    case kit::hand: tau = 60 + (500 * p) / 127u; break;
    case kit::rim: tau = 12 + (100 * p) / 127u; break;
    case kit::hat: tau = dsp::exp2(dsp::log2(28) + static_cast<i32>((308053u * p) >> 7)); break;      // 28 ms * 26^p
    case kit::ride: tau = dsp::exp2(dsp::log2(550) + static_cast<i32>((196608u * p) >> 7)); break;    // 0.55 s * 8^p
    case kit::bell: tau = 50 + (600 * static_cast<u32>(dsp::power_exact(static_cast<i32>(p << 8), 85197))) / 32767u; break;
    case kit::tine: tau = 250 + (2200 * p) / 127u; break;
    case kit::clock: tau = 6 + (244 * p2) / 127u; break;
    case kit::block: tau = 20 + (90 * p) / 127u; break;
    case kit::clap: tau = 40 + (450 * p) / 127u; break;
    case kit::shaker: tau = 15 + (385 * p2) / 127u; break;
    case kit::ratchet: tau = 10 + (60 * p) / 127u; break;
    default: tau = 100; break;
    }
    return tau * 3;
}

// The value of slot `slot` of a key on `model`, raw 0..127, with its pad fields.
inline void value(char* out, u32 capacity, u32 model, u32 slot, u32 raw, i32 feel) {
    raw = raw > 127 ? 127 : raw;
    switch (slot) {
    case 4: {   // tilt: the division, or the period when FREE
        const motion::Feel f = motion::feel(feel);
        if (f.locked) { put(out, capacity, divisions[raw >> 4]); return; }
        const u32 dt = motion::period(raw, f, 0);
        duration(out, capacity, (dt * 1486u) >> 16);         // samples -> ms (1000 / 44100 = 1486 / 65536)
        return;
    }
    case 5: number(out, capacity, "", (raw * 16 + 64) >> 7, ""); return;                 // frets
    case 6: hundredths(out, capacity, (table::restitution[raw] * 100u + 16384u) >> 15, ""); return;   // bounce
    case 7: number(out, capacity, "", table::ball_count[raw], ""); return;               // balls
    default: break;
    }
    // The body: a tune is a note (a tone a frequency); a decay a time.
    const char* label = kit::model[model].labels[slot];
    if (label[0] == 't' && label[1] == 'u') { note(out, capacity, tune_hz(model, raw)); return; }
    if (label[0] == 't' && label[1] == 'o' && label[2] == 'n' && slot == 0) {
        hertz(out, capacity, dsp::exp2(tune_hz(model, raw)));
        return;
    }
    if (label[0] == 'd' && label[1] == 'e') { duration(out, capacity, decay_ms(model, raw)); return; }
    amount(out, capacity, raw);
}

// SPACE (-16..15): the frets' spacing. 0 is Galileo's: the ball rings them at
// even times ("even"). Below, the frets sit evenly along the board, so the
// taps speed up as the ball does; above, they spread and the run slows into
// the landing.
inline void space(char* out, u32 capacity, i32 v) {
    v = v < -16 ? -16 : v > 15 ? 15 : v;
    if (!v) put(out, capacity, "even");
    else if (v < 0) number(out, capacity, "-", static_cast<u32>(-v), "");
    else number(out, capacity, "+", static_cast<u32>(v), "");
}
// FEEL (0..15): LOCK, swing 8..33 % (sw), looseness 1..12 ms (lax), FREE.
const u8 swing_percent[7] = {8, 13, 17, 21, 25, 29, 33};
const u8 lax_ms[7] = {1, 2, 3, 5, 7, 9, 12};
inline void feel(char* out, u32 capacity, i32 v) {
    v = v < 0 ? 0 : v > 15 ? 15 : v;
    if (v == 0) put(out, capacity, "lock");
    else if (v <= 7) number(out, capacity, "sw", swing_percent[v - 1], "");
    else if (v <= 14) number(out, capacity, "lax", lax_ms[v - 8], "");
    else put(out, capacity, "free");
}
}
}
