#!/usr/bin/env python3
"""Renders THUNDER's real engine on the host and measures it: every model at
its defaults, and each control swept with the others at their defaults.
Not part of the machine.

    python3 -I tools/sweep.py --sdk <open-xy> [--steps 5] [--model BOOM] [--wav DIR]

For each setting: the peak (of the SDK's half-scale ceiling), the loudness
(RMS over the sound, dB), the spectral centroid, how long until it falls
40 dB, a harmonic-to-noise ratio, and how far each step moves the sound from
the last (the mean change of its spectrum and envelope, in dB). A control
that moves the sound less than about 1.5 dB a step is flagged.
"""
import argparse, os, pathlib, re, subprocess, sys, tempfile
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
SR = 44100


def kit():
    text = (ROOT / "kit.h").read_text()
    models = re.findall(r'\{"(\w+)", \{([^}]*)\}, \{([^}]*)\}\}', text)
    return [(n, [l.strip().strip('"') for l in labels.split(",")], [int(x) for x in d.split(",")]) for n, labels, d in models]


def build(sdk, scratch):
    binary = pathlib.Path(scratch) / "render"
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools/render.cpp"), "-o", str(binary)], check=True)
    return binary


def render(binary, jobs, scratch):
    """jobs: (model, controls, tune, drive, seconds) -> arrays (float, 1.0 = full scale)."""
    lines, paths = [], []
    for i, (m, c, tune, drive, sec) in enumerate(jobs):
        p = os.path.join(scratch, f"j{i}.raw")
        paths.append(p)
        lines.append(f"{p} {m} {' '.join(map(str, c))} {tune} {drive} {sec}")
    r = subprocess.run([str(binary)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    out = []
    for p in paths:
        out.append(np.fromfile(p, dtype="<i4").astype(np.float64) / 2 ** 31)
        os.unlink(p)
    return out


BANDS = np.geomspace(40, 16000, 25)


def features(x):
    n = max(len(x), 64)
    x = np.pad(x, (0, max(0, 4096 - len(x))))
    seg = x[: int(0.8 * SR)]
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 16)) ** 2
    f = np.fft.rfftfreq(1 << 16, 1 / SR)
    bands = np.array([X[(f >= a) & (f < b)].sum() + 1e-12 for a, b in zip(BANDS[:-1], BANDS[1:])])
    bands_db = 10 * np.log10(bands / bands.sum())
    hop = int(0.01 * SR)
    env = np.array([np.sqrt(np.mean(x[i:i + hop] ** 2)) + 1e-9 for i in range(0, min(len(x), int(2.0 * SR)), hop)])
    env_db = 20 * np.log10(env)
    return bands_db, env_db


def measure(x):
    pk = np.max(np.abs(x)) if len(x) else 0
    if pk == 0:
        return dict(peak=0, rms=-120, cen=0, t40=0, hnr=0)
    above = np.nonzero(np.abs(x) > pk * 0.01)[0]
    t40 = above[-1] / SR
    live = x[: max(64, above[-1])]
    rms = 20 * np.log10(np.sqrt(np.mean(live[: int(0.5 * SR)] ** 2)) + 1e-12)
    seg = x[: int(0.6 * SR)]
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    cen = float((f * X).sum() / (X.sum() + 1e-12))
    on = x[: int(0.15 * SR)]
    on = on - on.mean()
    ac = np.correlate(on, on, "full")[len(on) - 1:]
    ac /= ac[0] + 1e-12
    lo, hi = int(SR / 1200), int(SR / 25)
    r = float(np.clip(np.max(ac[lo:hi]), 1e-4, 0.9999)) if len(ac) > hi else 1e-4
    hnr = 10 * np.log10(r / (1 - r))
    return dict(peak=pk * 2, rms=rms, cen=cen, t40=t40, hnr=hnr)


def spectrogram(x):
    """Band energies (dB re the loudest) in 23 ms frames over the first 1.5 s."""
    n, hop = 1024, 512
    x = np.pad(x[: int(1.5 * SR)], (0, int(1.5 * SR) + n - len(x[: int(1.5 * SR)])))
    f = np.fft.rfftfreq(n, 1 / SR)
    edges = np.geomspace(60, 16000, 19)
    idx = [(f >= a) & (f < b) for a, b in zip(edges[:-1], edges[1:])]
    w = np.hanning(n)
    frames = []
    for i in range(0, int(1.5 * SR), hop):
        X = np.abs(np.fft.rfft(x[i:i + n] * w)) ** 2
        frames.append([X[m].sum() + 1e-14 for m in idx])
    S = 10 * np.log10(np.array(frames))
    # weighted by the ear (A-weighting at each band's centre)
    fc = np.sqrt(edges[:-1] * edges[1:])
    f2 = fc ** 2
    ra = 12194 ** 2 * f2 ** 2 / ((f2 + 20.6 ** 2) * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194 ** 2))
    S = S + 20 * np.log10(ra) + 2.0
    return S - S.max()


