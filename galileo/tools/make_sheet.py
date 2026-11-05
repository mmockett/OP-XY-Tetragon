#!/usr/bin/env python3
"""Draws GALILEO's screen from its real display code: builds tools/frames.cpp
against the SDK, applies each frame to panel.svg.fragment as the renderers do,
and rasterises the frames with headless Chrome at 1:1, text in the device's
own face. Not part of the machine.

    XY_FONT=<UniversNextPro-Light.ttf> python3 tools/make_sheet.py --sdk <open-xy> [--out docs]

Writes docs/controls-1x.png (every control at 0 / 50 / 100, Shift on both rows,
frames mid-run, the ball's path lit as it passes), docs/bodies-1x.png
(the fifteen bodies at their defaults and each body control at 0 and 100,
cropped to the foot of the plane) and docs/screen.png (one frame mid-run);
with --models, model-<name>-1x.png, each body's controls on the whole screen. The font is
stock content: it is read from XY_FONT and never copied here.
"""
import argparse
import copy
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import catalog  # noqa: E402

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"


def build_frames(sdk, work):
    exe = work / "frames"
    subprocess.run(["c++", "-std=c++11", "-O2", f"-I{sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools" / "frames.cpp"),
                    str(ROOT / "tables.cpp"), "-o", str(exe)], check=True)
    return exe


def run_script(exe, script):
    r = subprocess.run([str(exe)], input=script.encode(), capture_output=True, check=True)
    return [json.loads(line) for line in r.stdout.decode().splitlines() if line.strip()]


BASE = ET.fromstring((ROOT / "panel.svg.fragment").read_text())


def svg(frame, w=480, h=222, view="0 0 480 222"):
    root = copy.deepcopy(BASE)
    by = {e.get("id"): e for e in root.iter() if e.get("id")}
    palette = [by[f"galileo_palette_{i}"] for i in range(16) if f"galileo_palette_{i}" in by]
    for i, (x, y, s, c, si, shade, paint) in enumerate(frame["nodes"]):
        e = by.get(f"galileo_node_{i}")
        if e is None:
            continue
        k = s / 1024
        a, b = c / 1024 * k, si / 1024 * k
        e.set("transform", f"matrix({a:.5f} {b:.5f} {-b:.5f} {a:.5f} {x / 1024:.4f} {y / 1024:.4f})")
        if paint in (1, 2):
            e.set("stroke", palette[shade].get("stroke"))
            e.set("stroke-width", palette[shade].get("stroke-width"))
            if paint == 2 and e.get("fill") not in (None, "none"):
                e.set("fill", palette[shade].get("fill"))
    for p, pts in enumerate(frame["paths"]):
        by[f"galileo_node_{p}"].set("d", "M" + " L".join(f"{x / 1024:.4f},{y / 1024:.4f}" for x, y in pts) + " Z")
    for t, (x, y, s, chars) in enumerate(frame["texts"]):
        e = by.get(f"galileo_text_{t}")
        if e is None:
            continue
        e.set("transform", f"matrix({s / 1024:.4f} 0 0 {s / 1024:.4f} {x / 1024:.4f} {y / 1024:.4f})")
        e.text = chars
    body = ET.tostring(root, encoding="unicode").replace("ns0:", "").replace(":ns0", "")
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view}" width="{w}" height="{h}" style="display:block">'
            f'<rect width="480" height="222" fill="#000"/>{body}</svg>')


def key_of(model, **over):
    return catalog.key(model, **over)


def scene(name, model, values, pad=(0, 0, 0, 0), row=1, note=False, run=0.0, amp=None, shift=None, release=None, extra=""):
    m = catalog.model_index(model)
    lines = [f"scene {name}", f"model {m}", "slots " + " ".join(map(str, values)), "pad " + " ".join(map(str, pad)),
             f"row {row}"]
    if amp:
        lines.append("amp " + " ".join(map(str, amp)))
    if shift is not None:
        lines.append(f"shift 1 {shift}")
        lines.append(f"choices {[g for g, _ in catalog.GROUPS].index(catalog.MODELS[m][1])} 0 0 0")
    lines.append(extra)
    lines.append("run 0.1 60")
    if note:
        lines.append("note")
        if release is not None:
            lines.append(f"run {release} 60")
            lines.append("release")
            lines.append(f"run {max(0.0, run - release)} 60")
        else:
            lines.append(f"run {run} 60")
    lines.append("emit")
    return "\n".join(lines)


