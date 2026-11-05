#!/usr/bin/env python3
"""Measures GALILEO's screen churn from its real display code (tools/frames.cpp):
the nodes that change each frame while a run plays, at 30 and 60 frames a
second, for the default keys and the heaviest motion; and that a screen at
rest changes nothing. A development check beside tests/screen.cpp, which
asserts the same budgets.

    python3 tools/check_screen.py [--sdk <open-xy>]
"""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import catalog  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--sdk", default=str(ROOT.parent.parent / "open-xy"))
args = parser.parse_args()
work = pathlib.Path(tempfile.mkdtemp(prefix="galileo-churn-"))
subprocess.run(["c++", "-std=c++11", "-O2", f"-I{args.sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools" / "frames.cpp"),
                str(ROOT / "tables.cpp"), "-o", str(work / "frames")], check=True)
T = catalog.TILT
cases = [(f"key {53 + k} {m}", m, v) for k, (m, v) in enumerate(catalog.DEFAULT_KIT)]
cases += [
    ("press roll: 16 frets at 1/64", "SNARE", catalog.key("SNARE", frets=127, tilt=T["1/64"])[1]),
    ("shaker: 24 balls", "SHAKER", catalog.key("SHAKER", balls=127, frets=catalog.frets(1), tilt=T["1/8"])[1]),
    ("worst: 16 frets 1/64, 24 balls, full bounce", "SNARE", catalog.key("SNARE", frets=127, tilt=T["1/64"], balls=127, bounce=127)[1]),
    ("worst, tilt 0: 16 frets 1/4, 24 balls, full bounce", "RIDE", catalog.key("RIDE", frets=127, tilt=0, balls=127, bounce=127)[1]),
]
rows = []
for fps in (30, 60):
    for name, model, values in cases:
        script = "\n".join([f"scene x", f"model {catalog.model_index(model)}", "slots " + " ".join(map(str, values)), "row 1",
                            "run 0.2 60", "note", f"churn 4 {fps}", f"churn 2 {fps}"])
        out = subprocess.run([str(work / "frames")], input=script.encode(), capture_output=True, check=True).stdout.decode()
        runs = [json.loads(l)["churn"] for l in out.splitlines()]
        moving = [c for c in runs[0] if c] or [0]
        rows.append((fps, name, sum(moving) / len(moving), max(runs[0]), max(runs[1])))
worst_mean = max(r[2] for r in rows)
worst_peak = max(r[3] for r in rows)
for fps, name, mean, peak, rest in rows:
    print(f"{fps} fps  mean {mean:5.1f}  peak {peak:3d}  after rest {rest}  {name}")
print(f"worst mean {worst_mean:.1f}, worst peak {worst_peak}")
