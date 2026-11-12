#!/usr/bin/env python3
"""Draws contact sheets of CURIO's screen at 1:1 from the real display code.

    python3 tools/sheet.py --sdk <open-xy checkout> [--font UniversNextPro-Light.ttf] [--out docs]

It compiles tools/frames.cpp, asks it for each scene's frame, applies each
frame to panel.svg.fragment as the device's renderer does (node transforms,
palette paint lending its stroke width, the four reshaped outlines, text
slots), and screenshots the page with headless Chrome at 1:1. The device's
face is stock content, so it is never in this repository: pass it with
--font or XY_FONT. Writes docs/sheet-*.png, and docs/screen.png: one frame
alone at 1:1 (480 x 222, as the suite's other kits').
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
NAMES = ["ball", "cask", "stone", "drip", "ice", "sheet", "spark", "cork",
         "cicada", "ratchet", "pod", "match", "glass", "bell", "chalk", "roar"]


def defaults():
    src = (ROOT / "kit.h").read_text()
    rows = re.findall(r'\{"([a-z ]+)", \d, \{[^}]*\}, \{([^}]*)\}', src)
    return [[int(v) for v in d.split(",")] for _, d in rows]


def build(sdk, cxx):
    out = pathlib.Path(tempfile.mkdtemp()) / "frames"
    subprocess.run([cxx, "-std=c++11", "-O2", f"-I{sdk}/sdk/include", str(ROOT / "tools" / "frames.cpp"), "-o", str(out)], check=True)
    return out


def scene(model, ctl, shift=0, line=0, pads=(0, 0, 0, 0), pan=0, level=0, choke=0, hit=0, amp=(-1, 0, 0, 0)):
    return [model, *ctl, shift, line, *pads, pan, level, choke, hit, *amp]


def frames(exe, scenes):
    text = "\n".join(" ".join(str(v) for v in s) for s in scenes) + "\n"
    out = subprocess.run([str(exe)], input=text, capture_output=True, text=True, check=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def svg(fragment, frame):
    ET.register_namespace("", "http://www.w3.org/2000/svg")
    g = ET.fromstring(fragment)
    palette = {}
    for el in g.iter():
        i = el.get("id", "")
        if "_palette_" in i:
            palette[int(i.rsplit("_", 1)[1])] = (el.get("fill"), el.get("stroke"), el.get("stroke-width"))
    for el in g.iter():
        i = el.get("id", "")
        if "_node_" in i:
            n = int(i.rsplit("_", 1)[1])
            x, y, s, c, sn, shade, paint = frame["nodes"][n]
            k = s / 1024
            el.set("transform", f"matrix({k * c / 1024:.4f} {k * sn / 1024:.4f} {-k * sn / 1024:.4f} {k * c / 1024:.4f} {x / 1024:.3f} {y / 1024:.3f})")
            if n < 4:
                p = frame["paths"][n]
                el.set("d", "M" + " L".join(f"{p[2 * j] / 1024:.3f},{p[2 * j + 1] / 1024:.3f}" for j in range(64)) + " Z")
            if paint:
                f, st, w = palette[shade]
                el.set("stroke", st)
                el.set("stroke-width", w)
                if paint == 2 and el.get("fill", "none") != "none":
                    el.set("fill", f)
        elif "_text_" in i:
            t = int(i.rsplit("_", 1)[1])
            x, y, s, chars = frame["texts"][t]
            el.set("transform", f"matrix({s / 1024:.4f} 0 0 {s / 1024:.4f} {x / 1024:.3f} {y / 1024:.3f})")
            el.text = chars
    body = ET.tostring(g, encoding="unicode")
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220"><rect width="480" height="220" fill="#000"/>{body}</svg>'


def page(rows, font, columns):
    css = (f"@font-face {{ font-family: UniversNextPro-Light; src: url('file://{font}'); }}" if font else "") + \
        "body { margin: 0; background: #181818; color: #9a9a9a; font: 12px UniversNextPro-Light, sans-serif; }" \
        " .cell { display: inline-block; margin: 6px; vertical-align: top; } .cell div { height: 16px; }"
    cells = "".join(f'<div class="cell"><div>{label}</div>{s}</div>' for label, s in rows)
    width = columns * 492 + 12
    return f"<!DOCTYPE html><html><head><style>{css}</style></head><body style='width:{width}px'>{cells}</body></html>", width


def shoot(html, width, height, png):
    folder = pathlib.Path(tempfile.mkdtemp())
    (folder / "sheet.html").write_text(html)
    subprocess.run([CHROME, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                    "--allow-file-access-from-files", "--virtual-time-budget=3000", f"--window-size={width},{height}",
                    f"--screenshot={png}", f"file://{folder / 'sheet.html'}"], check=True, capture_output=True)


# docs/screen.png: the specimen and the moment after its hit (ms).
HERO = ("cicada, 40 ms after a hit", (8, 40))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk", required=True)
    ap.add_argument("--cxx", default="c++")
    ap.add_argument("--font", default=os.environ.get("XY_FONT", ""))
    ap.add_argument("--out", default=str(ROOT / "docs"))
    ap.add_argument("--only", default="")
    ap.add_argument("--each", action="store_true", help="also write screen-<specimen>.png and screen-<specimen>-hit.png, one frame each at 1:1")
    a = ap.parse_args()
    exe = build(a.sdk, a.cxx)
    fragment = (ROOT / "panel.svg.fragment").read_text()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    defs = defaults()
    sheets = {}
    # Every specimen: all controls at 0, 50 and 100, its defaults, a hit.
    for m in range(16):
        d = defs[m]
        rows = [("all 0", scene(m, [0] * 8)), ("all 50", scene(m, [64] * 8)), ("all 100", scene(m, [127] * 8)),
                ("defaults", scene(m, d)), ("defaults, hit +120 ms", scene(m, d, hit=120))]
        sheets.setdefault("specimens", []).extend((f"{NAMES[m]} · {l}", s) for l, s in rows)
    # Each control of a specimen at 0, 50 and 100 (the others at their defaults).
    for m in range(16):
        d = defs[m]
        rows = []
        for c in range(8):
            for v in (0, 64, 127):
                ctl = list(d)
                ctl[c] = v
                rows.append((f"{NAMES[m]} · control {c + 1} at {round(v / 1.27)}", scene(m, ctl)))
        sheets[f"controls-{NAMES[m].replace(' ', '')}"] = rows
    # Shift on both rows.
    d = defs[0]
    sheets["shift"] = [
        ("Shift, first row", scene(0, d, shift=1, line=0, pan=-40, level=-6, choke=1)),
        ("Shift, second row", scene(0, d, shift=1, line=1, pads=(-5, 8, 0, 4))),
        ("Shift, first row: bell jar", scene(13, defs[13], shift=1, line=0, choke=0)),
        ("second row, no Shift", scene(0, d, line=1)),
    ]
    # The README's screens: four specimens mid-hit.
    sheets["screens"] = [("ball, 60 ms after a hit", scene(0, defs[0], hit=60)), ("glass, 120 ms after a hit", scene(12, defs[12], hit=120)),
                         ("cicada, 40 ms after a hit", scene(8, defs[8], hit=40)), ("bell jar, 60 ms after a hit", scene(13, defs[13], hit=60)),
                         ("spark, 40 ms after a hit", scene(6, defs[6], hit=40)), ("sheet, 80 ms after a hit", scene(5, defs[5], hit=80)),
                         ("match, 60 ms after a hit", scene(11, defs[11], hit=60)), ("roar, 300 ms after a hit", scene(15, defs[15], hit=300))]
    if not a.only or a.only == "screen":
        label, hero = HERO
        f = frames(exe, [scene(hero[0], defs[hero[0]], hit=hero[1])])[0]
        css = (f"@font-face {{ font-family: UniversNextPro-Light; src: url('file://{a.font}'); }}" if a.font else "") + \
            "body { margin: 0; background: #000; } svg { display: block; }"
        html = f"<!DOCTYPE html><html><head><style>{css}</style></head><body>{svg(fragment, f)}</body></html>"
        shoot(html, 480, 222, out / "screen.png")
        print("wrote", out / "screen.png", f"({label})")
    if a.each:
        css = (f"@font-face {{ font-family: UniversNextPro-Light; src: url('file://{a.font}'); }}" if a.font else "") + \
            "body { margin: 0; background: #000; } svg { display: block; }"
        scenes = []
        for m in range(16):
            scenes += [(f"screen-{NAMES[m]}", scene(m, defs[m])), (f"screen-{NAMES[m]}-hit", scene(m, defs[m], hit=60))]
        for (name, _), f in zip(scenes, frames(exe, [s for _, s in scenes])):
            html = f"<!DOCTYPE html><html><head><style>{css}</style></head><body>{svg(fragment, f)}</body></html>"
            shoot(html, 480, 222, out / f"{name}.png")
        print("wrote", len(scenes), "screens, one a specimen at its defaults and 60 ms into a hit")
    for name, rows in sheets.items():
        if a.only and a.only not in name:
            continue
        fr = frames(exe, [s for _, s in rows])
        bad = [l for (l, _), f in zip(rows, fr) if not f["valid"]]
        if bad:
            print("INVALID frames:", bad)
        cols = 4 if name == "shift" else 5 if name == "specimens" else 2 if name == "screens" else 3
        html, width = page([(l, svg(fragment, f)) for (l, _), f in zip(rows, fr)], a.font, cols)
        height = ((len(rows) + cols - 1) // cols) * 244 + 12
        png = out / f"sheet-{name}.png"
        shoot(html, width, height, png)
        print("wrote", png)


if __name__ == "__main__":
    main()
