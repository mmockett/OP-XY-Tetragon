#!/usr/bin/env python3
"""Compares the engine with the prototype the concept was heard on (its proto.py,
given with --proto): the same metrics on each model's default note, and on a
few motion variants. A development check; not part of the machine.

    open-xy/.venv/bin/python -I tools/compare_proto.py --proto <C-motion folder>
"""
import argparse
import importlib.util
import math
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import catalog  # noqa: E402
import engine_host as E  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument("--proto", required=True)
parser.add_argument("--wav", help="write a WAV alternating prototype and engine notes")
args = parser.parse_args()
spec = importlib.util.spec_from_file_location("proto", pathlib.Path(args.proto) / "proto.py")
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)


def bands(y):
    """Energy in octave bands, dB, of the first 0.6 s."""
    y = y[:int(0.6 * E.SR)]
    Y = np.abs(np.fft.rfft(y * np.hanning(len(y)))) ** 2
    f = np.fft.rfftfreq(len(y), 1 / E.SR)
    out = []
    for lo in [31.25 * 2 ** k for k in range(9)]:
        sel = (f >= lo) & (f < 2 * lo)
        out.append(10 * math.log10(Y[sel].sum() + 1e-20))
    out = np.array(out)
    return out - out.max()


def t60(y):
    a = np.abs(y)
    env = np.convolve(a, np.ones(441) / 441, "same")
    pk = env.max()
    idx = np.nonzero(env > pk * 1e-2)[0]
    return idx[-1] / E.SR if len(idx) else 0


cases = [(m, {}, None) for m in catalog.NAMES]
cases += [("SNARE", {"frets": 75, "tilt": 68}, {"grid": "lock"}), ("KICK", {"bounce": 46, "tilt": 45}, {"grid": "lock"}),
          ("HAT", {"frets": 25, "tilt": 68}, {"grid": "lock"}), ("SHAKER", {"balls": 70, "frets": 6, "tilt": 15}, {"grid": "lock"})]
pieces = []
print(f"{'case':28s} {'cen p/e':>13s} {'crest p/e':>11s} {'t40 p/e':>11s}  band diff (dB, 31 Hz..8 kHz octaves)")
for model, over, pad in cases:
    yp, _ = P.voice(model, pad, **over)
    m = catalog.MODELS[catalog.model_index(model)]
    labels = m[2] + catalog.MOTION
    pc = P.controls(model, **over)
    values = [min(127, round(pc[l] * 1.27)) for l in labels]
    feel = 0 if (pad and pad.get("grid") == "lock") else 15
    ye = E.one(model, values, feel=feel, seconds=max(2.0, len(yp) / E.SR + 0.5))
    mp, me = P.metrics(yp), P.metrics(ye)
    d = bands(ye) - bands(yp)
    name = model + ("" if not over else " " + ",".join(f"{k}{v}" for k, v in over.items()))
    print(f"{name:28s} {mp['centroid_hz']:6d}/{me['centroid_hz']:<6d} {mp['crest_db']:5.1f}/{me['crest_db']:<5.1f} "
          f"{t60(yp):5.2f}/{t60(ye):<5.2f}  " + " ".join(f"{x:+4.0f}" for x in d))
    if args.wav:
        pieces += [yp / (np.abs(yp).max() + 1e-12) * 0.5, np.zeros(int(0.3 * E.SR)), ye / (np.abs(ye).max() + 1e-12) * 0.5,
                   np.zeros(int(0.6 * E.SR))]
if args.wav:
    E.write_wav(args.wav, np.concatenate(pieces))
