#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
QT_PREFIX="${QT_PREFIX:-$(brew --prefix qt 2>/dev/null || true)}"

if [[ -z "${QT_PREFIX}" ]]; then
  echo "Qt was not found. Install it with: brew install qt" >&2
  exit 1
fi

cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build/macos-release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="${QT_PREFIX}" "$@"
cmake --build "${ROOT_DIR}/build/macos-release" --parallel
