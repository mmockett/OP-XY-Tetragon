#!/usr/bin/env python3
"""Draws Syahi's screen from its real display() (tools/frames.cpp) at 1:1, the
way the OP-XY paints it: every node takes its palette entry's colour and
stroke width (scaled with the node), a fill only where the artwork has one,
text in the device's face (XY_FONT). Writes contact sheets.

    XY_FONT=<UniversNextPro-Light.ttf> python3 tools/sheet.py --frames <frames binary> --out <dir> [--scale 1]
"""
import argparse, html, json, math, os, pathlib, re, subprocess, sys, tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kits  # noqa: E402

ROOT = HERE.parent
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"


def attrs(tag):
    return dict(re.findall(r'([\w-]+)="([^"]*)"', tag))


def set_attr(tag, name, value):
    if re.search(rf'\s{name}="[^"]*"', tag):
        return re.sub(rf'(\s){name}="[^"]*"', lambda m: f'{m.group(1)}{name}="{value}"', tag)
    return re.sub(r"(/?>)$", f' {name}="{value}"\\1', tag)


def draw(frame):
    svg = (ROOT / "panel.svg.fragment").read_text()
    svg = re.sub(r"<!--.*?-->", "", svg, flags=re.S)
    palette = {int(m.group(1)): attrs(m.group(0)) for m in re.finditer(r'<\w+\b[^>]*_palette_(\d+)"[^>]*>', svg)}
    nodes, paths = frame["nodes"], frame["paths"]

    def node(m):
        i = int(m.group(2))
        tag = m.group(0)
        x, y, scale, cos, sin, shade, paint = nodes[i]
        a, b = scale * cos / 1024 / 1024, scale * sin / 1024 / 1024
        tag = set_attr(tag, "transform", f"matrix({a:.5f} {b:.5f} {-b:.5f} {a:.5f} {x / 1024:.4f} {y / 1024:.4f})")
        if paint and shade in palette:
            p = palette[shade]
            tag = set_attr(tag, "stroke", p.get("stroke", "#fff"))
            tag = set_attr(tag, "stroke-width", p.get("stroke-width", "1"))
            if paint == 2 and re.search(r'\sfill="(?!none)', tag):
                tag = set_attr(tag, "fill", p.get("fill", "#fff"))
        if i < len(paths) and m.group(1) == "path":
            pts = paths[i]["points"]
            tag = set_attr(tag, "d", "M" + " L".join(f"{px / 1024:.4f} {py / 1024:.4f}" for px, py in pts) + " Z")
            tag = set_attr(tag, "fill", "none")
        return tag

    svg = re.sub(r'<(path|circle)\b[^>]*_node_(\d+)"[^>]*?/?>', node, svg)

    def text(m):
        i = int(m.group(2))
        x, y, scale, chars = frame["texts"][i]
        return set_attr(m.group(1), "transform", f"matrix({scale / 1024:.4f} 0 0 {scale / 1024:.4f} {x / 1024:.3f} {y / 1024:.3f})") + html.escape(chars) + "</text>"

    svg = re.sub(r'(<text\b[^>]*_text_(\d+)"[^>]*>)[^<]*</text>', text, svg)
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 220" width="480" height="220"><rect width="480" height="220" fill="#000"/>{svg}</svg>'


def state(model, ctl, shift=0, line=0, pan=0, level=0, pads=(0, 0, 0, 0), choke=0, hit=-1, serial=0):
    return [model, *ctl, shift, line, pan, level, *pads, choke, hit, serial]


def frames(binary, states):
    text = "\n".join(" ".join(map(str, s)) for s in states) + "\n"
    out = subprocess.run([binary], input=text, capture_output=True, text=True, check=True).stdout
    return [json.loads(l) for l in out.splitlines()]


