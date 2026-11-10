#!/usr/bin/env python3
"""Golden renders, compare reports and counted cycles for the Tetragon kits.

    golden.py render   --kit DIR --sdk SDK --cxx CXX --out OUT    # scenario hashes (host build)
    golden.py identity BASE_OUT HEAD_OUT --kits "thunder curio"   # fails on any differing block
    golden.py hits     --kit DIR --sdk SDK --cxx CXX --out OUT    # single hits, raw and WAV
    golden.py report   BASE_OUT HEAD_OUT --kits "..." --md FILE   # loudness, peak, decay, centroid, bands
    golden.py bfin     --kit DIR --sdk SDK --tools BIN --out OUT [--run RUN]   # counted cycles (simulator)
    golden.py counts   BASE_OUT HEAD_OUT --kits "..." --md FILE   # the counted table, base against head

Host builds run only in CI (GitHub Actions). The Blackfin build cross-compiles
the same harness (golden.cpp) with the SDK's pinned bfin-elf tools and the
firmware's flags; `--run` executes it in the GDB Blackfin simulator, whose
cycle counter counts each instruction plus its modelled branch and multi-cycle
costs (no cache or memory stalls). 100% of a block is 580,352 cycles.
"""
import argparse, hashlib, json, math, os, pathlib, re, struct, subprocess, sys, tempfile, wave

HERE = pathlib.Path(__file__).resolve().parent
BLOCK = 580352
LEVELS = [64, 0, 127]
# The benchmark's heaviest model and level (index into LEVELS) per kit, from
# tetragon-capture-001, for the host scenarios' retrigger and ceiling runs.
# The counted build finds its own, as the benchmark does.
HEAVY = {"thunder": (5, 0), "curio": (5, 0), "galileo": (14, 2), "syahi": (5, 0)}
BFIN_FLAGS = ["-mcpu=bf707", "-O2", "-std=gnu++11", "-ffreestanding", "-fno-exceptions", "-fno-rtti",
              "-fno-threadsafe-statics", "-fno-use-cxa-atexit", "-fno-asynchronous-unwind-tables",
              "-fno-unwind-tables", "-fno-common", "-fno-tree-loop-distribute-patterns",
              "-fomit-frame-pointer", "-nostdinc++", "-Wall", "-Werror", "-Wno-unused-variable"]


def kit_info(kit):
    kit = pathlib.Path(kit)
    d = json.loads((kit / "definition.json").read_text())
    r = d["resources"]
    pads = r.get("padControls", [])
    name = d["id"]
    heavy = HEAVY.get(name, (0, 0))
    machine = json.loads((kit / "machine.json").read_text()) if (kit / "machine.json").exists() else {}
    sources = [str((kit / x).resolve()) for x in machine.get("sources", [])]
    return dict(sources=sources, id=name, models=r["modelCount"], capacity=r["voiceCapacity"], choke=int(bool(r.get("chokeSameKey"))),
                pads=[(p["bits"], bool(p.get("signed"))) for p in pads], heavy=heavy)


def defines(info):
    bits = ",".join(str(b) for b, _ in info["pads"]) or "0"
    signs = sum(1 << i for i, (_, s) in enumerate(info["pads"]) if s)
    return [f"-DGOLDEN_NS={info['id']}", f"-DGOLDEN_MODELS={info['models']}", f"-DGOLDEN_CAPACITY={info['capacity']}",
            f"-DGOLDEN_CHOKE={info['choke']}", f"-DGOLDEN_PAD_COUNT={len(info['pads'])}",
            f"-DGOLDEN_PAD_BITS={{{bits}}}", f"-DGOLDEN_PAD_SIGNS={signs}",
            f"-DGOLDEN_HEAVY_MODEL={info['heavy'][0]}", f"-DGOLDEN_HEAVY_LEVEL={info['heavy'][1]}"]


def build_host(kit, sdk, cxx, out):
    info = kit_info(kit)
    out.mkdir(parents=True, exist_ok=True)
    binary = out / "golden"
    cmd = [cxx, "-std=c++11", "-O2", "-w", *defines(info), f"-I{pathlib.Path(kit).resolve()}",
           f"-I{pathlib.Path(sdk).resolve() / 'sdk' / 'include'}", str(HERE / "golden.cpp"), *info["sources"], "-o", str(binary)]
    subprocess.run(cmd, check=True)
    return info, binary


