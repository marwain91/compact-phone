#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

# Match the GitHub-hosted Linux release runner from an arm64 Mac. This uses
# Docker emulation locally but avoids validating the wrong architecture.
export DOCKER_DEFAULT_PLATFORM="${DOCKER_DEFAULT_PLATFORM:-linux/amd64}"
export VCPKG_ARCHIVES_DIR="${VCPKG_ARCHIVES_DIR:-vcpkg-archives-amd64}"
export COMPACTPHONE_DEV_IMAGE="${COMPACTPHONE_DEV_IMAGE:-compactphone-dev:latest}"

compose=(docker compose -f tools/dev/docker-compose.yml)

if [[ "$COMPACTPHONE_DEV_IMAGE" == ghcr.io/* ]]; then
  docker pull "$COMPACTPHONE_DEV_IMAGE"
else
  "${compose[@]}" build dev
fi
"${compose[@]}" up -d --no-build dev
"${compose[@]}" exec -T dev \
  bash -c "cmake --preset linux && cmake --build --preset linux"
"${compose[@]}" exec -T dev \
  bash tools/release/build-linux-appimage.sh

printf "\nBuilt %s\n" "$repo_root/dist/Compact-Phone-Linux-x86_64.AppImage"
