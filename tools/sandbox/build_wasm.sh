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
# Real MPEG-1 clip preloaded for the curated `video` docs demo, decoded in
# the browser by the widget's built-in pl_mpeg backend (Sintel excerpt,
# CC-BY 3.0 — see examples/14_video/assets/ATTRIBUTION.md).
VIDEO_ASSET="$ROOT/examples/14_video/assets/sample_sintel.mpg"

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
  -I"$FT_BUILD/include"
  -I"$ROOT/vendors/freetype/include"
)

SRCS=(
  "$ROOT/tools/sandbox/incense_sandbox_main.c"
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

emcc "${OBJS[@]}" \
  -O2 \
  "$FT_BUILD/libfreetype.a" \
  --preload-file "$VIDEO_ASSET@/assets/sample_sintel.mpg" \
  -sUSE_WEBGL2=1 \
  -sFETCH=1 \
  -sASYNCIFY=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_FUNCTIONS=_main,_aroma_sandbox_init,_aroma_sandbox_reload,_aroma_sandbox_has_error,_aroma_sandbox_get_last_error,_aroma_sandbox_set_theme,_aroma_sandbox_get_width,_aroma_sandbox_get_height \
  -sEXPORTED_RUNTIME_METHODS=ccall \
  -o "$OUT"

cp "$OUT" "$COPY_DIR/incense_sandbox.js"
cp "${OUT%.js}.wasm" "$COPY_DIR/incense_sandbox.wasm"
if [ -f "${OUT%.js}.data" ]; then
  cp "${OUT%.js}.data" "$COPY_DIR/incense_sandbox.data"
fi
ls -la "$OUT" "${OUT%.js}.wasm"
