#!/usr/bin/env python3
"""Writes GALILEO's kit presets for a firmware build, as MNMD's
tools/build_firmware.py writes MD KIT's (write_presets, kit_region): each
preset is the device's version-4 structure with one region per key, which
stores the key's model and eight controls the way the SDK's drum kind does.

    python3 tools/presets.py <out folder>

writes <out>/galileo/default.json (the default kit) and one file per named kit
(tools/catalog.py PRESETS). Copy the folder to the device's
/content/presets/galileo/. A command-line build installs no presets, and the
OP-XY may not list an engine without its default.json.

Every preset carries machine.json's amp envelope: no attack, full decay and
sustain, the longest release. A run or a bounce lasts up to several seconds,
and the kit frees each voice itself once it has rung out; a short M2 envelope
cuts the runs (the screen shows the cut).

Each key's pad fields (SPACE, FEEL, FX I, FX II) go in the region's "random",
the top half of stock's pad word +0x14 as its preset writer exports it, and its
pan and level in "pan" and "gain"; each is written only when not neutral.
A send is TRK or the key's own 0..100 in 1% steps (SDK 1.15): random keeps its
nearest step as a code, which older firmware plays, and the amount above bit
16 (send_code). Writing checks every key's sends in pure Python first;
`--check` decodes every key back through the SDK's own drum_storage.hpp.
"""
import argparse
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import catalog  # noqa: E402


def kit_value(model, controls):
    """The 60 bits a key stores (the SDK's drum_storage.hpp), a kit of up to 16
    models: the eight controls (7 bits each), then the model (4)."""
    assert len(catalog.MODELS) <= 16
    return sum(c << (7 * i) for i, c in enumerate(controls)) | model << 56


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


# The pad word +0x14's top half, as stock's preset writer exports it ("random",
# word >>> 16, written only when non-zero; read back as int << 16): the kit's pad
# controls from bit 16 (SPACE 5 bits signed, then FEEL 4), FX I's code at bits
# 25..27 and FX II's at 28..30 (the SDK's drum_storage.hpp). Bit 31 stays clear,
# or the reader's max(word, 0) would drop the lot. Above the half, from bit 16,
# the sends' amounts (SDK 1.15).
PAD_BITS = (("space", 5), ("feel", 4))


def pad_random(pad):
    value, low = 0, 0
    for name, bits in PAD_BITS:
        value |= (pad.get(name, 0) & ((1 << bits) - 1)) << low
        low += bits
    fx1, fx2 = pad.get("fx1", TRK), pad.get("fx2", TRK)
    assert send_ok(fx1) and send_ok(fx2)
    value |= send_code(fx1) << 9 | send_code(fx2) << 12
    assert 0 <= value <= 0x7fff
    return value | send_index(fx1) << 16 | send_index(fx2) << 23


def kit_region(key, model, controls, pad=None):
    """A key as a preset region: "fm slap" is Drumline type 7, the SDK's mark; the
    60 bits fill 15 bits of each of four params. Its pad fields go in "random",
    its pan (-100..100) and level (whole dB, -30..0) in "pan" and "gain", each
    written only when not neutral, as stock writes them."""
    pad = pad or {}
    value = kit_value(model, controls)
    region = {"hikey": key, "model": "fm slap", "params": [(value >> (15 * w)) & 0x7fff for w in range(4)]}
    if pad_random(pad):
        region["random"] = pad_random(pad)
    if pad.get("pan", 0):
        region["pan"] = pad["pan"]
    if pad.get("level", 0):
        region["gain"] = pad["level"]
    return region


def regions(keys):
    return [kit_region(53 + i, catalog.model_index(k[0]), k[1], k.pad) for i, k in enumerate(keys)]


