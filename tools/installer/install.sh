#!/bin/bash
# AromaUI SDK installer (ships inside aroma-lavender-linux.tar.xz).
#
# Usage:
#   ./install.sh [--prefix DIR] [--in-place] [--yes] [--no-symlink]
#
#   --prefix DIR   install the SDK tree into DIR (default: ~/.aroma/sdk)
#   --in-place     use this directory as the SDK (no copy, just wire up)
#   --yes, -y      skip the confirmation prompt
#   --no-symlink   do not link bin/aroma into ~/.local/bin
#   --help         this text
#
# What it does: checks prerequisites, places the SDK tree, links the
# `aroma` command onto PATH, then runs `aroma doctor` (informational).
set -euo pipefail

PREFIX="${HOME}/.aroma/sdk"
IN_PLACE=0
YES=0
SYMLINK=1

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)    PREFIX="${2:?--prefix needs a directory}"; shift 2 ;;
        --prefix=*)  PREFIX="${1#--prefix=}"; shift ;;
        --in-place)  IN_PLACE=1; shift ;;
        --yes|-y)    YES=1; shift ;;
        --no-symlink) SYMLINK=0; shift ;;
        --help|-h)   sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "error: unknown option: $1 (try --help)" >&2; exit 2 ;;
    esac
done

SRC="$(cd "$(dirname "$0")" && pwd)"
if [ ! -x "$SRC/bin/aroma" ] || [ ! -f "$SRC/tools/cli/aroma.py" ]; then
    echo "error: $SRC does not look like an AromaUI SDK tree." >&2
    echo "Run this script from the extracted aroma-lavender/ directory." >&2
    exit 1
fi

need() { command -v "$1" >/dev/null 2>&1; }
missing=()
need python3 || missing+=("python3 (>= 3.8)")
need gcc || need cc || missing+=("a C compiler (gcc/clang)")
need cmake || missing+=("cmake (>= 3.22.1)")
need make || need ninja || missing+=("make or ninja")
need tar || missing+=("tar")

echo "AromaUI SDK installer"
echo "  source: $SRC"
if [ "$IN_PLACE" -eq 1 ]; then
    SDK_DIR="$SRC"
    echo "  target: $SDK_DIR (in place)"
else
    SDK_DIR="$PREFIX"
    echo "  target: $SDK_DIR"
fi
if [ "${#missing[@]}" -gt 0 ]; then
    echo "  missing prerequisites:"
    for m in "${missing[@]}"; do echo "    - $m"; done
    echo "Install them first (e.g. sudo apt install python3 gcc cmake make), then re-run."
    exit 1
fi
echo "  prerequisites: python3, compiler, cmake, make: OK"

if [ "$YES" -eq 0 ] && [ -t 0 ]; then
    printf "Continue? [Y/n] "
    read -r answer || answer="y"
    case "$answer" in
        [Nn]*) echo "Aborted."; exit 0 ;;
    esac
fi

if [ "$IN_PLACE" -eq 0 ]; then
    echo "==> Copying SDK to $SDK_DIR ..."
    mkdir -p "$SDK_DIR"
    if command -v rsync >/dev/null 2>&1; then
        rsync -a --delete --exclude 'install.sh' "$SRC/" "$SDK_DIR/"
    else
        cp -a "$SRC/." "$SDK_DIR/"
    fi
    cp -a "$SRC/install.sh" "$SDK_DIR/install.sh" 2>/dev/null || true
else
    mkdir -p "$SDK_DIR"
fi

if [ "$SYMLINK" -eq 1 ]; then
    BIN_DIR="${HOME}/.local/bin"
    mkdir -p "$BIN_DIR"
    echo "==> Linking $BIN_DIR/aroma ..."
    ln -sf "$SDK_DIR/bin/aroma" "$BIN_DIR/aroma"
    case ":$PATH:" in
        *":$BIN_DIR:"*) ;;
        *) echo "NOTE: $BIN_DIR is not on your PATH. Add it:"; echo "  export PATH=\"\$HOME/.local/bin:\$PATH\"  # then restart your shell";;
    esac
fi

echo "==> Checking installation (aroma doctor) ..."
if ! "$SDK_DIR/bin/aroma" doctor; then
    echo "(doctor reported missing optional components — Linux desktop builds work without them.)"
fi

echo
echo "Done. Next steps:"
echo "  aroma create myapp          # new C project"
echo "  aroma create myapp --python # new Python project"
echo "  cd myapp && aroma run linux"
