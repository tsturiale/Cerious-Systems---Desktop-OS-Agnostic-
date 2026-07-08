#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build/linux-release" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "${ROOT_DIR}/build/linux-release" --parallel
