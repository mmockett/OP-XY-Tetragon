#!/usr/bin/env python3
"""Sweeps every control of every model through its travel, with the others at
the model's defaults, through the real engine, and measures how much each
step changes the sound: level, spectrum and time. Flags a control whose sound barely moves over more than a
fifth of its travel, or across its whole travel.

    open-xy/.venv/bin/python -I tools/sweep.py [--json out.json] [MODEL ...]

The distance between two renders is in dB: the RMS of their loudness envelopes'
difference (2 ms frames for the first 100 ms, then 10 ms), half the RMS of their
sixth-octave spectra's differences over the strike (12 ms), the attack (60 ms)
and the body (to 2 s), and their level difference.
"""
import argparse
import json
import math
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import catalog  # noqa: E402
import engine_host as E  # noqa: E402

STEPS = [0, 16, 32, 48, 64, 80, 96, 112, 127]
FLAT = 1.5          # dB: under this between two steps an eighth apart is "flat"
SECONDS = 3.5


def bands(y, floor=-70):
    """Energy in sixth-octave bands, 40 Hz .. 16 kHz, dB under the loudest."""
    if len(y) < 64:
        return np.full(52, float(floor))
    Y = np.abs(np.fft.rfft(y * np.hanning(len(y)), n=max(len(y), 8192))) ** 2
    f = np.fft.rfftfreq(max(len(y), 8192), 1 / E.SR)
    out = []
    for k in range(52):
        lo = 40 * 2 ** (k / 6)
        out.append(10 * math.log10(Y[(f >= lo) & (f < lo * 2 ** (1 / 6))].sum() + 1e-30))
    out = np.array(out)
    return np.maximum(out - out.max(), floor)


def features(y):
    peak = float(np.abs(y).max()) + 1e-9

    def env(seg, ms):
        n = int(ms / 1000 * E.SR)
        frames = len(seg) // n
        e = np.sqrt((seg[:frames * n].reshape(frames, n) ** 2).mean(1) + 1e-20)
        return np.maximum(20 * np.log10(e / peak), -60)
    attack = int(0.06 * E.SR)
    envelope = np.concatenate([env(y[:int(0.1 * E.SR)], 2), env(y[int(0.1 * E.SR):], 10)])
    rms = 20 * math.log10(math.sqrt(float((y ** 2).mean())) + 1e-12)
    return envelope, bands(y[:attack]), bands(y[attack:int(2 * E.SR)]), rms, bands(y[:int(0.012 * E.SR)])


def distance(a, b):
    ea, aa, ba, ra, ta = a
    eb, ab, bb, rb, tb = b
    return float(np.sqrt(((ea - eb) ** 2).mean()) + 0.5 * np.sqrt(((aa - ab) ** 2).mean()) +
                 0.5 * np.sqrt(((ba - bb) ** 2).mean()) + 0.5 * np.sqrt(((ta - tb) ** 2).mean()) + abs(ra - rb))


def sweep(model):
    m = catalog.MODELS[catalog.model_index(model)]
    labels = m[2] + catalog.MOTION
    base = E.defaults(model)
    report = {}
    for slot, label in enumerate(labels):
        feats = []
        for v in STEPS:
            values = list(base)
            values[slot] = v
            feats.append(features(E.one(model, values, seconds=SECONDS)))
        steps = [distance(feats[i], feats[i + 1]) for i in range(len(STEPS) - 1)]
        total = distance(feats[0], feats[-1])
        # Flat: two neighbouring steps (a quarter of the travel) both under FLAT.
        flat = [i for i in range(len(steps) - 1) if steps[i] < FLAT and steps[i + 1] < FLAT]
        report[label] = {"steps": [round(s, 1) for s in steps], "total": round(total, 1), "flat": flat}
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("models", nargs="*")
    parser.add_argument("--json")
    args = parser.parse_args()
    out = {}
    problems = 0
    for model in args.models or catalog.NAMES:
        out[model] = sweep(model)
        for label, r in out[model].items():
            bad = r["flat"] or r["total"] < 6
            problems += bool(bad)
            print(f"{model:8s} {label:7s} total {r['total']:6.1f}  steps {' '.join(f'{s:5.1f}' for s in r['steps'])}"
                  + ("   <- FLAT at " + ",".join(str(STEPS[i]) for i in r["flat"]) if r["flat"] else "")
                  + ("   <- SMALL" if r["total"] < 6 else ""))
    print(f"{problems} controls flagged")
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(out, indent=1))
