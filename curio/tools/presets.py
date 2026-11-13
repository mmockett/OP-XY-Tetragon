#!/usr/bin/env python3
"""CURIO's kit presets, and the files the OP-XY loads them from.

    python3 tools/presets.py --out <folder>        # writes <folder>/curio/<name>.json

Each kit sets every key's specimen and eight controls; a key a kit doesn't
name keeps the default kit's (kit.h). A preset is the OP-XY's version-4
structure with the kit in its regions, as MD KIT's are written (OP-XY-MNMD
tools/build_firmware.py, write_presets): a region per key, Drumline type 7
("fm slap", the SDK's mark) and the 60 bits the SDK stores (eight 7-bit
controls, the model in bits 56..59) in the top 15 bits of four params. The
SDK's builder installs only presets without regions, so a command-line
build adds these to content/presets/curio/ itself, as MNMD's does.

Each key's own settings travel too. Stock's preset writer keeps a pad's
pan (-100..100) and gain (dB) as "pan" and "gain", and its pad word +0x14 as
"random": the word's top 16 bits, written only when non-zero and restored
shifted back (max(random << 16, 0)). The SDK packs a padMix kit's pad fields
there: TUNE in bits 16..20 (signed), VARY in 21..24, FX I's code in 25..27 and
FX II's in 28..30 (0 follows the track's send; 1..7 send 0, 17, 33, 50, 67,
83, 100). Bit 31 stays clear, so the reader's max() keeps them. A send is TRK
or the key's own 0..100 in 1% steps (SDK 1.15): its code is the nearest of
those seven steps, and random's bits 16..22 (FX I) and 23..29 (FX II) carry
the amount itself (send_code, below).

    python3 tools/presets.py --check    # decode every preset back and compare
"""
import argparse
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
NAMES = ["ball", "cask", "stone", "drip", "ice", "sheet", "spark", "cork",
         "cicada", "ratchet", "pod", "match", "glass", "bell", "chalk", "roar"]
LABELS = {}


def kit_h():
    src = (ROOT / "kit.h").read_text()
    # Keyed by NAMES, the specimens' full names: kit.h's header names are
    # shortened to fit (cicda, rtcht, mtch).
    for name, (_, labels) in zip(NAMES, re.findall(r'\{"([a-z ]+)", \d, \{([^}]*)\}', src)):
        LABELS[name] = [l.strip().strip('"').lower() for l in labels.split(",")]
    key_model = [int(v) for v in re.search(r"key_model\[keys\] = \{([^}]*)\}", src).group(1).split(",")]
    rows = re.findall(r"\{([0-9, ]+)\},\s+//", src)
    key_values = [[int(v) for v in r.split(",")] for r in rows]
    return key_model, key_values


def pct(v):
    return min(127, round(v * 1.27))


