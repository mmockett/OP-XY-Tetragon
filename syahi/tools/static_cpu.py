#!/usr/bin/env python3
"""A static estimate of Syahi's cost on the Blackfin: compiles each of the
voice's per-sample loops on its own with the SDK's pinned compiler and flags,
counts the instructions in each loop's body, and adds them up into a block
for each model's busiest moment (a strike: every pass that model runs, every
mode sounding). One instruction a cycle is assumed; stalls add to that, so
read it as a floor, not a measurement.

    python3 tools/static_cpu.py --sdk <open-xy checkout> --toolchain <dir with bfin-elf-g++, bfin-elf-objdump>
"""
import argparse, pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROBE = r'''
#include "engine.cpp"
using namespace syahi;
#define PROBE extern "C" __attribute__((noinline))
PROBE void probe_mode(Partial& m, i32* out, const i32* r) { run(m, out, r, 0, 64, false); }
PROBE void probe_ramp(Partial& m, i32* out, const i32* r) { run(m, out, r, 0, 64, true); }
PROBE void probe_finish(Voice& v, const i32* in, i32* out) { finish(v, in, out); }
PROBE void probe_metal(Partial& m, i32* out, i32* sines, const i32* r) { run_metal(m, out, sines, r, 0, 64, true); }
PROBE void probe_wash(Core& v, const Scratch& s, i32* out) { wash_pass(v, s, out); }
PROBE void probe_noise(Core& v, const Strikes& s, i32* out) { noise_pass(v, s, out); }
PROBE void probe_rattle(Core& v, i32* out) { rattle_pass(v, out); }
PROBE void probe_plan(Core& v, Partial& m) { plan_mode(v, m, 1, 1 << 30, 0); }
PROBE void probe_render(Voice& v, Shared& s, i32* out, const i32* p) { opxy::u32 pitch[64]; render(v, s, 1, out, p, pitch); }
'''
FLAGS = ['-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
         '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables',
         '-fno-unwind-tables', '-fno-common', '-fno-tree-loop-distribute-patterns', '-fomit-frame-pointer',
         '-nostdinc++', '-w']


def functions(dis):
    out, name = {}, None
    for line in dis.splitlines():
        m = re.match(r'^[0-9a-f]+ <_?(\w+)>:$', line)
        if m:
            name = m.group(1)
            out[name] = []
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.*)$', line)
        if m and name:
            out[name].append((int(m.group(1), 16), m.group(2)))
    return out


def inner_loop(insns):
    """The innermost loop's body: the smallest hardware loop, or else the
    smallest backward branch's span, that holds a multiply or a table load."""
    loops = []
    for a, t in insns:
        m = re.search(r'LSETUP\s*\(0x([0-9a-f]+)[^,]*, 0x([0-9a-f]+)', t)
        if m:
            loops.append((int(m.group(1), 16), int(m.group(2), 16)))
        m = re.search(r'JUMP(?:\.[SL])? 0x([0-9a-f]+)', t)
        if m and int(m.group(1), 16) < a:
            loops.append((int(m.group(1), 16), a))
    best = None
    for s, e in loops:
        body = [t for a, t in insns if s <= a <= e]
        if any('*' in t for t in body) and (best is None or len(body) < len(best)):
            best = body
    return len(best or [])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--toolchain", required=True)
    a = ap.parse_args()
    tc = pathlib.Path(a.toolchain)
    inc = pathlib.Path(a.sdk) / "sdk" / "include"
    with tempfile.TemporaryDirectory() as t:
        src = pathlib.Path(t) / "probe.cpp"
        src.write_text(PROBE)
        obj = pathlib.Path(t) / "probe.o"
        asm = pathlib.Path(t) / "probe.s"
        subprocess.run([str(tc / "bfin-elf-g++"), *FLAGS, f"-I{inc}", f"-I{ROOT}", "-S", str(src), "-o", str(asm)], check=True)
        subprocess.run([str(tc / "bfin-elf-as"), "-mcpu=bf707", "-o", str(obj), str(asm)], check=True)
        dis = subprocess.run([str(tc / "bfin-elf-objdump"), "-d", str(obj)], capture_output=True, text=True, check=True).stdout
    f = functions(dis)
    loop = {k: inner_loop(f[f"probe_{k}"]) for k in ("mode", "metal", "wash", "noise", "rattle", "finish")}
    plan = len(f["probe_plan"])
    final = loop["finish"]
    print("per-sample loop bodies (Blackfin instructions):", ", ".join(f"{k} {v}" for k, v in loop.items()))
    print(f"a mode's plan, straight through: {plan} (the GRIT-off path of the last loop counted)")
    # A strike's block for each model: the passes it runs and its partials (8 modes + body/air where used).
    models = {
        "BOOM": dict(modes=8, extra=2, noise=1), "SNARE": dict(modes=8, noise=1, rattle=1), "TOM": dict(modes=8, extra=1),
        "TABLA": dict(modes=8, extra=1, noise=1), "BAR": dict(modes=8), "METAL": dict(modes=8, metal=1, wash=1, rattle=1, noise=1),
        "BOUNCE": dict(modes=8, noise=1), "WIRE": dict(modes=8, noise=1)}
    fixed = 8 * plan + 600   # the plans and the block's bookkeeping
    print(f"{'model':7} {'strike block':>12} {'ringing block':>13}  (instructions; % of the 580,352-cycle block at one a cycle)")
    for name, m in models.items():
        modes = m["modes"] * 64 * (loop["metal"] if m.get("metal") else loop["mode"])
        ring = modes
        extra = m.get("extra", 0) * 64 * loop["mode"]
        passes = 64 * (loop["wash"] * m.get("wash", 0) + loop["rattle"] * m.get("rattle", 0))
        strike = fixed + modes + extra + passes + 64 * loop["noise"] * m.get("noise", 0) + 64 * final
        ringing = fixed + ring + extra + passes + 64 * final
        print(f"{name:7} {strike:12,.0f} {ringing:13,.0f}   {100 * strike / 580352:4.1f}% / {100 * ringing / 580352:4.1f}%")


if __name__ == "__main__":
    main()
