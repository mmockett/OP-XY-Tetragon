#!/usr/bin/env python3
"""A steadier CPU estimate than timing, on a busy computer: the instructions
the host retires (macOS's /usr/bin/time -l) running THUNDER's voice through
the SDK's drum profiling workload (tools/cpu.py), against the SDK's reference
loop (590 Blackfin instructions a block) counted the same way. Load cannot
move it, so it compares versions fairly; as an absolute figure it is the same
kind of estimate as tools/cpu.py (the host's instructions are not the
Blackfin's). The multiply is the Blackfin-like shim of tools/cpu.py. Not
part of the machine.

    python3 -I tools/cpu_retired.py --sdk <open-xy>
"""
import argparse, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from cpu import SHIM  # noqa: E402

BENCH = r'''
#include "engine.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv) {
    const int blocks = std::atoi(argv[1]);
    const int m = std::atoi(argv[2]);
    const int setting = std::atoi(argv[3]);
    volatile opxy::i32 sink = 0;
    if (m < 0) {   // the SDK's reference loop
        static opxy::i32 table[256], output[64];
        for (int i = 0; i < 256; ++i) table[i] = (i < 128 ? i : 255 - i) * 16777216 - 1073741824;
        opxy::u32 phase = 0;
        opxy::i32 low = 0;
        for (int block = 0; block < blocks; ++block) {
            for (int i = 0; i < 64; ++i) { phase += 42852281u; low += (table[phase >> 24] - low) >> 4; output[i] = low; }
            sink = sink + output[block & 63];
        }
        return 0;
    }
    static thunder::Voice voice;
    static thunder::Shared shared;
    static opxy::i32 out[64];
    opxy::u32 pitch[64] = {};
    opxy::i32 params[8];
    for (int c = 0; c < 8; ++c) {
        const int raw = setting < 0 ? thunder::kit::model[m].defaults[c] : setting;
        params[c] = raw >= 127 ? 0x7fffffff : raw << 24;
    }
    const opxy::i32 pads[2] = {15, 15};
    std::memset(&voice, 0, sizeof voice);
    thunder::initialize(voice, shared, 1);
    for (int b = 0; b < blocks; ++b) {
        if (b % 100 == 0) thunder::start(voice, shared, 1);
        thunder::drum_select(voice, shared, 1, static_cast<opxy::u32>(m));
        thunder::drum_pad(voice, shared, 1, pads);
        thunder::render(voice, shared, 1, out, params, pitch);
        sink = sink + out[b & 63];
    }
    return 0;
}
'''


def retired(binary, *args):
    r = subprocess.run(["/usr/bin/time", "-l", str(binary), *map(str, args)], capture_output=True, text=True, check=True)
    return int(re.search(r"(\d+)\s+instructions retired", r.stderr).group(1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--blocks", type=int, default=20000)
    a = ap.parse_args()
    scratch = pathlib.Path(tempfile.mkdtemp(prefix="thunder-retired-"))
    (scratch / "shim" / "opxy").mkdir(parents=True)
    (scratch / "shim" / "opxy" / "q31.hpp").write_text(SHIM)
    (scratch / "bench.cpp").write_text(BENCH)
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{scratch / 'shim'}", f"-I{a.sdk}/sdk/include", f"-I{ROOT}",
                    str(scratch / "bench.cpp"), "-o", str(scratch / "bench")], check=True)
    b = scratch / "bench"
    # long enough that the process's own start (about 16 million, give or
    # take one) is a percent or so
    per = lambda m, s, n=a.blocks: (retired(b, n, m, s) - retired(b, 0, m, s)) / n
    ref = per(-1, 0, 10 * a.blocks)
    names = re.findall(r'\{"(\w+)", \{', (ROOT / "kit.h").read_text())
    print("Estimated Blackfin instructions per voice block (host instructions retired against the SDK's reference loop)")
    print(f"{'model':8s} {'defaults':>9s} {'all 64':>8s} {'all 0':>8s} {'all 127':>8s}   share of a 580,352-cycle block at the worst")
    worst = 0
    for m, name in enumerate(names):
        r = {s: per(m, s) / ref * 590 for s in (-1, 64, 0, 127)}
        w = max(r.values())
        worst = max(worst, w)
        print(f"{name:8s} {r[-1]:9,.0f} {r[64]:8,.0f} {r[0]:8,.0f} {r[127]:8,.0f}   {w / 580352 * 100:.2f}%")
    print(f"worst {worst:,.0f} instructions, {worst / 580352 * 100:.2f}% of a block")


if __name__ == "__main__":
    main()
