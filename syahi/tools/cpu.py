#!/usr/bin/env python3
"""A host estimate of Syahi's cost per voice per block, model by model, in
Blackfin instructions, by the SDK's own method (tooling/cpu.py): the voice's
render timed against the SDK's reference loop in the same run, scaled by that
loop's known instruction count. Each model is timed at its defaults and with
each control at nine positions (the others at the model's defaults), with GRIT
off and at +15; the slowest setting counts. It is an estimate: the device
measures with `sdk.py profile` on a profiling build.

    python3 tools/cpu.py --sdk <open-xy checkout> [--cxx clang++]
"""
import argparse, pathlib, subprocess, sys, tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import kits  # noqa: E402

BENCH = r'''
#include "engine.cpp"
#include <chrono>
#include <cstdio>
static syahi::Voice voice;
static syahi::Shared shared;
// The SDK's reference loop (tooling/cpu.py), nanoseconds a block, timed in the
// same process right beside each setting, so the ratio holds on whichever core runs it.
static double reference() {
    static opxy::i32 table[256], output[64];
    for (int i = 0; i < 256; ++i) table[i] = (i < 128 ? i : 255 - i) * 16777216 - 1073741824;
    opxy::u32 phase = 0;
    opxy::i32 low = 0;
    volatile opxy::i32 sink = 0;
    double best = 1e30;
    for (int run = 0; run < 3; ++run) {
        const auto begin = std::chrono::steady_clock::now();
        for (int block = 0; block < 4000; ++block) {
            for (int i = 0; i < 64; ++i) {
                phase += 42852281u;
                low += (table[phase >> 24] - low) >> 4;
                output[i] = low;
            }
            sink = sink + output[block & 63];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        if (ns < best) best = ns;
    }
    return best / 4000;
}
// A note's first 400 blocks (9 s), in reference blocks per block: the best of five.
static double time_note(unsigned model, const opxy::i32* values, opxy::i32 grit) {
    static opxy::i32 out[64];
    opxy::u32 pitch[64] = {};
    const opxy::i32 pads[2] = {grit ? 8 : 0, grit};
    volatile opxy::i32 sink = 0;
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        const double ref = reference();
        voice = syahi::Voice();
        syahi::initialize(voice, shared, 1);
        syahi::start(voice, shared, 1);
        const auto begin = std::chrono::steady_clock::now();
        for (int b = 0; b < 400; ++b) {
            syahi::drum_select(voice, shared, 1, model);
            syahi::drum_pad(voice, shared, 1, pads);
            syahi::render(voice, shared, 1, out, values, pitch);
            sink = sink + out[b & 63];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / 400 / ref;
        if (ns < best) best = ns;
    }
    return best;
}
int main() {
    for (unsigned m = 0; m < syahi::kit::models; ++m) {
        double worst = 0, defaults = 0;
        int worst_slot = -1, worst_value = 0, worst_grit = 0;
        for (int g = 0; g < 2; ++g)
            for (int slot = -1; slot < 8; ++slot)
                for (int step = 0; step < (slot < 0 ? 1 : 9); ++step) {
                    opxy::i32 values[8];
                    for (int c = 0; c < 8; ++c) values[c] = static_cast<opxy::i32>(syahi::kit::model_defaults[m][c]) << 24;
                    int v = 0;
                    if (slot >= 0) { v = step * 16 > 127 ? 127 : step * 16; values[slot] = v >= 127 ? 0x7fffffff : v << 24; }
                    const double ns = time_note(m, values, g ? 15 : 0);
                    if (slot < 0 && !g) defaults = ns;
                    if (ns > worst) { worst = ns; worst_slot = slot; worst_value = v; worst_grit = g ? 15 : 0; }
                }
        std::printf("%u %.2f %.2f %d %d %d\n", m, defaults, worst, worst_slot, worst_value, worst_grit);
    }
    return 0;
}
'''

REFERENCE = r'''
#include <opxy/types.hpp>
#include <chrono>
#include <cstdio>
int main() {
    static opxy::i32 table[256], output[64];
    for (int i = 0; i < 256; ++i) table[i] = (i < 128 ? i : 255 - i) * 16777216 - 1073741824;
    opxy::u32 phase = 0;
    opxy::i32 low = 0;
    volatile opxy::i32 sink = 0;
    double best = 1e30;
    for (int run = 0; run < 7; ++run) {
        const auto begin = std::chrono::steady_clock::now();
        for (int block = 0; block < 20000; ++block) {
            for (int i = 0; i < 64; ++i) {
                phase += 42852281u;
                low += (table[phase >> 24] - low) >> 4;
                output[i] = low;
            }
            sink = sink + output[block & 63];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        if (ns < best) best = ns;
    }
    std::printf("%.3f\n", best / 20000);
    return 0;
}
'''
REFERENCE_INSTRUCTIONS = 590   # the SDK's figure for the reference loop (tooling/cpu.py)
BLOCK_CYCLES = 580352          # the OP-XY's audio block (docs/profiling.md)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--cxx", default="c++")
    a = ap.parse_args()
    inc = pathlib.Path(a.sdk) / "sdk" / "include"
    with tempfile.TemporaryDirectory() as tmp:
        t = pathlib.Path(tmp)
        (t / "bench.cpp").write_text(BENCH)
        (t / "reference.cpp").write_text(REFERENCE)
        subprocess.run([a.cxx, "-std=c++11", "-O2", f"-I{inc}", f"-I{ROOT}", str(t / "bench.cpp"), "-o", str(t / "bench")], check=True)
        rows = subprocess.run([str(t / "bench")], capture_output=True, text=True, check=True).stdout.split("\n")
    ref = 1.0
    print(f"the reference loop's block = {REFERENCE_INSTRUCTIONS} Blackfin instructions")
    print(f"{'model':7} {'defaults':>9} {'heaviest':>9}  {'% of block':>10}  heaviest setting")
    worst = (0, "")
    for row in filter(None, rows):
        m, d, w, slot, v, g = row.split()
        name = kits.MODELS[int(m)]
        di = float(d) / ref * REFERENCE_INSTRUCTIONS
        wi = float(w) / ref * REFERENCE_INSTRUCTIONS
        label = "defaults" if int(slot) < 0 else f"{kits.LABELS[name][int(slot)]} {round(int(v) * 100 / 127)}"
        if int(g):
            label += ", GRIT +15"
        print(f"{name:7} {di:9,.0f} {wi:9,.0f}  {100 * wi / BLOCK_CYCLES:9.2f}%  {label}")
        worst = max(worst, (wi, name))
    print(f"heaviest: {worst[1]} about {worst[0]:,.0f} instructions, {100 * worst[0] / BLOCK_CYCLES:.2f}% of a block "
          "(instructions are not cycles: an estimate, not a device measurement)")


if __name__ == "__main__":
    main()
