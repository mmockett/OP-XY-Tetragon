// Host renderer: plays notes on Syahi's real engine and writes a mono WAV.
//   render <out.wav> <seconds> [<model> <c0..c7> <human> <grit> <time s> <velocity ignored>]...
// Each note is "model c0 c1 c2 c3 c4 c5 c6 c7 human grit start" (13 numbers).
// Notes on the same key choke (as chokeSameKey): give each a key number as a 14th value.
// With SYAHI_STEREO=1 each note takes two more values, its key's pan (-100..100) and level
// (dB), and the WAV is stereo, panned at constant power (-3 dB each side at the centre).
#include "../engine.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
struct Note { unsigned model; unsigned c[8]; int human, grit; double start; int key; double left, right; };
syahi::Shared shared;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage\n"); return 2; }
    const double seconds = std::atof(argv[2]);
    const char* stereo_env = std::getenv("SYAHI_STEREO");
    const bool stereo = stereo_env && stereo_env[0] == '1';
    const int stride = stereo ? 16 : 14;
    std::vector<Note> notes;
    for (int i = 3; i + stride - 1 < argc; i += stride) {
        Note n;
        n.model = std::atoi(argv[i]);
        for (int c = 0; c < 8; ++c) n.c[c] = std::atoi(argv[i + 1 + c]);
        n.human = std::atoi(argv[i + 9]);
        n.grit = std::atoi(argv[i + 10]);
        n.start = std::atof(argv[i + 11]);
        n.key = std::atoi(argv[i + 12]);
        (void)argv[i + 13];
        n.left = n.right = 1.0;
        if (stereo) {
            const double gain = std::pow(10.0, std::atof(argv[i + 15]) / 20.0);
            const double th = (std::atof(argv[i + 14]) + 100.0) / 200.0 * 1.5707963267948966;
            n.left = gain * std::cos(th);
            n.right = gain * std::sin(th);
        }
        notes.push_back(n);
    }
    const unsigned blocks = static_cast<unsigned>(seconds * 44100 / 64) + 1;
    std::vector<long long> mix(blocks * 64 * 2, 0);
    struct Active { syahi::Voice voice; const Note* note; unsigned fade; bool live; };
    std::vector<Active> voices(notes.size());
    opxy::u32 pitch[64] = {};
    for (unsigned b = 0; b < blocks; ++b) {
        const double t = b * 64 / 44100.0;
        for (size_t i = 0; i < notes.size(); ++i) {
            if (voices[i].live || notes[i].start > t || notes[i].start <= t - 64 / 44100.0) continue;
            // chokeSameKey: earlier voices on this key fade over 128 frames.
            for (size_t j = 0; j < notes.size(); ++j)
                if (j != i && voices[j].live && notes[j].key == notes[i].key && !voices[j].fade) voices[j].fade = 128;
            voices[i] = Active();
            voices[i].note = &notes[i];
            voices[i].live = true;
            syahi::initialize(voices[i].voice, shared, 1);
            syahi::start(voices[i].voice, shared, 1);
        }
        for (auto& a : voices) {
            if (!a.live) continue;
            opxy::i32 params[8], out[64], pads[2] = {a.note->human, a.note->grit};
            for (int c = 0; c < 8; ++c) params[c] = a.note->c[c] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(a.note->c[c] << 24);
            syahi::drum_select(a.voice, shared, 1, a.note->model);
            syahi::drum_pad(a.voice, shared, 1, pads);
            syahi::render(a.voice, shared, 1, out, params, pitch);
            for (int n = 0; n < 64; ++n) {
                long long y = out[n];
                if (a.fade) { y = y * (a.fade - 1) / 128; --a.fade; if (!a.fade) { a.live = false; } }
                if (y > 0x40000000 || y < -0x40000000) std::fprintf(stderr, "peak beyond half scale\n");
                mix[2 * (b * 64 + n)] += std::llround(static_cast<double>(y >> 1) * a.note->left);
                mix[2 * (b * 64 + n) + 1] += std::llround(static_cast<double>(y >> 1) * a.note->right);
            }
            if (syahi::finished(a.voice, shared)) a.live = false;
        }
    }
    FILE* f = std::fopen(argv[1], "wb");
    const unsigned channels = stereo ? 2 : 1, frames = blocks * 64, bytes = frames * 4 * channels;
    auto u32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](unsigned v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(channels); u32(44100); u32(44100 * 4 * channels); u16(4 * channels); u16(32);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (unsigned i = 0; i < frames; ++i)
        for (unsigned c = 0; c < channels; ++c) {
            // Mono keeps the engine's level; stereo writes the left channel then the right.
            long long m = stereo ? mix[2 * i + c] : mix[2 * i];
            int v = static_cast<int>(m > 0x3fffffff ? 0x3fffffff : m < -0x3fffffff ? -0x3fffffff : m);
            v *= 2;
            std::fwrite(&v, 4, 1, f);
        }
    std::fclose(f);
    return 0;
}
