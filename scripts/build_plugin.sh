#!/usr/bin/env sh
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
HDR="$ROOT/.llvm-hdrs/usr/lib/llvm-14/include"
INCL="$ROOT/include"
OUT="${1:-$ROOT/build/libFlowSketchPass.so}"
mkdir -p "$(dirname "$OUT")"
if test ! -f "$HDR/llvm/IR/Module.h"; then
  echo "Missing LLVM headers at $HDR — see README (unpack llvm-14-dev into .llvm-hdrs)." >&2
  exit 1
fi
${CXX:-g++} -std=c++17 -fPIC -shared -fno-rtti \
  -I"$HDR" -I"$INCL" \
  -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS \
  "$ROOT/pass/FlowSketchPass.cpp" -o "$OUT" \
  -L/usr/lib/x86_64-linux-gnu -lLLVM-14 -Wl,-rpath,/usr/lib/x86_64-linux-gnu
