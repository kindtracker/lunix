#!/bin/bash
set -e

CC="cc"
CFlags="-O0"
LDFlags=""
Libraries="-lunicorn"
OUT="build/lunix"

Jobs="$(nproc)"

mkdir -p build
rm -rf build/*
mkdir -p build/compile/lunix

CompileLunix() {
  File=$1
  Object="build/compile/lunix/$(echo $File | sed 's#/#_#g; s#\.c$#.o#')"

  echo "  CC  $File"
  $CC $CFlags -c "$File" \
    -Ivendors/lua \
    -o $Object
}

export -f CompileLunix
export CFlags
export CC

find src -name "*.c" |
  xargs -P "$Jobs" -n 1 bash -c 'CompileLunix "$1"' _

echo "  LD  $OUT"
$CC $CFlags $LDFlags $Libraries \
  build/compile/*/*.o \
  -o $OUT
