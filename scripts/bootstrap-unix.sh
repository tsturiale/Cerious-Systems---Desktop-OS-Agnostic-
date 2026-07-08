#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/.tools/vcpkg}"

if ! command -v node >/dev/null 2>&1; then
  echo "node is required. Install Node.js 20+ first." >&2
  exit 1
fi

if ! command -v npm >/dev/null 2>&1; then
  echo "npm is required. Install Node.js/npm first." >&2
  exit 1
fi

if ! command -v git >/dev/null 2>&1; then
  echo "git is required." >&2
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake 3.24+ is required." >&2
  exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
  echo "ninja is required. Install ninja-build on Linux or ninja via Homebrew on macOS." >&2
  exit 1
fi

echo "Installing frontend dependencies..."
npm --prefix "$ROOT/apps/terminal" ci
npm --prefix "$ROOT/clients/openfin-terminal" ci

if [ ! -d "$VCPKG_ROOT/.git" ]; then
  echo "Bootstrapping vcpkg at $VCPKG_ROOT..."
  mkdir -p "$(dirname "$VCPKG_ROOT")"
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi

if [ ! -x "$VCPKG_ROOT/vcpkg" ]; then
  "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

UNAME="$(uname -s)"
ARCH="$(uname -m)"
case "$UNAME:$ARCH" in
  Linux:x86_64) TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux}" ;;
  Linux:aarch64|Linux:arm64) TRIPLET="${VCPKG_TARGET_TRIPLET:-arm64-linux}" ;;
  Darwin:arm64) TRIPLET="${VCPKG_TARGET_TRIPLET:-arm64-osx}" ;;
  Darwin:x86_64) TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-osx}" ;;
  *) TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux}" ;;
esac

echo "Installing native dependencies through vcpkg triplet $TRIPLET..."
"$VCPKG_ROOT/vcpkg" install "openssl:$TRIPLET" "zstd:$TRIPLET"

cat <<EOF

Bootstrap complete.

Next:
  npm run build:native:unix
  npm run build:frontend

For a local backend:
  cp .env.example .env
  edit .env with DATABENTO_API_KEY and credentials
  npm run start:backend:unix

EOF
