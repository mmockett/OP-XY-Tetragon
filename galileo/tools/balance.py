#!/usr/bin/env python3
"""Writes tools/key_levels.json: each kit preset's key levels, so every key
sounds at its role's loudness (catalog.TARGET, the loudest 400 ms K-weighted,
LUFS-M max, re the half-scale ceiling) at its own settings. A key's level can
only cut, so a key over its target is cut to it (whole dB); one at or under it
stays at 0 dB. In the default kit (the first preset, default.json) only a key
more than 0.75 dB over its target is cut, so the rest start at 0 dB as the
device starts them. Run after tools/calibrate.py, with the SDK's Python:

    open-xy/.venv/bin/python -I tools/balance.py
"""
import json
import pathlib
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import catalog  # noqa: E402
import engine_host as E  # noqa: E402

out = {}
for n, (name, _role, keys) in enumerate(catalog.PRESETS):
    levels = {}
    for i, k in enumerate(keys):
        model = catalog.NAMES[k[0]] if isinstance(k[0], int) else k[0]
        ys = E.hits(model, k[1], k.pad.get("space", 0), k.pad.get("feel", 0))
        loud = float(np.mean([E.momentary_max(y) for y in ys]))
        over = loud - catalog.TARGET[catalog.ROLE[model]]
        level = int(min(0, round(-over))) if n or over > 0.75 else 0
        if level:
            levels[str(i)] = max(-30, level)
        print(f"{name:14s} {i:2d} {model:8s} {loud:6.1f} -> level {level}")
    out[name] = levels
(HERE / "key_levels.json").write_text(json.dumps(out, indent=1) + "\n")
