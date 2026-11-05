#!/usr/bin/env python3
"""A static count of GALILEO's per-voice cost on the Blackfin: builds the module
with the SDK's pinned compiler (sdk.py compile), disassembles the voice code,
and counts the instructions in each body's per-sample hardware loops (LSETUP).
Each loop is one part's pass over the block; counting every loop as running all
64 samples gives a body's worst block, every part ringing at once. Add the
render's own work per block (the motion, the impacts, the ceiling and the
silence check), counted the same way.

A count, not a measurement: it leaves out pipeline stalls and memory waits,
and counts a skipped part as running. Only `sdk.py profile` on an OP-XY gives
the figure a catalog record needs.

    python3 tools/static_cpu.py [--sdk <open-xy>] [--out <scratch folder>]
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PY = pathlib.Path.home() / "Documents" / "GitHub" / "open-xy" / ".venv" / "bin" / "python"
# Each body's per-sample loops and how many times a block each runs (a loop
# over a bank of square waves runs once per square; one over modes, once per mode).
parser = argparse.ArgumentParser()
parser.add_argument("--sdk", default=str(ROOT.parent.parent / "open-xy"))
parser.add_argument("--out", default=tempfile.mkdtemp(prefix="galileo-static-"))
args = parser.parse_args()
out = pathlib.Path(args.out) / "module"
subprocess.run([str(PY), "sdk.py", "compile", "--machine", str(ROOT), "--out", str(out), "--overwrite", "--profiling", "off"],
               cwd=args.sdk, check=True, capture_output=True)
objdump = pathlib.Path(args.sdk) / "local" / "toolchain" / "bin" / "bfin-elf-objdump"
text = subprocess.run([str(objdump), "-d", "-C", str(out / "galileo" / "voice.o")], capture_output=True, text=True, check=True).stdout

functions = {}
name = None
for line in text.splitlines():
    m = re.match(r"^[0-9a-f]+ <(.*)>:$", line)
    if m:
        name = m.group(1)
        functions[name] = []
        continue
    m = re.match(r"^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S.*)$", line)
    if name and m:
        functions[name].append((int(m.group(1), 16), m.group(2)))


def loops(code):
    out = []
    for addr, ins in code:
        m = re.match(r"LSETUP\(0x([0-9a-f]+) .*?, 0x([0-9a-f]+) .*?\) (LC[01])", ins)
        if m:
            a, b = int(m.group(1), 16), int(m.group(2), 16)
            body = [i for x, i in code if a <= x <= b]
            out.append((a, b, len(body), m.group(3)))
    return out


report = []
total_by_body = {}
for fn, code in functions.items():
    short = re.sub(r"\(.*", "", fn).replace("galileo::body::", "").replace("galileo::", "")
    if not short.startswith("run_") and short not in ("render",):
        continue
    ls = loops(code)
    straight = len(code)
    # Nested loops: an LC0 loop inside an LC1 loop's range runs once per outer pass.
    per_block = 0
    rows = []
    for a, b, n, lc in ls:
        outer = [o for o in ls if o[0] <= a and b <= o[1] and o != (a, b, n, lc)]
        times = 64
        if outer:
            # The inner loop runs per sample; the outer loop walks parts (squares, modes, partials).
            times = 64 * {"run_hat": 6, "run_ride": 8, "run_modes": 7, "run_skins": 2, "run_kick": 2,
                          "run_snare": 2, "run_hand": 2}.get(short, 3)
            per_block += n * times
            rows.append(f"    inner loop {n:3d} instructions x {times}")
        elif any(a <= o[0] and o[1] <= b for o in ls if o != (a, b, n, lc)):
            rows.append(f"    outer loop {n:3d} instructions (counted through its inner loop)")
        else:
            per_block += n * times
            rows.append(f"    loop {n:3d} instructions x 64")
    per_block += straight
    total_by_body[short] = per_block
    report.append(f"{short}: about {per_block} instructions a block, every part ringing ({straight} in the function)")
    report += rows
print("\n".join(report))
render = total_by_body.get("render", 0)
bodies = {k: v for k, v in total_by_body.items() if k.startswith("run_")}
worst = max(bodies, key=bodies.get)
print(f"render's own work, counted once: about {render} instructions a block (more per impact)")
print(f"worst body: {worst}, about {bodies[worst] + render} instructions a block with render")
