#!/usr/bin/env python3
"""Writes Syahi's kit presets into a firmware build, as MNMD's
tools/build_firmware.py write_presets does for MD KIT: content/presets/syahi/
default.json (the default kit) and one preset per kit in tools/kits.py, each a
version-4 OP-XY preset whose 24 regions are the kit's keys.

    python3 tools/presets.py <build folder>          # after sdk.py build
    python3 tools/presets.py --out <folder>          # without a build: the init params from definition.json

Command-line builds install no presets, and the OP-XY may not list a machine
without one. A preset holds each key's model, eight controls and choke group (its play
mode), and its pad fields as stock's preset writer stores them: `pan`, `gain`
(the key's level in dB) and `random`, the top 16 bits of the pad word at
+0x14, written only when non-zero and restored shifted back (the SDK's
abi-1.2.1.md; drumline-research). Syahi's fields there are HUMAN (bits
16..19), GRIT (20..24, two's complement), FX I (25..27) and FX II (28..30);
bit 31 stays clear, so stock's reader (max(random << 16, 0)) gives them back.
Every preset written is decoded back and compared (check()).
"""
import argparse, json, pathlib, re, sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import kits  # noqa: E402

PLAYMODES = {0: None, 1: "mute1", 2: "mute2", 3: "mute3"}


def kit_value(model, controls):
    """The 60 bits a key stores in a kit of up to 16 models (the SDK's drum_storage.hpp):
    the eight controls (7 bits each), then the model (4 bits)."""
    return sum(c << (7 * i) for i, c in enumerate(controls)) | model << 56


# The pad word's fields (resources.padControls in declaration order from bit 16,
# then the sends), as (name, low bit, bits, signed).
FIELDS = [("human", 16, 4, False), ("grit", 20, 5, True), ("fx1", 25, 3, False), ("fx2", 28, 3, False)]


def pad_word(pad):
    word = 0
    for name, low, bits, signed in FIELDS:
        v = pad.get(name, 0)
        lo, hi = (-(1 << (bits - 1)), (1 << (bits - 1)) - 1) if signed else (0, (1 << bits) - 1)
        assert lo <= v <= hi, (name, v)
        word |= (v & ((1 << bits) - 1)) << low
    assert not word & 0x8000ffff, "only bits 16..30"
    return word


def unpack(word):
    """The fields back from a pad word, as the SDK's drum_field_get reads them."""
    out = {}
    for name, low, bits, signed in FIELDS:
        raw = (word >> low) & ((1 << bits) - 1)
        out[name] = raw - (1 << bits) if signed and raw >> (bits - 1) else raw
    return out


def kit_region(key, model, controls, choke, pad=None):
    """A key as a preset region (the SDK's docs/abi-1.2.1.md, the drum kind): "fm slap" is
    Drumline type 7, the SDK's mark; the 60 bits fill 15 bits of each param; random, pan and
    gain as stock's writer gives them, each only when non-zero."""
    pad = pad or {}
    value = kit_value(model, controls)
    region = {"hikey": key, "model": "fm slap", "params": [(value >> (15 * w)) & 0x7fff for w in range(4)]}
    if PLAYMODES[choke]:
        region["playmode"] = PLAYMODES[choke]
    random = pad_word(pad) >> 16
    if random:
        region["random"] = random
    if pad.get("pan", 0):
        assert -100 <= pad["pan"] <= 100
        region["pan"] = pad["pan"]
    if pad.get("level", 0):
        assert -30 <= pad["level"] <= 0
        region["gain"] = pad["level"]
    return region


def regions(keys, pads=None):
    pads = pads or {}
    return [kit_region(53 + i, kits.MODELS.index(model), controls, choke, pads.get(53 + i))
            for i, (_role, model, controls, choke) in enumerate(keys)]


