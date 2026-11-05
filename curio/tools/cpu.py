#!/usr/bin/env python3
"""A host estimate of a drum kit's CPU per voice, model by model, the way the
SDK's synth estimate works (tooling/cpu.py): the engine's render through
drum_select and drum_pad, at the model's defaults and then each control at
nine positions with the rest at the defaults, the note struck afresh before
each run of 400 blocks; the slowest setting counts. The host's nanoseconds
are scaled by the SDK's reference loop (about 590 Blackfin instructions a
block), timed in the same run, and taken as the best of three. Instructions
are not cycles and the host is not the Blackfin: this is an estimate.

    python3 tools/cpu.py --sdk <open-xy checkout> [--machine <kit folder>] [--cxx clang++]

--machine defaults to this kit; MD KIT (whose voices are measured on the
device) can be passed for scale.
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent

BENCH = r'''
#include "engine.cpp"
@HARNESS@
int main() {
    static @NS@::Voice voice;
    static @NS@::Shared shared;
    static opxy::u32 pitch[64];
    static opxy::i32 output[64];
    const opxy::i32 pads[4] = {15, 15, 0, 0};
    volatile opxy::i32 sink = 0;
    std::printf("%.1f\n", worst_block([&] { voice = @NS@::Voice(); @NS@::initialize(voice, shared, 1); @NS@::start(voice, shared, 1); },
        [&](const opxy::i32* parameters, int b) {
            @NS@::drum_select(voice, shared, 1, @MODEL@);
            @PAD@
            @NS@::render(voice, shared, 1, output, parameters, pitch);
            sink = sink + output[b & 63];
        }));
    return 0;
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--machine", default=str(ROOT))
    ap.add_argument("--cxx", default="c++")
    ap.add_argument("--only", type=int, nargs="*", help="model numbers")
    a = ap.parse_args()
    sys.path.insert(0, a.sdk)
    from tooling.cpu import HARNESS, REFERENCE, REFERENCE_INSTRUCTIONS
    machine = pathlib.Path(a.machine).resolve()
    ns = json.loads((machine / "definition.json").read_text())["id"]
    count = json.loads((machine / "definition.json").read_text())["resources"]["modelCount"]
    kit = (machine / "kit.h").read_text()
    if ns == "curio":
        defaults = [[int(v) for v in d.split(",")] for d in re.findall(r'\{"[a-z ]+", \d, \{[^}]*\}, \{([^}]*)\}', kit)]
        names = re.findall(r'\{"([a-z ]+)", \d, \{', kit)
    else:
        # MD KIT: each machine's first key's values, else 64s.
        key_model = [int(v) for v in re.search(r"key_model\[keys\] = \{([^}]*)\}", kit).group(1).split(",")]
        key_values = [[int(v) for v in row.split(",")] for row in re.findall(r"\{([0-9, ]+)\},\s+//", kit)]
        defaults = [key_values[key_model.index(m)] if m in key_model else [64] * 8 for m in range(count)]
        names = [f"{ns} {m}" for m in range(count)]
    work = pathlib.Path(tempfile.mkdtemp())

    def run(source, name):
        (work / f"{name}.cpp").write_text(source)
        subprocess.run([a.cxx, "-std=c++11", "-O2", f"-I{a.sdk}/sdk/include", f"-I{machine}", str(work / f"{name}.cpp"),
                        "-o", str(work / name)], check=True)
        return float(subprocess.run([str(work / name)], capture_output=True, text=True, check=True).stdout)

    positions = [0x7fffffff * step // 8 for step in range(9)]
    results = []
    for m in range(count):
        if a.only and m not in a.only:
            continue
        d = defaults[m]
        harness = (HARNESS.replace("@BLOCKS@", "400")
                   .replace("@DEFAULTS@", ", ".join(str(min(v << 24, 0x7fffffff)) for v in d))
                   .replace("@POSITIONS@", ", ".join(str(p) for _ in range(8) for p in positions))
                   .replace("@COUNTS@", ", ".join(["9"] * 8)).replace("@POSITION_COUNT@", "72"))
        pad = f"{ns}::drum_pad(voice, shared, 1, pads);" if ns == "curio" else ""
        source = BENCH.replace("@HARNESS@", harness).replace("@NS@", ns).replace("@MODEL@", str(m)).replace("@PAD@", pad)
        best = min(run(source, f"m{m}") / run(REFERENCE, "reference") for _ in range(3))
        instructions = round(best * REFERENCE_INSTRUCTIONS, -2)
        results.append((names[m], instructions))
        print(f"{names[m]:10s} about {instructions:7,.0f} instructions a block, {100 * instructions / 580352:.2f}% of the block", flush=True)
    worst = max(results, key=lambda r: r[1])
    print(f"heaviest: {worst[0]}, about {worst[1]:,.0f} ({100 * worst[1] / 580352:.2f}%)")


if __name__ == "__main__":
    main()
