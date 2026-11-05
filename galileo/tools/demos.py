#!/usr/bin/env python3
"""Renders GALILEO's demos from the real engine on the host (tools/render.cpp),
the way the SDK's adapter drives it (one voice per key, choked by its next
note; 16 voices), into docs/:

- demo-hits.wav: the default kit's 24 keys, one by one;
- demo-motion.wav: one note each, the motion row at work: flam, drag, ruff,
  press roll, a kick's rebound, a hat roll, a tine run, a coin settling, a
  shaker's strokes, more hands on a clap, a guiro scrape, an agogo pair, a
  clock's tick-tock, the frets' spacing from accelerating to slowing;
- demo-clockwork.wav, demo-workshop.wav: grooves on the Clockwork (120 BPM)
  and Workshop (104 BPM) kits, eight bars each.

The grooves pass their tempo as the proposed drum_transport would; on SDK 1.10
as released the kit learns it from the notes (tests/tempo.cpp). The demos follow
the suite's shared standard: stereo 44.1 kHz WAV, each key at its kit's own pan
and level, no sends, reverb or other processing, and each file normalised to
-16 LUFS integrated (ITU-R BS.1770: K-weighting, 400 ms blocks, the absolute
and relative gates) or, where that would put a peak over -1 dBFS, to a -1 dBFS
peak.

    open-xy/.venv/bin/python -I tools/demos.py
"""
import pathlib
import sys
import wave

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import catalog  # noqa: E402
import engine_host as E  # noqa: E402

DOCS = HERE.parent / "docs"
SR = E.SR
T = catalog.TILT


def sequence(items, gap=0.3, bpm_x10=1200, fall=-30):
    """One note per item (model, values, space, feel[, pad]) at its key's pan and
    level, each starting once the last has fallen 30 dB under its peak, the tails
    left to ring under it."""
    pieces, at = [], 0
    for item in items:
        model, values, space, feel = item[:4]
        pad = item[4] if len(item) > 4 else {}
        y, _ = E.render([(0, 0, model, values, space, feel, pad.get("pan", 0))], 6 * SR, bpm_x10,
                        levels={0: pad.get("level", 0)})
        a = np.abs(y).max(axis=1)
        end = int(np.nonzero(a > a.max() * 10 ** (-60 / 20))[0][-1]) + int(0.05 * SR)
        pieces.append((at, y[:end]))
        at += int(np.nonzero(a > a.max() * 10 ** (fall / 20))[0][-1]) + int(gap * SR)
    out = np.zeros((max(t + len(y) for t, y in pieces), 2))
    for t, y in pieces:
        out[t:t + len(y)] += y
    return out


# BS.1770's K-weighting and gated integrated loudness, in numpy alone.
def _biquad(b, a, n=SR):
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
    """A +4 dB high shelf, then a 38 Hz high-pass."""
    G, Q, fc = 3.99984385397, 0.7071752369554193, 1681.9744509555319
    A, w = 10 ** (G / 40), 2 * np.pi * fc / SR
    al, c = np.sin(w) / (2 * Q), np.cos(w)
    shelf = _biquad([A * ((A + 1) + (A - 1) * c + 2 * np.sqrt(A) * al), -2 * A * ((A - 1) + (A + 1) * c),
                     A * ((A + 1) + (A - 1) * c - 2 * np.sqrt(A) * al)],
                    [(A + 1) - (A - 1) * c + 2 * np.sqrt(A) * al, 2 * ((A - 1) - (A + 1) * c),
                     (A + 1) - (A - 1) * c - 2 * np.sqrt(A) * al])
    Q, fc = 0.5003270373253953, 38.13547087613982
    w = 2 * np.pi * fc / SR
    al, c = np.sin(w) / (2 * Q), np.cos(w)
    hp = _biquad([(1 + c) / 2, -(1 + c), (1 + c) / 2], [1 + al, -2 * c, 1 - al])
    h = np.convolve(shelf[:8192], hp)[:SR]
    n = 1 << int(np.ceil(np.log2(len(x) + len(h))))
    return np.fft.irfft(np.fft.rfft(x, n, axis=0) * np.fft.rfft(h, n)[:, None], n, axis=0)[:len(x)]


def lufs(x):
    """Integrated loudness, LUFS (BS.1770-4), of a stereo signal."""
    y = k_weight(x)
    block, hop = int(0.4 * SR), int(0.1 * SR)
    z = np.array([np.mean(y[i:i + block] ** 2, axis=0).sum() for i in range(0, len(y) - block + 1, hop)])
    z = z[-0.691 + 10 * np.log10(z + 1e-20) > -70]
    z = z[-0.691 + 10 * np.log10(z) > -0.691 + 10 * np.log10(z.mean()) - 10]
    return -0.691 + 10 * np.log10(z.mean())


TARGET, CEILING = -16.0, 10 ** (-1 / 20)


def write(path, x):
    """16-bit stereo at -16 LUFS, or at a -1 dBFS peak if that is lower."""
    x = np.asarray(x, float)
    gain = min(10 ** ((TARGET - lufs(x)) / 20), CEILING / (np.max(np.abs(x)) + 1e-12))
    x = x * gain
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(np.round(np.clip(x, -1, 1) * 32767).astype("<i2").tobytes())
    print(f"wrote {path}: {lufs(x):.1f} LUFS, peak {20 * np.log10(np.max(np.abs(x))):.1f} dBFS, {len(x) / SR:.1f} s")


def hits():
    return sequence([(k[0], k[1], k.pad.get("space", 0), k.pad.get("feel", 0), k.pad) for k in catalog.DEFAULT_KIT])


