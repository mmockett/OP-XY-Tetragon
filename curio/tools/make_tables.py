#!/usr/bin/env python3
"""Writes ../tables.h: the sine, 2^x and log2 tables the engine and the screen
share, and the thunder sheet's mode ratios. Every value is computed here from a
formula; nothing is sampled from another instrument.

    python3 tools/make_tables.py
"""
import math
import pathlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "tables.h"


def rows(values, per=12):
    out = []
    for i in range(0, len(values), per):
        out.append("    " + ", ".join(str(v) for v in values[i:i + per]) + ",")
    return "\n".join(out)


def main():
    sine = [round(32767 * math.sin(2 * math.pi * i / 4096)) for i in range(4096)]
    exp2 = [round((2 ** (i / 256)) * (1 << 30)) for i in range(257)]
    exp2[-1] = (1 << 31) - 1
    log2 = [round(math.log2(1 + i / 256) * 4096) for i in range(257)]
    # tanh over 0..4 in 256 steps, Q15 (a soft clip's curve).
    tanh = [round(32767 * math.tanh(4 * i / 256)) for i in range(257)]
    # The thunder sheet: twelve of the prototype's 44 modes, which were drawn
    # log-uniform over 1..70 times the fundamental (numpy default_rng(7)): its
    # close low cluster, which beats, then a spread to the top.
    sheet = [1.016, 1.051, 1.164, 1.205, 1.928, 2.265, 2.603, 3.117, 4.807, 8.722, 14.06, 29.009]
    ratios = [round(r * 4096) for r in sheet]
    # Each stands for its neighbours among the 44 (random phases add as
    # power): the square root of how many it stands for, Q12.
    stands = [2, 1, 1, 1, 2, 2, 3, 5, 4, 6, 5, 11]
    weights = [round(math.sqrt(n) * 4096) for n in stands]
    # Their starting phases, the prototype's (numpy default_rng(3)), as turns.
    phases = [367860372, 3441447623, 2500366906, 404279440, 1860266043, 686073414, 1680312283, 2520280674,
              2785489044, 1348559610, 130335110, 4000606794]
    text = f"""#pragma once
// Written by tools/make_tables.py: edit the generator, then run it.
#include <opxy/types.hpp>

namespace curio {{
namespace table {{
// sin(2 pi i / 4096), Q15.
const opxy::i16 sine[4096] = {{
{rows(sine, 16)}
}};
// 2^(i / 256), Q30, i = 0..256.
const opxy::u32 exp2[257] = {{
{rows(exp2, 8)}
}};
// log2(1 + i / 256), Q12, i = 0..256.
const opxy::u16 log2[257] = {{
{rows(log2, 16)}
}};
// tanh(4 i / 256), Q15, i = 0..256.
const opxy::u16 tanh[257] = {{
{rows(tanh, 16)}
}};
// The thunder sheet's twelve mode ratios over its fundamental, Q12, and
// each one's weight for the modes of the prototype's 44 it stands for, Q12.
const opxy::u32 sheet_ratio[12] = {{
{rows(ratios, 8)}
}};
const opxy::u16 sheet_weight[12] = {{
{rows(weights, 8)}
}};
const opxy::u32 sheet_phase[12] = {{
{rows([str(v) + "u" for v in phases], 8)}
}};
}}
}}
"""
    OUT.write_text(text)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