# Each kit: key (1..24) -> (specimen, {control: 0..100}); unnamed controls take
# the specimen's default-kit values on that key, or the design defaults.
KITS = {
    "cabinet": {},   # the default kit (kit.h)
    # Dusty boom bap at 90-100 BPM: a soft felt ball with a ghost bounce, a
    # wet ice snare with a deep lake, cork rims, a dark cicada, a tuned glass.
    "cellar": {
        1: ("ball", {"drop": 50, "bounce": 38, "skin": 40, "ball": 22, "decay": 30, "floor": 25}),
        3: ("ice", {"load": 45, "sheet": 55, "tone": 25, "ring": 25, "cold": 35, "lake": 65, "shards": 15}),
        4: ("cork", {"neck": 55, "fizz": 0, "thock": 60, "bottle": 20}),
        7: ("cicada", {"ribs": 20, "song": 60, "shell": 45, "body": 25, "tail": 15}),
        11: ("match", {"scrape": 35, "flare": 30, "draw": 15, "hiss": 30}),
        13: ("pod", {"seeds": 35, "shake": 20, "shell": 40}),
        10: ("cask", {"size": 45, "fill": 20, "stave": 35, "cellar": 40}),
        15: ("glass", {"water": 30, "strike": 30, "ring": 50, "slosh": 25}),
    },
    # Industrial techno at 124+: a steel ball with no bounce and grit, a
    # spark and sheet clap stack, open cicadas, ratchet 16ths, a bell jar
    # half pumped out, a roar riser.
    "foundry": {
        1: ("ball", {"drop": 75, "bounce": 0, "skin": 33, "ball": 55, "decay": 30, "floor": 10, "grit": 35}),
        5: ("spark", {"charge": 55, "gap": 45, "jar": 40, "ozone": 45, "scatter": 30}),
        6: ("sheet", {"size": 35, "shake": 15, "rattle": 60, "hang": 22, "beater": 60, "metal": 65}),
        20: ("sheet", {"size": 70, "shake": 55, "rattle": 30, "hang": 70, "thunder": 55, "metal": 55}),
        11: ("cicada", {"ribs": 50, "song": 80, "shell": 72, "body": 35, "tail": 38, "chirp": 25}),
        14: ("ratchet", {"teeth": 0, "slat": 80, "wood": 70, "box": 0}),
        16: ("bell", {"strokes": 9, "bell": 40, "air": 55, "ring": 30}),
        23: ("roar", {"whirl": 55, "size": 45, "cord": 50, "swell": 80, "length": 70, "throb": 70, "buzz": 45}),
    },
    # UK garage and 2-step: a crisp, cold ice, chirping cicada hats, a high
    # cork, stone toms.
    "frost": {
        1: ("ball", {"drop": 45, "bounce": 20, "skin": 50, "ball": 45, "decay": 30, "floor": 10}),
        3: ("ice", {"load": 70, "sheet": 25, "shards": 30, "tone": 45, "ring": 30, "cold": 90, "lake": 40}),
        4: ("cork", {"neck": 85, "pop": 70, "thock": 70, "bottle": 10}),
        7: ("cicada", {"ribs": 15, "song": 85, "shell": 85, "body": 45, "tail": 10, "chirp": 40}),
        9: ("cicada", {"ribs": 8, "song": 70, "shell": 70, "body": 50, "tail": 5, "chirp": 60}),
        11: ("cicada", {"ribs": 45, "song": 90, "shell": 90, "body": 40, "tail": 35, "chirp": 50}),
        8: ("stone", {"pitch": 15, "mallet": 40, "ring": 40, "dull": 40}),
        10: ("stone", {"pitch": 30, "mallet": 40, "ring": 40, "dull": 40}),
        12: ("stone", {"pitch": 45, "mallet": 40, "ring": 40, "dull": 40}),
    },
    # Glitch and IDM micro-percussion: drip rolls, a cicada swarm, chalk, a
    # worn ratchet, cork fizz.
    "hothouse": {
        2: ("drip", {"size": 30, "rise": 80, "tap": 80, "depth": 20, "splash": 40, "echo": 50}),
        7: ("cicada", {"ribs": 35, "song": 95, "shell": 80, "body": 70, "tail": 20, "swarm": 80}),
        13: ("drip", {"size": 75, "rise": 60, "tap": 40, "echo": 30}),
        14: ("ratchet", {"teeth": 60, "crank": 85, "slat": 70, "wood": 30, "wear": 80, "spring": 50}),
        18: ("cork", {"neck": 60, "fizz": 90, "spill": 80, "glug": 30}),
        22: ("chalk", {"pitch": 70, "press": 70, "glide": 80, "length": 10, "chatter": 60}),
        21: ("chalk", {"pitch": 30, "press": 30, "glide": 10, "length": 20, "grit": 60}),
    },
    # Tuned percussion: glass, stone, drip and bell jar up a pentatonic scale
    # across the keys (the specimens' own pitch controls), a light kick, a shaker.
    "glasshouse": {
        1: ("ball", {"drop": 35, "bounce": 15, "skin": 55, "ball": 30, "decay": 25}),
        13: ("pod", {"seeds": 40, "shake": 15, "shell": 70}),
        **{k: ("glass", {"rim": r, "water": 20, "ring": 55, "strike": 50}) for k, r in zip((15, 17, 19, 21, 23), (95, 80, 66, 52, 38))},
        **{k: ("stone", {"pitch": p, "ring": 50, "mallet": 55, "dull": 20}) for k, p in zip((8, 10, 12, 14, 16), (20, 33, 40, 53, 66))},
        **{k: ("bell", {"bell": b, "air": 10, "ring": 40}) for k, b in zip((18, 20, 22), (20, 42, 53))},
        24: ("drip", {"size": 40, "rise": 30, "basin": 70, "echo": 40}),
    },
    # Brushes and acoustic: match brushes with a long draw, a pod, a soft
    # knuckle on the cask, stone.
    "parlour": {
        1: ("cask", {"size": 20, "fill": 10, "knock": 15, "stave": 20, "cellar": 10}),
        3: ("match", {"scrape": 60, "flare": 20, "draw": 90, "hiss": 20, "phos": 0}),
        5: ("match", {"scrape": 40, "flare": 15, "draw": 60, "hiss": 10}),
        7: ("pod", {"seeds": 30, "shake": 10, "shell": 60, "size": 20}),
        11: ("pod", {"seeds": 60, "shake": 50, "shell": 50, "swish": 50}),
        8: ("cask", {"size": 35, "fill": 30, "knock": 20}),
        10: ("cask", {"size": 55, "fill": 30, "knock": 20}),
        12: ("cask", {"size": 75, "fill": 30, "knock": 20}),
        17: ("stone", {"pitch": 25, "mallet": 25, "ring": 60}),
    },
    # Cinematic and trailer: a dribbling ball boom, sheet thunder rolls, the
    # roar, an ice "pew" with spread, a bell jar at full vacuum.
    "storm": {
        1: ("ball", {"drop": 90, "bounce": 70, "skin": 15, "ball": 20, "decay": 90, "floor": 80}),
        2: ("cask", {"size": 0, "fill": 0, "knock": 40, "cellar": 70}),
        6: ("sheet", {"size": 80, "shake": 30, "hang": 85, "thunder": 90, "roll": 70, "beater": 30}),
        20: ("sheet", {"size": 100, "shake": 80, "hang": 100, "thunder": 100, "metal": 40}),
        23: ("roar", {"whirl": 30, "size": 80, "cord": 70, "swell": 90, "length": 100, "throb": 80, "buzz": 30, "wind": 60}),
        24: ("ice", {"sheet": 90, "tone": 100, "ring": 80, "spread": 80, "load": 40}),
        16: ("bell", {"strokes": 30, "air": 100, "jar": 80, "ring": 70}),
    },
}
CHOKED = {7, 9, 11}   # the hats cut each other (playmode mute1)

