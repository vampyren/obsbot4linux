#!/usr/bin/env bash
# Build OBSBOT4Linux inside an isolated Ubuntu 24.04 (GLIBC 2.39 target) container.
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR"

echo ">> Ensuring docker builder image is up to date..."
docker build -t obsbot4linux-builder -f Dockerfile.build .

echo ">> Running AppImage build inside container..."
docker run --rm \
    -u $(id -u):$(id -g) \
    -v "$SCRIPT_DIR":/workspace \
    obsbot4linux-builder \
    /workspace/qt/packaging/build-appimage.sh

echo ">> Built artifact ready at dist/OBSBOT4Linux-x86_64.AppImage"