def capture(html, png, width, height, scale=1):
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                    "--virtual-time-budget=3000", f"--force-device-scale-factor={scale}", f"--window-size={width},{height}",
                    f"--screenshot={png}", f"file://{html}"], check=True, capture_output=True)


def sheet(rows, out_png, font, work, cols=3, title=None):
    face = (f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{font}")}}'
            'body{margin:0;background:#111;color:#bbb;font:13px UniversNextPro-Light}'
            '.r{position:absolute;left:8px}.c{position:absolute}.cap{position:absolute;color:#999;font-size:12px}</style>')
    parts = []
    y = 8
    for label, cells in rows:
        parts.append(f'<div class="r" style="top:{y}px">{label}</div>')
        y += 20
        for j, (frame, cap) in enumerate(cells):
            x = 8 + j * 492
            parts.append(f'<div class="c" style="left:{x}px;top:{y}px">{svg(frame)}</div>')
            parts.append(f'<div class="cap" style="left:{x}px;top:{y + 224}px">{cap}</div>')
        y += 248
    html = work / (pathlib.Path(out_png).stem + ".html")
    html.write_text(f"<html><head>{face}</head><body>{''.join(parts)}</body></html>")
    capture(html, out_png, 8 + cols * 492, y + 4)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", default=str(ROOT.parent.parent / "open-xy"))
    parser.add_argument("--out", default=str(ROOT / "docs"))
    parser.add_argument("--font", default=os.environ.get("XY_FONT", ""))
    parser.add_argument("--models", action="store_true", help="also one sheet per model")
    args = parser.parse_args()
    if not os.path.exists(args.font):
        sys.exit("set XY_FONT or --font to UniversNextPro-Light.ttf: the text mustn't fall back to another face")
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="galileo-sheet-"))
    exe = build_frames(args.sdk, work)
    T = catalog.TILT
    # The snare roll key, its motion row, at three settings of each control.
    roll = dict(frets=catalog.frets(8), tilt=T["1/32"], bounce=catalog.bounce(0.45))
    base = key_of("SNARE", **roll)[1]
    rows = []

    def row(label, model, slot_values, slot, captions, **kw):
        script = []
        for v, cap in zip(slot_values, captions):
            vals = list(base if model == "SNARE" else key_of(model)[1])
            vals[slot] = v
            script.append(scene(f"s{len(script)}", model, vals, **kw))
        frames = run_script(exe, "\n".join(script))
        rows.append((label, [(f, cap) for f, cap in zip(frames, captions)]))

    third = [0, 64, 127]
    row("tilt 0 / 50 / 100: the board's angle (the division, the strike's force)", "SNARE", third, 4, ["tilt 0", "tilt 50", "tilt 100"])
    row("frets 0 / 50 / 100: bells on the rail (a single hit, a ruff, a roll)", "SNARE", third, 5, ["frets 0", "frets 50", "frets 100"])
    row("bounce 0 / 50 / 100: the hops over the body", "SNARE", third, 6, ["bounce 0", "bounce 50", "bounce 100"])
    row("balls 0 / 50 / 100: the handful behind the gate", "SNARE", third, 7, ["balls 0", "balls 50", "balls 100"])
    # SPACE and FEEL (pads), Shift on both rows
    frames = run_script(exe, "\n".join(scene(f"p{i}", "SNARE", base, pad=(v, 0, 0, 0)) for i, v in enumerate((-16, 0, 15))))
    rows.append(("SPACE -16 / 0 / +15 (Shift, second row): the bells slide along the rail", list(zip(frames, ["-16", "even", "+15"]))))
    frames = run_script(exe, "\n".join(scene(f"f{i}", "SNARE", base, pad=(0, v, 0, 0), note=True, run=0.35) for i, v in enumerate((0, 4, 15))))
    rows.append(("FEEL lock / sw21 / free, mid-run: the pendulum swings the beat, or is tied back", list(zip(frames, ["lock", "sw21", "free"]))))
    frames = run_script(exe, "\n".join([scene("sh0", "SNARE", base, row=0, shift=0, extra="pan -40\nlevel -6"),
                                        scene("sh1", "SNARE", base, row=1, shift=1, pad=(-6, 9, 0, 4)),
                                        scene("rowa", "SNARE", base, row=0)]))
    rows.append(("Shift on the first row (MACHINE, CHOKE, PAN, LEVEL), on the second (SPACE, FEEL, FX I, FX II); the body row",
                 list(zip(frames, ["Shift, row 1", "Shift, row 2", "body row bright"]))))
    frames = run_script(exe, "\n".join(scene(f"m{i}", "SNARE", base, note=True, run=r) for i, r in enumerate((0.12, 0.42, 0.7))))
    rows.append(("one note mid-run: the roll, the fall, the hops (1/32 at the learned 120 BPM)", list(zip(frames, ["0.12 s", "0.42 s", "0.70 s"]))))
    frames = run_script(exe, "\n".join(scene(f"b{i}", "SNARE", base, note=True, run=r) for i, r in enumerate((0.36, 0.52, 1.6))))
    rows.append(("the ball's path, lit as it passes: falling, hopping, at rest (the dots go out a few a frame)",
                 list(zip(frames, ["0.36 s", "0.52 s", "1.6 s"]))))
    sheet(rows, out / "controls-1x.png", args.font, work)
    print("wrote", out / "controls-1x.png")

    # A default hit (the snare, no frets: the sound lands at once), frame by
    # frame at 60 a second from the note: the travel the screen shows anyway.
    snare = key_of("SNARE")[1]
    frames = run_script(exe, "\n".join(scene(f"t{k}", "SNARE", snare, note=True, run=(k + 1) / 60) for k in range(12)))
    cells = [(fr, f"{k * 1000 / 60:.0f} ms after the note") for k, fr in enumerate(frames)]
    sheet([("a default snare hit, frame by frame at 60 fps: the ball's travel, shown over 0.1 s", cells[0:3]), ("", cells[3:6]),
           ("", cells[6:9]), ("", cells[9:12])], out / "travel-1x.png", args.font, work)
    print("wrote", out / "travel-1x.png")

    # Each model's body at its defaults, and each body control at 0 / 100.
    rows = []
    for name in catalog.NAMES:
        m = catalog.MODELS[catalog.model_index(name)]
        script = [scene(f"{name}d", name, key_of(name)[1], row=0)]
        for slot in range(4):
            for v in (0, 127):
                vals = list(key_of(name)[1])
                vals[slot] = v
                script.append(scene(f"{name}{slot}{v}", name, vals, row=0))
        frames = run_script(exe, "\n".join(script))
        cells = [(frames[0], f"{name} defaults")]
        for slot in range(4):
            cells.append((frames[1 + 2 * slot], f"{m[2][slot]} 0"))
            cells.append((frames[2 + 2 * slot], f"{m[2][slot]} 100"))
        rows.append((name, cells))
    # all fifteen bodies at once, cropped to the foot of the plane at 1:1
    face = (f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{args.font}")}}'
            'body{margin:0;background:#111;color:#aaa;font:11px UniversNextPro-Light}</style>')
    cells = []
    for r, (name, row_cells) in enumerate(rows):
        for j, (frame, cap) in enumerate(row_cells):
            x, y = 4 + j * 182, 4 + r * 196
            cells.append(f'<div style="position:absolute;left:{x}px;top:{y}px">{svg(frame, 178, 178, "302 38 178 178")}</div>'
                         f'<div style="position:absolute;left:{x}px;top:{y + 178}px">{cap}</div>')
    html = work / "bodies.html"
    html.write_text(f"<html><head>{face}</head><body>{''.join(cells)}</body></html>")
    capture(html, out / "bodies-1x.png", 4 + 9 * 182, 8 + len(rows) * 196)
    # with --models, one page per model: its defaults, then each body control at 0 and 100
    for name, cells in rows if args.models else []:
        grid = [(f"{name}: defaults, then each body control at 0 and 100", cells[0:3]), ("", cells[3:6]), ("", cells[6:9])]
        sheet(grid, out / f"model-{name.lower()}-1x.png", args.font, work)
    print("wrote models sheets")
    f = run_script(exe, scene("hero", "SNARE", base, note=True, run=0.6))[0]
    html = work / "screen.html"
    face = f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{args.font}")}}body{{margin:0;background:#000}}</style>'
    html.write_text(f"<html><head>{face}</head><body>{svg(f)}</body></html>")
    capture(html, out / "screen.png", 480, 222)
    print("wrote", out / "screen.png")


if __name__ == "__main__":
    main()
