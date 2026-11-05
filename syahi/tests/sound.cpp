// Syahi's sound checks: every default key sounds, within the declared peak and
// at a level that sits in a kit; every model stays bounded with each control at
// both ends and with GRIT at both ends; every control moves the sound all the
// way along its travel; FORM's sweep stays a drum (pitched, level-steady); a
// voice frees itself once silent; HUMAN varies hit to hit and only with HUMAN;
// the pad controls' captions fit. Host-only: floating point measures here.
#include "../engine.cpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

syahi::Shared shared;

// Renders a note: model, eight raw controls, HUMAN, GRIT, seconds. Returns samples (full scale 1).
std::vector<double> play(unsigned model, const unsigned* raw, int human, int grit, double seconds,
                         bool* finished = nullptr) {
    static syahi::Voice voice;
    voice = syahi::Voice();
    syahi::initialize(voice, shared, 1);
    syahi::start(voice, shared, 1);
    opxy::i32 params[8], out[64], pads[2] = {human, grit};
    opxy::u32 pitch[64] = {};
    for (int c = 0; c < 8; ++c) params[c] = raw[c] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(raw[c] << 24);
    const unsigned blocks = static_cast<unsigned>(seconds * 44100 / 64);
    std::vector<double> y;
    y.reserve(blocks * 64);
    if (finished) *finished = false;
    for (unsigned b = 0; b < blocks; ++b) {
        syahi::drum_select(voice, shared, 1, model);
        syahi::drum_pad(voice, shared, 1, pads);
        syahi::render(voice, shared, 1, out, params, pitch);
        for (int i = 0; i < 64; ++i) y.push_back(out[i] / 2147483648.0);
        if (finished && syahi::finished(voice, shared)) { *finished = true; break; }
    }
    return y;
}
double peak(const std::vector<double>& y) {
    double p = 0;
    for (double v : y) p = std::fmax(p, std::fabs(v));
    return p;
}
// Level in dB in third-octave bands from 28 Hz to 16 kHz (biquad band-passes)
// over three windows: a fingerprint of the sound's spectrum and envelope, as
// tools/sweep.py takes it.
std::vector<double> feature(const std::vector<double>& y) {
    static const double windows[4] = {0, 0.05, 0.3, 1.2};
    std::vector<double> f;
    for (int band = 0; band < 28; ++band) {
        const double fc = 28.0 * std::pow(2.0, band / 3.0);
        const double w = 2 * M_PI * fc / 44100, q = 4.3, alpha = std::sin(w) / (2 * q);
        const double a0 = 1 + alpha, b0 = alpha / a0, b2 = -alpha / a0, a1 = -2 * std::cos(w) / a0, a2 = (1 - alpha) / a0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, energy[3] = {0, 0, 0};
        for (size_t n = 0; n < y.size() && n < 44100 * 1.2; ++n) {
            const double x = y[n], o = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = o;
            const double t = n / 44100.0;
            for (int k = 0; k < 3; ++k) if (t >= windows[k] && t < windows[k + 1]) energy[k] += o * o;
        }
        for (int k = 0; k < 3; ++k) f.push_back(10 * std::log10(energy[k] + 1e-14));
    }
    return f;
}
// How far two fingerprints are apart: the mean of the largest tenth of the
// band changes, dB, counting only bands within 50 dB of the loudest band of
// their window in either sound (a change far under the sound is not heard).
double distance(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t bands = a.size() / 3;
    double top_a[3] = {-300, -300, -300}, top_b[3] = {-300, -300, -300};
    for (size_t i = 0; i < a.size(); ++i) {
        top_a[i % 3] = std::fmax(top_a[i % 3], a[i]);
        top_b[i % 3] = std::fmax(top_b[i % 3], b[i]);
    }
    std::vector<double> d;
    for (size_t i = 0; i < a.size(); ++i) {
        const size_t w = i % 3;
        const double floor = std::fmax(top_a[w], top_b[w]) - 50;
        if (a[i] > floor || b[i] > floor) d.push_back(std::fabs(std::fmax(a[i], floor) - std::fmax(b[i], floor)));
    }
    (void)bands;
    if (d.empty()) return 0;
    std::sort(d.begin(), d.end());
    double sum = 0;
    const size_t n = d.size() / 10 + 1;
    for (size_t i = 0; i < n; ++i) sum += d[d.size() - 1 - i];
    return sum / n;
}
// Harmonic-to-noise ratio from the normalised autocorrelation over 15..200 ms (as the prototype measured).
double hnr(const std::vector<double>& y) {
    const size_t a = 44100 * 15 / 1000, b = 44100 * 200 / 1000;
    if (y.size() < b) return 0;
    std::vector<double> x(y.begin() + a, y.begin() + b);
    double mean = 0;
    for (double v : x) mean += v;
    mean /= x.size();
    for (double& v : x) v -= mean;
    double r0 = 0;
    for (double v : x) r0 += v * v;
    double best = 0;
    for (size_t lag = 44100 / 2000; lag < 44100 / 25; ++lag) {
        double r = 0;
        for (size_t n = 0; n + lag < x.size(); ++n) r += x[n] * x[n + lag];
        best = std::fmax(best, r / (r0 + 1e-20));
    }
    best = std::fmin(std::fmax(best, 1e-4), 0.9999);
    return 10 * std::log10(best / (1 - best));
}
const char* name(unsigned m) { return syahi::kit::model_names[m]; }
const char* label(unsigned m, int s) { return syahi::kit::labels[m][s]; }
}