# Each key's own settings: tune (semitones), vary (0..15), fx1 and fx2 (the
# key's own send, 0..100; TRK, the track's send, when not named), pan, level (dB).
PADS = {
    "cabinet": {},
    "cellar": {
        1: dict(vary=2, level=-1), 3: dict(pan=5, vary=4, fx2=33), 4: dict(pan=-30, vary=6),
        7: dict(pan=30, vary=8), 11: dict(pan=35, vary=4, fx2=17), 13: dict(pan=-45, vary=10),
        10: dict(pan=-20, vary=3, fx2=33, level=-3), 15: dict(pan=20, tune=-5, fx2=50), 20: dict(level=-1),
    },
    "foundry": {
        1: dict(vary=1), 5: dict(vary=5, fx2=33), 6: dict(pan=10, vary=4, fx2=33),
        20: dict(pan=-30, fx2=50), 11: dict(pan=25, vary=5), 14: dict(pan=-35, vary=9),
        16: dict(pan=40, vary=4, fx1=33, fx2=33), 23: dict(fx1=50, fx2=50),
    },
    "frost": {
        3: dict(vary=3, fx2=17), 4: dict(pan=-25, vary=5), 7: dict(pan=25, vary=7),
        9: dict(pan=30, vary=7), 11: dict(pan=35, vary=5),
        8: dict(pan=-30), 10: dict(pan=-10), 12: dict(pan=15), 20: dict(level=-1),
    },
    "hothouse": {
        2: dict(vary=12, pan=-40, fx1=33), 7: dict(vary=10, pan=35), 13: dict(vary=15, pan=50, fx1=50),
        14: dict(vary=13, pan=-50), 18: dict(vary=9, level=-4), 22: dict(vary=11, pan=20, fx1=33), 21: dict(vary=8, pan=-20), 20: dict(level=-1),
    },
    # The pentatonic scale is TUNE's: each instrument's keys share one voicing.
    "glasshouse": {
        **{k: dict(tune=t, fx2=33, pan=p, **({"level": lv} if lv else {}))
           for k, t, p, lv in zip((15, 17, 19, 21, 23), (0, 2, 4, 7, 9), (-40, -20, 0, 20, 40), (0, 0, 0, -1, -1))},
        **{k: dict(tune=t, pan=p) for k, t, p in zip((8, 10, 12, 14, 16), (0, 3, 5, 7, 10), (-30, -15, 0, 15, 30))},
        **{k: dict(tune=t, fx2=50) for k, t in zip((18, 20, 22), (0, 4, 7))},
        13: dict(vary=8, pan=-35),
    },
    "parlour": {
        3: dict(vary=9, fx2=17), 5: dict(vary=9, pan=-20), 7: dict(vary=10, pan=30), 11: dict(vary=8, pan=-30),
        8: dict(pan=-25, level=-1), 10: dict(pan=0, level=-1), 12: dict(pan=25, level=-2), 17: dict(fx2=33), 20: dict(level=-1),
    },
    "storm": {
        1: dict(fx2=33), 2: dict(fx2=50), 6: dict(fx2=67, pan=-20, level=-2), 20: dict(fx2=83, pan=25), 23: dict(fx1=67, fx2=83, level=-2),
        24: dict(fx1=83, fx2=67, vary=4), 16: dict(fx2=83, pan=40),
    },
}
for kit in KITS:
    PADS.setdefault(kit, {})