def cmd_render(a):
    out = pathlib.Path(a.out)
    info, binary = build_host(a.kit, a.sdk, a.cxx, out)
    with open(out / "scenes.txt", "w") as f:
        subprocess.run([str(binary), "scenes", str(out / "hashes.bin")], check=True, stdout=f)
    lines = (out / "scenes.txt").read_text().split("\n")
    print(f"{info['id']}: {sum(1 for l in lines if l.startswith('scene '))} scenarios")


def load_scenes(folder):
    scenes, offset = [], 0
    for line in (folder / "scenes.txt").read_text().splitlines():
        if not line.startswith("scene "):
            continue
        _, name, blocks, total = line.split()
        scenes.append((name, int(blocks), total, offset))
        offset += int(blocks)
    return scenes


def cmd_identity(a):
    failed = False
    for kit in a.kits.split():
        b, h = pathlib.Path(a.base) / kit, pathlib.Path(a.head) / kit
        bs, hs = load_scenes(b), load_scenes(h)
        bh, hh = (b / "hashes.bin").read_bytes(), (h / "hashes.bin").read_bytes()
        bmap = {s[0]: s for s in bs}
        diffs, missing = [], []
        for name, blocks, total, off in hs:
            if name not in bmap:
                missing.append(name)
                continue
            _, bblocks, btotal, boff = bmap[name]
            if btotal == total and bblocks == blocks:
                continue
            first = None
            for i in range(min(blocks, bblocks)):
                if bh[(boff + i) * 8:(boff + i + 1) * 8] != hh[(off + i) * 8:(off + i + 1) * 8]:
                    first = i
                    break
            diffs.append((name, first if first is not None else min(blocks, bblocks)))
        gone = [s[0] for s in bs if s[0] not in {x[0] for x in hs}]
        if diffs or missing or gone:
            failed = True
            print(f"FAIL {kit}: {len(diffs)} of {len(hs)} scenarios differ from the base")
            for name, first in diffs[:40]:
                print(f"  {kit} {name}: first differing block {first}")
            if len(diffs) > 40:
                print(f"  ... and {len(diffs) - 40} more")
            for name in missing[:10]:
                print(f"  {kit} {name}: not in the base")
            for name in gone[:10]:
                print(f"  {kit} {name}: not in the head")
        else:
            print(f"ok   {kit}: all {len(hs)} scenarios bit-identical")
    sys.exit(1 if failed else 0)


# ---------------------------------------------------------------- presets and hits

def preset_regions(kit):
    """Every preset's keys, decoded from the kit's own preset writer (pure Python):
    [(preset, key, model, controls[8], pads[2])]."""
    kit = pathlib.Path(kit).resolve()
    info = kit_info(kit)
    tool = kit / "tools" / "presets.py"
    if not tool.exists():
        return []
    work = pathlib.Path(tempfile.mkdtemp(prefix=f"golden-{info['id']}-"))
    for args in (["--out", str(work)], [str(work)]):
        r = subprocess.run([sys.executable, str(tool), *args], cwd=kit, capture_output=True, text=True)
        if r.returncode == 0 and list(work.rglob("*.json")):
            break
    out, seen = [], set()
    for path in sorted(work.rglob("*.json")):
        try:
            doc = json.loads(path.read_text())
        except ValueError:
            continue
        if "regions" not in doc or path.stem == "default":
            continue
        for reg in doc["regions"]:
            p = reg.get("params", [0, 0, 0, 0])
            value = sum((v & 0x7fff) << (15 * w) for w, v in enumerate(p))
            controls = [(value >> (7 * i)) & 127 for i in range(8)]
            model = (value >> 56) & 15
            word = (reg.get("random", 0) << 16)
            pads, low = [], 16
            for bits, signed in info["pads"]:
                raw = (word >> low) & ((1 << bits) - 1)
                pads.append(raw - (1 << bits) if signed and raw >> (bits - 1) else raw)
                low += bits
            pads += [0] * (2 - len(pads))
            key = reg.get("hikey", 0)
            sig = (model, tuple(controls), tuple(pads))
            out.append((path.stem, key, model, controls, pads[:2], sig in seen))
            seen.add(sig)
    return out


