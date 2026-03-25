#!/usr/bin/env sh
# Same pipeline as run_demo.sh, but compile app sources with clang -fpass-plugin
# (same idea as lisitsynSA/llvm_course LLVM_Pass). Build flowsketch_rt.c without the plugin.

set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/log" "$ROOT/build"
cd "$ROOT"
SRC="${1:-$ROOT/examples/advanced.c}"
PLUGIN="${2:-$ROOT/build/libFlowSketchPass.so}"
OBJ_APP="$ROOT/build/app_inst.o"
OBJ_RT="$ROOT/build/flowsketch_rt.o"
BIN="$ROOT/build/demo_clang_prof"
MERGE="$ROOT/build/flowsketch-merge"

if test ! -f "$PLUGIN"; then
  sh "$ROOT/scripts/build_plugin.sh" "$PLUGIN"
fi
if test ! -x "$MERGE"; then
  ${CXX:-g++} -std=c++17 "$ROOT/tools/flowsketch_merge.cpp" -o "$MERGE"
fi

clang -fpass-plugin="$PLUGIN" -O0 -g -I"$ROOT/include" -c "$SRC" -o "$OBJ_APP"
clang -O0 -g -I"$ROOT/include" -c "$ROOT/runtime/flowsketch_rt.c" -o "$OBJ_RT"
clang -O0 "$OBJ_APP" "$OBJ_RT" -o "$BIN"

rm -f "$ROOT/log/dynamic.flow.log"
( cd "$ROOT" && "$BIN" ) || true
"$MERGE" "$ROOT/log/static.flow.txt" "$ROOT/log/dynamic.flow.log" "$ROOT/log/flowsketch.dot"
dot -Tsvg "$ROOT/log/flowsketch.dot" -o "$ROOT/log/flowsketch.svg"
echo "SVG: $ROOT/log/flowsketch.svg (clang -fpass-plugin path)"
