#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/.tools/vcpkg}"
CONFIG="${CONFIG:-RelWithDebInfo}"

UNAME="$(uname -s)"
ARCH="$(uname -m)"
case "$UNAME:$ARCH" in
  Linux:x86_64) TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux}" ;;
  Linux:aarch64|Linux:arm64) TRIPLET="${VCPKG_TARGET_TRIPLET:-arm64-linux}" ;;
  Darwin:arm64) TRIPLET="${VCPKG_TARGET_TRIPLET:-arm64-osx}" ;;
  Darwin:x86_64) TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-osx}" ;;
  *) TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux}" ;;
esac

TOOLCHAIN="${CMAKE_TOOLCHAIN_FILE:-$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake}"
if [ ! -f "$TOOLCHAIN" ]; then
  echo "vcpkg toolchain not found at $TOOLCHAIN. Run npm run bootstrap:unix first." >&2
  exit 1
fi

build_service() {
  local name="$1"
  local src="$ROOT/native/$name"
  local build="$src/build"

  echo "Configuring $name..."
  cmake -S "$src" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DVCPKG_TARGET_TRIPLET="$TRIPLET"

  echo "Building $name..."
  cmake --build "$build" --config "$CONFIG" --parallel
}

build_service cerious-exchange-cpp
build_service gateway-cpp
build_service price-feed-cpp

cat <<EOF

Native Unix build complete:
  native/cerious-exchange-cpp/build/cerious_exchange_server
  native/gateway-cpp/build/cerious_gateway
  native/price-feed-cpp/build/cerious_price_feed
  native/price-feed-cpp/build/cerious_price_history

EOF
