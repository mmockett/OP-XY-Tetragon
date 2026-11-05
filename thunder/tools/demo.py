#!/usr/bin/env python3
"""Renders THUNDER's demos from the real engine on the host: docs/demo-hits.wav
(the default kit's 24 keys in turn) and two grooves on two kit presets,
docs/demo-kingston.wav and docs/demo-foundry.wav. Not part of the machine.

    python3 -I tools/demo.py --sdk <open-xy>

Each key plays as the OP-XY plays a THUNDER pad: one voice a key (a new hit
cuts the last, as chokeSameKey does), keys in choke group 1 cutting each
other, at the key's own level and pan (equal power). The OP-XY gives a kit no
velocity, so the accents are what a player sets on the device: a held step's
LEVEL lock (Shift's first row), a few dB under the key's own level on the
softer hits. The demos follow the suite's shared
standard: stereo 44.1 kHz WAV, no sends, reverb or other processing, and each
file normalised to -16 LUFS integrated (ITU-R BS.1770: K-weighting, 400 ms
blocks, the absolute and relative gates) or, where that would put a peak over
-1 dBFS, to a -1 dBFS peak.
"""
import argparse, os, pathlib, subprocess, sys, tempfile, wave
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import presets as P  # noqa: E402

SR = 44100


def build(sdk, scratch):
    b = pathlib.Path(scratch) / "render"
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools/render.cpp"), "-o", str(b)], check=True)
    return b


