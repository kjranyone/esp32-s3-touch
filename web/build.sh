#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT_DIR/web/dist"
APPS_OUT="$OUT_DIR/apps"
mkdir -p "$APPS_OUT"

COMMON_EXPORTS="['_hw_init','_hw_step','_hw_render','_hw_save','_hw_info_id','_hw_info_name','_hw_info_board','_hw_fb_width','_hw_fb_height','_hw_fb_is_mono','_hw_fb_rgba_ptr','_hw_touch_down','_hw_touch_move','_hw_touch_up','_hw_uart_rx','_hw_audio_set_rate','_hw_audio_render','_hw_audio_set_muted','_hw_audio_is_muted','_hw_gpio_out_ptr','_hw_gpio_dir_ptr','_hw_gpio_set_in','_hw_pwm_ptr','_hw_adc_set','_hw_encoder_step','_hw_encoder_btn','_hw_led_ptr','_hw_led_pin','_hw_led_count','_hw_led_version','_hw_jumper_set']"
RUNTIME_METHODS="['ccall','cwrap','UTF8ToString','HEAPU8','HEAPF32']"

build_cartridge() {
  local id="$1"
  local export_name="$2"
  local inc_dir="$3"
  local init_mem="$4"
  shift 4
  echo "[build] Compiling cartridge '$id' ($export_name)..."
  em++ -O3 -std=gnu++17 \
    -I "$ROOT_DIR/web/shims" \
    -I "$ROOT_DIR/include" \
    -I "$ROOT_DIR/src" \
    -I "$inc_dir" \
    "$ROOT_DIR/src/hal/wasm/wasm_host.cpp" \
    "$ROOT_DIR/src/sfx.cpp" \
    "$@" \
    -o "$APPS_OUT/${id}.js" \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="$export_name" \
    -s ENVIRONMENT=web \
    -s INITIAL_MEMORY="$init_mem" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_FUNCTIONS="$COMMON_EXPORTS" \
    -s EXPORTED_RUNTIME_METHODS="$RUNTIME_METHODS"
}

build_cartridge "crystal_garden" "createCrystalGarden" "$ROOT_DIR/src/apps/crystal_garden" 67108864 \
  "$ROOT_DIR/src/apps/crystal_garden/app_crystal_garden.cpp" \
  "$ROOT_DIR/src/apps/crystal_garden/beat.cpp" \
  "$ROOT_DIR/src/apps/crystal_garden/garden_game.cpp" \
  "$ROOT_DIR/src/apps/crystal_garden/game_params.cpp"

build_cartridge "io_workbench" "createIoWorkbench" "$ROOT_DIR/src/apps/io_workbench" 16777216 \
  "$ROOT_DIR/src/apps/io_workbench/app_io_workbench.cpp"

build_cartridge "oled_pocket" "createOledPocket" "$ROOT_DIR/src/apps/oled_pocket" 16777216 \
  "$ROOT_DIR/src/apps/oled_pocket/app_oled_pocket.cpp"

build_cartridge "hw_diag" "createHwDiag" "$ROOT_DIR/src/apps/hw_diag" 16777216 \
  "$ROOT_DIR/src/apps/hw_diag/app_hw_diag.cpp"

# Sync modular 3D workbench, HTML shell & Web Component embed example
rm -rf "$OUT_DIR/sim"
cp -R "$ROOT_DIR/web/sim" "$OUT_DIR/sim"
cp "$ROOT_DIR/web/index.html" "$OUT_DIR/index.html"
cp "$ROOT_DIR/web/embed.html" "$OUT_DIR/embed.html"
rm -f "$OUT_DIR/crystal_garden.js" "$OUT_DIR/crystal_garden.wasm"
touch "$OUT_DIR/.nojekyll"

ls -lh "$APPS_OUT" "$OUT_DIR/index.html"
echo "[build] Done -> $OUT_DIR"
