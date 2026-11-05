#!/usr/bin/env python3
"""Writes syahi/levels.h: each model's output gain, its soft knee and its
level trim at each control, so that every hit sounds at its role's loudness
whatever its controls, as far as its peak allows. Renders the real engine on
the host (tools/levels.cpp). Not part of the machine.

    python -I tools/calibrate.py --sdk <open-xy>

Loudness is BS.1770's K-weighting over the loudest 400 ms of a hit (LUFS-M
max); levels are in dB under the SDK's half-scale ceiling. A model's default
hit is set to its role's loudness (LOW for kicks and toms, REST for the
others) with its true peak (4x oversampled) at most -1 dB, the loudest of six
hits. Where its peak would stop it short, its knee starts lower, as little as
it takes: the knee rounds the hit's first sharp moments under the ceiling.
Each control's trim at 0, 32, 64, 96 and 127, the others at their defaults,
brings that hit to the role's loudness, but never lifts its peak past -1 dB
and takes back only part of a rise (CUT_SHARE); none at the default itself.

Everything after the knee is linear, so one render with the knees off at a
known gain gives each hit; the knee is applied here, as the engine does it.
"""
import argparse, os, pathlib, subprocess, sys, tempfile
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import kits  # noqa: E402

SR = 44100
LOW, REST = -16.5, -19.0          # LUFS-M max, dB re the ceiling
ROLE = {"BOOM": LOW, "TOM": LOW}
TP = -1.0                          # the true peak a default may reach, dB re the ceiling
KNEES = [0.35, 0.32, 0.29, 0.26, 0.23, 0.20]   # where a knee may start, of full scale
MOST_ROUNDING = 3.5                # dB the knee may take off a default's peak
POINTS = [0, 32, 64, 96, 127]
SEEDS, SWEEP_SEEDS = 6, 6
LINEAR = -2.0                      # log2 gain of the linear render (knees off)
# A cut takes back only part of what a control adds: a longer DECAY should
# still ring louder, as a struck thing does, and cuts measured one control at a
# time add up past the mark when several move (a crash's long DECAY with its
# SIZZLE). A preset key's LEVEL takes off the rest (tools/kits.py PADS).
CUT_SHARE, DECAY_CUT_SHARE = 0.75, 0.5
# A yarn mallet on a bar is soft as much as dull: BAR's MALLET keeps half the
# level it loses, so the first quarter of its travel still sounds.
BOOST_SHARE = {("BAR", 3): 0.5}


# ---- measures (BS.1770 K-weighting, designed for 44.1 kHz as pyloudnorm does)
def _biquad_ir(b, a, n):
    y = np.zeros(n); x1 = x2 = y1 = y2 = 0.0
    for i in range(n):
        xi = 1.0 if i == 0 else 0.0
        yi = b[0] * xi + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2
        x2, x1, y2, y1 = x1, xi, y1, yi
        y[i] = yi
    return y


