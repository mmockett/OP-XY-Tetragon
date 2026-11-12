#!/usr/bin/env python3
"""Runs a kit's own docs tool (its sheet or screen maker) where host code may
not run, with the real display code's frames computed on CI. Pure Python.

Each kit's docs tool compiles tools/frames.cpp with `c++` (CURIO: --cxx, SYAHI:
--frames takes the binary) and pipes scenes into it. This puts a stand-in
`c++` first on PATH that writes, in the binary's place, a stub that answers
from a store of frames, keyed by the exact scenes it is given:

    record:  python3 tools/review/replay.py record <kit> <frames artifact> -- <tool command>
             The stub keeps each request it has no answer for as
             tools/review/docs/<kit>/<hash>.in and answers with placeholder
             frames shaped like the artifact's, so the tool runs to its end
             (its pictures are then meaningless: write them somewhere else).
    CI:      the Screen frames workflow runs the real binary over every .in
             and keeps <hash>.out beside it in its artifact (docs/<kit>/).
    replay:  python3 tools/review/replay.py replay <kit> <frames artifact> -- <tool command>
             The stub answers every request from the artifact's docs/<kit>/
             <hash>.out, and refuses one it has none for.

In the tool command, {frames} stands for the stub's path (SYAHI's --frames).
The tools render with headless Chrome in the device's face: set XY_FONT.
"""
import os
import pathlib
import stat
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
STUB = r'''#!{python}
import hashlib, os, pathlib, sys
data = sys.stdin.buffer.read()
h = hashlib.sha256(data).hexdigest()[:24]
mode, kit, store, template = os.environ["REPLAY_MODE"], os.environ["REPLAY_KIT"], os.environ["REPLAY_STORE"], os.environ["REPLAY_TEMPLATE"]
answer = pathlib.Path(store) / kit / f"{{h}}.out"
if answer.exists():
    sys.stdout.write(answer.read_text())
    sys.exit(0)
if mode == "replay":
    sys.stderr.write(f"replay: no frames for {{kit}} request {{h}}: record it and run CI\n")
    sys.exit(1)
ask = pathlib.Path(os.environ["REPLAY_REQUESTS"]) / kit / f"{{h}}.in"
ask.parent.mkdir(parents=True, exist_ok=True)
ask.write_bytes(data)
lines = data.decode().splitlines()
n = sum(1 for l in lines if l.strip() == "emit") if kit == "galileo" else sum(1 for l in lines if l.strip())
line = pathlib.Path(template).read_text().splitlines()[0]
sys.stdout.write((line + "\n") * n)
'''
CXX = r'''#!{python}
import os, stat, sys
out = sys.argv[sys.argv.index("-o") + 1]
open(out, "w").write(open(os.environ["REPLAY_STUB"]).read())
os.chmod(out, 0o755)
'''


def main():
    mode, kit, artifact = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3]).resolve()
    assert mode in ("record", "replay") and sys.argv[4] == "--", __doc__
    command = sys.argv[5:]
    work = pathlib.Path(tempfile.mkdtemp(prefix="replay-"))
    stub = work / "frames"
    stub.write_text(STUB.format(python=sys.executable))
    stub.chmod(stub.stat().st_mode | stat.S_IEXEC)
    (work / "bin").mkdir()
    cxx = work / "bin" / "c++"
    cxx.write_text(CXX.format(python=sys.executable))
    cxx.chmod(cxx.stat().st_mode | stat.S_IEXEC)
    env = dict(os.environ, PATH=f"{work / 'bin'}:{os.environ['PATH']}", REPLAY_MODE=mode, REPLAY_KIT=kit,
               REPLAY_STORE=str(artifact / "docs"), REPLAY_TEMPLATE=str(artifact / f"{kit}.jsonl"),
               REPLAY_REQUESTS=str(HERE / "docs"), REPLAY_STUB=str(stub))
    command = [c.replace("{frames}", str(stub)) for c in command]
    sys.exit(subprocess.run(command, env=env).returncode)


if __name__ == "__main__":
    main()
