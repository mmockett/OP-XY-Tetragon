// CURIO's sound checks, run by sdk.py test: every key's default sounds,
// within the declared peak, and ends (finished frees its voice); every
// specimen stays bounded with all its controls at either end; each default
// hit peaks within its calibrated range; every control audibly changes its
// specimen across its whole travel; TUNE moves the pitch and VARY the hits.
#include "../engine.cpp"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace {
int failures = 0;
curio::Voice voice;
curio::Shared shared;

struct Take {
    double peak;
    int length;          // blocks until finished (or the cap)
    bool bounded;
    double feature[30];  // in ten windows: level, brightness and crossings, dB
};

// One hit through the engine as the drum adapter drives it: drum_select and
// drum_pad before every render.
Take hit(opxy::u32 model, const opxy::i32* values, opxy::i32 tune = 0, opxy::i32 vary = 0, int blocks = 10 * 690, bool fresh = true) {
    opxy::i32 out[64];
    opxy::u32 pitch[64] = {};
    if (fresh) {
        voice = curio::Voice();
        curio::initialize(voice, shared, 1);
    }
    curio::start(voice, shared, 1);
    Take t = Take();
    t.bounded = true;
    t.length = blocks;
    const int windows[11] = {0, 2, 5, 10, 20, 40, 80, 160, 320, 640, 1380};   // blocks
    double energy[10] = {}, diff[10] = {}, cross[10] = {};
    opxy::i32 last = 0;
    const opxy::i32 pads[4] = {tune, vary, 0, 0};
    for (int b = 0; b < blocks; ++b) {
        curio::drum_select(voice, shared, 1, model);
        curio::drum_pad(voice, shared, 1, pads);
        curio::render(voice, shared, 1, out, values, pitch);
        for (int i = 0; i < 64; ++i) {
            const double a = out[i] < 0 ? -static_cast<double>(out[i]) : out[i];
            if (a > t.peak) t.peak = a;
            if (out[i] > 1073741824 || out[i] < -1073741824) t.bounded = false;
            for (int w = 0; w < 10; ++w)
                if (b >= windows[w] && b < windows[w + 1]) {
                    energy[w] += static_cast<double>(out[i]) * out[i];
                    const double d = static_cast<double>(out[i]) - last;
                    diff[w] += d * d;
                    if ((out[i] < 0) != (last < 0)) cross[w] += 1;
                }
            last = out[i];
        }
        if (curio::finished(voice, shared) && t.length == blocks) { t.length = b; break; }
    }
    t.peak /= 2147483648.0;
    for (int w = 0; w < 10; ++w) {
        // Quieter than -100 dB counts as -100 dB, so silence doesn't dominate.
        const double floor = 4.6e18 * (windows[w + 1] - windows[w]) * 64 * 1e-10;
        const double e = energy[w] + floor;
        t.feature[w] = 10 * std::log10(e);
        t.feature[10 + w] = 10 * std::log10((diff[w] + floor) / e);   // brightness
        t.feature[20 + w] = energy[w] > floor ? 10 * std::log10(cross[w] + 1) : 0;   // pitch and noisiness
    }
    return t;
}
void values_of(const opxy::u8* raw, opxy::i32* values) {
    for (int c = 0; c < 8; ++c) values[c] = raw[c] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(raw[c]) << 24;
}
double distance(const Take& a, const Take& b) {
    double d = 0;
    for (int i = 0; i < 30; ++i) d += a.feature[i] > b.feature[i] ? a.feature[i] - b.feature[i] : b.feature[i] - a.feature[i];
    return d;
}
// Zero crossings over the first half second: a pitch stand-in.
int crossings(opxy::u32 model, const opxy::i32* values, opxy::i32 tune) {
    opxy::i32 out[64];
    opxy::u32 pitch[64] = {};
    voice = curio::Voice();
    curio::initialize(voice, shared, 1);
    curio::start(voice, shared, 1);
    const opxy::i32 pads[4] = {tune, 0, 0, 0};
    int n = 0;
    opxy::i32 last = 0;
    for (int b = 0; b < 345; ++b) {
        curio::drum_select(voice, shared, 1, model);
        curio::drum_pad(voice, shared, 1, pads);
        curio::render(voice, shared, 1, out, values, pitch);
        for (int i = 0; i < 64; ++i) {
            if (b > 4 && ((last < 0) != (out[i] < 0))) ++n;
            last = out[i];
        }
    }
    return n;
}
}

