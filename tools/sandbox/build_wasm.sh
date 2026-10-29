#!/bin/bash
# Rebuild the Incense live-preview runtime:
#   docs/incense_sandbox.{js,wasm}  (used by docs/sandbox.html)
#   docs/widget-library/wasm/incense_sandbox/incense_sandbox.{js,wasm}
#
# Usage:  bash tools/sandbox/build_wasm.sh
# Requires Emscripten (emcc) on PATH. Freetype must be built for wasm
# first (run once, reuse):
#   emcmake cmake -S vendors/freetype -B <ft-build> -DFT_DISABLE_ZLIB=ON \
#     -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_BROTLI=ON
#   cmake --build <ft-build>
# then:   FT_BUILD=<ft-build> bash tools/sandbox/build_wasm.sh
# After rebuilding, bump the ?v=N cache-buster in docs/sandbox.html
# (and wasm/incense_sandbox/index.html) to match.
#
# NOTE: two-step build (objects, then link) is deliberate: a single
# emcc invocation over the same sources and flags produces a binary
# that loads cleanly but renders blank, while the object-link build
# renders correctly. Suspected emcc whole-program/link-order quirk;
# do not "simplify" back without re-verifying visually in headless
# Chrome (see tools/sandbox/README).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/docs/incense_sandbox.js"
COPY_DIR="$ROOT/docs/widget-library/wasm/incense_sandbox"
FT_BUILD="${FT_BUILD:-/tmp/opencode/ft-build}"
OBJDIR="${OBJDIR:-$ROOT/tools/sandbox/build/obj}"
# Real MPEG-1 clip preloaded for the curated `video` docs demo, decoded in the
# browser by the widget's built-in pl_mpeg backend (CC-BY 3.0 — see
# examples/14_video/assets/ATTRIBUTION.md).
VIDEO_ASSET="$ROOT/examples/14_video/assets/sample_sintel.mpg"
# Generated assets for the curated dashboard demo (see the matching
# tools/sandbox/assets/make_*.py scripts to regenerate).
WELCOME_ASSET="$ROOT/tools/sandbox/assets/welcome.png"
AVATAR_ASSET="$ROOT/tools/sandbox/assets/avatar.png"
ANGELA_ASSET="$ROOT/tools/sandbox/assets/angela.png"
LOUD_LOGO_ASSET="$ROOT/tools/sandbox/assets/loud_logo.png"
FITNESS_HERO_ASSET="$ROOT/tools/sandbox/assets/fitness_hero.jpg"
FITNESS_LOGO_ASSET="$ROOT/tools/sandbox/assets/fitness_logo.png"
FITNESS_AVATAR_ASSET="$ROOT/tools/sandbox/assets/fitness_avatar.png"
FITNESS_SCRIM_ASSET="$ROOT/tools/sandbox/assets/fitness_scrim.png"
TRAVEL_AVATAR_ASSET="$ROOT/tools/sandbox/assets/travel_avatar.png"
TRAVEL_ALBUM_ASSET="$ROOT/tools/sandbox/assets/travel_album.jpg"
TRAVEL_MIC_ASSET="$ROOT/tools/sandbox/assets/travel_mic.png"
TRAVEL_CORNER_TL_ASSET="$ROOT/tools/sandbox/assets/travel_corner_tl.png"
TRAVEL_CORNER_TR_ASSET="$ROOT/tools/sandbox/assets/travel_corner_tr.png"
TRAVEL_CORNER_BL_ASSET="$ROOT/tools/sandbox/assets/travel_corner_bl.png"
TRAVEL_CORNER_BR_ASSET="$ROOT/tools/sandbox/assets/travel_corner_br.png"

# Alternate icon pack used by the Incense sandbox (see
# tools/gen_lucide_pack.py).  Loaded by the sandbox at runtime.
LUCIDE_FONT_ASSET="$ROOT/tools/sandbox/assets/lucide.ttf"

if [ ! -f "$FT_BUILD/libfreetype.a" ]; then
  echo "error: $FT_BUILD/libfreetype.a missing; build freetype first (see header)" >&2
  exit 1
fi

CFLAGS=(
  -O2 -std=gnu11
  -DAROMA_USE_EMSCRIPTEN
  -DSQLITE_THREADSAFE=0
  -DSQLITE_OMIT_LOAD_EXTENSION
  -DSQLITE_OMIT_PROGRESS_CALLBACK
  -I"$ROOT/include"
  -I"$ROOT/src"
  -I"$ROOT/src/backends"
  -I"$ROOT/src/backends/graphics/utils"
  -I"$ROOT/vendors/sqlite3"
  -I"$ROOT/tools/sandbox/stub"
  -I"$ROOT/examples/16_loud_dashboard/src"
  -I"$ROOT/examples/17_travel_dashboard/src"
  -I"$ROOT/examples/18_fitness_dashboard/src"
  -I"$FT_BUILD/include"
  -I"$ROOT/vendors/freetype/include"
  -I"$ROOT/vendors/pbplots/C"
)