def render(binary, jobs, scratch):
    lines = [f"{scratch}/d{i}.raw {m} {' '.join(map(str, c))} {tune} {drive} 6 {seed}" for i, (m, c, seed, tune, drive) in enumerate(jobs)]
    subprocess.run([str(binary)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    out = []
    for i in range(len(jobs)):
        f = f"{scratch}/d{i}.raw"
        out.append(np.fromfile(f, "<i4").astype(np.float64) / 2 ** 31)
        os.unlink(f)
    return out


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
    A, w = 10 ** (G / 40), 2 * np.pi * fc / SR
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


TARGET, CEILING = -16.0, 10 ** (-1 / 20)


def write(path, left, right):
    """One gain for the whole file, to -16 LUFS or a -1 dBFS peak, whichever is lower; nothing else."""
    x = np.stack([left, right], 1)
    before = loudness(x)
    gain = min(10 ** ((TARGET - before) / 20), CEILING / (np.max(np.abs(x)) + 1e-12))
    x = x * gain
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(np.clip(np.round(x * 32767), -32768, 32767).astype("<i2").tobytes())
    print(f"wrote {path}: {before:.1f} LUFS raw, now {loudness(x):.1f} LUFS, peak {20 * np.log10(np.max(np.abs(x))):.1f} dBFS")


def place(left, right, x, at, key, accent=1.0):
    """A hit into the mix at its key's level and pan (equal power), the mixer and nothing else.
    accent: the step's LEVEL lock, as a fraction of the key's level (LEVEL stops at -30 dB)."""
    g = 10 ** (max(-30.0, key["gain"] + 20 * np.log10(accent)) / 20)
    pan = (key["pan"] + 100) / 200
    stop = min(len(left), at + len(x))
    left[at:stop] += x[: stop - at] * g * np.cos(pan * np.pi / 2) * 1.414
    right[at:stop] += x[: stop - at] * g * np.sin(pan * np.pi / 2) * 1.414


def groove(binary, scratch, keys, events, bpm, swing, bars=8):
    step = 60 / bpm / 4
    total = int((bars * 16 * step + 3) * SR)
    left, right = np.zeros(total), np.zeros(total)
    hits = sorted(events)
    jobs = [(keys[k]["model"], keys[k]["c"], 1000 + n, keys[k]["tune"], keys[k]["drive"]) for n, (_s, k, _v) in enumerate(hits)]
    sounds = render(binary, jobs, scratch)
    starts = [int((s * step + (step * (swing - 0.5) * 2 if s % 2 else 0)) * SR) for s, _k, _v in hits]
    # each key's lane ends at its next hit, or at the next hit of its choke group
    for n, ((s, k, v), x, at) in enumerate(zip(hits, sounds, starts)):
        end = len(x)
        for m in range(n + 1, len(hits)):
            k2 = hits[m][1]
            if k2 == k or (keys[k]["choke"] and keys[k2]["choke"] == keys[k]["choke"]):
                end = min(end, starts[m] - at)
                break
        x = x[: max(0, end) + 128].copy()
        if end < len(x):
            x[end:] *= np.linspace(1, 0, len(x) - end)
        place(left, right, x, at, keys[k], v)
    return left, right


def kingston():
    """Steppers, 112 BPM: four on the floor, the snare on three, offbeat flicks."""
    ev = []
    for bar in range(8):
        b = bar * 16
        for s in range(0, 16, 4):
            ev.append((b + s, P.BD, 0.95 if s == 0 else 0.85))
        ev.append((b + 8, P.SD, 0.9))
        for s in (2, 6, 10, 14):
            ev.append((b + s, P.CH, 0.7))
        if bar % 2:
            ev.append((b + 14, P.OH, 0.6))
            ev += [(b + 11, P.CB, 0.7), (b + 13, P.CB, 0.55)]
        for s in (3, 7, 11, 15):
            if (s + bar) % 3 == 0:
                ev.append((b + s, P.M1, 0.5))
        if bar in (3, 7):
            ev += [(b + 12, P.MT, 0.8), (b + 13, P.MT, 0.6), (b + 14, P.LT, 0.8), (b + 15, P.LT, 0.6)]
        if bar == 3:
            ev.append((b + 4, P.M3, 0.8))
        if bar == 5:
            ev.append((b, P.M4, 0.9))
    return ev, 112, 0.58


def foundry():
    """Industrial techno, 124 BPM: the kick, snaps on two and four, a hat on
    every off-step, the cowbell figure, shakers and a ball bearing for fills."""
    rng = np.random.default_rng(5)
    ev = []
    for bar in range(8):
        b = bar * 16
        for s in range(0, 16, 4):
            ev.append((b + s, P.BD, 0.95))
        for s in (4, 12):
            ev.append((b + s, P.CP, 0.85))
        for s in range(16):
            if s % 4 == 2:
                ev.append((b + s, P.OH, 0.75))
            elif s % 2 == 1 or bar >= 4:
                ev.append((b + s, P.CH, 0.45 + 0.25 * (s % 4 == 0) + 0.1 * rng.random()))
        for s in (3, 6, 10, 13):
            if not (bar % 4 == 3 and s == 13):
                ev.append((b + s, P.CB, 0.6 + 0.2 * (s == 10)))
        for s in (1, 5, 9, 13):
            ev.append((b + s, P.M1, 0.5))
        if bar in (0, 4):
            ev.append((b, P.CC, 0.9))
        if bar in (3, 7):
            ev.append((b + 12, P.M2, 0.9))
        if bar in (2, 6):
            ev.append((b + 8, P.SD, 0.7))
    return ev, 124, 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--out", default=str(ROOT / "docs"))
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    scratch = tempfile.mkdtemp(prefix="thunder-demo-")
    binary = build(a.sdk, scratch)
    # the default kit's keys in turn, 0.7 s apart (longer ones overlap the next)
    keys = P.default_kit()
    sounds = render(binary, [(k["model"], k["c"], 7, 0, 0) for k in keys], scratch)
    gap = int(0.7 * SR)
    left, right = np.zeros(gap * 24 + 6 * SR), np.zeros(gap * 24 + 6 * SR)
    for i, x in enumerate(sounds):
        place(left, right, x, i * gap, keys[i])
    end = gap * 24 + 2 * SR
    write(out / "demo-hits.wav", left[:end], right[:end])
    for name, pattern in (("kingston", kingston()), ("foundry", foundry())):
        ev, bpm, swing = pattern
        l, r = groove(binary, scratch, P.PRESETS[name], ev, bpm, swing)
        write(out / f"demo-{name}.wav", l, r)


if __name__ == "__main__":
    main()
