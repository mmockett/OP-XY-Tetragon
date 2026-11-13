#!/usr/bin/env python3
"""Writes the review scenes each kit's tools/frames.cpp reads, one file a kit:
tools/review/<kit>.scenes. The review workflow (.github/workflows/frames.yml)
runs every kit's real display code over them on CI and keeps the frames;
tools/review/render.py draws them locally in the device's face. Pure Python.

    python3 tools/review/scenes.py

Each scene has a name, written to <kit>.names one a line in the frames' order.
Per kit: every model at its defaults at rest and 60 and 250 ms into a hit;
the first model with each control at 0 and 127, all at 0 and all at 127 (each
also mid-hit); and both Shift rows.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
HITS = (60, 250)


def plain(kit, models, line):
    """Scenes for kits whose frames.cpp reads one line a scene: line(m, c, hit, shift) -> text."""
    out = []
    for m, (name, defaults) in enumerate(models):
        out.append((f"{name}", line(m, defaults, None, 0)))
        for h in HITS:
            out.append((f"{name}, {h} ms after a hit", line(m, defaults, h, 0)))
    name, d = models[0]
    for i in range(8):
        for v in (0, 127):
            c = list(d)
            c[i] = v
            out.append((f"{name}, control {i + 1} at {v}", line(0, c, None, 0)))
    for v in (0, 127):
        out.append((f"{name}, all at {v}", line(0, [v] * 8, None, 0)))
        out.append((f"{name}, all at {v}, {HITS[0]} ms after a hit", line(0, [v] * 8, HITS[0], 0)))
    out.append((f"{name}, Shift, row 1", line(0, d, None, 1)))
    out.append((f"{name}, Shift, row 2", line(0, d, None, 2)))
    return out


def thunder():
    src = (ROOT / "thunder/kit.h").read_text()
    models = [(n.lower(), [int(v) for v in d.split(",")])
              for n, d in re.findall(r'\{"(\w+)", \{[^}]*\}, \{([^}]*)\}\}', src)]

    def line(m, c, hit, shift):
        row = 1 if shift == 2 else 0
        return " ".join(map(str, [m, *c, row, 1 if shift else 0, 0, 0, 0, 0, 0, 0, -1 if hit is None else hit]))
    return plain("thunder", models, line)


def curio():
    src = (ROOT / "curio/kit.h").read_text()
    models = [(n, [int(v) for v in d.split(",")])
              for n, d in re.findall(r'\{"([a-z ]+)", \d, \{[^}]*\}, \{([^}]*)\}', src)]

    def line(m, c, hit, shift):
        return " ".join(map(str, [m, *c, 1 if shift else 0, 1 if shift == 2 else 0, 0, 0, 0, 0, 0, 0, 0,
                                  0 if hit is None else hit, -1, 0, 0, 0]))
    return plain("curio", models, line)


def syahi():
    src = (ROOT / "syahi/kit.h").read_text()
    names = re.findall(r'\{"(\w+)", \{"TUNE"', src)
    body = re.search(r"model_defaults\[models\]\[8\] = \{(.*?)\};", src, re.S).group(1)
    defaults = [[int(v) for v in d.split(",")] for d in re.findall(r"\{([^}]*)\}", body)]
    models = [(n.lower(), d) for n, d in zip(names, defaults)]

    def line(m, c, hit, shift):
        return " ".join(map(str, [m, *c, 1 if shift else 0, 1 if shift == 2 else 0, 0, 0, 0, 0, 0, 0, 0,
                                  -1 if hit is None else hit, 1]))
    return plain("syahi", models, line)


def galileo():
    sys.path.insert(0, str(ROOT / "galileo/tools"))
    import catalog
    models = [(m[0].lower(), list(catalog.key(m[0])[1])) for m in catalog.MODELS]

    def line(m, c, hit, shift):
        s = [f"scene s", f"model {m}", "slots " + " ".join(map(str, c)), "pad 0 0 0 0", "row 0"]
        if shift:
            s.append(f"shift 1 {shift - 1}")
        s.append("run 0.1 60")
        if hit is not None:
            s += ["note", f"run {hit / 1000:.3f} 60"]
        s.append("emit")
        return "\n".join(s)
    return plain("galileo", models, line)


def main():
    for kit, make in (("thunder", thunder), ("curio", curio), ("syahi", syahi), ("galileo", galileo)):
        scenes = make()
        (HERE / f"{kit}.scenes").write_text("\n".join(s for _, s in scenes) + "\n")
        (HERE / f"{kit}.names").write_text("\n".join(n for n, _ in scenes) + "\n")
        print(f"{kit}: {len(scenes)} scenes")


if __name__ == "__main__":
    main()