for kit, keys in KITS.items():
    if kit == "glasshouse":
        # One voicing per instrument, tuned by TUNE (above).
        for k in (15, 17, 19, 21, 23):
            keys[k][1]["rim"] = 60
        for k in (8, 10, 12, 14, 16):
            keys[k][1]["pitch"] = 25
        for k in (18, 20, 22):
            keys[k][1]["bell"] = 30

# Each key's own FX I and FX II sends (SDK 1.15, etcetc000/open-xy#518): TRK,
# which follows the track's send, or the key's own amount, 0..100 in 1% steps.
# A region's "random" carries a send twice: its 3-bit code (FX I's at bits
# 9..11, FX II's at 12..14), 0 for TRK or 1..7 for the nearest of the seven
# steps firmware before SDK 1.15 plays (STEPS); and its amount + 1 (FX I's at
# bits 16..22, FX II's at 23..29), 0 for TRK, which SDK 1.15's preset import
# keeps in the key's record. Stock's import drops random's bits from 16, so
# older firmware reads the codes alone; SDK 1.15 plays the amount, as its code
# is always the amount's nearest step (the SDK's drum_send.hpp).
TRK = None
STEPS = (0, 17, 33, 50, 67, 83, 100)   # codes 1..7, as SDK 1.15 shows them


def send_code(amount):
    """A send's 3-bit code: 0 for TRK, else the amount's nearest step, halves up (drum_send_shadow)."""
    return 0 if amount is TRK else 1 + (6 * amount + 50) // 100


def send_index(amount):
    """A send's amount as random keeps it from bit 16: 0 for TRK, else the amount + 1."""
    return 0 if amount is TRK else amount + 1


def send_ok(amount):
    return amount is TRK or (type(amount) is int and 0 <= amount <= 100)


def sends_read(random):
    """Both sends as SDK 1.15 reads them back from random (drum_send_stored): the amount
    when its byte holds one and the code beside it is its step; else the old code, as
    ("code", c), or TRK."""
    sends = []
    for s in (0, 1):
        index, code = (random >> (16 + 7 * s)) & 0x7f, (random >> (9 + 3 * s)) & 7
        if 1 <= index <= 101 and code == send_code(index - 1):
            sends.append(index - 1)
        else:
            sends.append(("code", code) if code else TRK)
    return sends


def random_word(pad):
    """The pad word's top 16 bits as stock's preset keeps them ("random"), the sends'
    codes among them; then the sends' amounts from bit 16 (SDK 1.15)."""
    tune, vary, fx1, fx2 = pad.get("tune", 0), pad.get("vary", 0), pad.get("fx1", TRK), pad.get("fx2", TRK)
    assert -16 <= tune <= 15 and 0 <= vary <= 15 and send_ok(fx1) and send_ok(fx2)
    value = (tune & 0x1f) | vary << 5 | send_code(fx1) << 9 | send_code(fx2) << 12
    assert value < 0x8000   # bit 31 of the word stays clear
    return value | send_index(fx1) << 16 | send_index(fx2) << 23


def decode(region):
    """What the device restores: the model, controls and pad fields (the sends as their
    codes: stock's reader drops random's bits from 16)."""
    value = sum((w & 0x7fff) << (15 * i) for i, w in enumerate(region["params"]))
    controls = [(value >> (7 * i)) & 127 for i in range(8)]
    model = (value >> 56) & 15
    word = max(((region.get("random", 0) << 16) + 2 ** 31) % 2 ** 32 - 2 ** 31, 0)
    def field(low, bits, sign):
        raw = (word >> low) & ((1 << bits) - 1)
        return raw - (1 << bits) if sign and raw >> (bits - 1) else raw
    pad = dict(tune=field(16, 5, True), vary=field(21, 4, False), fx1=field(25, 3, False), fx2=field(28, 3, False),
               pan=region.get("pan", 0), level=region.get("gain", 0))
    return model, controls, pad


