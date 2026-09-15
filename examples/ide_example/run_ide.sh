#!/bin/bash
# Run AromaUI IDE - Electron desktop app
# Unset ELECTRON_RUN_AS_NODE to ensure Electron initializes properly
DIR="$(cd "$(dirname "$0")" && pwd)"
exec env -u ELECTRON_RUN_AS_NODE -u ELECTRON_NO_ATTACH_CONSOLE \
  "$DIR/node_modules/electron/dist/electron" "$DIR" "$@"