def shoot(page_html, png, w, h):
    with tempfile.TemporaryDirectory() as tmp:
        p = pathlib.Path(tmp) / "page.html"
        p.write_text(page_html)
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--allow-file-access-from-files", f"--user-data-dir={tmp}/c",
                        "--hide-scrollbars", f"--window-size={w},{h}", "--virtual-time-budget=2000", f"--screenshot={png}",
                        "file://" + str(p)], capture_output=True, timeout=180)


def sheet(cells, cols, png, scale=1):
    font = os.environ.get("XY_FONT", "")
    face = f"@font-face{{font-family:UniversNextPro-Light;src:url('file://{font}')}}" if font else ""
    W = 480 * scale
    css = (face + "body{margin:0;background:#161616;font-family:UniversNextPro-Light,sans-serif;color:#aaa}"
           f".g{{display:grid;grid-template-columns:repeat({cols},{W}px);gap:14px;padding:14px}}"
           f".c svg{{width:{W}px;height:{220 * scale}px;display:block}}.c div{{font-size:12px;padding:4px 0 0 2px}}")
    body = "".join(f'<div class="c">{svg}<div>{html.escape(label)}</div></div>' for svg, label in cells)
    rows = (len(cells) + cols - 1) // cols
    shoot(f"<html><head><style>{css}</style></head><body><div class='g'>{body}</div></body></html>", png,
          14 + cols * (W + 14), 14 + rows * (220 * scale + 34))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--only")
    ap.add_argument("--overview", action="store_true")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    if a.overview:
        overview(a.frames, out / "overview.png")
        screen(a.frames, out / "screen.png")
        return
    models = [a.only] if a.only else kits.MODELS
    for model in models:
        mi = kits.MODELS.index(model)
        d = kits.DEFAULTS[model]
        cells = []
        sts, labels = [state(mi, d)], ["defaults"]
        sts.append(state(mi, d, hit=60)); labels.append("hit, 60 ms after")
        sts.append(state(mi, d, shift=1, line=0, pan=-40, level=-6, choke=1)); labels.append("shift, first row")
        sts.append(state(mi, d, shift=1, line=1, pads=(6, -5, 0, 51))); labels.append("shift, second row")
        for slot, label in enumerate(kits.LABELS[model]):
            for v in (0, 64, 127):
                c = list(d)
                c[slot] = v
                sts.append(state(mi, c)); labels.append(f"{label} {round(v * 100 / 127)}")
        fr = frames(a.frames, sts)
        bad = [l for f, l in zip(fr, labels) if not f["valid"]]
        if bad:
            print(model, "INVALID", bad)
        cells = [(draw(f), l) for f, l in zip(fr, labels)]
        sheet(cells, 4 if a.scale == 1 else 3, out / f"{model.lower()}.png", a.scale)
        print("wrote", out / f"{model.lower()}.png")



def screen(binary, png, scale=1):
    """One frame: TABLA at its defaults, 60 ms after a stroke, for the README, at the screen's own 480 x 222."""
    mi = kits.MODELS.index("TABLA")
    f = frames(binary, [state(mi, kits.DEFAULTS["TABLA"], hit=60)])[0]
    font = os.environ.get("XY_FONT", "")
    face = f"@font-face{{font-family:UniversNextPro-Light;src:url('file://{font}')}}" if font else ""
    svg = draw(f).replace('width="480" height="220"', f'width="{480 * scale}" height="{220 * scale}"')
    shoot(f"<html><head><style>{face}body{{margin:0;background:#000}}svg{{display:block}}</style></head><body>{svg}</body></html>", png, 480 * scale, 222 * scale)


def overview(binary, png):
    """Every model at its defaults, at rest and 60 ms after a hit."""
    sts, labels = [], []
    for mi, model in enumerate(kits.MODELS):
        sts.append(state(mi, kits.DEFAULTS[model])); labels.append(f"{model}")
        sts.append(state(mi, kits.DEFAULTS[model], hit=60)); labels.append(f"{model}, hit")
    fr = frames(binary, sts)
    sheet([(draw(f), l) for f, l in zip(fr, labels)], 4, png)


if __name__ == "__main__":
    main()
