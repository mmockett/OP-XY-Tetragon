// Renders hits through SYAHI's real engine on the host, for tools/calibrate.py
// and level audits. Not part of the machine. Job lines on stdin:
// out.raw model c0..c7 human grit seconds. Shared persists, so repeating a job
// gives the key's next note (the next seed). Writes Q31 int32.
#include "../engine.cpp"
#include <cstdio>
#include <cstring>
int main() {
    static syahi::Shared shared; static syahi::Voice voice;
    char path[512], line[1024]; int m, c[8], human, grit; double seconds;
    while (std::fgets(line, sizeof line, stdin)) {
        if (std::sscanf(line, "%511s %d %d %d %d %d %d %d %d %d %d %d %lf", path, &m, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &human, &grit, &seconds) < 13) continue;
        std::memset(&voice, 0, sizeof voice);
        syahi::initialize(voice, shared, 1); syahi::start(voice, shared, 1);
        opxy::i32 params[8]; for (int i = 0; i < 8; ++i) params[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        const opxy::i32 pads[2] = {human, grit};
        opxy::u32 pitch[64] = {}; opxy::i32 out[64];
        FILE* f = std::fopen(path, "wb");
        const unsigned blocks = static_cast<unsigned>(seconds * 44100 / 64);
        unsigned b = 0;
        for (; b < blocks && !(b && syahi::finished(voice, shared)); ++b) {
            syahi::drum_select(voice, shared, 1, m); syahi::drum_pad(voice, shared, 1, pads);
            syahi::render(voice, shared, 1, out, params, pitch); std::fwrite(out, 4, 64, f);
        }
        std::fclose(f); std::printf("%s %u\n", path, b * 64); std::fflush(stdout);
    }
}
