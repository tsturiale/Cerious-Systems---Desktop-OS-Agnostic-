#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="$(node "$ROOT/scripts/make-openfin-manifest.mjs")"

if [ ! -d "$ROOT/clients/openfin-terminal/node_modules" ]; then
  npm --prefix "$ROOT/clients/openfin-terminal" ci
fi

echo "Launching Cerious Desktop with manifest:"
echo "  $MANIFEST"
echo
npx --prefix "$ROOT/clients/openfin-terminal" openfin-cli -l -c "$MANIFEST"
