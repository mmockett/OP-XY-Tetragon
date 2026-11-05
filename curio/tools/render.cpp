// Renders hits through CURIO's engine on the host, for tools/demos.py.
// stdin: 24 lines "model c0..c7 tune vary" (controls 0..127), then lines
// "sample key level tune" (level 0..1000, the note's amplitude; tune in
// semitones, added to the key's TUNE); argv[1] the length
// in samples. stdout: 24 float32 stems of that length, key by key. Keys in
// the same choke group (argv[2], a list like "6,8,10") cut each other, as
// stock's mute1 does, over 128 samples.
#include "../engine.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    using namespace curio;
    const long length = argc > 1 ? std::atol(argv[1]) : 44100;
    bool choke[24] = {};
    if (argc > 2)
        for (char* p = argv[2]; *p;) { choke[std::strtol(p, &p, 10)] = true; if (*p == ',') ++p; }
    int model[24], ctl[24][8], tune[24], vary[24];
    for (int k = 0; k < 24; ++k) {
        if (std::scanf("%d", &model[k]) != 1) return 1;
        for (int c = 0; c < 8; ++c) if (std::scanf("%d", &ctl[k][c]) != 1) return 1;
        if (std::scanf("%d %d", &tune[k], &vary[k]) != 2) return 1;
    }
    struct Hit { long at; int key; int level; int tune; };
    std::vector<Hit> hits;
    Hit h;
    while (std::scanf("%ld %d %d %d", &h.at, &h.key, &h.level, &h.tune) == 4) hits.push_back(h);
    std::vector<float> stems(24 * static_cast<size_t>(length), 0.0f);
    static Shared shared;
    for (size_t n = 0; n < hits.size(); ++n) {
        const Hit& x = hits[n];
        // Where a choked key's next hit cuts this one.
        long end = length;
        if (choke[x.key])
            for (size_t m = n + 1; m < hits.size(); ++m)
                if (choke[hits[m].key] && hits[m].at > x.at) { end = hits[m].at + 128; break; }
        static Voice voice;
        voice = Voice();
        initialize(voice, shared, 1);
        start(voice, shared, 1);
        opxy::i32 values[8];
        for (int c = 0; c < 8; ++c) values[c] = ctl[x.key][c] >= 127 ? 0x7fffffff : ctl[x.key][c] << 24;
        const opxy::i32 pads[4] = {tune[x.key] + x.tune, vary[x.key], 0, 0};
        opxy::i32 out[64];
        opxy::u32 pitch[64] = {};
        for (long t = x.at; t < end && t < x.at + 10 * 44100; t += 64) {
            drum_select(voice, shared, 1, static_cast<opxy::u32>(model[x.key]));
            drum_pad(voice, shared, 1, pads);
            render(voice, shared, 1, out, values, pitch);
            for (int i = 0; i < 64 && t + i < length; ++i) {
                float g = x.level / 1000.0f;
                if (end < length && t + i >= end - 128) g *= (end - t - i) / 128.0f;
                stems[static_cast<size_t>(x.key) * length + t + i] += g * out[i] / 2147483648.0f;
            }
            if (finished(voice, shared)) break;
        }
    }
    std::fwrite(stems.data(), sizeof(float), stems.size(), stdout);
    return 0;
}
