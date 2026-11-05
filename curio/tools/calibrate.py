#!/usr/bin/env python3
"""Writes curio/levels.h: each specimen's output gain and its controls' level
trims, so every specimen's default hit sounds at its role's loudness and its
controls move its tone more than its level. Renders the real engine on the
host (tools/hit.cpp). Not part of the machine.

    <python with numpy> tools/calibrate.py --sdk <open-xy checkout> [--cxx clang++]

Loudness is BS.1770's K-weighting over the loudest 400 ms of the hit
(momentary loudness, LUFS-M), in dB under the SDK's half-scale ceiling, the
mean over several strikes (the specimens' noise differs per hit):

- the kicks and booms (ball, cask) at -16.5, everything else at -19 (the
  mean of the defaults and the default kit's keys on the specimen, the
  defaults no more than 1 dB over), a
  little louder than MD KIT's kit (its loudest kick -17.7, snares -21.4,
  claps -21.5 to -22.9, cowbells -22.2 to -23.6, in the same measure);
- the output stage's peak limiter (engine.cpp) holds every sample to the
  specimen's limit, 0.42 of full scale, lowered for a specimen whose loudest
  strike's true peak (4x oversampled), at its defaults or on any key of the
  default kit, would pass 1 dB under the ceiling; at
  a specimen's defaults it may take at most 3 dB off a hit's first
  milliseconds (MAX_GR), so a specimen short of its loudness under those two
  stays short;
- each control's trim at 0, 32, 64, 96 and 127, the others at their
  defaults, makes up three quarters of the loudness that control moves
  (COMPENSATE), from -12 to +6 dB, a boost only as far as the limiter's
  3 dB allows there, and nothing at the control's default.
"""
import argparse
import os
import pathlib
import re
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
SR = 44100
CEILING = 2.0 ** 30
TARGET = {"low": -16.5, "rest": -19.0}
LOW = {"ball", "cask"}
LIMIT_DB = 20 * np.log10(0.84)   # the limiter's 0.42 of full scale, under the ceiling, at most
MAX_GR, TP_MAX = 3.0, -1.0
COMPENSATE, TRIM_LOW, TRIM_HIGH = 0.75, -12.0, 6.0
POINTS = [0, 32, 64, 96, 127]
SEEDS, SWEEP_SEEDS = [1, 2, 3, 4, 5, 6], [1, 2, 3]
NAMES = ["ball", "cask", "stone", "drip", "ice", "sheet", "spark", "cork",
         "cicada", "ratchet", "pod", "match", "glass", "bell", "chalk", "roar"]


def _biquad_ir(b, a, n):
    y, x1, x2, y1, y2 = np.zeros(n), 0.0, 0.0, 0.0, 0.0
    for i in range(n):
        xi = 1.0 if i == 0 else 0.0
        yi = (b[0] * xi + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2) / a[0]
        x2, x1, y2, y1 = x1, xi, y1, yi
        y[i] = yi
    return y


