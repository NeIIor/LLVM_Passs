#!/usr/bin/env sh
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/log" "$ROOT/build"
SRC="${1:-$ROOT/examples/advanced.c}"
PLUGIN="${2:-$ROOT/build/libFlowSketchPass.so}"
BC="$ROOT/build/demo.bc"
LL="$ROOT/build/demo.ll"
BIN="$ROOT/build/demo_prof"
MERGE="$ROOT/build/flowsketch-merge"

if test ! -f "$PLUGIN"; then
  sh "$ROOT/scripts/build_plugin.sh" "$PLUGIN"
fi
if test ! -x "$MERGE"; then
  ${CXX:-g++} -std=c++17 "$ROOT/tools/flowsketch_merge.cpp" -o "$MERGE"
fi

clang -emit-llvm -c -O0 -g -Xclang -disable-O0-optnone -o "$BC" "$SRC"
opt -load-pass-plugin="$PLUGIN" -passes='default<O0>' -S -o "$LL" "$BC"
clang -O0 -g "$LL" -o "$BIN" -I"$ROOT/include" "$ROOT/runtime/flowsketch_rt.c"

rm -f "$ROOT/log/dynamic.flow.log"
( cd "$ROOT" && "$BIN" ) || true
"$MERGE" "$ROOT/log/static.flow.txt" "$ROOT/log/dynamic.flow.log" "$ROOT/log/flowsketch.dot"
dot -Tsvg "$ROOT/log/flowsketch.dot" -o "$ROOT/log/flowsketch.svg"
echo "SVG: $ROOT/log/flowsketch.svg"
echo "DOT: $ROOT/log/flowsketch.dot"
