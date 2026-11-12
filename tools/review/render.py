#!/usr/bin/env python3
"""Draws the review frames (the artifact of .github/workflows/frames.yml) as the
OP-XY's renderer applies a frame to its artwork: each node's transform, its
palette paint, the reshaped outlines and the text slots. Text is set in the
device's face, Univers Next Pro Light, which is stock content: pass it with
XY_FONT, never commit it. Headless Chrome rasterises. Pure Python.

    gh run download <run> -n frames -D <frames>
    XY_FONT=<UniversNextPro-Light.ttf> python3 tools/review/render.py <frames> <out> [--scale 2] [--only curio]

Writes <out>/<kit>/<NNN>.png, one a scene, and <out>/<kit>.html, a page of
them all with their names.
"""
import argparse
import html
import os
import pathlib
import re
import subprocess
import tempfile

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"


def attr(tag, name, value):
    if re.search(rf'\s{name}="[^"]*"', tag):
        return re.sub(rf'(\s){name}="[^"]*"', lambda m: f'{m.group(1)}{name}="{value}"', tag, count=1)
    return re.sub(r"\s*/?>$", lambda m: f' {name}="{value}"{m.group(0)}', tag, count=1)


def get(tag, name):
    m = re.search(rf'\s{name}="([^"]*)"', tag)
    return m.group(1) if m else None


def points(path):
    if isinstance(path, dict):
        path = path["points"]
    if path and isinstance(path[0], (int, float)):
        return list(zip(path[0::2], path[1::2]))
    return [tuple(p) for p in path]


def svg(fragment, frame):
    s = re.sub(r"<!--.*?-->", "", fragment, flags=re.S)
    palette = {}
    for m in re.finditer(r'<\w+\b[^>]*_palette_(\d+)"[^>]*>', s):
        palette[int(m.group(1))] = (get(m.group(0), "fill"), get(m.group(0), "stroke"), get(m.group(0), "stroke-width"))
    nodes, paths = frame["nodes"], frame.get("paths", [])

    def node(m):
        tag, i = m.group(0), int(m.group(2))
        x, y, scale, cos, sin, shade, paint = nodes[i]
        a, b = scale * cos / 1048576, scale * sin / 1048576
        tag = attr(tag, "transform", f"matrix({a:.5f} {b:.5f} {-b:.5f} {a:.5f} {x / 1024:.4f} {y / 1024:.4f})")
        if paint and shade in palette:
            f, st, w = palette[shade]
            tag = attr(tag, "stroke", st)
            tag = attr(tag, "stroke-width", w)
            if paint == 2 and (get(tag, "fill") or "none") != "none":
                tag = attr(tag, "fill", f)
        if m.group(1) == "path" and i < len(paths):
            pts = points(paths[i])
            tag = attr(tag, "d", "M" + " L".join(f"{px / 1024:.4f},{py / 1024:.4f}" for px, py in pts) + " Z")
        return tag

    s = re.sub(r'<(path|circle)\b[^>]*_node_(\d+)"[^>]*?/?>', node, s)

    def text(m):
        x, y, scale, chars = frame["texts"][int(m.group(2))][:4]
        return attr(m.group(1), "transform", f"matrix({scale / 1024:.4f} 0 0 {scale / 1024:.4f} {x / 1024:.3f} {y / 1024:.3f})") \
            + html.escape(chars) + "</text>"

    s = re.sub(r'(<text\b[^>]*_text_(\d+)"[^>]*>)[^<]*</text>', text, s)
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222" width="480" height="222"><rect width="480" height="222" fill="#000"/>{s}</svg>'


def shoot(page, png, w, h, scale, work):
    f = work / (png.stem + ".html")
    f.write_text(page)
    for attempt in range(3):   # Chrome is now and then killed on a busy Mac: try again
        r = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--allow-file-access-from-files",
                            f"--user-data-dir={work}/profile", f"--force-device-scale-factor={scale}", f"--window-size={w},{h}",
                            "--virtual-time-budget=2000", f"--screenshot={png}", f.as_uri()],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0:
            return
    raise RuntimeError(f"Chrome could not draw {png.name}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--only", default="")
    ap.add_argument("--font", default=os.environ.get("XY_FONT", ""))
    a = ap.parse_args()
    assert a.font and pathlib.Path(a.font).exists(), "set XY_FONT to Univers Next Pro Light (never a fallback face)"
    face = f"@font-face{{font-family:UniversNextPro-Light;src:url('{pathlib.Path(a.font).as_uri()}')}}"
    src, out = pathlib.Path(a.frames), pathlib.Path(a.out)
    import json
    for kit in ("thunder", "curio", "syahi", "galileo"):
        if a.only and kit not in a.only.split(","):
            continue
        frag = (src / f"{kit}.svg.fragment").read_text()
        names = (src / f"{kit}.names").read_text().splitlines()
        frames = [json.loads(l) for l in (src / f"{kit}.jsonl").read_text().splitlines() if l.startswith("{")]
        assert len(frames) == len(names), (kit, len(frames), len(names))
        (out / kit).mkdir(parents=True, exist_ok=True)
        work = pathlib.Path(tempfile.mkdtemp(dir=out))
        cells = []
        for k, (name, fr) in enumerate(zip(names, frames)):
            png = out / kit / f"{k:03d}.png"
            body = svg(frag, fr)
            shoot(f"<!doctype html><style>{face}body{{margin:0;background:#000}}svg{{display:block}}</style>{body}",
                  png, 480, 222, a.scale, work)
            cells.append(f'<figure><img src="{kit}/{png.name}" width="480"><figcaption>{k:03d} {html.escape(name)}'
                         f'{"" if fr.get("valid", 1) else " (INVALID FRAME)"}</figcaption></figure>')
        (out / f"{kit}.html").write_text(
            f"<!doctype html><style>{face}body{{background:#1a1a1a;color:#bbb;font:13px UniversNextPro-Light;"
            "display:grid;grid-template-columns:repeat(3,480px);gap:4px 10px}figure{margin:0}</style>" + "".join(cells))
        subprocess.run(["rm", "-rf", str(work)])
        print(f"{kit}: {len(frames)} frames")


if __name__ == "__main__":
    main()
