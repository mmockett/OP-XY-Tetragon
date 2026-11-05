#!/usr/bin/env python3
"""Renders THUNDER's screen from the real display code: contact sheets at the
screen's own size (480 x 222, 1:1) of every model's eight controls at 0, 50
and 100, both Shift rows and hits.
It builds tools/frames.cpp against the SDK, applies each frame to
panel.svg.fragment as the renderers do and rasterises the sheets with a
headless Chrome, its text in the device's face. Not part of the machine.

    XY_FONT=<UniversNextPro-Light.ttf> python3 tools/make_sheet.py --sdk <open-xy> [--out docs]

Writes <out>/sheets/<model>.png, <out>/controls-1x.png (every model at its
defaults and struck) and <out>/shift-1x.png. The font is
stock content: it is read from --font or $XY_FONT and never copied here.
"""
import argparse, copy, json, os, pathlib, re, subprocess, sys, tempfile, xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument("--sdk", required=True)
ap.add_argument("--out", default=str(ROOT / "docs"))
ap.add_argument("--chrome", default="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
ap.add_argument("--font", default=os.environ.get("XY_FONT", ""))
ap.add_argument("--only")
ap.add_argument("--quick", action="store_true", help="only controls-1x.png and screen.png")
args = ap.parse_args()
if not os.path.exists(args.font):
    sys.exit("set XY_FONT or --font to UniversNextPro-Light.ttf: the text mustn't fall back to another face")
S = pathlib.Path(tempfile.mkdtemp(prefix="thunder-sheet-"))
OUT = pathlib.Path(args.out)
(OUT / "sheets").mkdir(parents=True, exist_ok=True)
FACE = f'<style>@font-face{{font-family:"UniversNextPro-Light";src:url("file://{args.font}")}}</style>'
CHROME = [args.chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files", "--virtual-time-budget=3000"]
subprocess.run(["c++", "-std=c++11", "-O2", f"-I{args.sdk}/sdk/include", f"-I{ROOT}", str(ROOT / "tools/frames.cpp"), "-o", str(S / "frames")], check=True)
base = ET.fromstring((ROOT / "panel.svg.fragment").read_text())
kit = (ROOT / "kit.h").read_text()
MODELS = [(n, [l.strip().strip('"') for l in labels.split(",")], [int(x) for x in d.split(",")])
          for n, labels, d in re.findall(r'\{"(\w+)", \{([^}]*)\}, \{([^}]*)\}\}', kit)]


def frames(scenes):
    text = "\n".join(" ".join(map(str, s)) for s in scenes) + "\n"
    out = subprocess.run([str(S / "frames")], input=text, capture_output=True, text=True, check=True).stdout
    return [json.loads(line) for line in out.split("\n") if line]


def svg(data):
    root = copy.deepcopy(base)
    by = {e.get("id"): e for e in root.iter() if e.get("id")}
    palette = [by[f"thunder_palette_{i}"] for i in range(16)]
    for i, (x, y, s, c, si, shade, paint) in enumerate(data["nodes"]):
        e = by[f"thunder_node_{i}"]
        k = s / 1024
        a, b = c / 1024 * k, si / 1024 * k
        e.set("transform", f"matrix({a} {b} {-b} {a} {x / 1024} {y / 1024})")
        if paint in (1, 2):
            e.set("stroke", palette[shade].get("stroke"))
            e.set("stroke-width", palette[shade].get("stroke-width"))
            if paint == 2 and e.get("fill") not in (None, "none"):
                e.set("fill", palette[shade].get("fill"))
    for p, pts in enumerate(data["paths"]):
        by[f"thunder_node_{p}"].set("d", "M" + " L".join(f"{x / 1024},{y / 1024}" for x, y in pts) + " Z")
    for t, (x, y, s, chars) in enumerate(data["texts"]):
        e = by[f"thunder_text_{t}"]
        e.set("transform", f"matrix({s / 1024} 0 0 {s / 1024} {x / 1024} {y / 1024})")
        e.text = chars
    body = ET.tostring(root, encoding="unicode").replace("ns0:", "").replace(":ns0", "")
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222" width="480" height="222" style="display:block">'
            f'<rect width="480" height="222" fill="#000"/>{body}</svg>')


def sheet(tiles, path, cols=3):
    style = ("body{margin:0;background:#1c1c1c;color:#bbb;font:13px UniversNextPro-Light}"
             ".cell{float:left;width:480px;height:246px;margin:0 8px 8px 0}.v{height:20px;line-height:20px;padding-left:4px}")
    parts = [f'<div class="cell"><div class="v">{cap}</div>{svg(f)}</div>' for cap, f in tiles]
    html = S / (path.stem + ".html")
    html.write_text(f"<html><head>{FACE}<style>{style}</style></head><body>{''.join(parts)}</body></html>")
    rows = (len(tiles) + cols - 1) // cols
    w, h = cols * 488, rows * 254
    subprocess.run(CHROME + ["--force-device-scale-factor=1", f"--window-size={w},{h}", f"--screenshot={path}", f"file://{html}"],
                   check=True, capture_output=True)
    print("wrote", path)


def scene(m, c, hit=-1, row=0, shift=0, pads=(0, 0, 0, 0, 0, 0)):
    return [m, *c, row, shift, pads[0], pads[1], pads[2], pads[3], pads[4], pads[5], hit]


overview = []
for mi, (name, labels, d) in enumerate(MODELS):
    if args.only and name != args.only:
        continue
    if args.quick:
        fs = frames([scene(mi, d), scene(mi, d, 30)])
        overview += [(f"{name} · defaults", fs[0]), (f"{name} · struck, 30 ms on", fs[1])]
        continue
    scenes, caps = [], []
    for k in range(8):
        for v in (0, 64, 127):
            c = list(d)
            c[k] = v
            scenes.append(scene(mi, c, row=k // 4))
            caps.append(f"{name} · {labels[k].lower()} {round(v / 127 * 100)}")
    scenes.append(scene(mi, d))
    caps.append(f"{name} · defaults")
    for ms in (30, 120, 400):
        scenes.append(scene(mi, d, ms))
        caps.append(f"{name} · struck, {ms} ms on")
    scenes.append(scene(mi, d, row=0, shift=1, pads=(0, 0, -20, -6, 0, 0)))
    caps.append(f"{name} · shift, first row")
    scenes.append(scene(mi, d, row=1, shift=1, pads=(5, 9, 3, 0, 0, 0)))
    caps.append(f"{name} · shift, second row")
    fs = frames(scenes)
    bad = [cap for cap, f in zip(caps, fs) if not f["valid"]]
    if bad:
        sys.exit(f"invalid frames: {bad}")
    sheet(list(zip(caps, fs)), OUT / "sheets" / f"{name.lower()}.png")
    overview.append((f"{name} · defaults", fs[24]))
    overview.append((f"{name} · struck, 30 ms on", fs[25]))
if not args.only:
    sheet(overview, OUT / "controls-1x.png", cols=4)
    # the screen on its own, 1:1: the default kit's kick, struck, with a chain on the sheet
    one = frames([scene(0, MODELS[0][2][:7] + [70], 40)])[0]
    html = S / "screen.html"
    html.write_text(f"<html><head>{FACE}<style>body{{margin:0;background:#000}}</style></head><body>{svg(one)}</body></html>")
    subprocess.run(CHROME + ["--force-device-scale-factor=1", "--window-size=480,222", f"--screenshot={OUT / 'screen.png'}", f"file://{html}"],
                   check=True, capture_output=True)
    print("wrote", OUT / "screen.png")