def pitch(x):
    """The strongest partial's pitch (semitones) in 23 ms frames over the first second, where it sounds."""
    n, hop = 2048, 1024
    x = np.pad(x[: SR], (0, SR + n - len(x[: SR])))
    w = np.hanning(n)
    f = np.fft.rfftfreq(n, 1 / SR)
    out, lv = [], []
    for i in range(0, SR, hop):
        X = np.abs(np.fft.rfft(x[i:i + n] * w))
        k = int(np.argmax(X[2:])) + 2
        out.append(12 * np.log2(f[k] / 440))
        lv.append(X[k])
    lv = np.array(lv)
    return np.array(out), lv > lv.max() * 0.03


def distance(a, b):
    ba, ea = features(a)
    bb, eb = features(b)
    sa, sb = spectrogram(a), spectrogram(b)
    live = (sa > -45) | (sb > -45)
    # the change of the sound's spectrum moment to moment, where it sounds
    spec = float(np.mean(np.abs(np.clip(sa, -45, 0) - np.clip(sb, -45, 0))[live])) if live.any() else 0.0
    pa, la = pitch(a)
    pb, lb = pitch(b)
    both = la & lb
    if both.any():
        spec += min(6.0, float(np.mean(np.abs(pa - pb)[both])))
    n = max(len(ea), len(eb))
    ea = np.pad(ea, (0, n - len(ea)), constant_values=-180)
    eb = np.pad(eb, (0, n - len(eb)), constant_values=-180)
    ref = max(ea.max(), eb.max())
    mask = (ea > ref - 50) | (eb > ref - 50)
    env = float(np.mean(np.abs(np.clip(ea, ref - 50, 0) - np.clip(eb, ref - 50, 0))[mask])) if mask.any() else 0
    return spec, env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--steps", type=int, default=5)
    ap.add_argument("--model")
    ap.add_argument("--wav")
    ap.add_argument("--defaults", action="store_true")
    a = ap.parse_args()
    scratch = tempfile.mkdtemp(prefix="thunder-sweep-")
    binary = build(a.sdk, scratch)
    models = kit()
    if a.defaults:
        xs = render(binary, [(i, d, 0, 0, 6) for i, (n, l, d) in enumerate(models)], scratch)
        print(f"{'model':7s} {'peak':>5s} {'rms':>6s} {'cen':>6s} {'t40':>5s} {'hnr':>5s}")
        for (n, l, d), x in zip(models, xs):
            m = measure(x)
            print(f"{n:7s} {m['peak']:5.2f} {m['rms']:6.1f} {m['cen']:6.0f} {m['t40']:5.2f} {m['hnr']:5.1f}")
            if a.wav:
                write(os.path.join(a.wav, f"{n.lower()}.wav"), x)
        return
    steps = [round(127 * i / (a.steps - 1)) for i in range(a.steps)]
    weak = []
    for mi, (name, labels, dflt) in enumerate(models):
        if a.model and name != a.model:
            continue
        print(f"== {name}")
        for s in range(8):
            jobs = []
            for v in steps:
                c = list(dflt)
                c[s] = v
                jobs.append((mi, c, 0, 0, 6))
            xs = render(binary, jobs, scratch)
            ms = [measure(x) for x in xs]
            ds = [distance(xs[i], xs[i + 1]) for i in range(len(xs) - 1)]
            row = "  ".join(f"{m['peak']:.2f}/{m['rms']:.0f}/{m['cen']:.0f}/{m['t40']:.2f}" for m in ms)
            moves = " ".join(f"{sp:.1f}+{en:.1f}" for sp, en in ds)
            flag = [i for i, (sp, en) in enumerate(ds) if sp + en < 2.0]
            print(f"  {labels[s]:8s} {row}\n           moves {moves}{'  WEAK ' + str(flag) if flag else ''}")
            if flag:
                weak.append((name, labels[s], flag))
            if a.wav:
                for v, x in zip(steps, xs):
                    write(os.path.join(a.wav, f"{name.lower()}-{labels[s].lower()}-{v:03d}.wav"), x)
    print("weak:", weak)


def write(path, x):
    import wave
    s = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(s.tobytes())


if __name__ == "__main__":
    main()
