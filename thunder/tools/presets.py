#!/usr/bin/env python3
"""THUNDER's kit presets, and the files a build installs for them.

    python3 tools/presets.py --out <build>/candidate     # content/presets/thunder/*.json
    python3 tools/presets.py --check                     # each region decodes to its key's sound

A kit preset holds the 24 keys (53..76) as the drum kind stores them, the way
MNMD's tools/build_firmware.py writes MD KIT's: each key a region whose four
params carry its eight 7-bit controls and its model (the SDK's
drum_storage.hpp), marked "fm slap" (Drumline type 7, the SDK's mark), its
choke group as the play mode ("mute1" is group 1) and its own pan and level
(gain, in dB). The SDK's machine presets folder refuses regions, so these are
written into a build's content/presets/thunder/ after it is built, as MNMD's
are. default.json is the kit drum_defaults gives (kit.h).

The pad controls (TUNE, DRIVE) and the per-key sends live in bits 16..30 of
stock's pad word +0x14 (the SDK's drum_storage.hpp): TUNE (5 bits, signed)
from bit 16, DRIVE (4) from 21, FX I (3) from 25, FX II (3) from 28. Stock's
preset writer exports that word's top half as the region's "random" (written
only when it isn't 0) and its reader restores it shifted back, as
max(random << 16, 0); so a region carries them as random, with its top bit
(the word's bit 31) clear: TUNE in random's bits 0..4, DRIVE 5..8, FX I
9..11, FX II 12..14. A send's code is 0 (TRK, the track's send) or 1..7 (0,
17, 33, 50, 67, 83, 100).
"""
import argparse, json, math, pathlib, re

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODELS = ["BOOM", "SLAP", "POP", "FLICK", "TOM", "CRASH", "CLANK", "GRIT", "HOP", "ZAP", "BOW", "SHAKE"]


def default_kit():
    text = (ROOT / "kit.h").read_text()
    models = [int(x) for x in re.search(r"key_model\[keys\] = \{([^}]*)\}", text).group(1).split(",")]
    values = [[int(x) for x in row.split(",")] for row in re.findall(r"\{([0-9, ]+)\},\s+// \d+", text)]
    choke = [int(x) for x in re.search(r"key_choke\[keys\] = \{([^}]*)\}", text).group(1).split(",")]
    assert len(models) == len(values) == len(choke) == 24
    return [{"model": m, "c": list(v), "choke": g, "pan": 0, "gain": LEVELS.get("default", {}).get(k, 0), "tune": 0, "drive": 0,
             "fx1": 0, "fx2": 0} for k, (m, v, g) in enumerate(zip(models, values, choke))]


def model_defaults(name):
    text = (ROOT / "kit.h").read_text()
    for n, d in re.findall(r'\{"(\w+)", \{[^}]*\}, \{([^}]*)\}\}', text):
        if n == name:
            return [int(x) for x in d.split(",")]
    raise KeyError(name)


# The keys by role (index from key 53), as stock's kits lay them out.
BD, SD, HT, MT, LT, CP, RS, CB, CH, OH, RC, CC, M1, M2, M3, M4, BD2, SD2, PH, FT, SN, BW, ZP2, SK2 = range(24)


def note_size(model, hz):
    """SIZE (0..127) that tunes a model's sheet to a frequency: its range is
    exponential (models.h): BOOM 30..105, SLAP 110..330, TOM 55..420, CRASH
    110..650, CLANK 260..2100, HOP 180..1300, BOW 70..900 Hz."""
    lo, hi = {"BOOM": (30, 105), "SLAP": (110, 330), "TOM": (55, 420), "CRASH": (110, 650), "CLANK": (260, 2100),
              "HOP": (180, 1300), "BOW": (70, 900)}[model]
    return max(0, min(127, round(127 * math.log(hz / lo) / math.log(hi / lo))))


def hz(note):
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    n, octave = note[:-1], int(note[-1])
    return 440 * 2 ** ((names.index(n) + 12 * (octave + 1) - 69) / 12)


def kit(changes):
    """The default kit with changes: key -> {"model": name, "c": {slot: value}, "gain": dB, "pan": -100..100,
    "choke": 0/1, "tune": -16..15, "drive": 0..15, "fx1", "fx2": 0..7}."""
    keys = default_kit()
    for k, ch in changes.items():
        key = keys[k]
        if "model" in ch:
            key["model"] = MODELS.index(ch["model"])
            key["c"] = model_defaults(ch["model"])
        for s, v in ch.get("c", {}).items():
            key["c"][s] = v
        for f in ("gain", "pan", "choke", "tune", "drive", "fx1", "fx2"):
            if f in ch:
                key[f] = ch[f]
    return keys


