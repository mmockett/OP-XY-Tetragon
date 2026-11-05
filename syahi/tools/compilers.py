#!/usr/bin/env python3
"""SYAHI's host checks under more than one compiler. Apple clang at -O2
renders the calibration and the demos; g++ (CI's host compiler) and the
OP-XY's Blackfin GCC wrap a signed overflow, where clang may compute the
unwrapped value instead, so an overflow in the engine can make the device
play something other than what was calibrated. This step:

1. builds and runs each tests/*.cpp with clang -O2, g++ -O2, and clang with
   -fsanitize=address,undefined -fno-sanitize-recover=signed-integer-overflow,
   so a signed overflow fails the test, and requires the three to print the
   same;
2. renders a sweep through the real engine (tools/render.cpp) with each of the
   three builds and requires the renders to be bit-identical: every model with
   all controls at 0, at 64 and at 127 (and its pads at their ends), and each
   control alone at 0 and at 127 from 64.

tools/ubsan-ignore.txt lists the overflows kept on purpose: host and device
wrap them alike, and the calibrated sound includes the wrap. The sanitizer
skips the signed-overflow check only in the functions named there.

The SDK checkout comes from --sdk, or $SYAHI_SDK. Needs no numpy. With no g++ found (macOS's g++ is clang), it says so and fails.

    python3 tools/compilers.py [--sdk <open-xy>] [--clang clang++] [--gxx g++-16]
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
IGNORE = HERE / "ubsan-ignore.txt"
SANITIZE = ["-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=signed-integer-overflow"] + ([f"-fsanitize-ignorelist={IGNORE}"] if IGNORE.exists() else [])
MODELS = json.loads((ROOT / "definition.json").read_text())["resources"]["modelCount"]
SOURCES = json.loads((ROOT / "machine.json").read_text()).get("sources", [])
GAP = 64 * 1034          # samples a scenario (about 1.5 s)
CHUNK = 40


def find_gxx(given):
    for name in ([given] if given else ["g++-16", "g++-15", "g++-14", "g++-13", "g++"]):
        path = shutil.which(name)
        if path and "clang" not in subprocess.run([path, "--version"], capture_output=True, text=True).stdout.lower():
            return path
    return None


def builds(clang, gxx):
    gcc = [gxx]
    if platform.system() == "Darwin":
        sysroot = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
        if sysroot:
            gcc += ["-isysroot", sysroot]
    return {"clang": [clang, "-O2"], "g++": gcc + ["-O2"], "sanitized": [clang] + SANITIZE}


def build(cmd, source, sdk, exe):
    r = subprocess.run(cmd + ["-std=c++11", "-Wall", "-Werror", f"-I{sdk / 'sdk' / 'include'}", f"-I{ROOT}", str(source)]
                       + [str(ROOT / s) for s in SOURCES] + ["-o", str(exe)], capture_output=True, text=True)
    return None if r.returncode == 0 else f"does not compile:\n{r.stdout}{r.stderr}"


ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="print_stacktrace=1")


def scenarios():
    """(label, model, controls[8], pad0, pad1)"""
    out = []
    for m in range(MODELS):
        out.append((f"model{m}:all0", m, [0] * 8, 0, 0))
        out.append((f"model{m}:all64", m, [64] * 8, 0, 0))
        out.append((f"model{m}:all127", m, [127] * 8, 0, 0))
        out.append((f"model{m}:all127:pads", m, [127] * 8, 15, 15))
        out.append((f"model{m}:all0:pads", m, [0] * 8, 0, -16))
        for c in range(8):
            for v in (0, 127):
                vals = [64] * 8
                vals[c] = v
                out.append((f"model{m}:c{c}={v}", m, vals, 0, 0))
    return out


def render(exe, chunk, work):
    """Each scenario on one key, GAP apart (a note chokes the last), mono; the bytes of each."""
    args = []
    for j, (_label, m, vals, human, grit) in enumerate(chunk):
        args += [str(m)] + [str(v) for v in vals] + [str(human), str(grit), repr(j * GAP / 44100), "1", "0"]
    wav = work / "sweep.wav"
    r = subprocess.run([str(exe), str(wav), repr(len(chunk) * GAP / 44100)] + args, capture_output=True, env=ENV, timeout=1800)
    if r.returncode:
        raise SystemExit(f"FAIL {exe.name}: exit {r.returncode}\n{r.stderr.decode(errors='replace')[-3000:]}")
    data = wav.read_bytes()[44:]
    return [data[j * GAP * 4:(j + 1) * GAP * 4] for j in range(len(chunk))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", default=os.environ.get("SYAHI_SDK"), required="SYAHI_SDK" not in os.environ)
    ap.add_argument("--clang", default=os.environ.get("CLANG", "clang++"))
    ap.add_argument("--gxx", default=os.environ.get("GXX"))
    a = ap.parse_args()
    sdk = pathlib.Path(a.sdk).resolve()
    gxx = find_gxx(a.gxx)
    if not gxx:
        raise SystemExit("FAIL no g++ found (macOS's g++ is clang): brew install gcc, or pass --gxx")
    failures = 0
    work = pathlib.Path(tempfile.mkdtemp(prefix="syahi-compilers-"))
    cmds = builds(a.clang, gxx)
    print(f"clang: {shutil.which(a.clang) or a.clang}; g++: {gxx}")
    for test in sorted((ROOT / "tests").glob("*.cpp")):
        printed = {}
        for name, cmd in cmds.items():
            exe = work / f"{test.stem}-{name}"
            err = build(cmd, test, sdk, exe)
            if err:
                print(f"FAIL {test.name} ({name}) {err}")
                failures += 1
                continue
            r = subprocess.run([str(exe)], capture_output=True, text=True, env=ENV, timeout=1800)
            reports = (r.stdout + r.stderr).count("runtime error")
            print(f"{'ok  ' if r.returncode == 0 else 'FAIL'} {test.name} ({name})" + (f": {reports} recoverable sanitizer reports" if reports and r.returncode == 0 else ""))
            if r.returncode:
                print((r.stdout + r.stderr)[-3000:])
                failures += 1
            printed[name] = r.stdout
        if len(set(printed.values())) > 1:
            print(f"FAIL {test.name}: its output differs between builds")
            failures += 1
    exes = {}
    for name, cmd in cmds.items():
        exe = work / f"render-{name}"
        err = build(cmd, HERE / "render.cpp", sdk, exe)
        if err:
            raise SystemExit(f"FAIL tools/render.cpp ({name}) {err}")
        exes[name] = exe
    sweep = scenarios()
    hashes = {name: [] for name in exes}
    for i in range(0, len(sweep), CHUNK):
        for name, exe in exes.items():
            hashes[name] += [hashlib.sha256(b).hexdigest() for b in render(exe, sweep[i:i + CHUNK], work)]
    for name in exes:
        if name == "clang":
            continue
        labels = [sweep[j][0] for j in range(len(sweep)) if hashes[name][j] != hashes["clang"][j]]
        if labels:
            failures += 1
            print(f"FAIL sweep: {name} differs from clang -O2 in {len(labels)} of {len(sweep)} renders: "
                  + ", ".join(labels[:12]) + (" ..." if len(labels) > 12 else ""))
        else:
            print(f"ok   sweep: {name} renders all {len(sweep)} bit-identical to clang -O2")
    shutil.rmtree(work, ignore_errors=True)
    if failures:
        raise SystemExit(f"FAIL {failures} checks")
    print("PASS")


if __name__ == "__main__":
    main()
