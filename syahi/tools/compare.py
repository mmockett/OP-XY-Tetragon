#!/usr/bin/env python3
"""Renders hits on the real engine (tools/render.cpp, built to $SYAHI_RENDER) and,
when the prototype is given (--proto <folder with proto.py>), the prototype's,
and prints stand-ins for listening: peak, HNR, centroid, crest, length, and how
closely their spectra agree (1/3-octave bands over the first 0.3 s).

    python -I tools/compare.py [--proto DIR] [model c0..c7 [human grit]] ...
"""
import argparse, os, pathlib, subprocess, sys, tempfile, wave
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kits  # noqa: E402

SR = 44100
RENDER = os.environ.get("SYAHI_RENDER", "render")


def engine(model, ctl, human=0, grit=0, seconds=3.0, extra=()):
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "x.wav"
        args = [RENDER, str(out), str(seconds), str(kits.MODELS.index(model)), *map(str, ctl), str(human), str(grit), "0", "0", "1"]
        args += list(map(str, extra))
        subprocess.run(args, check=True)
        with wave.open(str(out)) as w:
            y = np.frombuffer(w.readframes(w.getnframes()), "<i4").astype(float) / 2 ** 31
    return y


def metrics(y):
    a = np.abs(y)
    if a.max() <= 0:
        return dict(peak=0, hnr=0, cen=0, crest=0, length=0)
    end = np.nonzero(a > a.max() * .01)[0][-1] + 1
    seg = y[:end]
    crest = 20 * np.log10(a.max() / (np.sqrt(np.mean(seg ** 2)) + 1e-12))
    s = y[:int(.3 * SR)]
    S = np.abs(np.fft.rfft(s * np.hanning(len(s))))
    f = np.fft.rfftfreq(len(s), 1 / SR)
    cen = (S * f).sum() / S.sum()
    x = y[int(.015 * SR):int(.2 * SR)]
    x = x - x.mean()
    m = 1 << (2 * len(x) - 1).bit_length()
    X = np.fft.rfft(x, m)
    ac = np.fft.irfft(np.abs(X) ** 2, m)[:len(x)]
    ac /= ac[0] + 1e-12
    lo, hi = int(SR / 2000), min(int(SR / 25), len(ac) - 1)
    rr = np.clip(ac[lo:hi].max(), 1e-4, .9999)
    hnr = 10 * np.log10(rr / (1 - rr))
    above = np.nonzero(a > a.max() * 10 ** (-60 / 20))[0]
    return dict(peak=a.max(), hnr=hnr, cen=cen, crest=crest, length=(above[-1] + 1) / SR)


def bands(y):
    s = y[:int(.3 * SR)]
    s = np.pad(s, (0, max(0, int(.3 * SR) - len(s))))
    S = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2
    f = np.fft.rfftfreq(len(s), 1 / SR)
    edges = 25 * 2 ** (np.arange(0, 30) / 3)
    e = np.array([S[(f >= a) & (f < b)].sum() for a, b in zip(edges[:-1], edges[1:])]) + 1e-20
    return 10 * np.log10(e / e.max())


def agree(a, b):
    """Mean absolute difference of band levels (dB) where either is within 40 dB of its top."""
    A, B = bands(a), bands(b)
    m = (A > -40) | (B > -40)
    return float(np.mean(np.abs(np.maximum(A, -60)[m] - np.maximum(B, -60)[m])))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--proto")
    p.add_argument("rest", nargs="*")
    a = p.parse_args()
    proto = None
    if a.proto:
        sys.path.insert(0, a.proto)
        import proto as proto  # noqa
    jobs = []
    r = a.rest
    i = 0
    while i < len(r):
        model = r[i]
        ctl = list(map(int, r[i + 1:i + 9]))
        i += 9
        hg = (0, 0)
        if i + 1 < len(r) and r[i].lstrip("-").isdigit():
            hg = (int(r[i]), int(r[i + 1]))
            i += 2
        jobs.append((model, ctl, hg))
    if not jobs:
        jobs = [(m, kits.DEFAULTS[m], (0, 0)) for m in kits.MODELS]
    print(f"{'model':7} {'controls':34} {'peak':>6} {'HNR':>6} {'cen':>6} {'crest':>6} {'len':>5} |"
          f" {'pHNR':>6} {'pcen':>6} {'pcrst':>6} {'plen':>5} {'bandsdB':>7}")
    for model, ctl, hg in jobs:
        y = engine(model, ctl, *hg)
        m = metrics(y)
        line = (f"{model:7} {str(ctl):34} {m['peak']:6.3f} {m['hnr']:6.1f} {m['cen']:6.0f} {m['crest']:6.1f} {m['length']:5.2f} |")
        if proto:
            z = proto.hit(model, ctl, 1.0, hg, 0)
            q = metrics(z)
            line += f" {q['hnr']:6.1f} {q['cen']:6.0f} {q['crest']:6.1f} {q['length']:5.2f} {agree(y, z):7.1f}"
        print(line)


if __name__ == "__main__":
    main()
