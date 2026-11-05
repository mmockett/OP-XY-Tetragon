#!/usr/bin/env python3
"""Renders Syahi's demos from the real engine on the host (tools/render.cpp):

    docs/demo-hits.wav     each model's default hit, then the default kit's keys
    docs/demo-form.wav     a tom swept through FORM (sine .. bell), then a tabla through LOAD
    docs/demo-volt.wav     a groove on the VOLT kit (808 / 909)
    docs/demo-raman.wav    a groove on the RAMAN kit (tabla, djembe, bars)
    docs/demo-foundry.wav  a groove on the FOUNDRY kit (industrial)

    SYAHI_RENDER=<render binary> python3 tools/demos.py

Each key plays with its preset's HUMAN, GRIT, pan and level. Keys sharing a choke group cut each other off, and a key cuts
its own last note (chokeSameKey), as on the OP-XY. Stereo, 16-bit, 44.1 kHz, each key panned at constant power
(tools/render.cpp with SYAHI_STEREO=1). Each file is then scaled by one gain to -16 LUFS integrated (ITU-R BS.1770:
K-weighted, gated), or less where that would put a sample peak over -1 dBFS; nothing else is done to the sound.
"""
import math, os, pathlib, random, subprocess, sys, tempfile, wave
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kits  # noqa: E402

DOCS = HERE.parent / "docs"
RENDER = os.environ.get("SYAHI_RENDER", "render")


TARGET_LUFS, CEILING_DB = -16.0, -1.0


def biquad(x, b, a):
    b0, b1, b2 = (v / a[0] for v in b)
    a1, a2 = a[1] / a[0], a[2] / a[0]
    y = np.empty_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x.tolist()):
        o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def loudness(st, fs=44100):
    """Integrated loudness in LUFS (ITU-R BS.1770-4): K-weighting, 400 ms blocks every 100 ms, gated at -70 LUFS and
    10 LU under the ungated mean. The K filters are designed for fs as pyloudnorm designs them."""
    A = 10 ** (3.99984385397 / 40)
    w = 2 * math.pi * 1681.9744509555319 / fs
    al = math.sin(w) / (2 * 0.7071752369554193)
    shelf = ([A * ((A + 1) + (A - 1) * math.cos(w) + 2 * math.sqrt(A) * al), -2 * A * ((A - 1) + (A + 1) * math.cos(w)),
              A * ((A + 1) + (A - 1) * math.cos(w) - 2 * math.sqrt(A) * al)],
             [(A + 1) - (A - 1) * math.cos(w) + 2 * math.sqrt(A) * al, 2 * ((A - 1) - (A + 1) * math.cos(w)),
              (A + 1) - (A - 1) * math.cos(w) - 2 * math.sqrt(A) * al])
    w = 2 * math.pi * 38.13547087613982 / fs
    al = math.sin(w) / (2 * 0.5003270373253953)
    high = ([(1 + math.cos(w)) / 2, -(1 + math.cos(w)), (1 + math.cos(w)) / 2], [1 + al, -2 * math.cos(w), 1 - al])
    k = [biquad(biquad(st[:, c], *shelf), *high) for c in range(st.shape[1])]
    n, hop = int(0.4 * fs), int(0.1 * fs)
    z = np.array([sum(np.mean(ch[i:i + n] ** 2) for ch in k) for i in range(0, len(k[0]) - n + 1, hop)])
    lk = -0.691 + 10 * np.log10(np.maximum(z, 1e-20))
    z = z[lk > -70]
    rel = -0.691 + 10 * np.log10(z.mean()) - 10
    z = z[-0.691 + 10 * np.log10(z) > rel]
    return -0.691 + 10 * math.log10(z.mean())


def render(notes, seconds, path):
    """notes: (time s, model, controls, human, grit, choke key, pan -100..100, level dB)."""
    args = []
    for t, model, c, human, grit, key, pan, level in notes:
        args += [kits.MODELS.index(model), *c, human, grit, f"{t:.5f}", key, 1, pan, level]
    with tempfile.TemporaryDirectory() as tmp:
        raw = pathlib.Path(tmp) / "x.wav"
        subprocess.run([RENDER, str(raw), f"{seconds:.3f}", *map(str, args)], check=True, env={**os.environ, "SYAHI_STEREO": "1"})
        with wave.open(str(raw)) as w:
            assert w.getnchannels() == 2
            st = np.frombuffer(w.readframes(w.getnframes()), "<i4").astype(float).reshape(-1, 2) / 2 ** 31
    lufs = loudness(st)
    gain_db = min(TARGET_LUFS - lufs, CEILING_DB - 20 * math.log10(np.abs(st).max()))
    st *= 10 ** (gain_db / 20)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
        w.writeframes(np.round(np.clip(st, -1, 32767 / 32768) * 32767).astype("<i2").tobytes())
    print(f"wrote {path.name}: {len(st) / 44100:.1f} s, {lufs:.1f} LUFS raw, {gain_db:+.1f} dB, "
          f"{loudness(st):.1f} LUFS, peak {20 * math.log10(np.abs(st).max()):.1f} dBFS")