def motion():
    k = catalog.key
    f, b, n = catalog.frets, catalog.bounce, catalog.balls
    items = [
        k("SNARE", balls=n(2)), k("SNARE", frets=f(2), tilt=T["1/32"]), k("SNARE", frets=f(3), tilt=T["1/32"]),
        k("SNARE", frets=f(16), tilt=T["1/64"]), k("KICK", bounce=b(0.5), tilt=T["1/16"]),
        k("KICK", decay=76, bounce=b(0.85), tilt=T["1/8"]), k("HAT", frets=f(4), tilt=T["1/32"]),
        k("HAT", frets=f(6), tilt=T["1/32T"]), k("TINE", frets=f(6), bounce=b(0.5), tilt=T["1/16"]),
        k("RIDE", bounce=b(0.9), tilt=80), k("SHAKER", balls=n(16), frets=f(1), tilt=T["1/8"]),
        k("CLAP", balls=n(8), room=76), k("RATCHET", teeth=127, frets=f(4), tilt=T["1/16"]),
        k("BELL", frets=f(4), pair=89, tilt=T["1/16"]), k("CLOCK", frets=f(8), tilt=T["1/8"], case=100),
    ]
    out = [(m, v, 0, 0) for m, v in items]
    # SPACE: eight frets of a tom, accelerating, Galileo's even beats, slowing.
    tom = k("TOM", frets=f(8), tilt=T["1/16"])
    out += [(tom[0], tom[1], s, 0) for s in (-16, 0, 15)]
    # FEEL: a hat roll locked, swung, loose.
    hat = k("HAT", frets=f(8), tilt=T["1/16"])
    out += [(hat[0], hat[1], 0, v) for v in (0, 5, 13)]
    return sequence(out)


def groove(kit, notes, bpm, bars):
    p16 = 60 / bpm / 4
    keys = [p for p in catalog.PRESETS if p[0] == kit][0][2]
    score = []
    for bar, step, key in notes:
        model, values = keys[key]
        pad = keys[key].pad    # the kit preset's own SPACE, FEEL and pan
        score.append((int((bar * 16 + step) * p16 * SR), key, model, values, pad.get("space", 0), pad.get("feel", 0),
                      pad.get("pan", 0)))
    levels = {i: k.pad.get("level", 0) for i, k in enumerate(keys)}
    y, _ = E.render(score, (bars * 16 * p16 + 2.5) * SR, int(round(bpm * 10)), levels=levels)
    y = y[:int((bars * 16 * p16 + 2.0) * SR)]
    fade = int(0.4 * SR)
    y[-fade:] *= np.linspace(1, 0, fade)[:, None]
    return y


def clockwork():
    K, KB, CP, SN, RM, H, HR, OH, HT, HF, SR_, RT, BL = 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 13, 14, 15
    n = []
    for bar in range(8):
        last = bar % 4 == 3
        n += [(bar, 0, K)]
        n += [(bar, 10, KB)] if bar % 2 == 0 else [(bar, 7, K), (bar, 11, KB)]
        if last:
            n += [(bar, 14, K)]
        for s in (4, 12):
            n += [(bar, s, CP), (bar, s, SN)]
        for s in range(0, 16, 2):
            if bar % 2 == 1 and s == 6:
                n += [(bar, s, HR)]
                continue
            if bar % 2 == 1 and s == 8:
                continue
            if s == 14 and bar % 4 == 1:
                n += [(bar, s, HT)]
                continue
            if s == 10 and last:
                n += [(bar, s, HF)]
                continue
            if s == 12 and last:
                continue
            n += [(bar, s, H)]
        if bar % 4 == 2:
            n += [(bar, 15, OH)]
        n += [(bar, 3, RM)] if bar % 2 else []
        if bar == 3:
            n += [(bar, 9, RT)]
        if bar == 7:
            n = [x for x in n if not (x[0] == 7 and x[1] >= 10 and x[2] in (CP, SN, H, HF, K))]
            n += [(bar, 10, SR_)]
        if bar in (1, 5):
            n += [(bar, 13, BL)]
    n += [(8, 0, K), (8, 0, CP)]
    return groove("clockwork", n, 120, 8)


def workshop():
    K, S, SG, SH, TN, FL, CK, HD, HS, RD = 0, 2, 3, 4, 8, 9, 10, 11, 12, 13
    n = []
    for bar in range(8):
        n += [(bar, 0, K), (bar, 7, K)] + ([(bar, 10, K)] if bar % 2 else [(bar, 11, K)])
        n += [(bar, 4, S), (bar, 12, S)]
        n += [(bar, 9, SG)] + ([(bar, 14, SG)] if bar % 2 == 0 else [])
        for s in (0, 4, 8, 12):
            n += [(bar, s, SH)]
        if bar % 2 == 0:
            n += [(bar, 2, CK)]
        n += [(bar, 6, HD), (bar, 15, HS)] if bar % 2 else [(bar, 13, HD)]
        if bar in (1, 5):
            n += [(bar, 8, TN)]
        if bar == 3:
            n += [(bar, 14, TN)]
    n = [x for x in n if not (x[0] == 7 and x[1] >= 8 and x[2] in (S, SG, K, HS, HD, SH))]
    n += [(7, 8, FL), (8, 0, K), (8, 0, RD), (8, 0, S)]
    return groove("workshop", n, 104, 8)


if __name__ == "__main__":
    DOCS.mkdir(exist_ok=True)
    write(DOCS / "demo-hits.wav", hits())
    write(DOCS / "demo-motion.wav", motion())
    write(DOCS / "demo-clockwork.wav", clockwork())
    write(DOCS / "demo-workshop.wav", workshop())