def hit_jobs(kit, seconds):
    info = kit_info(kit)
    blocks = int(seconds * 44100 / 64)
    jobs = []
    for m in range(info["models"]):
        for level in (0, 64, 127):
            jobs.append((f"model-m{m:02d}-l{level:03d}", m, [level] * 8, [0, 0], blocks))
    for preset, key, model, controls, pads, dup in preset_regions(kit):
        jobs.append((f"preset-{preset}-k{key}", model, controls, pads, blocks))
    return info, jobs


def write_wav(path, chunks, seconds):
    """Hits in sequence, each in a window of `seconds`, as 16-bit PCM at 44.1 kHz."""
    import numpy as np
    n = int(seconds * 44100)
    parts = []
    for x in chunks:
        y = np.zeros(n, dtype=np.int64)
        y[:min(n, len(x))] = x[:n]
        parts.append(y)
    pcm = np.clip(np.round(np.concatenate(parts) / 65536.0), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(pcm.tobytes())


def cmd_hits(a):
    import numpy as np
    out = pathlib.Path(a.out)
    raw = out / "raw"
    raw.mkdir(parents=True, exist_ok=True)
    info, binary = build_host(a.kit, a.sdk, a.cxx, out)
    _, jobs = hit_jobs(a.kit, a.seconds)
    text = "".join(f"{raw / name}.raw {m} {' '.join(map(str, c))} {p[0]} {p[1]} {n}\n" for name, m, c, p, n in jobs)
    subprocess.run([str(binary), "hits"], input=text, text=True, check=True, capture_output=True)
    (out / "hits.json").write_text(json.dumps([dict(name=n, model=m, controls=c, pads=p) for n, m, c, p, _ in jobs]))
    wavs = out / "wav"
    wavs.mkdir(exist_ok=True)
    groups = {}
    for name, *_ in jobs:
        group = name.rsplit("-k", 1)[0] if name.startswith("preset-") else name.rsplit("-l", 1)[0]
        groups.setdefault(group, []).append(name)
    for group, names in groups.items():
        chunks = [np.fromfile(raw / f"{n}.raw", dtype="<i4").astype(np.int64) for n in names]
        write_wav(wavs / f"{group}.wav", chunks, 1.0 if group.startswith("preset-") else 2.0)
    print(f"{info['id']}: {len(jobs)} hits, {len(groups)} WAVs")


# ---------------------------------------------------------------- measures (pure Python + numpy)

def biquad_response(b, a, w):
    import numpy as np
    z = np.exp(-1j * w)
    return (b[0] + b[1] * z + b[2] * z * z) / (a[0] + a[1] * z + a[2] * z * z)


def k_weight(x, fs=44100.0):
    """BS.1770 K-weighting (its shelf and high-pass, designed at fs), applied by FFT."""
    import numpy as np
    def shelf():
        f0, g, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
        k = math.tan(math.pi * f0 / fs)
        vh = 10 ** (g / 20)
        vb = vh ** 0.4996667741545416
        a0 = 1 + k / q + k * k
        return [(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0], \
               [1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    def highpass():
        f0, q = 38.13547087602444, 0.5003270373238773
        k = math.tan(math.pi * f0 / fs)
        a0 = 1 + k / q + k * k
        return [1, -2, 1], [1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    n = len(x) + 32768
    n = 1 << (n - 1).bit_length()
    w = np.linspace(0, math.pi, n // 2 + 1)
    h = biquad_response(*shelf(), w) * biquad_response(*highpass(), w)
    return np.fft.irfft(np.fft.rfft(x, n) * h, n)[:len(x)]


def measures(x):
    """x: float samples (1.0 = full scale). Loudness (LUFS, gated as BS.1770 over
    400 ms blocks, or the whole hit when shorter), peak dBFS, time from the peak
    until the 10 ms envelope stays under peak - 40 dB, spectral centroid, and
    octave-band energies (dB of the total)."""
    import numpy as np
    fs = 44100
    if len(x) == 0 or not np.any(x):
        return dict(lufs=None, peak=None, decay_ms=0, centroid=None, bands=[])
    peak = float(np.max(np.abs(x)))
    y = k_weight(x)
    blk, hop = int(0.4 * fs), int(0.1 * fs)
    if len(y) >= blk:
        ms = np.array([np.mean(y[i:i + blk] ** 2) for i in range(0, len(y) - blk + 1, hop)])
    else:
        ms = np.array([np.mean(np.concatenate([y, np.zeros(blk - len(y))]) ** 2)])
    lk = -0.691 + 10 * np.log10(np.maximum(ms, 1e-20))
    g = ms[lk > -70]
    lufs = None
    if len(g):
        rel = -0.691 + 10 * math.log10(np.mean(g)) - 10
        g2 = ms[(lk > -70) & (lk > rel)]
        lufs = -0.691 + 10 * math.log10(np.mean(g2)) if len(g2) else None
    win = int(0.01 * fs)
    env = np.array([np.max(np.abs(x[i:i + win])) for i in range(0, len(x), win)])
    ip = int(np.argmax(env))
    above = np.nonzero(env >= peak * 10 ** (-40 / 20))[0]
    decay = (int(above[-1]) - ip + 1) * 10 if len(above) else 0
    spec = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / fs)
    total = float(np.sum(spec)) or 1e-30
    centroid = float(np.sum(f * spec) / total)
    bands = []
    for c in [31.25 * 2 ** i for i in range(10)]:
        sel = (f >= c / math.sqrt(2)) & (f < c * math.sqrt(2))
        bands.append(10 * math.log10(max(float(np.sum(spec[sel])) / total, 1e-12)))
    return dict(lufs=lufs, peak=20 * math.log10(peak), decay_ms=decay, centroid=centroid, bands=bands)


def fmt(v, d=1):
    return "—" if v is None else f"{v:.{d}f}"


def cmd_report(a):
    import numpy as np
    lines = ["# Compare report: base against head", "",
             "Per hit: integrated loudness (LUFS), peak (dBFS), decay to −40 dB (ms), spectral centroid (Hz), "
             "and the largest octave-band energy difference (dB, bands within 40 dB of the hit's strongest). "
             "Flags: `L` loudness moved more than 1.5 LU, `P` the head peaks above −6.0 dBFS (the kits' declared "
             "peak) and above the base, `B` a band moved more than 6 dB, `=` bit-identical.", ""]
    summary = {}
    for kit in a.kits.split():
        b, h = pathlib.Path(a.base) / kit, pathlib.Path(a.head) / kit
        if not (h / "hits.json").exists():
            continue
        hits = json.loads((h / "hits.json").read_text())
        lines += [f"## {kit}", "", "| Hit | LUFS base → head | Peak | Decay ms | Centroid Hz | Band Δ | Flags |",
                  "|---|---|---|---|---|---:|---|"]
        rows = []
        for hit in hits:
            name = hit["name"]
            hb, hh = b / "raw" / f"{name}.raw", h / "raw" / f"{name}.raw"
            if not hb.exists() or not hh.exists():
                continue
            xb, xh = hb.read_bytes(), hh.read_bytes()
            same = xb == xh
            mb = measures(np.frombuffer(xb, dtype="<i4") / 2147483648.0)
            mh = mb if same else measures(np.frombuffer(xh, dtype="<i4") / 2147483648.0)
            flags = "=" if same else ""
            dband = 0.0
            if mb["bands"] and mh["bands"]:
                keep = [i for i in range(len(mb["bands"])) if max(mb["bands"][i], mh["bands"][i]) > max(mb["bands"]) - 40]
                dband = max((abs(mb["bands"][i] - mh["bands"][i]) for i in keep), default=0.0)
            if mb["lufs"] is not None and mh["lufs"] is not None and abs(mh["lufs"] - mb["lufs"]) > 1.5:
                flags += "L"
            if mh["peak"] is not None and mh["peak"] > -6.0 and (mb["peak"] is None or mh["peak"] > mb["peak"] + 0.05):
                flags += "P"
            if dband > 6:
                flags += "B"
            rows.append((name, mb, mh, dband, flags))
            lines.append(f"| {name} | {fmt(mb['lufs'])} → {fmt(mh['lufs'])} | {fmt(mb['peak'])} → {fmt(mh['peak'])} | "
                         f"{mb['decay_ms']} → {mh['decay_ms']} | {fmt(mb['centroid'], 0)} → {fmt(mh['centroid'], 0)} | "
                         f"{dband:.1f} | {flags} |")
        lines.append("")
        summary[kit] = [dict(name=n, base=mb, head=mh, band_delta=d, flags=f) for n, mb, mh, d, f in rows]
    pathlib.Path(a.md).write_text("\n".join(lines) + "\n")
    if a.json:
        pathlib.Path(a.json).write_text(json.dumps(summary, indent=1))
    print("\n".join(lines))


# ---------------------------------------------------------------- the counted build (Blackfin)

def cmd_bfin(a):
    kit, sdk, tools, out = pathlib.Path(a.kit).resolve(), pathlib.Path(a.sdk).resolve(), pathlib.Path(a.tools), pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    info = kit_info(kit)
    inc = [f"-I{sdk / 'sdk' / 'include'}", f"-I{sdk / 'sdk' / 'platform' / 'blackfin' / 'freestanding'}", f"-I{kit}"]
    cxx, asm, ld = tools / "bfin-elf-g++", tools / "bfin-elf-as", tools / "bfin-elf-ld"
    d = defines(info) + [f"-DGOLDEN_BENCH_BLOCKS={a.blocks}", "-DGOLDEN_BFIN"]
    subprocess.run([str(cxx), *BFIN_FLAGS, *d, *inc, "-S", str(HERE / "golden.cpp"), "-o", str(out / "golden.s")], check=True)
    subprocess.run([str(cxx), *BFIN_FLAGS, *d, *inc, "-g", "-S", str(HERE / "golden.cpp"), "-o", str(out / "golden-g.s")], check=True)
    subprocess.run([str(cxx), *BFIN_FLAGS, "-S", str(sdk / "sdk/platform/blackfin/memory.cpp"), "-o", str(out / "memory.s")], check=True)
    units = ["golden.s", "memory.s"]
    for i, src in enumerate(info["sources"]):
        subprocess.run([str(cxx), *BFIN_FLAGS, *inc, "-S", src, "-o", str(out / f"extra{i}.s")], check=True)
        units.append(f"extra{i}.s")
    objs = []
    for s in units:
        subprocess.run([str(asm), "-mcpu=bf707", "-o", str(out / (s[:-2] + ".o")), str(out / s)], check=True)
        objs.append(str(out / (s[:-2] + ".o")))
    for s, o in ((sdk / "sdk/platform/blackfin/divide.s", "divide.o"), (HERE / "bfin_start.s", "start.o")):
        subprocess.run([str(asm), "-mcpu=bf707", "-o", str(out / o), str(s)], check=True)
        objs.append(str(out / o))
    subprocess.run([str(ld), "-e", "start", "-Ttext=0x10000", "-o", str(out / "golden.elf"), *objs], check=True)
    # Short builds (200 blocks) for the simulator's PC profile: the whole
    # benchmark, then the kit, the heaviest voice and that voice x capacity alone.
    for name, only in (("profile", None), ("profile-kit", 1), ("profile-voice", 2), ("profile-ceiling", 3)):
        dp = defines(info) + ["-DGOLDEN_BENCH_BLOCKS=200", "-DGOLDEN_BFIN"] + ([f"-DGOLDEN_PROFILE={only}"] if only else [])
        subprocess.run([str(cxx), *BFIN_FLAGS, *dp, *inc, "-S", str(HERE / "golden.cpp"), "-o", str(out / f"{name}.s")], check=True)
        subprocess.run([str(asm), "-mcpu=bf707", "-o", str(out / f"{name}.o"), str(out / f"{name}.s")], check=True)
        subprocess.run([str(ld), "-e", "start", "-Ttext=0x10000", "-o", str(out / f"{name}.elf"),
                        str(out / f"{name}.o"), *objs[1:]], check=True)
    census = subprocess.run([sys.executable, str(HERE / "census.py"), str(out / "golden-g.s")], capture_output=True, text=True)
    (out / "census.txt").write_text(census.stdout + census.stderr)
    print(f"{info['id']}: built {out / 'golden.elf'}")
    if a.run:
        simulate(out, a.run, a.timeout)


def simulate(out, run, timeout):
    out = pathlib.Path(out)
    r = subprocess.run([run, str(out / "golden.elf")], capture_output=True, text=True, timeout=timeout)
    (out / "cycles.txt").write_text(r.stdout + r.stderr)
    print(r.stdout[-3000:], r.stderr[-2000:])
    if "done" not in r.stdout:
        sys.exit(f"{out}: the simulator run did not finish")


def cmd_sim(a):
    simulate(a.out, a.run, a.timeout)
    if a.profile:
        profile(pathlib.Path(a.out), a.run, a.timeout)


def elf_functions(path):
    """(start, size, name) of every function symbol in a little-endian ELF32."""
    data = pathlib.Path(path).read_bytes()
    shoff, = struct.unpack_from("<I", data, 32)
    shentsize, shnum = struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for sh in sections:
        if sh[1] != 2:   # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]
        for off in range(sh[4], sh[4] + sh[5], 16):
            name, value, size, info, _, _ = struct.unpack_from("<IIIBBH", data, off)
            if info & 15 != 2 or not size:   # STT_FUNC
                continue
            end = data.index(b"\0", strtab[4] + name)
            out.append((value, size, data[strtab[4] + name:end].decode()))
    return sorted(out)


def profile(out, run, timeout):
    """The simulator's PC samples, summed per function, for each profiling build.
    For the single runs each function's cost is also given in cycles a sample
    (a voice-sample for the kit and the ceiling)."""
    sections = []
    for name, voices in (("profile", 0), ("profile-kit", 16), ("profile-voice", 1), ("profile-ceiling", 16)):
        elf = out / f"{name}.elf"
        if not elf.exists():
            continue
        work = pathlib.Path(tempfile.mkdtemp())
        freq = 1009 if name == "profile" else 101
        # --verbose makes the run print its statistics at exit, which writes gmon.out.
        r = subprocess.run([str(pathlib.Path(run).resolve()), "--verbose", "--profile-pc", f"--profile-pc-frequency={freq}",
                            "--profile-pc-granularity=4", str(elf.resolve())],
                           cwd=work, capture_output=True, text=True, timeout=timeout)
        if not (work / "gmon.out").exists():
            sections.append(f"## {name}: no profile: " + (r.stdout + r.stderr)[-500:])
            continue
        raw = (work / "gmon.out").read_bytes()
        low, high, size = struct.unpack_from("<III", raw, 0)
        counts = struct.unpack_from("<%dH" % ((len(raw) - 12) // 2), raw, 12)
        step = (high - low) / max(1, len(counts))
        funcs = elf_functions(elf)
        totals, total = {}, 0
        for i, c in enumerate(counts):
            if not c:
                continue
            pc = low + int(i * step)
            total += c
            name_f = next((n for s, z, n in funcs if s <= pc < s + z), "?")
            totals[name_f] = totals.get(name_f, 0) + c
        per = freq / (200 * 64 * voices) if voices else 0
        lines = [f"## {name}: {total} PC samples, every {freq} cycles, 200 blocks"
                 + (f"; cycles a {'voice-' if voices > 1 else ''}sample = samples x {per:.4f}" if voices else "")]
        for f, c in sorted(totals.items(), key=lambda x: -x[1])[:40]:
            lines.append(f"{100.0 * c / max(1, total):6.2f}%" + (f" {c * per:7.1f} cyc" if voices else "") + f"  {f}")
        sections.append("\n".join(lines))
    (out / "profile.txt").write_text("\n\n".join(sections) + "\n")
    print("\n\n".join(s[:1500] for s in sections))


def load_cycles(folder):
    rows = {}
    p = folder / "cycles.txt"
    if not p.exists():
        return rows
    for line in p.read_text().splitlines():
        parts = line.split()
        if len(parts) == 6 and parts[0] == "cycles":
            _, kind, m, level, mean, most = parts
            key = kind if kind != "voice" else f"voice-m{m}-l{level}"
            rows[key] = dict(kind=kind, model=m, level=level, mean=int(mean), most=int(most))
    return rows


def census_summary(folder):
    """Hardware loops, software loops and loop instructions, from census.txt."""
    p = folder / "census.txt"
    hw = sw = n_hw = n_sw = 0
    if p.exists():
        for line in p.read_text().splitlines():
            m = re.search(r"(HWLOOP|BACKJ)\s+n=\s*(\d+)", line)
            if m:
                if m.group(1) == "HWLOOP":
                    hw += 1
                    n_hw += int(m.group(2))
                else:
                    sw += 1
                    n_sw += int(m.group(2))
    return dict(hw=hw, sw=sw, hw_insns=n_hw, sw_insns=n_sw)


def pct(c):
    return 100.0 * c / BLOCK


def cmd_counts(a):
    lines = ["# Counted cycles: base against head", "",
             "From the GDB Blackfin simulator running the benchmark's schedule (golden.cpp, the firmware's flags): "
             "each instruction plus modelled branch and multi-cycle costs, no cache or memory stalls. % of the "
             "580,352-cycle block, mean / worst block. The device has measured 1.25–1.9 times these.", ""]
    regress = False
    for kit in a.kits.split():
        b, h = pathlib.Path(a.base) / kit, pathlib.Path(a.head) / kit
        cb, ch = load_cycles(b), load_cycles(h)
        sb, sh = census_summary(b), census_summary(h)
        lines += [f"## {kit}", "",
                  f"Census: hardware loops {sb['hw']} → {sh['hw']} ({sb['hw_insns']} → {sh['hw_insns']} instructions), "
                  f"software loops {sb['sw']} → {sh['sw']} ({sb['sw_insns']} → {sh['sw_insns']}).", "",
                  "| Run | Base mean / worst % | Head mean / worst % | Δ mean |", "|---|---|---|---:|"]
        keys = list(ch) or list(cb)
        heavy_b = max((v["mean"] for k, v in cb.items() if v["kind"] == "voice"), default=0)
        heavy_h = max((v["mean"] for k, v in ch.items() if v["kind"] == "voice"), default=0)
        for k in keys:
            x, y = cb.get(k), ch.get(k)
            label = k if not y or y["kind"] not in ("choke", "ceiling") else f"{k} (m{y['model']} @{y['level']})"
            bx = f"{pct(x['mean']):.2f} / {pct(x['most']):.2f}" if x else "—"
            hy = f"{pct(y['mean']):.2f} / {pct(y['most']):.2f}" if y else "—"
            dm = f"{pct(y['mean']) - pct(x['mean']):+.2f}" if x and y else ""
            lines.append(f"| {label} | {bx} | {hy} | {dm} |")
        lines += ["", f"Heaviest single voice: {pct(heavy_b):.2f}% → {pct(heavy_h):.2f}% (mean).", ""]
        # A refactor must not slow any run down. The whole-program census moves
        # with inlining, so it is reported, not judged.
        slower = [k for k in keys if cb.get(k) and ch.get(k) and pct(ch[k]["mean"]) - pct(cb[k]["mean"]) > 0.02]
        if a.strict and slower:
            regress = True
            lines.append(f"**FAIL {kit}: a refactor made {', '.join(slower)} slower.**\n")
        if sh["hw"] < sb["hw"]:
            lines.append(f"Note: {sb['hw'] - sh['hw']} fewer hardware loops in the whole program than the base.\n")
    pathlib.Path(a.md).write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    sys.exit(1 if regress else 0)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    s = p.add_subparsers(dest="cmd", required=True)
    for name in ("render", "hits"):
        q = s.add_parser(name)
        q.add_argument("--kit", required=True)
        q.add_argument("--sdk", required=True)
        q.add_argument("--cxx", default="g++")
        q.add_argument("--out", required=True)
        q.add_argument("--seconds", type=float, default=3.0)
    for name in ("identity", "report", "counts"):
        q = s.add_parser(name)
        q.add_argument("base")
        q.add_argument("head")
        q.add_argument("--kits", default="thunder curio galileo syahi")
        q.add_argument("--md", default="report.md")
        q.add_argument("--json")
        q.add_argument("--strict", action="store_true")
    q = s.add_parser("bfin")
    q.add_argument("--kit", required=True)
    q.add_argument("--sdk", required=True)
    q.add_argument("--tools", required=True)
    q.add_argument("--out", required=True)
    q.add_argument("--run")
    q.add_argument("--blocks", type=int, default=1500)
    q.add_argument("--timeout", type=int, default=3000)
    q = s.add_parser("sim")
    q.add_argument("--profile", action="store_true")
    q.add_argument("--out", required=True)
    q.add_argument("--run", required=True)
    q.add_argument("--timeout", type=int, default=3000)
    a = p.parse_args()
    dict(sim=cmd_sim, render=cmd_render, identity=cmd_identity, hits=cmd_hits, report=cmd_report, bfin=cmd_bfin, counts=cmd_counts)[a.cmd](a)


if __name__ == "__main__":
    main()
