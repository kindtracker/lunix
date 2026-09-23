#!/bin/bash
set -e

CC="cc"
CC2="aarch64-linux-gnu-gcc"
CFlags="-O0"
LDFlags=""
Libraries="-lunicorn"
Out="build/lunix"

Jobs="$(nproc)"

mkdir -p build
rm -rf build/*
mkdir -p build/compile/lunix

CompileLunix() {
  File=$1
  Object="build/compile/lunix/$(echo "$File" | sed 's#^examples/##; s#/#_#g; s#\.c$##')"

  echo "  CC  $File"
  $CC $CFlags -c "$File" \
    -o $Object
}

CompileExample() {
  File=$1
  Out="examples/$(echo "$File" | sed 's#^examples/##; s#/#_#g; s#\.c$##')"

  echo "  CC  $File"
  $CC2 -c "$File" \
    -o $Out
}

export -f CompileLunix
export -f CompileExample
export CFlags
export CC
export CC2

if [ "${1:-}" = "examples" ]; then
  find examples -name "*.c" |
    xargs -P "$Jobs" -n 1 bash -c 'CompileExample "$1"' _
else
  find src -name "*.c" |
    xargs -P "$Jobs" -n 1 bash -c 'CompileLunix "$1"' _

  echo "  LD  $Out"
  $CC $CFlags $LDFlags $Libraries \
    build/compile/*/* \
    -o $Out
fi