# Each key's LEVEL (dB, a cut): the engine brings every model's default hit
# to its role's loudness (tools/calibrate.py: kicks and toms -16.5 LUFS, the
# rest -19, re the ceiling), and holds it as the controls turn; a key whose
# own settings still sound over that by more than 0.75 LU is cut to it here,
# in whole dB, measured at its settings, pad controls included. Keys not
# named play at 0 dB, as MD KIT's do.
LEVELS = {
    "kingston": {},
    "foundry": {BD: -7, CP: -7, CB: -11},
    "tin-roof": {},
    "proscenium": {BD: -1, LT: -2, FT: -2},
    "brushes": {BD: -2},
    "gamelan-shed": {HT: -1, MT: -1, LT: -1, FT: -1},
    "cold-plate": {RS: -1},
    "sheet-808": {BD: -9, RS: -1, BD2: -10},
}

PRESETS = {
    # dub steppers: a deep, cambered kick, a wired snare with a long metal
    # ring, offbeat flicks, a stirrup cowbell, the laser and a thunderclap
    "kingston": kit({
        BD: {"c": {0: 44, 1: 51, 5: 84, 6: 74}},
        SD: {"c": {0: 66, 1: 64, 5: 56, 6: 40, 7: 90, 2: 60}, "fx1": 6},
        CH: {"c": {0: 60, 1: 90, 4: 30, 2: 96}, "pan": 20},
        OH: {"c": {1: 56, 6: 70, 5: 30}, "pan": 20},
        CB: {"c": {0: 60, 1: 86, 4: 110, 5: 60}, "pan": -30},
        MT: {"c": {0: 60, 5: 64, 6: 50}},
        M3: {"c": {0: 70, 2: 70, 3: 90, 4: 70}, "pan": -20, "fx1": 7},
        M4: {"c": {2: 70, 6: 80}, "fx1": 5},
        M1: {"c": {0: 60, 3: 70}, "pan": 35},
    }),
    # industrial techno: steel rods, a stiff plate, hard snaps and a ball
    # bearing for fills
    "foundry": kit({
        BD: {"c": {0: 48, 1: 76, 2: 44, 4: 96, 5: 64, 6: 40}, "drive": 6},
        CP: {"c": {0: 70, 2: 80, 3: 52, 4: 100, 5: 40, 6: 30}, "drive": 3, "fx1": 3},
        CH: {"c": {0: 84, 1: 104, 2: 110, 4: 10}},
        OH: {"c": {0: 76, 1: 72, 4: 10, 5: 30, 6: 30}},
        CB: {"c": {0: 34, 1: 90, 2: 120, 4: 10, 5: 40}, "pan": 25, "drive": 9},
        M1: {"c": {0: 90, 1: 90, 3: 110, 4: 20, 5: 20}, "pan": -30},
        M2: {"c": {0: 50, 2: 120, 4: 30, 5: 60}},
        CC: {"c": {0: 40, 1: 50, 4: 0, 5: 60, 6: 60, 7: 50}},
        SD: {"model": "CLANK", "c": {0: 40, 1: 100, 2: 127, 4: 0, 5: 90, 6: 60}},
    }),
    # lo-fi boom bap: damped throughout, felt beaters, a snare with loose
    # links, a dusty shaker
    "tin-roof": kit({
        BD: {"c": {0: 46, 1: 65, 2: 0, 5: 40, 6: 40}},
        SD: {"c": {0: 54, 1: 92, 2: 30, 5: 100, 6: 110, 7: 20}},
        HT: {"c": {1: 92, 2: 10}}, MT: {"c": {1: 92, 2: 10}}, LT: {"c": {1: 92, 2: 10}},
        CH: {"c": {0: 48, 1: 110, 2: 40}},
        OH: {"c": {0: 48, 1: 84, 2: 40}},
        CP: {"c": {1: 100, 4: 30, 7: 90}},
        M1: {"c": {0: 50, 2: 30, 3: 60, 4: 80}},
        RC: {"c": {1: 100, 2: 30}},
    }),
    # cinematic: a huge storm, a swelling crash, bowed drones, low toms that
    # roll, kicks that dive
    "proscenium": kit({
        M4: {"c": {0: 10, 2: 127, 6: 100, 7: 90}},
        SK2: {"c": {0: 0, 1: 20, 2: 110, 3: 30, 7: 110}},
        CC: {"c": {0: 30, 1: 10, 5: 110, 6: 60}, "fx2": 6},
        BW: {"c": {0: 30, 1: 20, 4: 110, 7: 60}},
        M2: {"model": "BOW", "c": {0: 60, 1: 30, 2: 90, 4: 90}},
        LT: {"c": {0: 10, 6: 70}}, FT: {"c": {0: 0, 6: 100, 1: 30}},
        MT: {"c": {0: 30, 6: 50}},
        BD: {"c": {0: 30, 1: 23, 5: 127, 6: 110}},
        BD2: {"c": {0: 20, 1: 20, 4: 40, 5: 110, 7: 50}},
    }),
    # jazz brushes: flicks scraped as brushes, felt toms, a soft snare, a ride
    "brushes": kit({
        CH: {"c": {1: 90, 2: 30, 7: 90}},
        OH: {"c": {1: 60, 2: 30, 7: 110, 6: 30}},
        PH: {"c": {1: 110, 2: 20, 7: 70}},
        SD: {"c": {0: 60, 1: 80, 2: 20, 5: 60, 6: 90, 7: 20}},
        SD2: {"model": "FLICK", "c": {0: 40, 1: 70, 2: 10, 7: 127}},
        HT: {"c": {2: 10, 1: 60}}, MT: {"c": {2: 0, 1: 60}}, LT: {"c": {2: 0, 1: 60}}, FT: {"c": {2: 0, 1: 50}},
        RC: {"c": {0: 90, 1: 60, 2: 60, 3: 30}, "pan": 30, "fx2": 3},
        BD: {"c": {0: 60, 1: 62, 2: 0, 5: 20}},
    }),
    # tuned percussion: taut toms and shackles on a pentatonic scale, a ball
    # bearing for ornaments
    "gamelan-shed": kit({
        # the toms share one sheet (SIZE at G3), the shackles another (at
        # D5); each key's TUNE sets its note of the scale, so SIZE retunes
        # the whole set at once
        **{k: {"c": {0: note_size("TOM", hz("G3")), 4: 127, 1: 30, 2: 90, 3: 80}, "tune": st, "pan": pan}
           for k, st, pan in zip((LT, MT, HT, FT), (-5, 0, 2, -8), (-30, -10, 10, -45))},
        **{k: {"model": "CLANK", "c": {0: note_size("CLANK", hz("D5")), 1: 40, 4: 110, 5: 50}, "tune": st, "pan": pan,
               "fx1": 4}
           for k, st, pan in zip((RS, CB, CP, SN, SD2), (0, 2, 5, 7, 9), (40, 25, 10, -10, -25))},
        M2: {"c": {0: 90, 2: 110, 4: 30, 5: 30, 6: 90}, "fx1": 5, "pan": 30},
        BD: {"c": {0: 60, 1: 47, 4: 127, 5: 20}},
        CC: {"c": {0: 30, 1: 30, 4: 127, 5: 60}, "fx2": 5},
    }),
    # minimal: small, damped, ticks, single snaps, short zaps
    "cold-plate": kit({
        BD: {"c": {0: 60, 1: 78, 2: 60, 5: 30}},
        SD: {"model": "POP", "c": {0: 80, 1: 110, 2: 0, 4: 110}},
        CH: {"c": {0: 110, 1: 120, 2: 127, 4: 80}},
        OH: {"c": {0: 100, 1: 96, 2: 110, 4: 80}},
        CP: {"c": {0: 90, 1: 110, 2: 0, 4: 127}},
        M3: {"c": {0: 110, 1: 110, 2: 10, 3: 0, 4: 30}},
        ZP2: {"c": {0: 127, 1: 120, 2: 0, 3: 20}},
        RS: {"c": {0: 110, 1: 120, 2: 127}},
        M1: {"c": {0: 110, 1: 110, 3: 40, 7: 10}},
    }),
    # trap: a long cambered kick, tight flicks, a stacked clap, a high zap
    "sheet-808": kit({
        BD: {"c": {0: 40, 1: 16, 2: 10, 5: 110, 6: 110}, "drive": 4},
        BD2: {"c": {0: 30, 1: 10, 5: 127, 6: 127}, "tune": -5, "drive": 8},
        CH: {"c": {0: 90, 1: 120, 2: 100}},
        OH: {"c": {0: 84, 1: 90, 2: 100}},
        CP: {"c": {2: 127, 3: 30, 4: 90, 5: 40, 7: 20}},
        SD: {"c": {0: 80, 1: 90, 2: 100, 5: 40, 7: 60}},
        M3: {"c": {0: 110, 1: 90, 2: 20, 3: 10, 4: 90}},
        RS: {"c": {0: 100, 1: 120}},
    }),
}
for _name, _keys in PRESETS.items():
    for _k, _key in enumerate(_keys):
        _key["gain"] = LEVELS.get(_name, {}).get(_k, 0)