def _k():
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    K = np.tan(np.pi * fc / SR); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416; a0 = 1 + K / Q + K * K
    shelf = _biquad_ir([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
                       [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], 8192)
    Q, fc = 0.5003270373253953, 38.13547087613982
    K = np.tan(np.pi * fc / SR); a0 = 1 + K / Q + K * K
    hp = _biquad_ir([1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], 16384)
    return np.convolve(shelf, hp)[:16384]


KW = _k()


def lufs(x):
    n = 1 << int(np.ceil(np.log2(len(x) + int(0.4 * SR) + len(KW))))
    y = np.fft.irfft(np.fft.rfft(x, n) * np.fft.rfft(KW, n), n)[:len(x) + int(0.4 * SR)]
    c = np.concatenate([[0], np.cumsum(y * y)])
    b = int(0.4 * SR)
    s = np.arange(0, max(1, len(y) - b + 1), 441)
    return -0.691 + 10 * np.log10(max(((c[s + b] - c[s]) / b).max(), 1e-20))


def true_peak(x):
    n = 1 << int(np.ceil(np.log2(len(x) + 256)))
    return float(np.max(np.abs(np.fft.irfft(np.fft.rfft(x, n), 4 * n)) * 4))


def db(v):
    return 20 * np.log10(max(v, 1e-12))


def knee(x, k):
    """The engine's knee, x and k in units of the ceiling (2 x full scale)."""
    if k >= 1:
        return x
    r = 1 - k
    a = np.abs(x)
    out = np.where(a > k, k + r * np.tanh(np.minimum((a - k) / r, 4.0)), a)
    return np.sign(x) * out


def hit(xs, gain, k):
    """mean loudness and the loudest true peak of the hits xs at a gain (linear) through knee k."""
    ys = [knee(x * gain, k) for x in xs]
    return 10 * np.log10(np.mean([10 ** (lufs(y) / 10) for y in ys])), max(db(true_peak(y)) for y in ys)


def solve(f, lo, hi, want, steps=24):
    """The largest log2 gain in [lo, hi] at which the increasing f stays at or under want."""
    if f(lo) > want:
        return lo
    if f(hi) <= want:
        return hi
    for _ in range(steps):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if f(mid) <= want else (lo, mid)
    return lo


# ---- the header
def knee_row(k):
    if k >= 1:
        return [0x7fffffff, 0, 0]
    r = 0.5 * (1 - k)                       # of full scale
    return [int(round(k * 0.5 * 2 ** 31)), int(round(16384 / r)), int(round(r * 2 ** 31))]


def write(gain, knees, trim):
    lines = ["#pragma once", "// Generated by tools/calibrate.py: each model's output gain, its soft knee and its",
             "// trim at each control.", "#include <opxy/types.hpp>", "", "namespace syahi {", "namespace levels {",
             "// log2 of each model's output gain after GRIT, Q16 (BOOM, SNARE, TOM, TABLA, BAR, METAL, BOUNCE, WIRE).",
             "const opxy::i32 gain[8] = {" + ", ".join(str(int(round(g * 65536))) for g in gain) + "};",
             "// Each model's knee: where it starts (Q31 of full scale), its tanh table's steps (Q8) a unit, and its",
             "// range up to the ceiling at half scale.",
             "const opxy::i32 knee[8][3] = {" + ", ".join("{" + ", ".join(map(str, knee_row(k))) + "}" for k in knees) + "};",
             "// log2 trims in 1/32 octave at each control's 0, 32, 64, 96 and 127.",
             "const signed char trim[8][8][5] = {"]
    for m in range(8):
        lines.append("    {" + ", ".join("{" + ", ".join(str(int(np.clip(round(v * 32), -127, 127))) for v in trim[m][s]) + "}"
                                         for s in range(8)) + "},")
    lines += ["};", "}", "}", ""]
    (ROOT / "levels.h").write_text("\n".join(lines))


# ---- rendering
def build(sdk, scratch):
    b = pathlib.Path(scratch) / "levels"
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{sdk}/sdk/include", f"-I{ROOT}", str(HERE / "levels.cpp"), "-o", str(b)], check=True)
    return b


def render(binary, jobs, scratch):
    """jobs: (model, controls, seeds). Each a list of hits (float, 1 = the ceiling)."""
    lines, where = [], []
    for j, (m, c, seeds) in enumerate(jobs):
        for s in range(seeds):
            lines.append(f"{scratch}/{len(lines)}.raw {kits.MODELS.index(m)} {' '.join(map(str, c))} 0 0 8")
            where.append(j)
    subprocess.run([str(binary)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    out = [[] for _ in jobs]
    for i, j in enumerate(where):
        f = f"{scratch}/{i}.raw"
        out[j].append(np.fromfile(f, "<i4").astype(np.float64) / 2 ** 30)
        os.unlink(f)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    a = ap.parse_args()
    scratch = tempfile.mkdtemp(prefix="syahi-cal-")
    # the linear render: every gain at LINEAR, no knee, no trim
    write([LINEAR] * 8, [1.0] * 8, [[[0.0] * 5 for _ in range(8)] for _ in range(8)])
    binary = build(a.sdk, scratch)
    jobs = []
    for m in kits.MODELS:
        d = kits.DEFAULTS[m]
        jobs.append((m, d, SEEDS))
        for s in range(8):
            for p in POINTS:
                c = list(d); c[s] = p
                jobs.append((m, c, SWEEP_SEEDS))
    hits = render(binary, jobs, scratch)
    gain, knees, trim = [], [], []
    for mi, m in enumerate(kits.MODELS):
        base = hits[mi * 41]
        want = ROLE.get(m, REST)
        chosen = None
        for k in KNEES:
            kk = k * 2   # of the ceiling
            g_tp = solve(lambda g: hit(base, 2 ** g, kk)[1], -6, 6, TP)
            g_l = solve(lambda g: hit(base, 2 ** g, kk)[0], -6, 6, want)
            g = min(g_tp, g_l)
            loud, tp = hit(base, 2 ** g, kk)
            pre = max(db(np.abs(x).max() * 2 ** g) for x in base)
            post = max(db(np.abs(knee(x * 2 ** g, kk)).max()) for x in base)
            option = (k, g, loud, tp, pre - post)
            if pre - post > MOST_ROUNDING:
                break
            chosen = option
            if loud >= want - 1.0:
                break
        k, g, loud, tp, rounding = chosen
        gain.append(LINEAR + g)
        knees.append(k * 2)
        print(f"{m:7s} knee {k:.2f} of full scale  gain {(LINEAR + g) * 6.02:+6.2f} dB  loudness {loud:6.1f} (want {want})"
              f"  true peak {tp:5.1f}  knee takes {rounding:.1f} dB off the peak")
        # each control's trim: the role's loudness, its peak at most TP
        d = kits.DEFAULTS[m]
        rows = []
        for s in range(8):
            row = []
            for j, p in enumerate(POINTS):
                xs = hits[mi * 41 + 1 + s * 5 + j]
                l0, _ = hit(xs, 2 ** g, k * 2)
                if l0 > want:
                    t = solve(lambda t: hit(xs, 2 ** (g + t), k * 2)[0], -4, 0, want)   # the cut to the role's loudness
                    t *= DECAY_CUT_SHARE if s == 1 else CUT_SHARE
                else:
                    t_l = solve(lambda t: hit(xs, 2 ** (g + t), k * 2)[0], 0, 2, want)
                    t_p = solve(lambda t: hit(xs, 2 ** (g + t), k * 2)[1], 0, 2, max(TP, hit(xs, 2 ** g, k * 2)[1]))
                    t = min(t_l, t_p) * BOOST_SHARE.get((m, s), 1.0)
                row.append(t)
            # no trim at the default itself (the engine's interpolation): a
            # default on a point is that point; one between two moves them both
            if d[s] in POINTS:
                row[POINTS.index(d[s])] = 0.0
                rows.append(row)
                continue
            at = d[s] / 127 * 4
            i = min(int(at), 3)
            z = row[i] + (row[i + 1] - row[i]) * (at - i)
            rows.append([v - z for v in row])
        trim.append(rows)
        print("   trims (dB):", "; ".join(f"{kits.LABELS[m][s]} " + " ".join(f"{v * 6.02:+.1f}" for v in rows[s]) for s in range(8)))
    write(gain, knees, trim)
    print("wrote", ROOT / "levels.h")


if __name__ == "__main__":
    main()
