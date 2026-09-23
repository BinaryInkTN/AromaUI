#!/usr/bin/env bash
# Seed + serve the Aroma Store store repo from freshly built .apak bundles.
#
# Usage:
#   ./tools/seed_store.sh [repo_dir] [port]
#
# Defaults: repo_dir=./store_repo  port=8080
# Then in the car: open "Aroma Store" -> Installed tab -> check the server
# URL (default http://127.0.0.1:8080) -> For You / Games / Apps -> Refresh.
set -euo pipefail

REPO="${1:-store_repo}"
PORT="${2:-8080}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/examples/car_infotainment/build/dist"

if [ ! -d "$DIST" ]; then
    echo "seed: no dist dir ($DIST) - build the infotainment first:" >&2
    echo "  cmake -S examples/car_infotainment -B examples/car_infotainment/build" >&2
    echo "  cmake --build examples/car_infotainment/build -j" >&2
    exit 1
fi

mkdir -p "$REPO"
cp -v "$DIST"/*.apak "$REPO/"
echo "seed: validating..."
for f in "$REPO"/*.apak; do
    python3 "$ROOT/tools/apak.py" validate "$f"
done
echo "seed: serving $REPO on port $PORT"
exec python3 "$ROOT/tools/aroma_store_server.py" --dir "$REPO" --port "$PORT"
