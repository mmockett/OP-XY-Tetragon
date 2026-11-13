# Usage: python3 census.py <voice-g.s> [function substring]
"""Static loop census of a GCC Blackfin .s file built with -g (pure text parsing)."""
import re, sys, os, collections
path = sys.argv[1]
want = sys.argv[2] if len(sys.argv) > 2 else None
files = {}
items = []   # (kind, value, func, loc)
func = None; loc = None
for raw in open(path, encoding='utf-8', errors='replace'):
    line = raw.strip()
    if not line: continue
    m = re.match(r'\.file (\d+) "(.*)"', line)
    if m: files[m.group(1)] = os.path.basename(m.group(2)); continue
    m = re.match(r'\.loc (\d+) (\d+)', line)
    if m: loc = (files.get(m.group(1), '?'), int(m.group(2))); continue
    if line.startswith('.'):
        m = re.match(r'(\.L[\w$.]+):$', line)
        if m: items.append(('label', m.group(1), func, loc))
        continue
    m = re.match(r'([\w$.]+):$', line)
    if m: func = m.group(1); items.append(('func', func, func, loc)); continue
    if line.startswith('/*') or line.startswith('//'): continue
    if ';' in line:
        items.append(('insn', line, func, loc))
labels = {v: i for i, (k, v, f, l) in enumerate(items) if k == 'label'}
def body(a, b):
    """Instructions from label a to the instruction after label b."""
    i, j = labels[a], labels[b]
    out = [it for it in items[i:j] if it[0] == 'insn']
    for it in items[j:]:
        if it[0] == 'insn': out.append(it); break
    return out
def describe(ins):
    lines = collections.Counter((l[0], l[1]) for k, v, f, l in ins if l)
    top = sorted(lines.items(), key=lambda x: -x[1])[:6]
    calls = sorted(set(re.sub(r'.*call\s+', '', v).rstrip(';') for k, v, f, l in ins if 'call' in v))
    br = sum(1 for k, v, f, l in ins if re.search(r'\bjump', v))
    loads = sum(1 for k, v, f, l in ins if re.search(r'=\s*[BW]?\s*\[', v) or re.search(r'= W\[|= B\[|= \[', v))
    return top, calls, br, loads
for idx, (k, v, f, l) in enumerate(items):
    if k == 'insn' and v.startswith('LSETUP'):
        if want and want not in (f or ''): continue
        m = re.match(r'LSETUP \(([^,]+), ([^)]+)\)', v)
        ins = body(m.group(1).strip(), m.group(2).strip())
        top, calls, br, loads = describe(ins)
        srcs = ' '.join(f'{a}:{b}x{c}' for (a, b), c in top)
        print(f'{f[:40]:40s} HWLOOP n={len(ins):4d} br={br:2d} ld={loads:3d} calls={calls} | {srcs}')
# backward branches (software loops)
for idx, (k, v, f, l) in enumerate(items):
    if k == 'insn':
        m = re.search(r'jump(?:\.[sl])?\s+(\.L[\w$.]+)', v)
        if m and m.group(1) in labels and labels[m.group(1)] < idx:
            if want and want not in (f or ''): continue
            seg = [it for it in items[labels[m.group(1)]:idx+1] if it[0] == 'insn']
            if len(seg) > 400: continue
            top, calls, br, loads = describe(seg)
            srcs = ' '.join(f'{a}:{b}x{c}' for (a, b), c in top)
            print(f'{f[:40]:40s} BACKJ  n={len(seg):4d} br={br:2d} ld={loads:3d} calls={calls} | {srcs}')