SRCS=(
  "$ROOT/tools/sandbox/incense_sandbox_main.c"
  "$ROOT/examples/17_travel_dashboard/src/travel_demo.c"
  "$ROOT/examples/18_fitness_dashboard/src/fitness_demo.c"

  "$ROOT/examples/16_loud_dashboard/src/loud_dashboard.c"
  "$ROOT/tools/sandbox/stub_curl.c"
  "$ROOT/src/core/aroma_ui_impl.c"
  "$ROOT/src/core/aroma_logger.c"
  "$ROOT/src/core/aroma_node.c"
  "$ROOT/src/core/aroma_animation.c"
  "$ROOT/src/core/aroma_layout.c"
  "$ROOT/src/core/aroma_slab_alloc.c"
  "$ROOT/src/core/aroma_style.c"
  "$ROOT/src/core/aroma_time.c"
  "$ROOT/src/core/aroma_timer.c"
  "$ROOT/src/core/aroma_velocity.c"
  "$ROOT/src/core/aroma_graphics_wrapper.c"
  "$ROOT/src/core/aroma_font.c"
  "$ROOT/src/core/aroma_ubuntu_font.c"
  "$ROOT/src/core/aroma_event.c"
  "$ROOT/src/core/aroma_gesture.c"
  "$ROOT/src/core/aroma_drawlist.c"
  "$ROOT/src/core/aroma_test.c"
  "$ROOT/src/core/aroma_native_utils.c"
  "$ROOT/src/core/utils/mpc.c"
  "$ROOT/src/core/aroma_3d.c"
  "$ROOT/src/core/cJSON.c"
  "$ROOT/src/core/aroma_incense.c"
  "$ROOT/src/core/aroma_incense_loader.c"
  "$ROOT/src/backends/graphics/aroma_graphics_gles3.c"
  "$ROOT/src/backends/graphics/utils/helpers_gles3.c"
  "$ROOT/src/backends/graphics/utils/aroma_gles3_text.c"
  "$ROOT/src/backends/graphics/aroma_graphics_emscripten_stub.c"
  "$ROOT/src/backends/aroma_abi.c"
  "$ROOT/src/backends/platforms/aroma_platform_emscripten.c"
  "$ROOT/vendors/sqlite3/sqlite3.c"
  # Vendored pbPlots (MIT, C99): rasterises the Chart widget's series.
  "$ROOT/vendors/pbplots/C/pbPlots.c"
)
for f in "$ROOT"/src/widgets/aroma_*.c; do
  SRCS+=("$f")
done

mkdir -p "$OBJDIR"
OBJS=()
for src in "${SRCS[@]}"; do
  obj="$OBJDIR/$(basename "$src" .c).o"
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    emcc "${CFLAGS[@]}" -c "$src" -o "$obj"
  fi
  OBJS+=("$obj")
done

# Decoding the preloaded PNGs with stb_image needs far more than
# emscripten's 64 KB default stack (the overflow surfaces as a bare
# "memory access out of bounds" at a random point during load).
emcc "${OBJS[@]}" \
  -O2 \
  "$FT_BUILD/libfreetype.a" \
  --preload-file "$VIDEO_ASSET@/assets/sample_sintel.mpg" \
  --preload-file "$WELCOME_ASSET@/assets/welcome.png" \
  --preload-file "$AVATAR_ASSET@/assets/avatar.png" \
  --preload-file "$ANGELA_ASSET@/assets/angela.png" \
  --preload-file "$LOUD_LOGO_ASSET@/assets/loud_logo.png" \
  --preload-file "$FITNESS_HERO_ASSET@/assets/fitness_hero.jpg" \
  --preload-file "$FITNESS_LOGO_ASSET@/assets/fitness_logo.png" \
  --preload-file "$FITNESS_AVATAR_ASSET@/assets/fitness_avatar.png" \
  --preload-file "$FITNESS_SCRIM_ASSET@/assets/fitness_scrim.png" \
  --preload-file "$TRAVEL_AVATAR_ASSET@/assets/travel_avatar.png" \
  --preload-file "$TRAVEL_ALBUM_ASSET@/assets/travel_album.jpg" \
  --preload-file "$TRAVEL_MIC_ASSET@/assets/travel_mic.png" \
  --preload-file "$TRAVEL_CORNER_TL_ASSET@/assets/travel_corner_tl.png" \
  --preload-file "$TRAVEL_CORNER_TR_ASSET@/assets/travel_corner_tr.png" \
  --preload-file "$TRAVEL_CORNER_BL_ASSET@/assets/travel_corner_bl.png" \
  --preload-file "$TRAVEL_CORNER_BR_ASSET@/assets/travel_corner_br.png" \
  --preload-file "$LUCIDE_FONT_ASSET@/assets/lucide.ttf" \
  -sUSE_WEBGL2=1 \
  -sFETCH=1 \
  -sASYNCIFY=1 \
  -sSTACK_SIZE=1048576 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_FUNCTIONS=_main,_aroma_sandbox_init,_aroma_sandbox_reload,_aroma_sandbox_has_error,_aroma_sandbox_get_last_error,_aroma_sandbox_set_theme,_aroma_sandbox_set_theme_preset,_aroma_sandbox_get_width,_aroma_sandbox_get_height,_aroma_platform_canvas_resized \
  -sEXPORTED_RUNTIME_METHODS=ccall \
  -o "$OUT"

cp "$OUT" "$COPY_DIR/incense_sandbox.js"
cp "${OUT%.js}.wasm" "$COPY_DIR/incense_sandbox.wasm"
if [ -f "${OUT%.js}.data" ]; then
  cp "${OUT%.js}.data" "$COPY_DIR/incense_sandbox.data"
fi
ls -la "$OUT" "${OUT%.js}.wasm"
