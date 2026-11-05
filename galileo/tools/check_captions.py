#!/usr/bin/env python3
"""Measures every caption GALILEO's header can show (tools/captions.cpp) in the
device's font, Univers Next Pro Light, with the suite's tools/caption_width.py:
values at 20 px within 53 px, titles (lower case, as the header draws them) at
10 px within 56 px. Exit status 1 when anything is too wide.

    XY_FONT=<UniversNextPro-Light.ttf> python3 tools/check_captions.py [--sdk <open-xy>] [--ruler <caption_width.py>]
"""
import argparse
import importlib.util
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--sdk", default=str(ROOT.parent.parent / "open-xy"))
parser.add_argument("--ruler", default=os.environ.get("CAPTION_WIDTH", str(ROOT.parent / "tools" / "caption_width.py")))
args = parser.parse_args()
spec = importlib.util.spec_from_file_location("caption_width", args.ruler)
ruler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ruler)
work = pathlib.Path(tempfile.mkdtemp(prefix="galileo-captions-"))
subprocess.run(["c++", "-std=c++11", "-O2", f"-I{args.sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools" / "captions.cpp"),
                str(ROOT / "tables.cpp"), "-o", str(work / "captions")], check=True)
lines = subprocess.run([str(work / "captions")], capture_output=True, text=True, check=True).stdout.splitlines()
titles = sorted({l[7:].lower() for l in lines if l.startswith("title: ")})
values = sorted({l for l in lines if l and not l.startswith("title: ")})
wide = 0
widest_value = max(values, key=lambda t: ruler.width(t, ruler.VALUE_PX))
widest_title = max(titles, key=lambda t: ruler.width(t, ruler.TITLE_PX))
LAST = {"-30dB"}   # LEVEL is the last column: 55 px to the screen's edge
for t in values:
    w = ruler.width(t, ruler.VALUE_PX)
    if w > (55 if t in LAST else ruler.VALUE_ROOM):
        wide += 1
        print(f"WIDE value {w:5.1f}/{ruler.VALUE_ROOM} px  {t!r}")
for t in titles:
    w = ruler.width(t, ruler.TITLE_PX)
    if w > ruler.TITLE_ROOM:
        wide += 1
        print(f"WIDE title {w:5.1f}/{ruler.TITLE_ROOM} px  {t!r}")
print(f"{len(values)} values, widest {widest_value!r} {ruler.width(widest_value, ruler.VALUE_PX):.1f} px; "
      f"{len(titles)} titles, widest {widest_title!r} {ruler.width(widest_title, ruler.TITLE_PX):.1f} px; {wide} too wide")
sys.exit(1 if wide else 0)
