// Renders single hits through CURIO's engine on the host, for
// tools/calibrate.py: one job a line on stdin,
//   out.raw model c0..c7 tune vary seconds seed
// writes the hit (Q31, 32-bit little-endian) until it ends, and prints
// "out.raw samples prepeak": prepeak the hit's largest sample before the
// output stage's peak limiter (as a share of full scale), from a second
// voice struck alike. Not part of the machine.
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include tools/hit.cpp -o hit
#include "../engine.cpp"
#include <cstdio>

int main() {
    using namespace curio;
    static Voice voice, twin;
    static Shared shared, twin_shared;
    char path[512], line[1024];
    int m, c[8], tune, vary;
    unsigned seed;
    double seconds;
    while (std::fgets(line, sizeof line, stdin)) {
        if (std::sscanf(line, "%511s %d %d %d %d %d %d %d %d %d %d %d %lf %u", path, &m, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5],
                        &c[6], &c[7], &tune, &vary, &seconds, &seed) < 14) continue;
        voice = Voice();
        twin = Voice();
        shared.hits = twin_shared.hits = seed;
        initialize(voice, shared, 1);
        start(voice, shared, 1);
        initialize(twin, twin_shared, 1);
        opxy::i32 values[8];
        for (int i = 0; i < 8; ++i) values[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        const opxy::i32 pads[4] = {tune, vary, 0, 0};
        opxy::u32 pitch[64] = {};
        opxy::i32 out[64], raw[64];
        FILE* f = std::fopen(path, "wb");
        const unsigned blocks = static_cast<unsigned>(seconds * 44100 / 64);
        double prepeak = 0;
        unsigned b = 0;
        for (; b < blocks && !(b && finished(voice, shared)); ++b) {
            drum_select(voice, shared, 1, static_cast<opxy::u32>(m));
            drum_pad(voice, shared, 1, pads);
            render(voice, shared, 1, out, values, pitch);
            std::fwrite(out, 4, 64, f);
            // The twin: the same strike, its specimen's own output at the hit's gain.
            drum_select(twin, twin_shared, 1, static_cast<opxy::u32>(m));
            drum_pad(twin, twin_shared, 1, pads);
            if (!b) { twin.model = twin.next; begin(twin, twin_shared, values); }
            switch (twin.model) {
            case 0: run0(twin, raw); break; case 1: run1(twin, raw); break; case 2: run2(twin, raw); break;
            case 3: run3(twin, raw); break; case 4: run4(twin, raw); break; case 5: run5(twin, raw); break;
            case 6: run6(twin, raw); break; case 7: run7(twin, raw); break; case 8: run8(twin, raw); break;
            case 9: run9(twin, raw); break; case 10: run10(twin, raw); break; case 11: run11(twin, raw); break;
            case 12: run12(twin, raw); break; case 13: run13(twin, raw); break; case 14: run14(twin, raw); break;
            default: run15(twin, raw); break;
            }
            for (int i = 0; i < 64; ++i) {
                const double a = static_cast<double>(raw[i] < 0 ? -raw[i] : raw[i]) * twin.gain / 2147483648.0 * 64 / 2147483648.0;
                if (a > prepeak) prepeak = a;
            }
        }
        std::fclose(f);
        std::printf("%s %u %.6f\n", path, b * 64, prepeak);
        std::fflush(stdout);
    }
    return 0;
}
