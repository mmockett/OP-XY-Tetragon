// GALILEO's sound checks, through the engine as the SDK's drum adapter drives
// it: every key and every model sounds within the SDK's half-scale ceiling,
// the default keys stay under the soft knee (no limiting at defaults), every
// control moves the sound across its travel, fast rolls are no louder than
// slow ones, a voice is freed only once its whole run has rung out, and the
// SDK's tempo (drum_transport) sets the divisions. The finer sweeps are
// tools/sweep.py; the tempo learner's own checks are tests/tempo.cpp.
#include "../engine.cpp"
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL " __VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

const opxy::i32 half_scale = 1073741824;
const opxy::i32 knee = 901943132;   // engine.cpp's ceiling: straight below 0.42 of full scale

struct Result {
    double peak, energy;
    unsigned blocks_sounding;     // blocks with any output
    unsigned freed;               // the block finished() first said true, or 0
    unsigned motion_end;          // the block the last ball came to rest
    double env[64];               // RMS of each 50 ms
    double zcr[64];               // zero crossings in each 50 ms: a stand-in for pitch and brightness
    double fine[32], fine_zcr[32];  // the same over the first 93 ms, every 2.9 ms (two blocks)
};

galileo::Shared shared;
galileo::Voice voice;

// One note of a key: its model and eight controls (0..127), its pads, the
// tempo the SDK passes (0: none).
Result play(unsigned model, const unsigned char* c, int space = 0, int feel = 0, unsigned bpm_x10 = 0,
            unsigned blocks = 2200) {
    Result r;
    std::memset(&r, 0, sizeof(r));
    std::memset(&voice, 0, sizeof(voice));
    galileo::initialize(voice, shared, 1);
    galileo::start(voice, shared, 1);
    shared.latest = 0xdeadu;   // another key played since: no clock-keeping (tests/tempo.cpp checks that)
    opxy::i32 live[8];
    for (int i = 0; i < 8; ++i) live[i] = c[i] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(c[i]) << 24;
    opxy::u32 pitch[64];
    for (int i = 0; i < 64; ++i) pitch[i] = 25479004u;
    double acc = 0, crossings = 0;
    opxy::i32 last = 0;
    for (unsigned b = 0; b < blocks; ++b) {
        galileo::drum_select(voice, shared, 1, model);
        const opxy::i32 pads[4] = {space, feel, 0, 0};
        galileo::drum_pad(voice, shared, 1, pads);
        if (bpm_x10) galileo::drum_transport(voice, shared, 1, opxy::Transport{bpm_x10, 0, true});
        opxy::i32 out[64];
        galileo::render(voice, shared, 1, out, live, pitch);
        bool any = false;
        for (int i = 0; i < 64; ++i) {
            const double v = out[i] / 2147483648.0;
            const double a = v < 0 ? -v : v;
            if (a > r.peak) r.peak = a;
            r.energy += v * v;
            acc += v * v;
            any = any || out[i];
            if ((out[i] < 0) != (last < 0)) crossings += 1;
            last = out[i];
            if (out[i] > half_scale || out[i] < -half_scale) r.peak = 9;   // over the ceiling
        }
        if (b < 64) {
            double e = 0, z = 0;
            for (int i = 0; i < 64; ++i) { const double v = out[i] / 2147483648.0; e += v * v; if (i && ((out[i] < 0) != (out[i - 1] < 0))) z += 1; }
            r.fine[b >> 1] += e;
            r.fine_zcr[b >> 1] += z;
        }
        if ((b + 1) % 34 == 0 && (b + 1) / 34 <= 64) {
            r.env[(b + 1) / 34 - 1] = acc / (34 * 64);
            r.zcr[(b + 1) / 34 - 1] = crossings;
            acc = 0;
            crossings = 0;
        }
        r.blocks_sounding += any;
        if (!r.motion_end && voice.balls == 0 && galileo::body::ghost_due(voice.body) == 0xffffffffu) r.motion_end = b + 1;
        if (!r.freed && galileo::finished(voice, shared)) r.freed = b + 1;
    }
    galileo::destroy(voice, shared, 1);
    return r;
}
void key_values(unsigned key, unsigned& model, unsigned char* c) {
    opxy::u8 m, v[8];
    galileo::drum_defaults(53 + key, &m, v);
    model = m;
    for (int i = 0; i < 8; ++i) c[i] = v[i];
}
// The first default key on a model, or the model's defaults (key 53's controls otherwise).
void model_values(unsigned model, unsigned char* c) {
    for (unsigned k = 0; k < 24; ++k)
        if (galileo::kit::key_model[k] == model) { for (int i = 0; i < 8; ++i) c[i] = galileo::kit::key_values[k][i]; return; }
    for (int i = 0; i < 8; ++i) c[i] = 64;
}
double distance(const Result& a, const Result& b) {
    // The two renders' 50 ms loudness envelopes, compared in dB (floored at -60).
    double d = 0;
    int n = 0;
    double pa = 1e-12, pb = 1e-12;
    for (int i = 0; i < 64; ++i) { if (a.env[i] > pa) pa = a.env[i]; if (b.env[i] > pb) pb = b.env[i]; }
    for (int i = 0; i < 64; ++i) {
        double x = a.env[i] > pa * 1e-6 ? a.env[i] / pa : 1e-6, y = b.env[i] > pb * 1e-6 ? b.env[i] / pb : 1e-6;
        // log10 by repeated division, to stay off <cmath> differences: 10 log10(x / y)
        double ratio = x / y, db = 0;
        while (ratio > 10) { ratio /= 10; db += 10; }
        while (ratio < 0.1) { ratio *= 10; db -= 10; }
        db += (ratio - 1) / (ratio + 1) * 8.686;   // close enough within a decade for a change test
        d += db < 0 ? -db : db;
        ++n;
    }
    // Plus how far their zero-crossing rates part where both sound (pitch, brightness).
    double z = 0;
    int zn = 0;
    for (int i = 0; i < 64; ++i)
        if (a.env[i] > pa * 1e-3 && b.env[i] > pb * 1e-3) {
            const double hi = a.zcr[i] > b.zcr[i] ? a.zcr[i] : b.zcr[i], lo = a.zcr[i] > b.zcr[i] ? b.zcr[i] : a.zcr[i];
            z += hi > 0 ? (hi - lo) / hi : 0;
            ++zn;
        }
    if (zn) d += z / zn * 10 * n;
    // And the strike itself: the first 93 ms, finely.
    double fa = 1e-12, fb = 1e-12;
    for (int i = 0; i < 32; ++i) { if (a.fine[i] > fa) fa = a.fine[i]; if (b.fine[i] > fb) fb = b.fine[i]; }
    double fd = 0;
    for (int i = 0; i < 32; ++i) {
        const double x = a.fine[i] / fa, y = b.fine[i] / fb;
        fd += (x > y ? x - y : y - x) * 10;
        if (a.fine[i] > fa * 1e-3 && b.fine[i] > fb * 1e-3) {
            const double hi = a.fine_zcr[i] > b.fine_zcr[i] ? a.fine_zcr[i] : b.fine_zcr[i];
            const double lo = a.fine_zcr[i] > b.fine_zcr[i] ? b.fine_zcr[i] : a.fine_zcr[i];
            fd += hi > 0 ? (hi - lo) / hi * 10 : 0;
        }
    }
    d += fd / 32 * n;
    // Plus their level difference, and their energy ratio.
    const double ea = a.energy + 1e-12, eb = b.energy + 1e-12;
    const double er = ea > eb ? ea / eb : eb / ea;
    return d / n + (er - 1) * 3;
}
}

