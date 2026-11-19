#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT_DIR/web/dist"
mkdir -p "$OUT_DIR"

echo "[build] Compiling Crystal Garden Web Simulator (WASM)..."
em++ -O3 -std=gnu++17 \
  -I "$ROOT_DIR/web/shims" \
  -I "$ROOT_DIR/include" \
  -I "$ROOT_DIR/src" \
  "$ROOT_DIR/web/web_main.cpp" \
  "$ROOT_DIR/src/beat.cpp" \
  "$ROOT_DIR/src/garden_game.cpp" \
  "$ROOT_DIR/src/game_params.cpp" \
  -o "$OUT_DIR/crystal_garden.js" \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="createCrystalGarden" \
  -s ENVIRONMENT=web \
  -s INITIAL_MEMORY=67108864 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORTED_FUNCTIONS="['_emu_init','_emu_step','_emu_render','_emu_get_rgba_ptr','_emu_pointer_down','_emu_pointer_move','_emu_pointer_up','_emu_trigger_pet','_emu_send_command','_emu_save','_emu_get_wave','_emu_get_run','_emu_get_party_count']" \
  -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap','UTF8ToString','HEAPU8']"

cp "$ROOT_DIR/web/index.html" "$OUT_DIR/index.html"
touch "$OUT_DIR/.nojekyll"

ls -lh "$OUT_DIR/crystal_garden.js" "$OUT_DIR/crystal_garden.wasm" "$OUT_DIR/index.html"
echo "[build] Done -> $OUT_DIR"
