#!/usr/bin/env python3
"""GALILEO's host checks under more than one compiler. Apple clang at -O2
renders the calibration and the demos; g++ (CI's host compiler) and the
OP-XY's Blackfin GCC wrap a signed overflow, where clang may compute the
unwrapped value instead, so any overflow in the engine makes the device play
something other than what was calibrated (KICK's PUNCH did, until it was
rewritten). This step:

1. builds and runs each tests/*.cpp with clang -O2, g++ -O2, and clang with
   -fsanitize=address,undefined -fno-sanitize-recover=signed-integer-overflow,
   so a signed overflow fails the test;
2. renders a sweep through the real engine (tools/render.cpp) with each of the
   three builds and requires the renders to be bit-identical: the 24 default
   keys, every model with all controls at 0 and at 127 (and with its pads at
   their ends), and each control alone at 0 and at 127.

The SDK comes from --sdk or $GALILEO_SDK (default: ../open-xy beside this
repository). Needs no numpy. With no g++ found (macOS's g++ is clang), it says so and fails.

    python3 tools/compilers.py [--sdk <open-xy>] [--clang clang++] [--gxx g++-16]
"""
import argparse
import hashlib
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import catalog  # noqa: E402

SANITIZE = ["-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=signed-integer-overflow"]
GAP = 64 * 1034          # samples between notes on the sweep's one key (about 1.5 s)
CHUNK = 40               # scenarios a render


def find_gxx(given):
    names = [given] if given else ["g++-16", "g++-15", "g++-14", "g++-13", "g++"]
    for name in names:
        path = shutil.which(name)
        if not path:
            continue
        version = subprocess.run([path, "--version"], capture_output=True, text=True).stdout
        if "clang" not in version.lower():
            return path
    return None


def builds(clang, gxx):
    """name -> compiler command, its flags first."""
    gcc = [gxx]
    if platform.system() == "Darwin":
        sysroot = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
        if sysroot:
            gcc += ["-isysroot", sysroot]
    return {"clang": [clang, "-O2"], "g++": gcc + ["-O2"], "sanitized": [clang] + SANITIZE}


def compile_run(cmd, source, sdk, out, stdin=None):
    exe = out
    build = cmd + ["-std=c++11", "-Wall", "-Werror", f"-I{sdk / 'sdk' / 'include'}", f"-I{ROOT}",
                   str(source), str(ROOT / "tables.cpp"), "-o", str(exe)]
    r = subprocess.run(build, capture_output=True, text=True)
    if r.returncode:
        return None, f"does not compile:\n{r.stdout}{r.stderr}"
    env = dict(os.environ, UBSAN_OPTIONS="print_stacktrace=1", ASAN_OPTIONS="detect_leaks=0")
    if stdin is None:
        r = subprocess.run([str(exe)], capture_output=True, text=True, env=env, timeout=1800)
        return r, None
    return exe, None


def scenarios():
    """(label, model index, controls[8], space, feel)"""
    out = []
    for i, k in enumerate(catalog.DEFAULT_KIT):
        name = k[0]
        out.append((f"key{53 + i}", catalog.model_index(name), list(k[1]), k.pad.get("space", 0), k.pad.get("feel", 0)))
    for m, name in enumerate(catalog.NAMES):
        base = list(catalog.key(name)[1])
        out.append((f"{name}:all0", m, [0] * 8, 0, 0))
        out.append((f"{name}:all127", m, [127] * 8, 0, 0))
        out.append((f"{name}:all127:pads", m, [127] * 8, 15, 15))
        out.append((f"{name}:all0:pads", m, [0] * 8, -16, 15))
        for c in range(8):
            for v in (0, 127):
                vals = list(base)
                vals[c] = v
                out.append((f"{name}:c{c}={v}", m, vals, 0, 0))
    return out


