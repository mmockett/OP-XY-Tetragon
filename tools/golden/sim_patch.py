#!/usr/bin/env python3
"""Teach GDB's Blackfin simulator the one Blackfin+ (BF70x) instruction the kits
compile to that the classic core lacks: the 32 x 32 fractional multiply
`Rd = Rs * Rt (T)` (opxy::q31::multiply), encoded 0xc231 with op1 = 1. It
truncates (floors) the 63-bit product shifted by 31 and saturates MIN * MIN,
as the SDK's portable multiply does, in one cycle.

    sim_patch.py <gdb source>/sim/bfin/bfin-sim.c
"""
import sys

path = sys.argv[1]
src = open(path).read()
anchor = """		 dst, src0, src1);

  if (w1 == 0 && w0 == 0)
    illegal_instruction (cpu);
  if (((1 << mmod) & (P ? 0x313 : 0x1b57)) == 0)"""
patch = """		 dst, src0, src1);

  /* Blackfin+: Rd = Rs * Rt (T), 32 x 32 fractional, truncating, saturating.  */
  if (M == 0 && P == 0 && w1 == 0 && w0 == 1 && op1 == 1 && op0 == 0 && MM == 1
      && mmod == 1 && !h01 && !h11 && !h00 && !h10)
    {
      bs64 prod = (bs64)(bs32)DREG (src0) * (bs64)(bs32)DREG (src1);
      bs64 q = prod >> 31;
      bu32 r = q > 0x7fffffffLL ? 0x7fffffffu : (bu32)q;
      TRACE_INSN (cpu, "R%i = R%i * R%i (T);", dst, src0, src1);
      STORE (DREG (dst), r);
      return;
    }

  if (w1 == 0 && w0 == 0)
    illegal_instruction (cpu);
  if (((1 << mmod) & (P ? 0x313 : 0x1b57)) == 0)"""
if patch in src:
    print("already patched")
    sys.exit(0)
if src.count(anchor) != 1:
    sys.exit("sim_patch: the dsp32mult decoder's checks were not found")
open(path, "w").write(src.replace(anchor, patch))
print("patched", path)