def kit_keys(name):
    key_model, key_values = kit_h()
    defaults = {n: v for n, v in zip(NAMES, re.findall(r'\{"[a-z ]+", \d, \{[^}]*\}, \{([^}]*)\}', (ROOT / "kit.h").read_text()))}
    keys = []
    for k in range(24):
        model, values = key_model[k], list(key_values[k])
        if k + 1 in KITS[name]:
            spec, ctl = KITS[name][k + 1]
            m = NAMES.index(spec)
            if m != model:
                values = [int(v) for v in defaults[spec].split(",")]
            model = m
            for label, v in ctl.items():
                values[LABELS[spec].index(label)] = pct(v)
        keys.append((model, values))
    return keys


def region(key, model, controls, choked, pad=None):
    pad = pad or {}
    value = sum(c << (7 * i) for i, c in enumerate(controls)) | model << 56
    params = [(value >> (15 * w)) & 0x7fff for w in range(4)]
    r = {"hikey": key, "model": "fm slap", "params": params}
    if choked:
        r["playmode"] = "mute1"
    if random_word(pad):
        r["random"] = random_word(pad)
    if pad.get("pan", 0):
        assert -100 <= pad["pan"] <= 100
        r["pan"] = pad["pan"]
    if pad.get("level", 0):
        assert -30 <= pad["level"] <= 0   # the kit plays 0 dB at most
        r["gain"] = pad["level"]
    return r


def regions_of(name):
    return [region(53 + k, m, v, k + 1 in CHOKED, PADS[name].get(k + 1)) for k, (m, v) in enumerate(kit_keys(name))]


def check():
    """Every preset decodes back to its kit: models, controls and pad fields."""
    for name in KITS:
        for k, ((m, v), r) in enumerate(zip(kit_keys(name), regions_of(name))):
            dm, dv, dp = decode(r)
            pad = {"tune": 0, "vary": 0, "fx1": TRK, "fx2": TRK, "pan": 0, "level": 0, **PADS[name].get(k + 1, {})}
            want = {**pad, "fx1": send_code(pad["fx1"]), "fx2": send_code(pad["fx2"])}
            assert (dm, dv) == (m, v), (name, k + 1)
            assert dp == want, (name, k + 1, dp, want)
            random = r.get("random", 0)
            assert 0 <= random < 1 << 30 and not random & 0x8000, (name, k + 1, random)
            # SDK 1.15 reads each send back as the amount written. The kits were
            # voiced on the seven steps, so each amount is the percent SDK 1.15
            # shows for (and turns on from) the code these presets carried before
            # SDK 1.15 (drum_send_percent), and the code is unchanged.
            assert sends_read(random) == [pad["fx1"], pad["fx2"]], (name, k + 1, random)
            assert all(a is TRK or a == STEPS[send_code(a) - 1] for a in (pad["fx1"], pad["fx2"])), (name, k + 1)
    print(f"{len(KITS)} kits decode back: specimens, controls, TUNE, VARY, sends, pan and level")


def preset(regions):
    """The device's version-4 structure, as the SDK's builder writes its init
    presets (builder/core.mjs, defaultPreset), with a drum kit's neutral offsets."""
    return {
        "engine": {
            "bendrange": 8191, "highpass": 0,
            "modulation": {s: {"amount": 16384, "target": 0} for s in ("aftertouch", "modwheel", "pitchbend", "velocity")},
            "params": [16384] * 8, "playmode": "poly",
            "portamento.amount": 0, "portamento.type": 0, "transpose": 0,
            "tuning.root": 0, "tuning.scale": 0, "velocity.sensitivity": 0, "volume": 16384, "width": 0,
        },
        "envelope": {
            "amp": {"attack": 0, "decay": 32767, "release": 32767, "sustain": 32767},
            "filter": {"attack": 0, "decay": 0, "release": 0, "sustain": 32767},
        },
        "fx": {"active": False, "params": [0, 0, 0, 0, 0, 32767, 0, 0], "type": "svf"},
        "lfo": {"active": False, "params": [0, 0, 0, 0, 0, 0, 0, 0], "type": "value"},
        "octave": 0, "platform": "OP-XY", "regions": regions, "type": "curio", "version": 4,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", help="a folder: writes <out>/curio/<name>.json")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    check()
    if not a.out:
        return
    folder = pathlib.Path(a.out) / "curio"
    folder.mkdir(parents=True, exist_ok=True)
    for name in KITS:
        regions = regions_of(name)
        target = folder / ("default.json" if name == "cabinet" else f"{name}.json")
        target.write_text(json.dumps(preset(regions), indent=2) + "\n")
        if name == "cabinet":
            (folder / "cabinet.json").write_text(json.dumps(preset(regions), indent=2) + "\n")
        print("wrote", target)


if __name__ == "__main__":
    main()