def kit_value(model, controls):
    """The 60 bits a key stores in a kit of up to 16 models (the SDK's
    drum_storage.hpp): the eight controls, 7 bits each, then the model."""
    return sum(c << (7 * i) for i, c in enumerate(controls)) | model << 56


def pad_random(key):
    """The key's pad fields as the region's random: the pad word's bits 16..30, shifted down."""
    assert -16 <= key["tune"] <= 15 and 0 <= key["drive"] <= 15 and 0 <= key["fx1"] <= 7 and 0 <= key["fx2"] <= 7
    return (key["tune"] & 31) | key["drive"] << 5 | key["fx1"] << 9 | key["fx2"] << 12


def unpack_random(random):
    """What the SDK decodes from the restored word, max(random << 16, 0)."""
    word = max(random << 16, 0)
    field = lambda low, bits: (word >> low) & ((1 << bits) - 1)
    tune = field(16, 5)
    return {"tune": tune - 32 if tune & 16 else tune, "drive": field(21, 4), "fx1": field(25, 3), "fx2": field(28, 3)}


def region(index, key):
    value = kit_value(key["model"], key["c"])
    r = {"hikey": 53 + index, "model": "fm slap", "params": [(value >> (15 * w)) & 0x7fff for w in range(4)]}
    if key["choke"]:
        r["playmode"] = "mute1"
    if key["pan"]:
        r["pan"] = key["pan"]
    r["gain"] = key["gain"]
    random = pad_random(key)
    if random:
        r["random"] = random
    return r


