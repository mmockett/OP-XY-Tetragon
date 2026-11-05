"""Runs GALILEO's real engine on the host (tools/render.cpp) and measures it.
Used by tools/calibrate.py, tools/sweep.py and tools/demos.py. Needs numpy:
run these with the SDK's Python (open-xy/.venv/bin/python -I).

The SDK checkout comes from $GALILEO_SDK (default: ../open-xy
beside this repository).
"""
import math
import os
import pathlib
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import catalog  # noqa: E402

SR = 44100
SDK = pathlib.Path(os.environ.get("GALILEO_SDK", ROOT.parent.parent / "open-xy"))
BUILD = pathlib.Path(os.environ.get("GALILEO_BUILD", tempfile.gettempdir())) / "galileo-render"


def build():
    sources = [ROOT / "tools" / "render.cpp", ROOT / "tables.cpp"] + sorted(ROOT.glob("*.h")) + [ROOT / "engine.cpp"]
    if BUILD.exists() and all(BUILD.stat().st_mtime > s.stat().st_mtime for s in sources):
        return BUILD
    subprocess.run(["c++", "-std=c++11", "-O2", "-Wall", "-Werror", f"-I{SDK / 'sdk' / 'include'}", f"-I{ROOT}",
                    str(ROOT / "tools" / "render.cpp"), str(ROOT / "tables.cpp"), "-o", str(BUILD)], check=True)
    return BUILD


def note_line(at, key, model, values, space=0, feel=0, pan=0):
    m = catalog.model_index(model) if isinstance(model, str) else model
    return f"note {int(at)} {key} {m} {' '.join(str(int(v)) for v in values)} {space} {feel} {pan}"


def render(notes, length, bpm_x10=0, stats=False, levels=None):
    """notes: (at_samples, key, model, values[8], space, feel, pan); levels: {key: dB},
    each key's mixer level. Returns stereo float (n, 2) in full-scale units, and the
    stats text."""
    lines = [f"length {int(length)}", f"bpm {bpm_x10}"] + (["stats"] if stats else [])
    lines += [f"level {k} {int(db)}" for k, db in (levels or {}).items() if db]
    lines += [note_line(*n) for n in notes]
    r = subprocess.run([str(build())], input="\n".join(lines).encode(), capture_output=True, check=True)
    a = np.frombuffer(r.stdout, dtype="<i4").astype(np.float64) / 2 ** 31
    return a.reshape(-1, 2)[:int(length)], r.stderr.decode()


def one(model, values, space=0, feel=0, seconds=4.0, bpm_x10=1200):
    """One note, mono (the left channel of a centred voice)."""
    y, _ = render([(0, 0, model, values, space, feel, 0)], seconds * SR, bpm_x10)
    return y[:, 0]


def defaults(model, **over):
    return catalog.key(model, **over)[1]


def metrics(y):
    """Peak, RMS over the sound, spectral centroid, crest, length to -60 dB."""
    a = np.abs(y)
    peak = float(a.max()) if len(a) else 0.0
    if peak <= 0:
        return {"peak": 0.0, "rms_db": -200.0, "centroid": 0.0, "crest_db": 0.0, "length": 0.0}
    idx = np.nonzero(a > peak * 1e-3)[0]
    end = idx[-1] + 1
    seg = y[:end]
    rms = math.sqrt(float((seg ** 2).mean()))
    head = y[:min(len(y), int(0.6 * SR))]
    Y = np.abs(np.fft.rfft(head * np.hanning(len(head))))
    f = np.fft.rfftfreq(len(head), 1 / SR)
    Y[Y < Y.max() * 1e-3] = 0
    return {"peak": peak, "rms_db": 20 * math.log10(rms + 1e-12), "centroid": float((f * Y).sum() / (Y.sum() + 1e-12)),
            "crest_db": 20 * math.log10(peak / (rms + 1e-12)), "length": end / SR}


