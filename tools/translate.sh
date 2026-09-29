#!/bin/bash
# i386 ELF -> build/xlat (the generated C). HOOKS="sym ..." adds entry probes (verification builds only).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PY=${PY:-python3}
H=""; for s in $HOOKS; do H="$H --hook $s"; done
rm -rf "$ROOT/build/xlat"
$PY "$ROOT/xlat/elf2c.py" "$ROOT/build/i386/syndicatfx.elf" "$ROOT/build/xlat" --files 32 $H \
  --export-sym level__Seed --export-sym current_levno
