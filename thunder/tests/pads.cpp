// THUNDER's pad controls in the voice (the SDK's padControls, drum_pad):
// at 0 a key sounds exactly as without them; TUNE moves a key's pitch by
// semitones; DRIVE overloads the pickup, louder and harder, and never past
// the SDK's ceiling; values arrive clamped; their captions read as the
// header shows them. And a finished voice renders silence until its next note.
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
thunder::Voice voice;
thunder::Shared shared;
const unsigned frames_ = 32768;
opxy::i32 plain[frames_], with[frames_];

unsigned play(opxy::u32 key, const opxy::i32* pads, opxy::i32* out, double& peak) {
    opxy::u8 model, raw[8];
    thunder::drum_defaults(key, &model, raw);
    opxy::i32 values[8];
    for (int c = 0; c < 8; ++c) values[c] = raw[c] == 127 ? 0x7fffffff : static_cast<opxy::i32>(raw[c]) << 24;
    std::memset(&voice, 0, sizeof voice);
    shared = thunder::Shared();
    thunder::initialize(voice, shared, 1);
    thunder::start(voice, shared, 1);
    opxy::u32 pitch[64] = {};
    peak = 0;
    unsigned blocks = 0;
    for (; blocks < 44100 * 20 / 64; ++blocks) {
        thunder::drum_select(voice, shared, 1, model);
        if (pads) thunder::drum_pad(voice, shared, 1, pads);
        opxy::i32 block[64];
        thunder::render(voice, shared, 1, block, values, pitch);
        for (unsigned i = 0; i < 64; ++i) {
            const double a = std::fabs(block[i] / 2147483648.0);
            if (a > peak) peak = a;
            const unsigned at = blocks * 64 + i;
            if (out && at < frames_) out[at] = block[i];
        }
        if (thunder::finished(voice, shared)) { ++blocks; break; }
    }
    return blocks;
}
double rms(const opxy::i32* x, unsigned n) {
    double e = 0;
    for (unsigned i = 0; i < n; ++i) { const double v = x[i] / 2147483648.0; e += v * v; }
    return std::sqrt(e / n);
}
// The frequency where a sound's first 8192 samples are strongest, by a DFT
// over 40..800 Hz in 1 Hz steps.
double strongest(const opxy::i32* x) {
    double best = 0, at = 0;
    for (double f = 40; f < 800; f += 1) {
        double re = 0, im = 0;
        for (unsigned i = 0; i < 8192; i += 2) {
            const double w = 2 * 3.141592653589793 * f * i / 44100;
            re += x[i] * std::cos(w);
            im += x[i] * std::sin(w);
        }
        const double p = re * re + im * im;
        if (p > best) { best = p; at = f; }
    }
    return at;
}
void expect(const char* what, const char* shown, const char* wanted) {
    if (std::strcmp(shown, wanted)) { std::printf("FAIL %s reads \"%s\", not \"%s\"\n", what, shown, wanted); ++failures; }
}
}

int main() {
    const opxy::i32 neutral[2] = {0, 0};
    for (opxy::u32 key = 53; key < 77; ++key) {
        double a, b;
        const unsigned n = play(key, nullptr, plain, a), m = play(key, neutral, with, b);
        if (n != m || a != b || std::memcmp(plain, with, sizeof plain)) { std::printf("FAIL key %u differs with its pad controls at 0\n", key); ++failures; }
    }
    // TUNE: +12 doubles a tom's pitch, -12 halves it (key 56, the mid tom)
    {
        double peak;
        play(56, neutral, plain, peak);
        const double f0 = strongest(plain);
        const opxy::i32 up[2] = {12, 0}, down[2] = {-12, 0};
        play(56, up, with, peak);
        const double f1 = strongest(with);
        play(56, down, with, peak);
        const double f2 = strongest(with);
        if (std::fabs(f1 / f0 - 2) > 0.08 || std::fabs(f0 / f2 - 2) > 0.08) {
            std::printf("FAIL TUNE +-12 moves the tom from %.0f Hz to %.0f and %.0f Hz\n", f0, f1, f2); ++failures;
        }
    }
    // DRIVE: louder (more of the sound near the top) at 15, on every key
    // under the ceiling, and every key still ends
    for (opxy::u32 key = 53; key < 77; ++key) {
        double p0, p1;
        play(key, neutral, plain, p0);
        const opxy::i32 hot[2] = {0, 15};
        const unsigned blocks = play(key, hot, with, p1);
        if (p1 > 0.5) { std::printf("FAIL key %u passes the ceiling at DRIVE 15\n", key); ++failures; }
        if (blocks >= 44100 * 20 / 64) { std::printf("FAIL key %u never ends at DRIVE 15\n", key); ++failures; }
        if (!(rms(with, 8192) > 1.3 * rms(plain, 8192))) { std::printf("FAIL key %u: DRIVE 15 adds little (%.4f, %.4f)\n", key, rms(plain, 8192), rms(with, 8192)); ++failures; }
    }
    // values arrive clamped to their fields
    const opxy::i32 wild[2] = {99, 99}, under[2] = {-99, -9};
    thunder::drum_pad(voice, shared, 1, wild);
    if (voice.tune != 15 || voice.drive != 15) { std::printf("FAIL pad values unclamped\n"); ++failures; }
    thunder::drum_pad(voice, shared, 1, under);
    if (voice.tune != -16 || voice.drive != 0) { std::printf("FAIL pad values unclamped\n"); ++failures; }
    // captions
    char text[16];
    thunder::pad_control_text(0, 0, text); expect("TUNE 0", text, "0");
    thunder::pad_control_text(0, 5, text); expect("TUNE 5", text, "+5");
    thunder::pad_control_text(0, -16, text); expect("TUNE -16", text, "-16");
    thunder::pad_control_text(1, 0, text); expect("DRIVE 0", text, "OFF");
    thunder::pad_control_text(1, 15, text); expect("DRIVE 15", text, "15");
    if (thunder::pad_control_text(2, 0, text)) { std::printf("FAIL a third pad control has a caption\n"); ++failures; }
    // a finished voice renders silence, and its next note sounds again
    {
        double peak;
        play(53, neutral, nullptr, peak);
        opxy::i32 block[64], values[8] = {};
        opxy::u32 pitch[64] = {};
        thunder::render(voice, shared, 1, block, values, pitch);
        for (unsigned i = 0; i < 64; ++i) if (block[i]) { std::printf("FAIL a finished voice sounds\n"); ++failures; break; }
        thunder::start(voice, shared, 1);
        thunder::render(voice, shared, 1, block, values, pitch);
        thunder::render(voice, shared, 1, block, values, pitch);
        if (thunder::finished(voice, shared)) { std::printf("FAIL a new note on a finished voice is silent\n"); ++failures; }
    }
    return failures ? 1 : 0;
}
