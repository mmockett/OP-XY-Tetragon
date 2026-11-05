// The cost of CURIO's voices against MD KIT's heaviest, timed interleaved in
// one process so a busy host moves both alike: each CURIO voice at its worst
// setting (its defaults, then each control at 0 and at 127) over 1,500 blocks
// from a strike, against MD KIT's model 29 (PI-HH) with every control at 64,
// the smallest ratio of five rounds. That voice is MD KIT's worst in the
// catalog's device capture (v1.2.2, opxy-drum-callback-v1): 3.085% of the
// block. Build against that tag's machines/mdkit. An estimate, not a
// measurement, and a rough one: a host ranks engines differently from the
// Blackfin (tools/cpu.py puts PI-HH at 0.86% against the device's 3.085%,
// and the host's PI-HH is cheap once it falls silent), so these ratios
// overstate CURIO's cost. Host only:
//   c++ -std=c++11 -O2 -I <sdk>/sdk/include -I <mdkit> -DMDKIT_ENGINE='"<mdkit>/engine.cpp"' tools/cpu_ratio.cpp -o ratio && ./ratio 0 1 2 ...
#include "../engine.cpp"
#include MDKIT_ENGINE
#include <chrono>
#include <cstdio>
#include <cstdlib>
template <class F> double best(F f) { double b = 1e30; for (int r = 0; r < 3; ++r) { auto t0 = std::chrono::steady_clock::now(); f(); double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count(); if (ns < b) b = ns; } return b; }
// As the device benchmark (opxy-drum-callback-v1) times a voice: 1,500 blocks from a strike.
const int blocks = 1500;
double curio_cost(int m) {
    static curio::Voice v; static curio::Shared s; opxy::i32 out[64]; opxy::u32 p[64] = {}; const opxy::i32 pads[4] = {15, 15, 0, 0};
    double worst = 0;
    for (int c = -1; c < 8; ++c) for (int val = 0; val < 2; ++val) {
        if (c < 0 && val) continue;
        opxy::i32 vals[8]; for (int i = 0; i < 8; ++i) vals[i] = curio::kit::model[m].defaults[i] << 24; if (c >= 0) vals[c] = val ? 0x7fffffff : 0;
        double ns = best([&] { v = curio::Voice(); curio::initialize(v, s, 1); curio::start(v, s, 1); for (int b = 0; b < blocks; ++b) { curio::drum_select(v, s, 1, m); curio::drum_pad(v, s, 1, pads); curio::render(v, s, 1, out, vals, p); } });
        if (ns > worst) worst = ns;
    }
    return worst;
}
// MD KIT's voice as the catalog measured it: every control at 64.
double md_cost(int m) {
    static mdkit::Voice v; static mdkit::Shared s; opxy::i32 out[64]; opxy::u32 p[64] = {};
    opxy::i32 vals[8]; for (int i = 0; i < 8; ++i) vals[i] = 64 << 24;
    return best([&] { v = mdkit::Voice(); mdkit::initialize(v, s, 1); mdkit::start(v, s, 1); for (int b = 0; b < blocks; ++b) { mdkit::drum_select(v, s, 1, m); mdkit::render(v, s, 1, out, vals, p); } });
}
int main(int argc, char** argv) {
    for (int a = 1; a < argc; ++a) {
        const int m = atoi(argv[a]);
        double r = 1e9;
        for (int k = 0; k < 5; ++k) { const double cc = curio_cost(m), md = md_cost(29); if (cc / md < r) r = cc / md; }
        std::printf("%s: %.2f of MD KIT's PI-HH, so about %.2f%% of the block\n", curio::kit::model[m].name, r, r * 3.085);
    }
}