def loudness(y, span=0.4):
    """Short-term loudness in dB, K-weighted (BS.1770's high-pass and high
    shelf, from their magnitude responses), over the first `span` seconds."""
    h = np.asarray(y[:int(span * SR)], dtype=float)
    Y = np.abs(np.fft.rfft(h)) ** 2
    f = np.maximum(np.fft.rfftfreq(len(h), 1 / SR), 1e-3)
    k = (f / 38.0) ** 4 / (1 + (f / 38.0) ** 4) * (1 + 10 ** 0.4 * (f / 1500.0) ** 2) / (1 + (f / 1500.0) ** 2)
    energy = 2 * float((Y * k).sum()) / len(h) ** 2
    return 10 * math.log10(energy + 1e-20) - 0.691


def write_wav(path, y, normalise=False):
    """16-bit WAV: mono for a 1-D signal, stereo for (n, 2)."""
    y = np.asarray(y, dtype=float)
    if normalise:
        y = y / (np.abs(y).max() + 1e-12) * 10 ** (-1 / 20)
    data = (np.clip(y, -1, 1) * 32767).astype("<i2").tobytes()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1 if y.ndim == 1 else 2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data)


# The level audit's measures (tools/calibrate.py), in dB re the SDK's
# half-scale ceiling, the most a kit voice may put out (+6.02 on full scale).
CEILING_DB = 20 * math.log10(2)


def _biquad_ir(b, a, n):
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


def _k_ir():
    """BS.1770's two K-weighting stages, re-derived for 44.1 kHz (bilinear)."""
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    K = math.tan(math.pi * fc / SR)
    Vh, a0 = 10 ** (G / 20), 1 + K / Q + K * K
    Vb = Vh ** 0.4996667741545416
    shelf = _biquad_ir([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
                       [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], 8192)
    Q, fc = 0.5003270373253953, 38.13547087613982
    K = math.tan(math.pi * fc / SR)
    a0 = 1 + K / Q + K * K
    hp = _biquad_ir([1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], 16384)
    return np.convolve(shelf, hp)[:16384]


_K = None


def momentary_max(y):
    """The hit's loudness: K-weighted, the loudest 400 ms window (10 ms hop),
    LUFS-M max, in dB re the half-scale ceiling."""
    global _K
    if _K is None:
        _K = _k_ir()
    x = np.pad(np.asarray(y, float) * 2, (0, int(0.4 * SR)))
    n = 1 << int(np.ceil(np.log2(len(x) + len(_K))))
    k = np.fft.irfft(np.fft.rfft(x, n) * np.fft.rfft(_K, n), n)[:len(x)]
    c = np.concatenate([[0], np.cumsum(k * k)])
    block = int(0.4 * SR)
    starts = np.arange(0, len(k) - block + 1, int(0.01 * SR))
    return float(-0.691 + 10 * np.log10(max(((c[starts + block] - c[starts]) / block).max(), 1e-20)))


def true_peak(y):
    """4x oversampled peak, in dB re the half-scale ceiling."""
    x = np.asarray(y, float) * 2
    n = 1 << int(np.ceil(np.log2(len(x) + 256)))
    up = np.fft.irfft(np.fft.rfft(x, n), 4 * n) * 4
    return 20 * math.log10(max(float(np.abs(up).max()), 1e-12))


def hits(model, values, space=0, feel=0, seeds=3, gap=7.0, bpm_x10=1200):
    """The same note `seeds` times on one key, `gap` seconds apart (each its own
    random strokes); the list of each one's mono render (full-scale units)."""
    step = int(gap * SR) // 64 * 64     # notes start on a block, as the adapter starts them
    notes = [(i * step, 0, model, values, space, feel, 0) for i in range(seeds)]
    y, _ = render(notes, seeds * step, bpm_x10)
    return [y[i * step:(i + 1) * step, 0] for i in range(seeds)]