def check(target, files):
    """Decodes each written preset as stock's reader and the SDK would, and compares with the kit."""
    for name, (keys, pads) in files.items():
        data = json.loads((target / f"{name}.json").read_text())
        assert len(data["regions"]) == 24, name
        for i, region in enumerate(data["regions"]):
            role, model, controls, choke = keys[i]
            pad = (pads or {}).get(53 + i, {})
            value = sum(p << (15 * w) for w, p in enumerate(region["params"]))
            assert [(value >> (7 * c)) & 127 for c in range(8)] == list(controls), (name, role)
            assert value >> 56 == kits.MODELS.index(model), (name, role)
            word = max((region.get("random", 0) << 16), 0)   # stock's reader: max(int << 16, 0)
            assert word < 1 << 31, (name, role, "bit 31")
            back = unpack(word)
            for field in ("human", "grit", "fx1", "fx2"):
                assert back[field] == pad.get(field, 0), (name, role, field, back[field])
            assert region.get("pan", 0) == pad.get("pan", 0) and region.get("gain", 0) == pad.get("level", 0), (name, role)
            assert region.get("playmode") == PLAYMODES[choke], (name, role)
    print(f"checked {len(files)} presets: every key's model, controls, choke, HUMAN, GRIT, sends, pan and level decode back")


def preset(params, amp, keys, pads=None):
    """A preset in the device's version-4 structure, as the SDK's builder writes its init presets."""
    return {
        "engine": {
            "bendrange": 8191, "highpass": 0,
            "modulation": {source: {"amount": 16384, "target": 0} for source in ("aftertouch", "modwheel", "pitchbend", "velocity")},
            "params": list(params), "playmode": "poly",
            "portamento.amount": 0, "portamento.type": 0, "transpose": 0,
            "tuning.root": 0, "tuning.scale": 0, "velocity.sensitivity": 0, "volume": 16384, "width": 0,
        },
        "envelope": {
            "amp": {"attack": 128, "decay": 8192, "release": 32767, "sustain": 32767, **amp},
            "filter": {"attack": 0, "decay": 0, "release": 0, "sustain": 32767},
        },
        "fx": {"active": False, "params": [0, 0, 0, 0, 0, 32767, 0, 0], "type": "svf"},
        "lfo": {"active": False, "params": [0, 0, 0, 0, 0, 0, 0, 0], "type": "value"},
        "octave": 0, "platform": "OP-XY", "regions": regions(keys, pads), "type": "syahi", "version": 4,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build", nargs="?")
    ap.add_argument("--out")
    a = ap.parse_args()
    machine = json.loads((ROOT / "machine.json").read_text())
    definition = json.loads((ROOT / "definition.json").read_text())
    amp = machine.get("presetDefaults", {}).get("amp", {})
    if a.build:
        factory = json.loads((pathlib.Path(a.build) / "candidate" / "content" / "op1_factory.json").read_text())
        entry = next(e for e in factory["engine_types"] if e.get("type") == "syahi")
        params = entry["default_params"]
        target = pathlib.Path(a.build) / "candidate" / "content" / "presets" / "syahi"
    elif a.out:
        params = [round(p["default"] * 32767) for p in definition["parameters"]]
        target = pathlib.Path(a.out) / "syahi"
    else:
        sys.exit(__doc__)
    # The track's eight parameters are offsets to every key's controls, neutral at the middle.
    params = [16384] * 8 if not a.build else params
    target.mkdir(parents=True, exist_ok=True)
    files = {"default": (kits.DEFAULT_KIT, kits.PADS.get("DEFAULT")),
             **{name.lower(): (keys, kits.PADS.get(name)) for name, keys in kits.KITS.items()}}
    assert len(files) <= 16
    for name, (keys, pads) in files.items():
        assert re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,23}", name)
        (target / f"{name}.json").write_text(json.dumps(preset(params, amp, keys, pads), indent=2) + "\n")
    print(f"wrote {len(files)} presets to {target}")
    check(target, files)


if __name__ == "__main__":
    main()
