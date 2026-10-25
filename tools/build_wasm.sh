#!/usr/bin/env bash
# Rebuild web/leo.wasm with wasi-sdk (https://github.com/WebAssembly/wasi-sdk).
# Usage: WASI_SDK=/path/to/wasi-sdk-25.0 tools/build_wasm.sh
set -eu
cd "$(dirname "$0")/.."
: "${WASI_SDK:?set WASI_SDK to the wasi-sdk directory}"
"$WASI_SDK/bin/clang" --sysroot="$WASI_SDK/share/wasi-sysroot" -O2 -Iinclude \
  -DLEO_DEFAULT_GRAMMAR='"/grammar.txt"' -Wl,-z,stack-size=4194304 -o web/leo.wasm \
  src/grammar.c src/lexer.c src/first.c src/lr1_item.c src/closure.c src/goto.c src/state.c \
  src/canonical.c src/parsing_table.c src/conflict.c src/parser.c src/compiler/nova_*.c \
  src/compiler/leoc.c -lm
python3 tools/make_web.py
ls -l web/leo.wasm
