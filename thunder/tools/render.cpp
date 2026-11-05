// Renders THUNDER's real engine on the host, for measuring and for the demos.
// Not part of the machine.
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include -I. tools/render.cpp -o render
//   ./render < jobs
// Each job line: out.raw model c0 c1 c2 c3 c4 c5 c6 c7 tune drive seconds [seed]
// writes the hit as 32-bit little-endian integers (Q31), until it ends or for
// `seconds`, then a line with its length in samples and the blocks it took.
#include "../engine.cpp"
#include <cstdio>
#include <cstring>

int main() {
    static thunder::Voice voice;
    static thunder::Shared shared;
    char path[512];
    int m, c[8], tune, drive;
    double seconds;
    char line[1024];
    while (std::fgets(line, sizeof line, stdin)) {
        unsigned seed = 0;
        if (std::sscanf(line, "%511s %d %d %d %d %d %d %d %d %d %d %d %lf %u", path, &m, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5],
                        &c[6], &c[7], &tune, &drive, &seconds, &seed) < 13) continue;
        std::memset(&voice, 0, sizeof voice);
        shared.notes = seed;
        thunder::initialize(voice, shared, 1);
        thunder::start(voice, shared, 1);
        opxy::i32 params[8];
        for (int i = 0; i < 8; ++i) params[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        const opxy::i32 pads[2] = {tune, drive};
        opxy::u32 pitch[64] = {};
        opxy::i32 out[64];
        FILE* f = std::fopen(path, "wb");
        const unsigned blocks = static_cast<unsigned>(seconds * 44100 / 64);
        unsigned b = 0;
        for (; b < blocks && !(b && thunder::finished(voice, shared)); ++b) {
            thunder::drum_select(voice, shared, 1, static_cast<opxy::u32>(m));
            thunder::drum_pad(voice, shared, 1, pads);
            thunder::render(voice, shared, 1, out, params, pitch);
            std::fwrite(out, 4, 64, f);
        }
        std::fclose(f);
        std::printf("%s %u %d\n", path, b * 64, thunder::finished(voice, shared) ? 1 : 0);
        std::fflush(stdout);
    }
    return 0;
}