def preset(params, amp, regs):
    """The device's version-4 preset, as the SDK's browser builder writes its init
    presets (builder/core.mjs, defaultPreset)."""
    return {
        "engine": {
            "bendrange": 8191, "highpass": 0,
            "modulation": {s: {"amount": 16384, "target": 0} for s in ("aftertouch", "modwheel", "pitchbend", "velocity")},
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
        "octave": 0, "platform": "OP-XY", "regions": regs, "type": "galileo", "version": 4,
    }


def write(out):
    machine = json.loads((ROOT / "machine.json").read_text())
    amp = machine.get("presetDefaults", {}).get("amp", {})
    # A kit's track parameters are offsets to each key's controls (the SDK's
    # drum kind), neutral at the middle: the keys play as stored.
    params = [16384] * 8
    target = pathlib.Path(out) / "galileo"
    target.mkdir(parents=True, exist_ok=True)
    written = []
    for i, (name, _role, keys) in enumerate(catalog.PRESETS):
        slug = re.sub(r"[^a-z0-9_-]+", "-", name.lower()).strip("-")
        doc = preset(params, amp, regions(keys))
        for path in ([target / "default.json"] if i == 0 else []) + [target / f"{slug}.json"]:
            path.write_text(json.dumps(doc, indent=2) + "\n")
            written.append(path)
    return written


CHECK = r'''
// Decodes preset regions with the SDK's own drum_storage.hpp, as the adapter does.
#include "drum_storage.hpp"
#include <cstdio>
using namespace opxy::platform;
int main() {
    unsigned p[4], random;
    const opxy::u8 bits[2] = {5, 4};
    while (std::scanf("%u %u %u %u %u", &p[0], &p[1], &p[2], &p[3], &random) == 5) {
        opxy::i32 words[4];
        for (int i = 0; i < 4; ++i) words[i] = static_cast<opxy::i32>(p[i] << 16);
        opxy::u8 model = 99, controls[8] = {};
        drum_unpack(words, &model, controls);
        const opxy::i32 word = static_cast<opxy::i32>(random << 16);   // the reader: max(int << 16, 0)
        const opxy::i32 kept = word < 0 ? 0 : word;
        std::printf("%u", model);
        for (int i = 0; i < 8; ++i) std::printf(" %u", controls[i]);
        std::printf(" %d %d %d %d\n", drum_pad_get(kept, 0, bits, 1), drum_pad_get(kept, 1, bits, 1),
                    drum_field_get(kept, 25, 3, false), drum_field_get(kept, 28, 3, false));
    }
    return 0;
}
'''


def check(sdk):
    """Every preset's every key decodes, through the SDK's own storage code, to
    its model, controls and pad fields. Returns the number of keys checked."""
    import subprocess
    import tempfile
    work = pathlib.Path(tempfile.mkdtemp(prefix="galileo-presets-"))
    (work / "check.cpp").write_text(CHECK)
    platform = pathlib.Path(sdk) / "sdk" / "platform" / "opxy-1.2.1"
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{pathlib.Path(sdk) / 'sdk' / 'include'}", f"-I{platform}",
                    str(work / "check.cpp"), "-o", str(work / "check")], check=True)
    lines, wanted = [], []
    for name, _role, keys in catalog.PRESETS:
        for region, k in zip(regions(keys), keys):
            lines.append(" ".join(map(str, region["params"] + [region.get("random", 0)])))
            pad = k.pad
            wanted.append([catalog.model_index(k[0])] + list(k[1]) +
                          [pad.get("space", 0), pad.get("feel", 0), send_code(pad.get("fx1", TRK)), send_code(pad.get("fx2", TRK))])
            assert region.get("pan", 0) == pad.get("pan", 0) and region.get("gain", 0) == pad.get("level", 0)
    out = subprocess.run([str(work / "check")], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True).stdout
    got = [list(map(int, l.split())) for l in out.splitlines()]
    bad = [(i, w, g) for i, (w, g) in enumerate(zip(wanted, got)) if w != g]
    for i, w, g in bad[:5]:
        print(f"key {i}: wanted {w}, decodes to {g}")
    if bad or len(got) != len(wanted):
        raise SystemExit(f"{len(bad)} keys decode wrongly")
    return len(got)


def check_sends():
    """Pure Python: every preset's every key carries its sends as every firmware reads
    them. Returns the number of keys with a send of their own."""
    own = 0
    for name, _role, keys in catalog.PRESETS:
        for region, k in zip(regions(keys), keys):
            sends = [k.pad.get("fx1", TRK), k.pad.get("fx2", TRK)]
            random = region.get("random", 0)
            assert 0 <= random < 1 << 30 and not random & 0x8000, (name, region["hikey"], random)
            # Firmware before SDK 1.15: stock's reader, max(random << 16, 0) as a
            # 32-bit int, keeps the codes alone.
            word = (random << 16) & 0xffffffff
            assert not word >> 31 and [(word >> 25) & 7, (word >> 28) & 7] == [send_code(a) for a in sends], (name, region["hikey"])
            # SDK 1.15 reads each send back as the amount written. The kits were
            # voiced on the seven steps, so each amount is the percent SDK 1.15
            # shows for (and turns on from) the code these presets carried before
            # SDK 1.15 (drum_send_percent), and the code is unchanged.
            assert sends_read(random) == sends, (name, region["hikey"], random)
            assert all(a is TRK or a == STEPS[send_code(a) - 1] for a in sends), (name, region["hikey"])
            own += any(a is not TRK for a in sends)
    return own


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("out", nargs="?")
    parser.add_argument("--check", action="store_true", help="decode every key back with the SDK's storage code")
    parser.add_argument("--sdk", default=str(ROOT.parent.parent / "open-xy"))
    args = parser.parse_args()
    if args.check:
        print(f"{check(args.sdk)} keys decode to their models, controls and pad fields")
    if args.out:
        print(f"{check_sends()} keys carry sends of their own, read back alike by every firmware")
        for path in write(args.out):
            print("wrote", path)