def hits():
    notes, t = [], 0.3
    for m in kits.MODELS:
        notes.append((t, m, kits.DEFAULTS[m], 0, 0, 1, 0, 0))
        t += 1.2
    t += 0.6
    for i, (role, model, c, choke) in enumerate(kits.DEFAULT_KIT):
        pad = kits.PADS["DEFAULT"].get(53 + i, {})
        notes.append((t, model, c, pad.get("human", 0), pad.get("grit", 0), 100 + choke if choke else 53 + i,
                      pad.get("pan", 0), pad.get("level", 0)))
        t += 0.75 if model in ("METAL", "BAR", "WIRE") else 0.5
    render(notes, t + 2.5, DOCS / "demo-hits.wav")


def form():
    notes, t = [], 0.3
    for v in range(0, 128, 12):
        # A mid tom (TUNE 90, about G3) struck with a stick (MALLET 70), so bar, plate and bell ring out.
        c = list(kits.DEFAULTS["TOM"]); c[4] = v; c[1] = 70; c[0] = 90; c[6] = 70
        notes.append((t, "TOM", c, 0, 0, 1, 0, 0)); t += 0.9
    t += 0.6
    for v in (0, 25, 50, 76, 101, 127):
        c = list(kits.DEFAULTS["TABLA"]); c[2] = v; c[5] = 70
        notes.append((t, "TABLA", c, 0, 0, 2, 0, 0)); t += 0.9
    render(notes, t + 2.0, DOCS / "demo-form.wav")


def groove(kit, bpm, bars, rows, swing=0.0, human=0, path=None, seed=0):
    """rows: key -> one or more 16-step strings ('x' full, 'o' softer, 'g' ghost).
    The engine takes no velocity, so softer steps play HUMAN-varied hits instead."""
    rnd = random.Random(seed)
    keys = kits.KITS[kit]
    step = 60 / bpm / 4
    notes = []
    for key, pats in rows.items():
        pats = pats if isinstance(pats, list) else [pats]
        role, model, c, choke = keys[key - 53]
        pad = kits.PADS.get(kit, {}).get(key, {})
        for b in range(bars):
            p = pats[b % len(pats)].replace(" ", "")
            for s, ch in enumerate(p):
                if ch == ".":
                    continue
                t = (b * 16 + s) * step + (swing * step if s % 2 else 0) + rnd.gauss(0, 0.002 * human)
                h = max({"x": human, "o": max(human, 6), "g": 12}[ch], pad.get("human", 0))
                notes.append((max(0.0, t) + 0.05, model, c, h, pad.get("grit", 0), 100 + choke if choke else key,
                              pad.get("pan", 0), pad.get("level", 0)))
    render(notes, bars * 16 * step + 2.5, path)


def main():
    DOCS.mkdir(exist_ok=True)
    hits()
    form()
    groove("VOLT", 124, 8, {
        53: ["x...x...x...x...", "x...x...x...x..o"],
        57: "....x.......x...",
        55: ["................", "..............go"],
        59: ["x.o.x.o.x.o.x.o.", "x.oox.o.x.o.x.og"],
        61: "..x...x...x...x.",
        62: ["................", "......x.....x.x."],
        70: ["...........g....", ".......g......g."],
        56: ["...o.....o......", "...o.....o...o.."],
    }, swing=0.08, path=DOCS / "demo-volt.wav")
    groove("RAMAN", 96, 8, {
        53: ["x.....o...x.....", "x.....o...x..o..", "x.....o...x.....", "x..o..o...x..x.."],
        54: "...g.......g....",
        55: ["..x...x.x...x...", "..x...x.x.o.x..."],
        56: ["x.......x.......", "x...........x..."],
        57: ["gg.g.g.g.g.gg.g.", "g.gg.g.g.g.g.ggg"],
        62: ["................", "..............o.", "................", "...........o.ox."],
        63: "x.ogx.ogx.ogx.og",
        76: ["x...............", "................", "................", "................"],
        67: ["x.......o.......", "................", "....o...........", "........o......."],
        68: ["......o.........", "x.......o.......", "................", "o.......o......."],
        69: ["....o.......o...", "....o...........", "x.......o.......", "....o..........."],
        70: ["..............o.", "............o...", "............o...", "............x..."],
    }, swing=0.12, human=4, path=DOCS / "demo-raman.wav", seed=2)
    groove("FOUNDRY", 100, 8, {
        53: ["x.......x.x.....", "x.......x.......", "x.......x.x....x", "x.....x.x......."],
        55: "........x.......",
        59: ["..o...o...o...o.", "..o...o...o.o.o."],
        64: ["......x.........", "...............x", "......x.....x...", "...x...........x"],
        57: ["................", "............x...", "................", "........x......."],
        65: ["................", "................", "................", "........x..x.x.."],
        66: ["................", "................", "................", "..........x...x."],
        70: ["x...............", "................", "................", "................",
             "x...............", "................", "................", "................"],
        69: ["................", "..............g.", "................", "..g............."],
    }, human=2, path=DOCS / "demo-foundry.wav", seed=3)


if __name__ == "__main__":
    main()
