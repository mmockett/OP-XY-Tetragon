#!/usr/bin/env python3
"""Sweeps every control of every model through 0..127 in steps of 16 (and 127)
with the others at the model's defaults, and prints how much each step changes
the sound: the mean change, in dB, of its level in 1/3-octave bands over
three windows (0-50 ms, 50-300 ms, 300-1500 ms). A step whose largest tenth of band changes (within 50 dB of the sound)
averages under 1 dB is flagged as flat.

    SYAHI_RENDER=<render> python -I tools/sweep.py [MODEL ...]
"""
import sys, pathlib
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import compare, kits  # noqa: E402

SR = 44100
STEPS = [0, 16, 32, 48, 64, 80, 96, 112, 127]


def feature(y):
    out = []
    for a, b in ((0, .05), (.05, .3), (.3, 1.5)):
        s = y[int(a * SR):int(b * SR)]
        S = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2
        f = np.fft.rfftfreq(len(s), 1 / SR)
        edges = 25 * 2 ** (np.arange(0, 30) / 3)
        e = np.array([S[(f >= lo) & (f < hi)].sum() for lo, hi in zip(edges[:-1], edges[1:])])
        out.append(10 * np.log10(e + 1e-14))
    return np.concatenate(out)


def change(a, b):
    """The mean of the largest tenth of the band changes, dB, counting only bands
    within 50 dB of the loudest band of their window in either sound: a change
    far under the sound is not heard."""
    a, b = np.asarray(a).reshape(3, -1), np.asarray(b).reshape(3, -1)
    d = []
    for wa, wb in zip(a, b):
        floor = max(wa.max(), wb.max()) - 50
        m = (wa > floor) | (wb > floor)
        d += list(np.abs(np.maximum(wa, floor)[m] - np.maximum(wb, floor)[m]))
    d = np.sort(d)[::-1]
    return float(np.mean(d[:max(1, len(d) // 10)])) if len(d) else 0.0


def main():
    models = sys.argv[1:] or kits.MODELS
    flat = 0
    for model in models:
        for slot, label in enumerate(kits.LABELS[model]):
            feats = []
            for v in STEPS:
                c = list(kits.DEFAULTS[model])
                c[slot] = v
                feats.append(feature(compare.engine(model, c, seconds=1.6)))
            d = [change(feats[i], feats[i + 1]) for i in range(len(STEPS) - 1)]
            bad = [STEPS[i] for i, x in enumerate(d) if x < 1.0]
            flat += len(bad)
            print(f"{model:7} {label:8} " + " ".join(f"{x:5.1f}" for x in d) + (f"   FLAT from {bad}" if bad else ""))
    print("flat steps:", flat)


if __name__ == "__main__":
    main()
