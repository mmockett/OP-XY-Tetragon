#!/usr/bin/env python3
"""Measures header text in the OP-XY's own font, Univers Next Pro Light, at the
header's sizes, and flags any that would run into the next column.

    python3 tools/caption_width.py 5.5mm 600um M1.98 ...   # values at 20 px
    python3 tools/caption_width.py --title liquid ...      # titles at 10 px
    python3 tools/caption_width.py --file captions.txt     # one caption a line;
                                                           # "title: x" lines are titles

XY_FONT names the font file. width(text, px) measures any other screen text,
such as a hint, at its own size.

The header (SDK docs/ux.md): titles at x 5 + 120 i (10 px), values at
x 65 + 120 i (20 px on the edited page). The next column's title starts at
x 125 + 120 i, and the last column ends at the screen's edge, 480. So a value
has 55 px (the last column's room) and a title 58 px. The limits leave a small
gap so text never touches its neighbour. Widths are the font's advance widths
(no kerning), which is what the device's text renderer lays out.

Exit status 1 when anything is too wide.
"""
import os
import pathlib
import struct
import sys

# The OP-XY's screen font, Univers Next Pro Light; it is never committed here.
_font = os.environ.get("XY_FONT")
if not _font or not pathlib.Path(_font).is_file():
    sys.exit("caption_width.py: set XY_FONT to the path of UniversNextPro-Light.ttf (the OP-XY's screen font)")
FONT = pathlib.Path(_font)
VALUE_PX, VALUE_ROOM = 20, 53     # 55 px of room, 2 px kept clear
TITLE_PX, TITLE_ROOM = 10, 56     # 58 px of room, 2 px kept clear


def font_metrics(path):
    data = path.read_bytes()
    count = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for i in range(count):
        tag, _, offset, length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        tables[tag.decode()] = (offset, length)
    head = tables["head"][0]
    units = struct.unpack(">H", data[head + 18:head + 20])[0]
    hhea = tables["hhea"][0]
    long_metrics = struct.unpack(">H", data[hhea + 34:hhea + 36])[0]
    hmtx = tables["hmtx"][0]
    advances = [struct.unpack(">H", data[hmtx + 4 * i:hmtx + 4 * i + 2])[0] for i in range(long_metrics)]
    # cmap: the Unicode BMP subtable, format 4.
    cmap = tables["cmap"][0]
    glyph_of = {}
    subtables = struct.unpack(">H", data[cmap + 2:cmap + 4])[0]
    for i in range(subtables):
        platform, encoding, offset = struct.unpack(">HHI", data[cmap + 4 + 8 * i:cmap + 12 + 8 * i])
        at = cmap + offset
        if struct.unpack(">H", data[at:at + 2])[0] != 4 or (platform, encoding) not in ((3, 1), (0, 3), (0, 4)):
            continue
        segments = struct.unpack(">H", data[at + 6:at + 8])[0] // 2
        ends = struct.unpack(f">{segments}H", data[at + 14:at + 14 + 2 * segments])
        starts_at = at + 16 + 2 * segments
        starts = struct.unpack(f">{segments}H", data[starts_at:starts_at + 2 * segments])
        deltas = struct.unpack(f">{segments}h", data[starts_at + 2 * segments:starts_at + 4 * segments])
        ranges_at = starts_at + 4 * segments
        ranges = struct.unpack(f">{segments}H", data[ranges_at:ranges_at + 2 * segments])
        for s in range(segments):
            for code in range(starts[s], ends[s] + 1):
                if code == 0xFFFF:
                    continue
                if ranges[s] == 0:
                    glyph = (code + deltas[s]) & 0xFFFF
                else:
                    where = ranges_at + 2 * s + ranges[s] + 2 * (code - starts[s])
                    glyph = struct.unpack(">H", data[where:where + 2])[0]
                    if glyph:
                        glyph = (glyph + deltas[s]) & 0xFFFF
                glyph_of[code] = glyph
        break
    return units, advances, glyph_of


UNITS, ADVANCES, GLYPHS = font_metrics(FONT)


def width(text, px):
    total = 0
    for ch in text:
        glyph = GLYPHS.get(ord(ch), 0)
        total += ADVANCES[min(glyph, len(ADVANCES) - 1)]
    return total * px / UNITS


def main(argv):
    titles, values = [], []
    if argv[:1] == ["--file"]:
        for line in pathlib.Path(argv[1]).read_text().splitlines():
            line = line.rstrip("\n")
            if line.startswith("title: "):
                titles.append(line[7:])
            elif line:
                values.append(line)
    elif argv[:1] == ["--title"]:
        titles = argv[1:]
    else:
        values = argv
    failed = False
    for kind, items, px, room in (("value", values, VALUE_PX, VALUE_ROOM), ("title", titles, TITLE_PX, TITLE_ROOM)):
        for text in dict.fromkeys(items):
            w = width(text, px)
            ok = w <= room
            failed |= not ok
            print(f"{'ok  ' if ok else 'WIDE'} {kind:5} {w:5.1f}/{room} px  {text!r}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