def render(exe, chunk):
    lines = [f"length {GAP * len(chunk)}", "bpm 1200"]
    for j, (_label, m, vals, space, feel) in enumerate(chunk):
        lines.append(f"note {j * GAP} 0 {m} {' '.join(str(v) for v in vals)} {space} {feel} 0")
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    r = subprocess.run([str(exe)], input="\n".join(lines).encode(), capture_output=True, env=env, timeout=1800)
    if r.returncode:
        raise SystemExit(f"FAIL {exe.name}: exit {r.returncode}\n{r.stderr.decode(errors='replace')[-3000:]}")
    data = r.stdout
    step = GAP * 8   # stereo int32
    return [hashlib.sha256(data[j * step:(j + 1) * step]).hexdigest() for j in range(len(chunk))], \
        r.stderr.decode(errors="replace").count("runtime error")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", default=os.environ.get("GALILEO_SDK", str(ROOT.parent.parent / "open-xy")))
    ap.add_argument("--clang", default=os.environ.get("CLANG", "clang++"))
    ap.add_argument("--gxx", default=os.environ.get("GXX"))
    a = ap.parse_args()
    sdk = pathlib.Path(a.sdk).resolve()
    gxx = find_gxx(a.gxx)
    if not gxx:
        raise SystemExit("FAIL no g++ found (macOS's g++ is clang): brew install gcc, or pass --gxx")
    failures = 0
    work = pathlib.Path(tempfile.mkdtemp(prefix="galileo-compilers-"))
    cmds = builds(a.clang, gxx)
    print(f"clang: {shutil.which(a.clang) or a.clang}; g++: {gxx}")
    # 1. the kit's own tests, under each build
    outputs = {}
    for test in sorted((ROOT / "tests").glob("*.cpp")):
        for name, cmd in cmds.items():
            r, err = compile_run(cmd, test, sdk, work / f"{test.stem}-{name}")
            if err:
                print(f"FAIL {test.name} ({name}) {err}")
                failures += 1
                continue
            reports = r.stdout.count("runtime error") + r.stderr.count("runtime error")
            ok = r.returncode == 0
            print(f"{'ok  ' if ok else 'FAIL'} {test.name} ({name})" + (f": {reports} sanitizer reports" if reports else ""))
            if not ok:
                print((r.stdout + r.stderr)[-3000:])
                failures += 1
            outputs.setdefault(test.name, {})[name] = r.stdout
        same = len(set(outputs.get(test.name, {}).values())) <= 1
        if not same:
            print(f"FAIL {test.name}: its output differs between builds")
            failures += 1
    # 2. the sweep, bit for bit
    exes = {}
    for name, cmd in cmds.items():
        exe, err = compile_run(cmd, HERE / "render.cpp", sdk, work / f"render-{name}", stdin=True)
        if err:
            raise SystemExit(f"FAIL tools/render.cpp ({name}) {err}")
        exes[name] = exe
    sweep = scenarios()
    hashes = {name: [] for name in exes}
    reports = 0
    for i in range(0, len(sweep), CHUNK):
        chunk = sweep[i:i + CHUNK]
        for name, exe in exes.items():
            h, n = render(exe, chunk)
            hashes[name] += h
            reports += n
    differ = {name: [sweep[j][0] for j in range(len(sweep)) if hashes[name][j] != hashes["clang"][j]]
              for name in exes if name != "clang"}
    for name, labels in differ.items():
        if labels:
            failures += 1
            print(f"FAIL sweep: {name} differs from clang -O2 in {len(labels)} of {len(sweep)} renders: "
                  + ", ".join(labels[:12]) + (" ..." if len(labels) > 12 else ""))
        else:
            print(f"ok   sweep: {name} renders all {len(sweep)} bit-identical to clang -O2")
    if reports:
        print(f"note: {reports} recoverable sanitizer reports in the sweep (not signed overflow)")
    shutil.rmtree(work, ignore_errors=True)
    if failures:
        raise SystemExit(f"FAIL {failures} checks")
    print("PASS")


if __name__ == "__main__":
    main()
