#!/bin/bash
# Applies patches/*.patch to extern/syndicatfx as a whole: a clean tree gets the series, a dirty one
# is taken to have it already (the series is judged as a whole, never patch by patch).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
UP=$ROOT/extern/syndicatfx
[ "$(git -C "$UP" rev-parse --show-toplevel)" = "$UP" ] || { echo "extern/syndicatfx is not checked out (git submodule update --init)"; exit 1; }
if [ -n "$(git -C "$UP" status --porcelain)" ]; then echo "extern/syndicatfx already patched"; exit 0; fi
for p in "$ROOT"/patches/*.patch; do git -C "$UP" apply "$p"; echo "applied $(basename "$p")"; done
