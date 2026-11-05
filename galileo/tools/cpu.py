#!/usr/bin/env python3
"""A host estimate of GALILEO's cost per voice, the way the SDK's tooling/cpu.py
estimates a synth's (it makes none for a drum kit), anchored to MD KIT's device
capture.

Each model's render is timed on the host, at its defaults, at each control's
ends, at its heaviest motion (16 frets at 1/64, 24 balls, full bounce) and with
everything up, over a note's first 73 ms (its busiest) and over the whole note
until its voice is freed. The host's Q31 multiply is a 64-bit divide in the
SDK's portable path, where the Blackfin's is one instruction, so the timing is
compiled with a one-instruction multiply (a copy of the SDK's headers, patched,
in a scratch folder), which keeps multiply-heavy code from looking heavier
than it is.

Two scales: the SDK's reference loop (a table oscillator through a one-pole,
about 590 Blackfin instructions a block) gives instructions a block; and MD
KIT's heaviest voice, timed the same way in the same run, measured 3.085% of
the block on an OP-XY (MD KIT v1.2.2's catalog record, opxy-drum-callback-v1),
gives a percentage, if MD KIT's checkout is
there (--mdkit). Both are estimates, not device measurements: only `sdk.py
profile` on an OP-XY gives the figure a catalog record needs.

    python3 tools/cpu.py [--sdk <open-xy>] [--mdkit <MD KIT folder>]
"""
import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

MDKIT_PCT = 3.085374   # MD KIT v1.2.2's catalog record (opxy-drum-callback-v1): its heaviest voice, % of the block

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import catalog  # noqa: E402

