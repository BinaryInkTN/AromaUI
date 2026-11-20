#!/usr/bin/env bash
# aroma-dev.sh - build (once) and enter the AromaUI dev container.
#
# Usage:
#   ./aroma-dev.sh              # build image if missing, open a shell
#   ./aroma-dev.sh --rebuild    # force a fresh image build first
#   ./aroma-dev.sh <command>    # run a command instead of a shell
#
# Env overrides:
#   AROMA_SDK_DIR   host path of the AromaUI folder (default: auto-detected by
#                   walking up from the current dir, else /home/yassine/Projects/AromaUI)
#   IMAGE           image tag (default: aroma-dev)
#   DOCKER          docker command (default: "sudo docker"; set DOCKER=docker
#                   if your user is in the docker group)
#   RUN_AS_USER=1   run as your uid:gid so build output isn't root-owned

set -euo pipefail

IMAGE="${IMAGE:-aroma-dev}"
DOCKER="${DOCKER:-sudo docker}"

# Find the AromaUI root: walk up from the current directory until we hit a
# folder that has both examples/ and a top-level CMakeLists.txt.
find_sdk_dir() {
    local d
    d="$(pwd -P)"
    while [ "$d" != "/" ]; do
        if [ -d "$d/examples" ] && [ -f "$d/CMakeLists.txt" ]; then
            echo "$d"
            return 0
        fi
        d="$(dirname "$d")"
    done
    return 1
}

AROMA_SDK_DIR="${AROMA_SDK_DIR:-$(find_sdk_dir || echo /home/yassine/Projects/AromaUI)}"

REBUILD=0
if [ "${1:-}" = "--rebuild" ]; then
    REBUILD=1
    shift
fi

if [ ! -d "$AROMA_SDK_DIR" ]; then
    echo "error: AromaUI folder not found: $AROMA_SDK_DIR" >&2
    echo "       run this from inside the AromaUI tree, or set AROMA_SDK_DIR=/path/to/AromaUI" >&2
    exit 1
fi
AROMA_SDK_DIR="$(cd "$AROMA_SDK_DIR" && pwd -P)"

# ---------------------------------------------------------------------------
# Build the image once. The Dockerfile is embedded below and fed via stdin,
# so this single file is all you need.
# ---------------------------------------------------------------------------
if [ "$REBUILD" -eq 1 ] || ! $DOCKER image inspect "$IMAGE" >/dev/null 2>&1; then
    echo ">> Building image '$IMAGE' (one-time, takes a few minutes)..."
    $DOCKER build -t "$IMAGE" - <<'DOCKERFILE'
FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
        build-essential wget curl gnupg software-properties-common \
        pkg-config ca-certificates gdb \
        libx11-dev libxext-dev libxrender-dev libxrandr-dev \
        libxinerama-dev libxcursor-dev libxi-dev \
        libgles2-mesa-dev libegl1-mesa-dev libgl1-mesa-dev \
        libglu1-mesa-dev mesa-utils \
        libasound2-dev alsa-utils \
        libfreetype6-dev libfontconfig1-dev \
        libdbus-1-dev libudev-dev libinput-dev libxkbcommon-dev \
        libcurl4-openssl-dev libzip-dev libvulkan-dev \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update \
    && apt-get install -y gcc-11 g++-11 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100 \
    && rm -rf /var/lib/apt/lists/*

RUN wget -qO- https://github.com/Kitware/CMake/releases/download/v3.28.1/cmake-3.28.1-linux-x86_64.tar.gz \
    | tar -xz -C /usr/local --strip-components=1

WORKDIR /workspace
DOCKERFILE
fi

# ---------------------------------------------------------------------------
# Run the container.
# ---------------------------------------------------------------------------
# Let the container talk to your X server (best effort).
if command -v xhost >/dev/null 2>&1; then
    xhost +local:docker >/dev/null 2>&1 || true
fi

WORK_DIR="$(pwd -P)"

# The AromaUI folder is mounted twice:
#   /aroma                    - what AROMA_SDK_PATH points at
#   <same path as on host>    - so absolute paths already baked into a
#                               host-side build dir (CMakeCache.txt etc.)
#                               stay valid inside the container
ARGS=(
    -it --rm
    -e "DISPLAY=${DISPLAY:-:0}"
    -e AROMA_SDK_PATH=/aroma
    -v /tmp/.X11-unix:/tmp/.X11-unix
    -v "$AROMA_SDK_DIR":/aroma
    -v "$AROMA_SDK_DIR":"$AROMA_SDK_DIR"
    -v "$WORK_DIR":/workspace
    -w "$WORK_DIR"
)

# If you launched from outside the AromaUI tree, mount that folder too.
case "$WORK_DIR/" in
    "$AROMA_SDK_DIR"/*) ;;
    *) ARGS+=(-v "$WORK_DIR":"$WORK_DIR") ;;
esac

echo ">> AromaUI : $AROMA_SDK_DIR  ->  /aroma  and  $AROMA_SDK_DIR"
echo ">> Work dir: $WORK_DIR  (also at /workspace)"

# Only pass devices that exist on this machine.
[ -e /dev/dri ] && ARGS+=(--device /dev/dri)
[ -e /dev/snd ] && ARGS+=(-v /dev/snd:/dev/snd)

if [ "${RUN_AS_USER:-0}" = "1" ]; then
    ARGS+=(-u "$(id -u):$(id -g)")
fi

if [ "$#" -gt 0 ]; then
    exec $DOCKER run "${ARGS[@]}" "$IMAGE" "$@"
else
    echo ">> Compiler / glibc:"
    exec $DOCKER run "${ARGS[@]}" "$IMAGE" bash -c \
        'gcc --version | head -1; g++ --version | head -1; cmake --version | head -1; ldd --version | head -1; echo; exec bash'
fi