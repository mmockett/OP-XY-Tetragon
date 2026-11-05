#!/usr/bin/env python3
"""Sets GALILEO's levels, measured through the real engine, then regenerates
kit.h (tools/make_kit.py). Run with the SDK's Python:

    open-xy/.venv/bin/python -I tools/calibrate.py [--rounds 3]

- tools/levels.json, each model's output gain: its default key (tools/catalog.py,
  motion row included) sounds at its role's loudness, catalog.TARGET: the
  loudest 400 ms, K-weighted (LUFS-M max), in dB re the SDK's half-scale
  ceiling, the mean of three strokes.
- tools/trims.json, each body control's loudness trim at 0, 32, 64, 96 and 127
  (the other controls at their defaults): the hit kept at its default's
  loudness wherever the control stands, 0 at the default itself.

Both are held lower wherever the hit's sample peak would pass the engine's soft
knee (0.42 of full scale) or its true peak -1 dB under the ceiling, so nothing
is limited to reach its level: a control whose hit is short and peaky sounds
quieter rather than squashed.
"""
import argparse
import json
import math
import pathlib
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import catalog  # noqa: E402
import engine_host as E  # noqa: E402

levels_path = HERE / "levels.json"
trims_path = HERE / "trims.json"
KNEE_DB = 20 * math.log10(0.42 / 0.5)    # engine.cpp ceiling(), re the half-scale ceiling
POINTS = [0, 32, 64, 96, 127]
OCTAVE_DB = 20 * math.log10(2)


def measure(job):
    model, values = job
    ys = E.hits(model, values)
    loud = float(np.mean([E.momentary_max(y) for y in ys]))
    peak = max(20 * math.log10(max(float(np.abs(y).max()), 1e-9) * 2) for y in ys)
    tp = max(E.true_peak(y) for y in ys)
    return loud, peak, tp


def headroom(peak, tp):
    return min(KNEE_DB - 0.3 - peak, catalog.TRUE_PEAK - tp)


def regenerate():
    subprocess.run([sys.executable, str(HERE / "make_kit.py")], check=True, capture_output=True)
    E.build()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=3)
    a = ap.parse_args()
    trims = json.loads(trims_path.read_text()) if trims_path.exists() else {}
    trims = {n: [[float(v) for v in row] for row in trims.get(n, [[0] * 5] * 4)] for n in catalog.NAMES}
    pool = ThreadPoolExecutor(8)
    for r in range(a.rounds):
        # the models' gains, at their defaults
        regenerate()
        levels = json.loads(levels_path.read_text()) if levels_path.exists() else {}
        base = dict(zip(catalog.NAMES, pool.map(measure, [(n, catalog.key(n)[1]) for n in catalog.NAMES])))
        # the default kit's own keys of each model must stay under the knee too
        kit_keys = [(catalog.NAMES[k[0]] if isinstance(k[0], int) else k[0], k[1]) for k in catalog.DEFAULT_KIT]
        kit_measured = list(pool.map(measure, kit_keys))
        out, worst = {}, 0.0
        for name in catalog.NAMES:
            loud, peak, tp = base[name]
            target = catalog.TARGET[catalog.ROLE[name]]
            room = min([headroom(peak, tp)] + [headroom(m[1], m[2]) for (n, _v), m in zip(kit_keys, kit_measured) if n == name])
            step = min(target - loud, room)
            out[name] = int(round(levels.get(name, 1 << 26) * 10 ** (step / 20)))
            worst = max(worst, abs(step))
            print(f"round {r}: {name:8s} peak {peak:5.1f} tp {tp:5.1f} loudness {loud:6.1f} (target {target:6.1f})"
                  f"{'  held by its peak' if room < target - loud else ''}")
        levels_path.write_text(json.dumps(out, indent=1) + "\n")
        regenerate()
        # each body control's trim, against its model's default
        base = dict(zip(catalog.NAMES, pool.map(measure, [(n, catalog.key(n)[1]) for n in catalog.NAMES])))
        jobs, where = [], []
        for name in catalog.NAMES:
            d = list(catalog.key(name)[1])
            for s in range(4):
                for j, p in enumerate(POINTS):
                    v = list(d)
                    v[s] = p
                    jobs.append((name, v))
                    where.append((name, s, j))
        for (name, s, j), (loud, peak, tp) in zip(where, pool.map(measure, jobs)):
            want = min(base[name][0] - loud, headroom(peak, tp))
            trims[name][s][j] = float(np.clip(trims[name][s][j] + want / OCTAVE_DB * 32, -64, 32))
        for name in catalog.NAMES:
            d = catalog.key(name)[1]
            for s in range(4):
                t = trims[name][s]
                at = d[s] / 127 * 4
                i = min(int(at), 3)
                z = t[i] + (t[i + 1] - t[i]) * (at - i)
                trims[name][s] = [v - z for v in t]
        trims_path.write_text(json.dumps({n: [[int(round(v)) for v in row] for row in trims[n]] for n in catalog.NAMES}, indent=1) + "\n")
        print(f"round {r}: worst gain step {worst:.2f} dB; trims", {n: [[int(round(v)) for v in row] for row in trims[n]] for n in catalog.NAMES})
    regenerate()


if __name__ == "__main__":
    main()