BENCH = r'''
#include "engine.cpp"
#include <chrono>
#include <cstdio>
#include <cstring>
static galileo::Shared shared;
static galileo::Voice voice;
// ns per block of one note at these controls, timed over whole runs of blocks
// (a clock read per block would cost more than the block): the mean over the
// note until its voice is freed, and over its first 50 blocks (73 ms), the busiest.
static double run_blocks(unsigned model, const opxy::i32* live, unsigned limit, unsigned& blocks) {
    opxy::u32 pitch[64];
    for (int i = 0; i < 64; ++i) pitch[i] = 25479004u;
    static opxy::i32 out[64];
    double best = 1e30;
    for (int run = 0; run < 15; ++run) {
        std::memset(&voice, 0, sizeof(voice));
        std::memset(&shared, 0, sizeof(shared));
        galileo::initialize(voice, shared, 1);
        galileo::start(voice, shared, 1);
        shared.latest = 99;
        unsigned b = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (; b < limit && !galileo::finished(voice, shared); ++b) {
            galileo::drum_select(voice, shared, 1, model);
            galileo::render(voice, shared, 1, out, live, pitch);
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        if (ns < best) { best = ns; blocks = b ? b : 1; }
    }
    return best / blocks;
}
static void time_note(unsigned model, const opxy::i32* live, double& mean, double& worst) {
    unsigned blocks = 0;
    mean = run_blocks(model, live, 6000, blocks);
    worst = run_blocks(model, live, 50, blocks);
}
int main(int argc, char** argv) {
    (void)argc;
    unsigned model = static_cast<unsigned>(std::atoi(argv[1]));
    opxy::i32 live[8];
    for (int i = 0; i < 8; ++i) { int v = std::atoi(argv[2 + i]); live[i] = v >= 127 ? 0x7fffffff : v << 24; }
    double mean, worst;
    time_note(model, live, mean, worst);
    std::printf("%.2f %.2f\n", mean, worst);
    return 0;
}
'''
# MD KIT's heaviest voice over its first 73 ms, its models at mid and at full controls.
MDKIT = r'''
#include "engine.cpp"
#include <chrono>
#include <cstdio>
#include <cstring>
static mdkit::Shared shared; static mdkit::Voice voice;
int main() {
    opxy::u32 pitch[64]; for (int i = 0; i < 64; i++) pitch[i] = 25479004u;
    static opxy::i32 out[64];
    double worst = 0;
    for (unsigned m = 0; m < mdkit::kit::models; m++)
        for (int variant = 0; variant < 2; variant++) {
            opxy::i32 live[8];
            for (int i = 0; i < 8; i++) live[i] = variant ? 0x7fffffff : 64 << 24;
            double best = 1e30;
            for (int run = 0; run < 7; run++) {
                std::memset(&voice, 0, sizeof voice); std::memset(&shared, 0, sizeof shared);
                mdkit::initialize(voice, shared, 1); mdkit::start(voice, shared, 1);
                const auto t0 = std::chrono::steady_clock::now();
                for (int b = 0; b < 50; b++) { mdkit::drum_select(voice, shared, 1, m); mdkit::render(voice, shared, 1, out, live, pitch); }
                const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / 50;
                if (ns < best) best = ns;
            }
            if (best > worst) worst = best;
        }
    std::printf("%.2f\n", worst);
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", default=str(ROOT.parent.parent / "open-xy"))
    parser.add_argument("--mdkit", default=str(ROOT.parent.parent / "OP-XY-MNMD" / "machines" / "mdkit"))
    args = parser.parse_args()
    work = pathlib.Path(tempfile.mkdtemp(prefix="galileo-cpu-"))
    # The SDK's headers with a one-instruction Q31 multiply, as the Blackfin's.
    shutil.copytree(pathlib.Path(args.sdk) / "sdk" / "include", work / "include")
    q31 = work / "include" / "opxy" / "q31.hpp"
    q31.write_text(q31.read_text().replace("    const i64 product = static_cast<i64>(a) * b;\n",
                                           "    return static_cast<i32>((static_cast<i64>(a) * b) >> 31);\n"
                                           "    const i64 product = static_cast<i64>(a) * b;\n", 1))
    inc = f"-I{work / 'include'}"
    (work / "bench.cpp").write_text(BENCH.replace("#include <cstring>", "#include <cstring>\n#include <cstdlib>"))
    (work / "reference.cpp").write_text(REFERENCE)
    subprocess.run(["c++", "-std=c++11", "-O2", "-w", inc, f"-I{ROOT}", str(work / "bench.cpp"), str(ROOT / "tables.cpp"), "-o", str(work / "bench")], check=True)
    subprocess.run(["c++", "-std=c++11", "-O2", inc, str(work / "reference.cpp"), "-o", str(work / "reference")], check=True)

    def reference():
        return min(float(subprocess.run([str(work / "reference")], capture_output=True, text=True).stdout) for _ in range(3))

    ref = reference()
    md = None
    if (pathlib.Path(args.mdkit) / "engine.cpp").exists():
        (work / "mdkit.cpp").write_text(MDKIT)
        subprocess.run(["c++", "-std=c++11", "-O2", "-w", inc, f"-I{args.mdkit}", str(work / "mdkit.cpp"), "-o", str(work / "mdkit")], check=True)
        md = min(float(subprocess.run([str(work / "mdkit")], capture_output=True, text=True, check=True).stdout.split()[0]) for _ in range(3))
    rows = []
    for m, (name, *_rest) in enumerate(catalog.MODELS):
        base = catalog.key(name)[1]
        settings = [("defaults", base)]
        for slot in range(8):
            for v in (0, 127):
                vals = list(base)
                vals[slot] = v
                settings.append((f"{(catalog.MODELS[m][2] + catalog.MOTION)[slot]} {v}", vals))
        heavy = list(base)
        heavy[4:8] = [120, 127, 127, 127]
        settings.append(("heaviest motion", heavy))
        top = list(heavy)
        top[0:4] = [127, 127, 127, 127]
        settings.append(("everything up", top))
        worst_mean, worst_block, which = 0.0, 0.0, ""
        for label, vals in settings:
            out = subprocess.run([str(work / "bench"), str(m), *map(str, vals)], capture_output=True, text=True, check=True).stdout
            mean, worst = map(float, out.split())
            if md:
                # Paired: MD KIT's heaviest voice timed again straight after, so a
                # busy moment on the host slows both; scaled to the run's own MD KIT figure.
                again = float(subprocess.run([str(work / "mdkit")], capture_output=True, text=True, check=True).stdout.split()[0])
                mean, worst = mean * md / again, worst * md / again
            if worst > worst_block:
                worst_block, which = worst, label
            worst_mean = max(worst_mean, mean)
        rows.append((name, worst_mean / ref * 590, worst_block / ref * 590, which, worst_mean, worst_block))
    ref2 = reference()
    print(f"reference loop {ref:.1f} ns a block (again {ref2:.1f}); 590 Blackfin instructions")
    if md:
        print(f"MD KIT's heaviest voice {md:.0f} ns a block here, {MDKIT_PCT:.3f}% of the block on the device")
    for name, mean, worst, which, mean_ns, worst_ns in rows:
        pct = f"; about {worst_ns / md * MDKIT_PCT:.1f}% at its busiest, {mean_ns / md * MDKIT_PCT:.1f}% over a note" if md else ""
        print(f"{name:8s} about {round(mean, -2):6.0f} instructions a block over a note; its first 73 ms about {round(worst, -2):6.0f} ({which}){pct}")
    heaviest = max(rows, key=lambda r: r[2])
    if md:
        print(f"heaviest: {heaviest[0]}, about {heaviest[5] / md * MDKIT_PCT:.1f}% of the block at its busiest ({heaviest[3]})")
    print(f"heaviest: {heaviest[0]} about {round(heaviest[2], -2):.0f} a block in a note's first 73 ms ({heaviest[3]})")


if __name__ == "__main__":
    main()
