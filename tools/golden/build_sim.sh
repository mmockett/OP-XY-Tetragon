#!/bin/bash
# Builds GDB's Blackfin simulator (bfin-elf-run) with the Blackfin+ multiply
# patch (sim_patch.py), for golden.py's counted runs. CI only (golden.yml).
#   build_sim.sh <gdb version> <out dir>
set -euo pipefail
version=$1
out=$(mkdir -p "$2" && cd "$2" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
cd "$work"
curl -sSfL "https://ftp.gnu.org/gnu/gdb/gdb-$version.tar.xz" | tar xJ
python3 "$here/sim_patch.py" "gdb-$version/sim/bfin/bfin-sim.c"
mkdir build && cd build
"../gdb-$version/configure" --target=bfin-elf --disable-gdb --disable-gdbserver --disable-binutils \
  --disable-ld --disable-gas --disable-gprof --disable-nls --disable-werror --disable-readline \
  --with-system-zlib --without-zstd --enable-sim > configure.log 2>&1 || { tail -40 configure.log; exit 1; }
jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
make -j"$jobs" all-sim MAKEINFO=true > make.log 2>&1 || { grep -n "error" make.log | head -40; tail -40 make.log; exit 1; }
run=$(find . -path '*bfin*' -name run -type f | head -1)
cp "$run" "$out/bfin-elf-run"
"$out/bfin-elf-run" --version | head -1
