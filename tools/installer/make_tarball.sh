#!/bin/bash
# Build the redistributable Linux SDK tarball:
#   aroma-lavender-linux.tar.xz  (top-level dir: aroma-lavender/)
#
# Usage (run from the repo root):
#   bash tools/installer/make_tarball.sh [--out DIR] [--name NAME]
#
# The tarball contains everything a developer needs (CLI, headers,
# sources, vendors, examples) minus tracked build junk, plus install.sh
# at its root. Upload the result to the GitHub release; the docs'
# one-liner pulls it from there. Requires git submodules to be checked
# out (vendors/*) for full Android/web builds to work.
set -euo pipefail

OUT="."
NAME="aroma-lavender-linux.tar.xz"
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="${2:?}"; shift 2 ;;
        --out=*) OUT="${1#--out=}"; shift ;;
        --name) NAME="${2:?}"; shift 2 ;;
        --name=*) NAME="${1#--name=}"; shift ;;
        --help|-h) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "error: unknown option: $1" >&2; exit 2 ;;
    esac
done

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="$STAGE/aroma-lavender"

echo "==> Staging SDK tree ..."
mkdir -p "$PKG"
for entry in bin include src tools vendors examples docs/overview docs/cli-toolchain docs/widget-library docs/core-framework docs/backend-abstraction-layer docs/sandbox.html docs/default.aroma docs-cross.txt README.md CMakeLists.txt; do
    [ -e "$ROOT/$entry" ] || continue
    mkdir -p "$PKG/$(dirname "$entry")"
    cp -a "$ROOT/$entry" "$PKG/$entry"
done
[ -f "$ROOT/README.md" ] && cp -a "$ROOT/README.md" "$PKG/" || true
cp -a "$ROOT/tools/installer/install.sh" "$PKG/install.sh"
chmod +x "$PKG/install.sh" "$PKG/bin/aroma"

echo "==> Pruning build junk ..."
prune() { find "$PKG" "$@" -exec rm -rf {} + 2>/dev/null || true; }
prune -type d \( -name ".cxx" -o -name "build" -o -name "__pycache__" -o -name ".gradle" -o -name "node_modules" \)
prune -type f \( -name "*.o" -o -name "*.so" -o -name "*.a" -o -name "*.pyc" -o -name "*.ninja_deps" -o -name "*.ninja_log" \)

if [ ! -s "$ROOT/vendors/freetype/include/ft2build.h" ]; then
    echo "WARNING: vendors/freetype looks empty (submodule not checked out)."
fi
if [ ! -s "$ROOT/vendors/glps/CMakeLists.txt" ]; then
    echo "WARNING: vendors/glps looks empty (submodule not checked out)."
fi

mkdir -p "$OUT"
echo "==> Compressing $NAME ..."
tar -cJf "$OUT/$NAME" -C "$STAGE" aroma-lavender
ls -la "$OUT/$NAME"

echo "==> Sanity check ..."
tar -tf "$OUT/$NAME" | head -n 8
for need in aroma-lavender/install.sh aroma-lavender/bin/aroma aroma-lavender/tools/cli/aroma.py aroma-lavender/include/aroma_ui.h; do
    tar -tf "$OUT/$NAME" | grep -qx "$need" || { echo "error: missing $need" >&2; exit 1; }
done
echo "OK: $OUT/$NAME"