int main() {
    using namespace curio;
    // Every key's default: sounds, stays under half scale, and ends.
    for (opxy::u32 key = 53; key < 77; ++key) {
        opxy::u8 model, raw[8];
        drum_defaults(key, &model, raw);
        opxy::i32 values[8];
        values_of(raw, values);
        const Take t = hit(model, values);
        if (!t.bounded) { std::printf("FAIL key %u above half scale\n", key); ++failures; }
        if (t.peak < 0.05) { std::printf("FAIL key %u is nearly silent (peak %.3f)\n", key, t.peak); ++failures; }
        if (t.length >= 10 * 690) { std::printf("FAIL key %u never finishes\n", key); ++failures; }
    }
    // Each specimen's design defaults sound at its calibrated loudness
    // (tools/calibrate.py, levels.h), so they peak at most at the output
    // limiter's 0.42 of full scale and, the longest and softest (the match's
    // brush, the roar), no lower than about 0.2.
    for (opxy::u32 m = 0; m < kit::models; ++m) {
        opxy::i32 values[8];
        values_of(kit::model[m].defaults, values);
        double peak = 0;
        for (int h = 0; h < 4; ++h) { const Take t = hit(m, values); if (t.peak > peak) peak = t.peak; }
        if (peak < 0.18 || peak > 0.421) { std::printf("FAIL %s's default peaks at %.3f, not within 0.18..0.42\n", kit::model[m].name, peak); ++failures; }
        // Bounded at every corner of its controls.
        for (int corner = 0; corner < 3; ++corner) {
            opxy::i32 v[8];
            for (int c = 0; c < 8; ++c) v[c] = corner == 0 ? 0 : corner == 1 ? 0x7fffffff : (c & 1 ? 0x7fffffff : 0);
            const Take t = hit(m, v, 15, 15);
            if (!t.bounded) { std::printf("FAIL %s above half scale at corner %d\n", kit::model[m].name, corner); ++failures; }
        }
        // Names and labels fit.
        for (int s = 0; s < 8; ++s) {
            const char* l = parameter_name(m, static_cast<opxy::u32>(s));
            if (!l[0] || std::strlen(l) > 8) { std::printf("FAIL %s slot %d label\n", kit::model[m].name, s); ++failures; }
        }
    }
    // Every control is heard across its travel: 0, 32, 64, 96 and 127 with
    // the others at the design defaults each differ from the next, and the
    // ends differ clearly (a sum of log2 changes over eight windows).
    int weakest_m = 0, weakest_c = 0;
    double weakest = 1e9;
    for (opxy::u32 m = 0; m < kit::models; ++m)
        for (int c = 0; c < 8; ++c) {
            Take steps[5];
            const int at[5] = {0, 32, 64, 96, 127};
            for (int s = 0; s < 5; ++s) {
                opxy::u8 raw[8];
                std::memcpy(raw, kit::model[m].defaults, 8);
                raw[c] = static_cast<opxy::u8>(at[s]);
                opxy::i32 values[8];
                values_of(raw, values);
                shared.hits = 7;   // the same noise each time
                steps[s] = hit(m, values, 0, 0, 2 * 690);
            }
            const double ends = distance(steps[0], steps[4]);
            if (ends < weakest) { weakest = ends; weakest_m = static_cast<int>(m); weakest_c = c; }
            if (ends < 6.0) { std::printf("FAIL %s's %s barely changes the sound (%.2f)\n", kit::model[m].name, kit::model[m].labels[c], ends); ++failures; }
            for (int s = 0; s < 4; ++s)
                if (distance(steps[s], steps[s + 1]) < 1.0) {
                    std::printf("FAIL %s's %s is flat from %d to %d\n", kit::model[m].name, kit::model[m].labels[c], at[s], at[s + 1]); ++failures;
                }
        }
    std::printf("sound: the subtlest control end to end is %s's %s (%.2f)\n", kit::model[weakest_m].name, kit::model[weakest_m].labels[weakest_c], weakest);
    // TUNE: +12 semitones doubles the glass's ring, -12 halves it.
    {
        opxy::i32 values[8];
        values_of(kit::model[12].defaults, values);
        const int base = crossings(12, values, 0), up = crossings(12, values, 12), down = crossings(12, values, -12);
        const double r1 = static_cast<double>(up) / base, r2 = static_cast<double>(down) / base;
        if (r1 < 1.85 || r1 > 2.15 || r2 < 0.45 || r2 > 0.55) { std::printf("FAIL TUNE: %.2f and %.2f, not 2 and 0.5\n", r1, r2); ++failures; }
    }
    // VARY: at 15 successive hits of the chalk differ in level; at 0 they
    // don't. (The chalk's defaults peak under the output's limiter, which
    // holds the stone's strike, so its level shows in its peak.)
    {
        opxy::i32 values[8];
        values_of(kit::model[14].defaults, values);
        double lo0 = 9, hi0 = 0, lo15 = 9, hi15 = 0;
        for (int h = 0; h < 8; ++h) {
            const double a = hit(14, values, 0, 0, 200).peak, b = hit(14, values, 0, 15, 200).peak;
            lo0 = a < lo0 ? a : lo0; hi0 = a > hi0 ? a : hi0;
            lo15 = b < lo15 ? b : lo15; hi15 = b > hi15 ? b : hi15;
        }
        if (hi15 - lo15 < (hi0 - lo0) + 0.03) { std::printf("FAIL VARY spreads levels by %.3f against %.3f\n", hi15 - lo15, hi0 - lo0); ++failures; }
    }
    // The track menu's scramble: 40 rolls of every specimen, each control
    // drawn from its scramble_range as the SDK draws it, all sound, stay
    // under half scale and end within ten seconds.
    {
        opxy::u32 seed = 12345;
        for (opxy::u32 m = 0; m < kit::models; ++m)
            for (int roll = 0; roll < 40; ++roll) {
                opxy::u8 raw[8];
                for (opxy::u32 s = 0; s < 8; ++s) {
                    opxy::u8 low = 0, high = 127;
                    scramble_range(m, s, &low, &high);
                    if (low > high) { std::printf("FAIL %s's %s scramble range runs backwards\n", kit::model[m].name, kit::model[m].labels[s]); ++failures; }
                    seed = seed * 1664525u + 1013904223u;
                    raw[s] = static_cast<opxy::u8>(low + (seed >> 8) % (high - low + 1u));
                }
                opxy::i32 values[8];
                values_of(raw, values);
                const Take t = hit(m, values);
                if (!t.bounded) { std::printf("FAIL %s scramble %d above half scale\n", kit::model[m].name, roll); ++failures; }
                if (t.peak < 0.02) { std::printf("FAIL %s scramble %d is nearly silent (peak %.3f)\n", kit::model[m].name, roll, t.peak); ++failures; }
                if (t.length >= 10 * 690) { std::printf("FAIL %s scramble %d never finishes\n", kit::model[m].name, roll); ++failures; }
            }
    }
    // The pad controls' captions.
    char text[16];
    pad_control_text(0, -16, text);
    if (std::strcmp(text, "-16st")) { std::printf("FAIL TUNE caption %s\n", text); ++failures; }
    pad_control_text(0, 7, text);
    if (std::strcmp(text, "+7st")) { std::printf("FAIL TUNE caption %s\n", text); ++failures; }
    pad_control_text(1, 15, text);
    if (std::strcmp(text, "15")) { std::printf("FAIL VARY caption %s\n", text); ++failures; }
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
