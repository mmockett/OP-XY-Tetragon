#!/usr/bin/env python3
"""Renders CURIO's demos from the real engine on the host (tools/render.cpp):
docs/demo-hits.wav, every specimen's hit in turn, and two grooves on the
kit presets, docs/demo-cellar.wav (boom bap, 98 BPM) and
docs/demo-foundry.wav (industrial techno, 124 BPM). The demos follow the
suite's shared standard: stereo 44.1 kHz WAV, each key at its kit's own pan,
level, TUNE and VARY (tools/presets.py), no sends, reverb or other
processing, and each file normalised to -16 LUFS integrated (ITU-R BS.1770:
K-weighting, 400 ms blocks, the absolute and relative gates) or, where that
would put a peak over -1 dBFS, to a -1 dBFS peak.

    <python with numpy> tools/demos.py --sdk <open-xy checkout> [--cxx clang++]
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import presets  # noqa: E402

SR = 44100


def build(sdk, cxx):
    exe = pathlib.Path(tempfile.mkdtemp()) / "render"
    subprocess.run([cxx, "-std=c++11", "-O2", f"-I{sdk}/sdk/include", str(ROOT / "tools" / "render.cpp"), "-o", str(exe)], check=True)
    return exe


def stems(exe, keys, hits, length, choke=(6, 8, 10)):
    text = "".join(f"{m} {' '.join(map(str, v))} {t} {vy}\n" for m, v, t, vy in keys)
    text += "".join(f"{at} {k} {lv} {tn}\n" for at, k, lv, tn in hits)
    out = subprocess.run([str(exe), str(length), ",".join(map(str, choke))], input=text.encode(), capture_output=True, check=True).stdout
    return np.frombuffer(out, np.float32).reshape(24, length).astype(float)


def biquad(b, a, n=SR):
    """The impulse response of one biquad, n samples."""
    b0, b1, b2 = (v / a[0] for v in b)
    a1, a2 = a[1] / a[0], a[2] / a[0]
    y = np.zeros(n)
    x1 = x2 = y1 = y2 = 0.0
    for i in range(n):
        xi = 1.0 if i == 0 else 0.0
        yi = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, xi, y1, yi
        y[i] = yi
    return y


def k_weight(x):
    """BS.1770's K-weighting: a +4 dB high shelf, then a 38 Hz high-pass."""
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    A, w, = 10 ** (G / 40), 2 * np.pi * fc / SR
    al, c = np.sin(w) / (2 * Q), np.cos(w)
    shelf = biquad([A * ((A + 1) + (A - 1) * c + 2 * np.sqrt(A) * al), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - 2 * np.sqrt(A) * al)],
                   [(A + 1) - (A - 1) * c + 2 * np.sqrt(A) * al, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - 2 * np.sqrt(A) * al])
    Q, fc = 0.5003270373253953, 38.13547087613982
    w = 2 * np.pi * fc / SR
    al, c = np.sin(w) / (2 * Q), np.cos(w)
    hp = biquad([(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + al, -2 * c, 1 - al])
    h = np.convolve(shelf[:8192], hp)[:SR]
    n = 1 << int(np.ceil(np.log2(len(x) + len(h))))
    return np.fft.irfft(np.fft.rfft(x, n, axis=0) * np.fft.rfft(h, n)[:, None], n, axis=0)[:len(x)]


def loudness(x):
    """Integrated loudness, LUFS (BS.1770-4), of a stereo signal."""
    y = k_weight(x)
    block, hop = int(0.4 * SR), int(0.1 * SR)
    z = np.array([np.mean(y[i:i + block] ** 2, axis=0).sum() for i in range(0, len(y) - block + 1, hop)])
    z = z[-0.691 + 10 * np.log10(z + 1e-20) > -70]
    z = z[-0.691 + 10 * np.log10(z) > -0.691 + 10 * np.log10(z.mean()) - 10]
    return -0.691 + 10 * np.log10(z.mean())


def mix(st, pads):
    """Each key at its own pan (equal power) and level (dB): the mixer, nothing else."""
    L = np.zeros(st.shape[1])
    R = np.zeros(st.shape[1])
    for k in range(24):
        pad = pads.get(k + 1, {})
        g = 10 ** (pad.get("level", 0) / 20)
        p = pad.get("pan", 0) / 100
        L += st[k] * g * np.cos((p + 1) * np.pi / 4) * np.sqrt(2)
        R += st[k] * g * np.sin((p + 1) * np.pi / 4) * np.sqrt(2)
    return np.stack([L, R], 1)


TARGET, CEILING = -16.0, 10 ** (-1 / 20)


def write(path, x):
    x = np.asarray(x, float)
    if x.ndim == 1:
        x = np.stack([x, x], 1)
    gain = min(10 ** ((TARGET - loudness(x)) / 20), CEILING / (np.max(np.abs(x)) + 1e-12))
    x = x * gain
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(np.round(np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())
    print(f"wrote {path}: {loudness(x):.1f} LUFS, peak {20 * np.log10(np.max(np.abs(x))):.1f} dBFS")


def keys_of(kit):
    """Each key's specimen, controls, TUNE and VARY, as the kit's preset sets them."""
    pads = presets.PADS[kit]
    return [(m, v, pads.get(k + 1, {}).get("tune", 0), pads.get(k + 1, {}).get("vary", 0)) for k, (m, v) in enumerate(presets.kit_keys(kit))]


def pattern(rows, key, bars, step, swing, levels={"X": 1000, "x": 850, "o": 600, "g": 350}, tune=None, only=None):
    hits = []
    for bar in range(bars):
        if only and bar not in only:
            continue
        row = rows[bar % len(rows)]
        for i, ch in enumerate(row):
            if ch in levels:
                t = (bar * 16 + i) * step + (swing - 0.5) * 2 * step * (i % 2)
                hits.append((int(t * SR), key, levels[ch], tune(bar, i) if tune else 0))
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--cxx", default="c++")
    a = ap.parse_args()
    exe = build(a.sdk, a.cxx)
    docs = ROOT / "docs"
    docs.mkdir(exist_ok=True)

    # Every specimen's design-default hit, one a second and a half, drawer by drawer.
    src = (ROOT / "kit.h").read_text()
    import re
    defaults = [[int(v) for v in d.split(",")] for d in re.findall(r'\{"[a-z ]+", \d, \{[^}]*\}, \{([^}]*)\}', src)]
    keys = [(m, defaults[m], 0, 0) for m in range(16)] + [(0, defaults[0], 0, 0)] * 8
    hits = [(int((0.2 + 1.5 * m) * SR), m, 1000, 0) for m in range(16)]
    length = int((0.2 + 1.5 * 16 + 1) * SR)
    st = stems(exe, keys, hits, length, choke=())
    write(docs / "demo-hits.wav", mix(st, {}))

    # Cellar: dusty boom bap, 98 BPM, swung.
    bpm, swing, bars = 98, 0.57, 8
    step = 60 / bpm / 4
    k = {"kick": 0, "snare": 2, "rim": 3, "hat": 6, "ohat": 10, "shk": 12, "tom": 9, "glass": 14}
    rows = {
        "kick": ["X.....x...x.....", "X.....x.x.....o.", "X.....x...x.....", "X..x..x.....x..."],
        "snare": ["....X.......X...", "....X.......X..g", "....X.......X...", "....X..g....X.x."],
        "rim": ["..g.......g....g", "......g.......g.", "..g.......g....g", "......g..g......"],
        "hat": ["x.o.x.o.x.o.x.ox", "x.o.x.o.x.o.x.o.", "x.o.x.o.x.o.x.ox", "x.o.x.o.x.o.x..."],
        "ohat": ["................", "................", "................", "..............x."],
        "shk": ["gogogogogogogogo"],
        "tom": ["................", "................", "................", "........x..x.o.."],
        "glass": ["...........x....", "......x.........", "...........x....", "..x............."],
    }
    melody = [0, 3, 7, 10, 7, 3, 12, 5]
    count = {"n": 0}

    def glass_tune(bar, i):
        count["n"] += 1
        return melody[(count["n"] - 1) % len(melody)]   # over the key's own TUNE
    hits = []
    for name, r in rows.items():
        hits += pattern(r, k[name], bars, step, swing, tune=glass_tune if name == "glass" else (lambda b, i: {11: 5, 13: -3}.get(i, 0)) if name == "tom" else None,
                        only=range(4, 8) if name == "shk" else None)
    length = int((bars * 16 * step + 2) * SR)
    st = stems(exe, keys_of("cellar"), hits, length)
    write(docs / "demo-cellar.wav", mix(st, presets.PADS["cellar"]))

    # Foundry: industrial techno, 124 BPM, straight.
    bpm, swing, bars = 124, 0.5, 8
    step = 60 / bpm / 4
    k = {"kick": 0, "clap": 4, "sheet": 5, "crash": 19, "ohat": 10, "tick": 13, "bell": 15, "roar": 22}
    rows = {
        "kick": ["X...X...X...X...", "X...X...X...X...", "X...X...X...X...", "X...X...X...X.o."],
        "clap": ["....X.......X...", "....X.......X...", "....X.......X...", "....X.......X.x."],
        "sheet": ["....x.......x...", "....x.......x...", "....x.......x...", "....x.......xoxx"],
        "crash": ["X..............."],
        "ohat": ["..x...x...x...x."],
        "tick": ["xoxoxoxoxoxoxoxo"],
        "bell": ["................", "...x......x.....", "................", "...x......x..x.."],
        "roar": ["x..............."],
    }
    only = {"crash": (0, 4), "roar": (7,)}
    hits = []
    for name, r in rows.items():
        hits += pattern(r, k[name], bars, step, swing, only=only.get(name))
    length = int((bars * 16 * step + 2.5) * SR)
    st = stems(exe, keys_of("foundry"), hits, length)
    write(docs / "demo-foundry.wav", mix(st, presets.PADS["foundry"]))


if __name__ == "__main__":
    main()
