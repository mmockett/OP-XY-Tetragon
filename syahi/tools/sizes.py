#!/usr/bin/env python3
"""Measures Voice, Shared and DisplayState on the host and writes their sizes
into definition.json (the device is 32-bit and these hold only 32-bit words).

    python3 tools/sizes.py --sdk <open-xy checkout> [--cxx clang++]
"""
import argparse, json, pathlib, subprocess, tempfile
ROOT = pathlib.Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("--sdk", required=True)
ap.add_argument("--cxx", default="c++")
a = ap.parse_args()
with tempfile.TemporaryDirectory() as t:
    src = pathlib.Path(t) / "s.cpp"
    src.write_text('#include "engine.cpp"\n#include "display.cpp"\n#include <cstdio>\nint main(){std::printf("%zu %zu %zu", sizeof(syahi::Voice), sizeof(syahi::Shared), sizeof(syahi::DisplayState));}\n')
    subprocess.run([a.cxx, "-std=c++11", f"-I{pathlib.Path(a.sdk) / 'sdk/include'}", f"-I{ROOT}", str(src), "-o", f"{t}/s"], check=True)
    voice, shared, state = map(int, subprocess.run([f"{t}/s"], capture_output=True, text=True, check=True).stdout.split())
d = json.loads((ROOT / "definition.json").read_text())
r = d["resources"]
r["voiceBytes"], r["sharedBytes"], r["display"]["stateBytes"] = voice, shared, state
import re
art = (ROOT / "art.h").read_text()
r["display"]["nodes"] = int(re.search(r"nodes = (\d+);", art).group(1))
(ROOT / "definition.json").write_text(json.dumps(d, indent=2) + "\n")
print(f"voice {voice}, shared {shared}, display state {state}, nodes {r['display']['nodes']}")