int main() {
    using syahi::kit::models;
    // ---- every default key sounds, within the peak, at a usable level
    for (unsigned key = 53; key < 77; ++key) {
        opxy::u8 model, c8[8];
        syahi::drum_defaults(key, &model, c8);
        unsigned raw[8];
        for (int c = 0; c < 8; ++c) raw[c] = c8[c];
        const double p = peak(play(model, raw, 0, 0, 3.0));
        CHECK(p <= 0.5, "key %u peaks %.3f, beyond half scale", key, p);
        CHECK(p >= 0.12, "key %u peaks only %.3f", key, p);
    }
    // ---- each model at its defaults, each control at both ends, GRIT both ways: bounded and sounding
    for (unsigned m = 0; m < models; ++m) {
        unsigned raw[8];
        for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[m][c];
        const double p = peak(play(m, raw, 0, 0, 2.0));
        CHECK(p > 0.2 && p <= 0.5, "%s's defaults peak %.3f", name(m), p);
        for (int slot = 0; slot < 8; ++slot)
            for (unsigned v : {0u, 127u})
                for (int grit : {0, -16, 15}) {
                    unsigned r[8];
                    std::memcpy(r, raw, sizeof(r));
                    r[slot] = v;
                    const double q = peak(play(m, r, 8, grit, 1.0));
                    CHECK(q <= 0.5, "%s %s %u GRIT %d peaks %.3f", name(m), label(m, slot), v, grit, q);
                    CHECK(q > 0.01, "%s %s %u GRIT %d is silent", name(m), label(m, slot), v, grit);
                }
    }
    // ---- every control moves the sound across its whole travel: each quarter of
    // it changes some audible band by at least 0.75 dB (tools/sweep.py looks in eighths)
    for (unsigned m = 0; m < models; ++m)
        for (int slot = 0; slot < 8; ++slot) {
            std::vector<double> last;
            unsigned from = 0;
            for (unsigned v : {0u, 32u, 64u, 96u, 127u}) {
                unsigned r[8];
                for (int c = 0; c < 8; ++c) r[c] = syahi::kit::model_defaults[m][c];
                r[slot] = v;
                const std::vector<double> f = feature(play(m, r, 0, 0, 1.3));
                if (!last.empty()) {
                    const double d = distance(last, f);
                    CHECK(d >= 0.75, "%s %s barely moves from %u to %u (%.2f dB)", name(m), label(m, slot), from, v, d);
                }
                last = f;
                from = v;
            }
        }
    // ---- FORM's sweep on a tom: a drum all the way (pitched enough, level within 8 dB)
    {
        double lo = 1, hi = 0, worst = 99;
        for (unsigned form = 0; form <= 127; form += 12) {
            unsigned r[8] = {56, 70, 40, 64, form, 36, 50, 20};
            const std::vector<double> y = play(2, r, 0, 0, 1.2);
            const double p = peak(y), h = hnr(y);
            lo = std::fmin(lo, p);
            hi = std::fmax(hi, p);
            worst = std::fmin(worst, h);
            CHECK(h >= 4.0, "FORM %u is clangy: HNR %.1f dB", form, h);
        }
        CHECK(20 * std::log10(hi / lo) <= 8.0, "FORM's sweep moves the level %.1f dB", 20 * std::log10(hi / lo));
        std::printf("FORM sweep on a tom: HNR at least %.1f dB, level within %.1f dB\n", worst, 20 * std::log10(hi / lo));
    }
    // ---- a voice frees itself once silent, long decays included
    for (unsigned m = 0; m < models; ++m) {
        unsigned r[8];
        for (int c = 0; c < 8; ++c) r[c] = syahi::kit::model_defaults[m][c];
        bool done = false;
        play(m, r, 0, 0, 4.0, &done);
        CHECK(done, "%s's default hit never frees its voice", name(m));
        r[1] = 127;
        play(m, r, 0, 0, 16.0, &done);
        CHECK(done, "%s at the longest DECAY never frees its voice", name(m));
    }
    // ---- HUMAN: hits differ with it, and (on a model without noise) are identical without it
    {
        unsigned r[8];
        for (int c = 0; c < 8; ++c) r[c] = syahi::kit::model_defaults[4][c];
        r[3] = 20;   // a soft mallet: no tick, so nothing random
        shared = syahi::Shared();
        const std::vector<double> a = play(4, r, 0, 0, 0.3), b = play(4, r, 0, 0, 0.3);
        CHECK(a == b, "without HUMAN two hits differ");
        const std::vector<double> c = play(4, r, 12, 0, 0.3), d = play(4, r, 12, 0, 0.3);
        CHECK(c != d, "with HUMAN two hits are the same");
    }
    // ---- the pad controls' captions
    char text[32];
    for (int v = 0; v <= 15; ++v) CHECK(syahi::pad_control_text(0, v, text) && std::strlen(text) <= 15, "HUMAN %d caption", v);
    for (int v = -16; v <= 15; ++v) CHECK(syahi::pad_control_text(1, v, text) && std::strlen(text) <= 15, "GRIT %d caption", v);
    CHECK(!syahi::pad_control_text(2, 0, text), "a third pad field has a caption");
    // ---- names
    for (unsigned m = 0; m < models; ++m) {
        CHECK(syahi::parameter_count(m) == 8, "%s has %u controls", syahi::model_name(m), syahi::parameter_count(m));
        for (unsigned s = 0; s < 8; ++s) CHECK(std::strlen(syahi::parameter_name(m, s)) <= 8 && syahi::parameter_name(m, s)[0], "label %u.%u", m, s);
    }
    if (failures) std::printf("%d sound checks failed\n", failures);
    else std::printf("sound: all checks passed\n");
    return failures ? 1 : 0;
}
