#!/usr/bin/env python3
"""A host estimate of THUNDER's cost per voice, in Blackfin instructions per
64-sample block, the way the SDK's tooling/cpu.py estimates a synth: the
voice's render timed against the SDK's reference loop (590 Blackfin
instructions a block) in the same run. The SDK does not estimate drum kits,
so this follows its drum profiling workload (docs/profiling.md,
opxy-drum-callback-v1): one voice of every model with all its controls at
64, 0 and 127, each pad control at its top, a note struck every 100 blocks.
The SDK's portable multiply is several host instructions (a 64-bit product,
a floored divide, a saturation test) where the Blackfin's is one, so timing it
overstates fixed-point code like this several times over; --blackfin-multiply
(the default) times a shim whose multiply is a product and a shift, which a
static count of the Blackfin's modes loop matches (tools/cpu.py --check).
Not part of the machine; an estimate, not a device measurement.

    python3 -I tools/cpu.py --sdk <open-xy> [--runs 5]
"""
import argparse, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
BENCH = r'''
#include "engine.cpp"
#include <chrono>
#include <cstdio>
#include <cstring>
// The SDK's reference loop (tooling/cpu.py): 590 Blackfin instructions a block.
// Timed beside every model, so a busy computer slows both alike.
static double reference() {
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
    return best / 20000;
}
int main() {
    static thunder::Voice voice;
    static thunder::Shared shared;
    static opxy::i32 out[64];
    opxy::u32 pitch[64] = {};
    volatile opxy::i32 sink = 0;
    const int settings[4] = {64, 0, 127, -1};
    for (opxy::u32 m = 0; m < thunder::kit::models; ++m) {
        for (int s = 0; s < 4; ++s) {
            opxy::i32 params[8];
            for (int c = 0; c < 8; ++c) {
                const int raw = settings[s] < 0 ? thunder::kit::model[m].defaults[c] : settings[s];
                params[c] = raw >= 127 ? 0x7fffffff : raw << 24;
            }
            const opxy::i32 pads[2] = {15, 15};
            double best = 1e30;
            for (int run = 0; run < @RUNS@; ++run) {
                std::memset(&voice, 0, sizeof voice);
                thunder::initialize(voice, shared, 1);
                const auto begin = std::chrono::steady_clock::now();
                for (int b = 0; b < 1500; ++b) {
                    if (b % 100 == 0) thunder::start(voice, shared, 1);
                    thunder::drum_select(voice, shared, 1, m);
                    thunder::drum_pad(voice, shared, 1, pads);
                    thunder::render(voice, shared, 1, out, params, pitch);
                    sink = sink + out[b & 63];
                }
                const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / 1500;
                if (ns < best) best = ns;
            }
            std::printf("%u %d %.2f %.4f\n", m, settings[s], best, reference());
        }
    }
    return 0;
}
'''

# The Blackfin's one-instruction multiply, as near as the host gets: a product and a shift.
SHIM = r'''#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace q31 {
const i32 maximum = 2147483647;
inline i64 wide(i32 a, i32 b) { return static_cast<i64>(a) * b; }
inline i32 multiply(i32 a, i32 b) { return static_cast<i32>((static_cast<i64>(a) * b) >> 31); }
inline i32 add(i32 a, i32 b) { const i64 s = static_cast<i64>(a) + b; return s > maximum ? maximum : s < -maximum - 1 ? -maximum - 1 : static_cast<i32>(s); }
inline i32 half(i32 v) { return v >> 1; }
}}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--portable-multiply", action="store_true", help="time the SDK's own portable multiply")
    a = ap.parse_args()
    sys.path.insert(0, a.sdk)
    from tooling import cpu
    scratch = pathlib.Path(tempfile.mkdtemp(prefix="thunder-cpu-"))
    (scratch / "bench.cpp").write_text(BENCH.replace("@RUNS@", str(a.runs)))
    shim = []
    if not a.portable_multiply:
        (scratch / "shim" / "opxy").mkdir(parents=True)
        (scratch / "shim" / "opxy" / "q31.hpp").write_text(SHIM)
        shim = [f"-I{scratch / 'shim'}"]
    subprocess.run(["c++", "-std=c++11", "-O2", *shim, f"-I{a.sdk}/sdk/include", f"-I{ROOT}", str(scratch / "bench.cpp"), "-o", str(scratch / "bench")], check=True)
    ref = cpu.reference_cost("c++", scratch)
    out = subprocess.run([str(scratch / "bench")], capture_output=True, text=True, check=True).stdout
    names = re.findall(r'\{"(\w+)", \{', (ROOT / "kit.h").read_text())
    rows = {}
    for line in out.split("\n"):
        if line:
            m, s, ns, local = line.split()
            # against the reference timed beside it, or the run's own, whichever is quicker
            rows.setdefault(int(m), {})[int(s)] = float(ns) / min(ref, float(local)) * cpu.REFERENCE_INSTRUCTIONS
    print("Estimated Blackfin instructions per voice block (host-timed%s; not measured on the device)" % ("" if a.portable_multiply else ", one-instruction multiply"))
    print(f"{'model':8s} {'defaults':>9s} {'all 64':>8s} {'all 0':>8s} {'all 127':>8s}   share of a 580,352-cycle block at the worst")
    worst = 0
    for m, r in sorted(rows.items()):
        w = max(r.values())
        worst = max(worst, w)
        print(f"{names[m]:8s} {r[-1]:9,.0f} {r[64]:8,.0f} {r[0]:8,.0f} {r[127]:8,.0f}   {w / 580352 * 100:.2f}%")
    print(f"worst {worst:,.0f} instructions, {worst / 580352 * 100:.2f}% of a block (an instruction is at least a cycle)")


if __name__ == "__main__":
    main()