def preset(keys, params):
    # the SDK's preset template (tooling/default_preset.json)
    p = {
        "engine": {"bendrange": 8191, "highpass": 0,
                   "modulation": {s: {"amount": 16384, "target": 0} for s in ("aftertouch", "modwheel", "pitchbend", "velocity")},
                   "playmode": "poly", "portamento.amount": 0, "portamento.type": 0, "transpose": 0, "tuning.root": 0,
                   "tuning.scale": 0, "velocity.sensitivity": 0, "volume": 16384, "width": 0},
        "envelope": {"amp": {"attack": 128, "decay": 8192, "release": 32767, "sustain": 32767},
                     "filter": {"attack": 0, "decay": 0, "release": 0, "sustain": 32767}},
        "fx": {"active": False, "params": [0, 0, 0, 0, 0, 32767, 0, 0], "type": "svf"},
        "lfo": {"active": False, "params": [0, 0, 0, 0, 0, 0, 0, 0], "type": "value"},
        "octave": 0, "platform": "OP-XY", "regions": [], "version": 4}
    machine = json.loads((ROOT / "machine.json").read_text())
    p["engine"]["params"] = params
    p["envelope"]["amp"].update(machine.get("presetDefaults", {}).get("amp", {}))
    p["type"] = "thunder"
    p["regions"] = [region(i, k) for i, k in enumerate(keys)]
    return p


def check():
    """Each region decodes, as the SDK's drum_unpack does, to its key's model and controls."""
    for name, keys in [("default", default_kit()), *PRESETS.items()]:
        for i, k in enumerate(keys):
            r = region(i, k)
            value = sum(p << (15 * w) for w, p in enumerate(r["params"]))
            assert value >> 56 == k["model"] < 12, (name, i)
            assert [(value >> (7 * s)) & 127 for s in range(8)] == k["c"], (name, i)
            assert all(0 <= c <= 127 for c in k["c"]) and -30 <= r["gain"] <= 0 and -100 <= r.get("pan", 0) <= 100, (name, i)
            # the pad fields come back as they went in, and the word's bit 31 stays clear
            random = r.get("random", 0)
            assert 0 <= random < 1 << 15, (name, i, random)
            back = unpack_random(random)
            assert back == {f: k[f] for f in ("tune", "drive", "fx1", "fx2")}, (name, i, back)
    carried = sum(1 for keys in PRESETS.values() for k in keys if pad_random(k))
    print(f"{len(PRESETS) + 1} kits: every key decodes to its own sound, pan, level and pad fields ({carried} keys carry pad fields)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    check()
    if not a.out:
        return
    definition = json.loads((ROOT / "definition.json").read_text())
    params = [round(p["default"] * 32767) for p in definition["parameters"]]
    folder = pathlib.Path(a.out) / "content" / "presets" / "thunder"
    folder.mkdir(parents=True, exist_ok=True)
    for name, keys in [("default", default_kit()), *PRESETS.items()]:
        (folder / f"{name}.json").write_text(json.dumps(preset(keys, params), indent=2) + "\n")
        print("wrote", folder / f"{name}.json")


if __name__ == "__main__":
    main()