def _k_weighting():
    """BS.1770's two stages for 44.1 kHz (bilinear, as pyloudnorm derives them)."""
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    K, Vh = np.tan(np.pi * fc / SR), 10 ** (G / 20)
    Vb, a0 = Vh ** 0.4996667741545416, 1 + K / Q + K * K
    shelf = _biquad_ir([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
                       [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], 8192)
    Q, fc = 0.5003270373253953, 38.13547087613982
    K = np.tan(np.pi * fc / SR)
    a0 = 1 + K / Q + K * K
    hp = _biquad_ir([1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], 16384)
    return np.convolve(shelf, hp)[:16384]


KW = _k_weighting()


def loudness(x):
    """LUFS-M: the loudest 400 ms window, 10 ms apart."""
    x = np.pad(x, (0, int(0.4 * SR)))
    n = 1 << int(np.ceil(np.log2(len(x) + len(KW))))
    y = np.fft.irfft(np.fft.rfft(x, n) * np.fft.rfft(KW, n), n)[:len(x)]
    c = np.concatenate([[0], np.cumsum(y * y)])
    block = int(0.4 * SR)
    starts = np.arange(0, max(1, len(y) - block + 1), SR // 100)
    return float(-0.691 + 10 * np.log10(max(((c[starts + block] - c[starts]) / block).max(), 1e-20)))


def true_peak(x):
    n = 1 << int(np.ceil(np.log2(len(x) + 256)))
    return float(np.max(np.abs(np.fft.irfft(np.fft.rfft(x, n), 4 * n) * 4)))


def db(v):
    return 20 * np.log10(max(v, 1e-9))


def build(sdk, cxx, scratch):
    exe = pathlib.Path(scratch) / "hit"
    subprocess.run([cxx, "-std=c++11", "-O2", f"-I{sdk}/sdk/include", str(ROOT / "tools" / "hit.cpp"), "-o", str(exe)], check=True)
    return exe


def measure(exe, scratch, jobs):
    """jobs: (model, controls, tune, vary, seed). Each: loudness, true peak and pre-limiter peak, dB re the ceiling."""
    def chunk(part):
        lines = [f"{scratch}/{i}.raw {m} {' '.join(map(str, c))} {t} {v} 6 {s}" for i, (m, c, t, v, s) in part]
        out = subprocess.run([str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True).stdout.split("\n")
        res = {}
        for (i, _job), text in zip(part, out):
            x = np.fromfile(f"{scratch}/{i}.raw", "<i4").astype(float) / CEILING
            os.unlink(f"{scratch}/{i}.raw")
            res[i] = (loudness(x), db(true_peak(x)), db(float(text.split()[2]) * 2))
        return res
    indexed = list(enumerate(jobs))
    res = {}
    with ThreadPoolExecutor(8) as ex:
        for r in ex.map(chunk, [indexed[k::8] for k in range(8)]):
            res.update(r)
    return [res[i] for i in range(len(jobs))]


def defaults():
    src = (ROOT / "kit.h").read_text()
    return [[int(v) for v in d.split(",")] for d in re.findall(r'\{"[a-z ]+", \d, \{[^}]*\}, \{([^}]*)\}', src)]


def read_levels():
    src = (ROOT / "levels.h").read_text()
    gain = [int(v) / 2 ** 28 for v in re.search(r"gain\[16\] = \{([^}]*)\}", src).group(1).split(",")]
    rows = [[int(v) for v in r.split(",")] for r in re.findall(r"\{(-?\d+, -?\d+, -?\d+, -?\d+, -?\d+)\}", src)]
    trim = [[[v / 32 for v in rows[m * 8 + s]] for s in range(8)] for m in range(16)]
    found = re.search(r"limit\[16\] = \{([^}]*)\}", src)
    limit = [db(int(v) * 64 / 2 ** 30) for v in found.group(1).split(",")] if found else [LIMIT_DB] * 16
    return [20 * np.log10(g) for g in gain], trim, limit   # dB, octaves, dB re the ceiling


def write_levels(gain_db, trim, limit):
    lines = ["#pragma once",
             "// Generated by tools/calibrate.py: each specimen's output gain and its controls' level trims.",
             "#include <opxy/types.hpp>", "", "namespace curio {", "namespace levels {",
             "// Each specimen's gain, Q28.",
             "const opxy::i32 gain[16] = {" + ", ".join(str(int(round(10 ** (g / 20) * 2 ** 28))) for g in gain_db) + "};",
             "// Each specimen's peak limit, as the output stage compares it (an output sample is 64 times it).",
             "const opxy::i32 limit[16] = {" + ", ".join(str(int(round(10 ** (v / 20) * 2 ** 30 / 64))) for v in limit) + "};",
             "// log2 trims in 1/32 octave at each control's 0, 32, 64, 96 and 127.",
             "const signed char trim[16][8][5] = {"]
    for m in range(16):
        lines.append("    {" + ", ".join("{" + ", ".join(str(int(np.clip(round(v * 32), -127, 127))) for v in trim[m][s]) + "}"
                                         for s in range(8)) + "},")
    lines += ["};", "}", "}", ""]
    (ROOT / "levels.h").write_text("\n".join(lines))


def at_default(points, value):
    """The engine's interpolation of a control's five trims at a value."""
    c = 4095 if value >= 127 else value << 5
    i, f = c >> 10, (c & 1023) / 1024
    return points[i] + (points[i + 1] - points[i]) * f


def default_kit():
    src = (ROOT / "kit.h").read_text()
    models = [int(v) for v in re.search(r"key_model\[keys\] = \{([^}]*)\}", src).group(1).split(",")]
    values = [[int(v) for v in r.split(",")] for r in re.findall(r"\{([0-9, ]+)\},\s+//", src)]
    return list(zip(models, values))


def gain_round(exe, scratch, gain_db, trim, limit):
    d, keys = defaults(), default_kit()
    jobs = [(m, d[m], 0, 0, s) for m in range(16) for s in SEEDS]
    jobs += [(m, v, 0, 0, s) for m, v in keys for s in SWEEP_SEEDS]
    res = measure(exe, scratch, jobs)
    n = len(SEEDS) * 16
    worst = 0
    for m in range(16):
        r = res[m * len(SEEDS):(m + 1) * len(SEEDS)]
        loud = float(np.mean([x[0] for x in r]))
        tp = max(x[1] for x in r)
        gr = max(0.0, float(np.mean([x[2] for x in r])) - limit[m])
        target = TARGET["low" if NAMES[m] in LOW else "rest"]
        # aim the mean of the defaults and the default kit's keys on the
        # specimen at the target, keeping the defaults within 1 dB over it
        mine = [res[n + k * len(SWEEP_SEEDS):n + (k + 1) * len(SWEEP_SEEDS)] for k, (km, _v) in enumerate(keys) if km == m]
        on = [float(np.mean([x[0] for x in hit])) for hit in mine]
        # the true peak of the loudest strike, of the defaults or any key on it
        tp = max([tp] + [x[1] for hit in mine for x in hit])
        step = target - (loud + float(np.mean(on))) / 2 if on else target - loud
        step = min(step, target + 1.0 - loud, MAX_GR - gr)
        if tp > TP_MAX:
            # the strike's true peak runs over its samples: limit it lower
            # while the limiter's budget lasts, else turn the specimen down
            if gr < MAX_GR:
                limit[m] = min(LIMIT_DB, limit[m] + 0.9 * (TP_MAX - tp))
            else:
                step = min(step, TP_MAX - tp)
        elif limit[m] < LIMIT_DB and tp < TP_MAX - 0.3:
            limit[m] = min(LIMIT_DB, limit[m] + 0.5 * (TP_MAX - 0.2 - tp))
        gain_db[m] += 0.9 * step
        worst = max(worst, abs(step))
        print(f"  {NAMES[m]:8} {loud:6.1f} LUFS-M  tp {tp:5.1f}  limiter {gr:4.1f} dB  step {step:+5.2f}")
    write_levels(gain_db, trim, limit)
    return worst


def trim_round(exe, scratch, gain_db, trim, limit):
    d = defaults()
    jobs = [(m, d[m], 0, 0, s) for m in range(16) for s in SWEEP_SEEDS]
    for m in range(16):
        for c in range(8):
            for p in POINTS:
                v = list(d[m])
                v[c] = p
                jobs += [(m, v, 0, 0, s) for s in SWEEP_SEEDS]
    res = measure(exe, scratch, jobs)
    n = len(SWEEP_SEEDS)
    mean = lambda k, i: float(np.mean([x[i] for x in res[k * n:(k + 1) * n]]))
    for m in range(16):
        base = mean(m, 0)
        for c in range(8):
            want = []
            for j, _p in enumerate(POINTS):
                k = 16 + (m * 8 + c) * 5 + j
                desired = COMPENSATE * (base - mean(k, 0))
                if desired > 0:
                    desired = min(desired, max(0.0, limit[m] + MAX_GR - mean(k, 2)))
                want.append(np.clip(desired, TRIM_LOW, TRIM_HIGH) / 6.0206 + trim[m][c][j])
            z = at_default(want, d[m][c])
            trim[m][c] = [w - z for w in want]
    write_levels(gain_db, trim, limit)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--cxx", default="c++")
    ap.add_argument("--rounds", type=int, default=4, help="gain rounds")
    a = ap.parse_args()
    scratch = tempfile.mkdtemp(prefix="curio-cal-")
    # From the gains as they are, but no trims and the full limit: each run
    # makes up the same three quarters of what the controls move.
    gain_db, _trim, _limit = read_levels()
    trim = [[[0.0] * 5 for _ in range(8)] for _ in range(16)]
    limit = [LIMIT_DB] * 16
    write_levels(gain_db, trim, limit)
    for r in range(a.rounds):
        print(f"gains, round {r + 1}")
        gain_round(build(a.sdk, a.cxx, scratch), scratch, gain_db, trim, limit)
    print("trims")
    trim_round(build(a.sdk, a.cxx, scratch), scratch, gain_db, trim, limit)
    for g in range(a.rounds):
        print(f"gains, round {g + 1}")
        gain_round(build(a.sdk, a.cxx, scratch), scratch, gain_db, trim, limit)


if __name__ == "__main__":
    main()