int main() {
    // The drum kind's callbacks: names, counts, labels that fit eight characters.
    for (unsigned m = 0; m < galileo::kit::models; ++m) {
        CHECK(galileo::parameter_count(m) == 8, "model %u has %u controls", m, galileo::parameter_count(m));
        for (unsigned s = 0; s < 8; ++s) {
            const char* l = galileo::parameter_name(m, s);
            CHECK(l[0] && std::strlen(l) <= 8, "model %u slot %u label '%s'", m, s, l);
        }
        // The motion row is the same four on every model.
        static const char* const motion[4] = {"tilt", "frets", "bounce", "balls"};
        for (unsigned s = 0; s < 4; ++s) CHECK(!std::strcmp(galileo::parameter_name(m, 4 + s), motion[s]), "model %u slot %u", m, 4 + s);
    }
    // Every default key sounds, within the ceiling, under the knee, and frees its voice after its run.
    for (unsigned k = 0; k < 24; ++k) {
        unsigned model;
        unsigned char c[8];
        key_values(k, model, c);
        const Result r = play(model, c, 0, 0, 0, 16000);
        CHECK(r.peak > 0.05, "key %u is (nearly) silent: peak %.3f", 53 + k, r.peak);
        CHECK(r.peak <= knee / 2147483648.0 + 1e-6, "key %u reaches the soft knee at its defaults: peak %.3f", 53 + k, r.peak);
        CHECK(r.freed, "key %u never frees its voice", 53 + k);
        CHECK(!r.freed || r.freed > r.motion_end, "key %u frees its voice (block %u) before its run ends (block %u)", 53 + k, r.freed, r.motion_end);
    }
    // Every model at every control's ends: bounded, sounding, and the control moves the sound.
    for (unsigned m = 0; m < galileo::kit::models; ++m) {
        unsigned char base[8];
        model_values(m, base);
        for (unsigned s = 0; s < 8; ++s) {
            unsigned char lo[8], hi[8];
            std::memcpy(lo, base, 8);
            std::memcpy(hi, base, 8);
            lo[s] = 0;
            hi[s] = 127;
            const Result a = play(m, lo), b = play(m, hi);
            CHECK(a.peak <= 0.5 && b.peak <= 0.5, "model %u slot %u leaves the ceiling (%.3f, %.3f)", m, s, a.peak, b.peak);
            CHECK(a.peak > 0.01 && b.peak > 0.01, "model %u slot %u silent at an end (%.3f, %.3f)", m, s, a.peak, b.peak);
            const double d = distance(a, b);
            CHECK(d > 1.0, "model %u slot %u (%s) barely moves the sound: %.2f", m, s, galileo::parameter_name(m, s), d);
        }
    }
    // Every model with everything at its top and at its bottom stays bounded.
    for (unsigned m = 0; m < galileo::kit::models; ++m) {
        unsigned char top[8], bottom[8];
        std::memset(top, 127, 8);
        std::memset(bottom, 0, 8);
        const Result a = play(m, top, 15, 15), b = play(m, bottom, -16, 0);
        CHECK(a.peak <= 0.5 && b.peak <= 0.5, "model %u leaves the ceiling at its extremes", m);
    }
    // A fast roll is no louder than a slow one: 8 frets of a snare at 1/16 and at 1/64,
    // the frets' part only (before the landing), within 4 dB.
    {
        unsigned char slow[8] = {57, 76, 44, 64, 56, 64, 0, 0}, fast[8] = {57, 76, 44, 64, 120, 64, 0, 0};
        const Result a = play(1, slow, 0, 0, 1200), b = play(1, fast, 0, 0, 1200);
        // frets of the slow roll: 8 x 125 ms = 1 s = 20 windows; of the fast: 8 x 31 ms = 5 windows
        double ea = 0, eb = 0;
        for (int i = 0; i < 19; ++i) ea += a.env[i];
        for (int i = 0; i < 4; ++i) eb += b.env[i];
        ea /= 19; eb /= 4;
        const double ratio = eb / ea;
        CHECK(ratio < 2.5 && ratio > 0.25, "a 1/64 roll's frets are %.1fx the energy of a 1/16 roll's", ratio);
    }
    // The SDK's tempo sets the divisions: a run of 8 frets at 1/16 lasts 8 sixteenths.
    {
        unsigned char c[8] = {57, 76, 44, 64, 56, 64, 0, 0};
        std::memset(&shared, 0, sizeof(shared));
        play(1, c, 0, 0, 900);   // 90 BPM
        CHECK(voice.run.dt == 7350u, "at 90 BPM a sixteenth is %u samples, not 7350", voice.run.dt);
        std::memset(&shared, 0, sizeof(shared));
        play(1, c, 0, 0, 0);     // none: the learned tempo, 120 BPM to start
        CHECK(voice.run.dt == 5512u, "with no tempo a sixteenth is %u samples, not 5512", voice.run.dt);
    }
    // The pad captions.
    {
        char t[16];
        CHECK(galileo::pad_control_text(0, 0, t) && !std::strcmp(t, "even"), "SPACE 0 reads '%s'", t);
        CHECK(galileo::pad_control_text(0, -16, t) && !std::strcmp(t, "-16"), "SPACE -16 reads '%s'", t);
        CHECK(galileo::pad_control_text(1, 0, t) && !std::strcmp(t, "lock"), "FEEL 0 reads '%s'", t);
        CHECK(galileo::pad_control_text(1, 15, t) && !std::strcmp(t, "free"), "FEEL 15 reads '%s'", t);
        CHECK(!galileo::pad_control_text(2, 0, t), "a third pad field has a caption");
    }
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
