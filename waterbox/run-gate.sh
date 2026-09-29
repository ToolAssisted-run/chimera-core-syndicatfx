#!/bin/bash
# The core gate. The sandboxed core must play Syndicate exactly as the native reference does (the same
# driver and translated game built for the host) - picture, sound, every step's length, the clock and
# every memory domain - survive a savestate before every step and a new host in the middle of a run,
# skip only the picture in turbo, and then:
#   - declare what tables.py says (the wire, the settings, the firmware), nothing drifted
#   - refuse a project without the game's files, naming one, and a damaged file, with both hashes
#   - export a property table that holds to chimera's docs/game-cores.md, obey a poke and hold a freeze
#   - ask for the game's stack as a stack (MAP_STACK)
#   - make the Sound Blaster's sound (the default), and none without a card, playing the same game
#   - package deterministically
# Every comparison is shown to have teeth: a run with other input must differ.
#
# The game is the user's Syndicate Plus CD, never in the repository: the gate takes SYNDICAT\DATA from
# tests/roms-local, or -d <dir>. Without it only the build, the declarations and the refusal of a
# project with no files run.
#
# The step-by-step equality with the ORIGINAL game (the DOS executable in DOSBox-X) is not this gate's:
# see tests/oracle/README.md (the level block, turn by turn, against the oracle's dumps).
#
# Usage: ./run-gate.sh [-q] [-m <miniBox dir>] [-d <game dir>]
#   -q skips the build (uses what is built)
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
data="${SYNDICATE_DIR:-$root/tests/roms-local}"
quick=0
while getopts "qm:d:" opt; do
	case "$opt" in
		q) quick=1 ;;
		m) mb="$OPTARG" ;;
		d) data="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
mb="$(cd "$mb" 2>/dev/null && pwd)" || { echo "miniBox not found; pass -m or set MINIBOX_DIR" >&2; exit 1; }

nat="$root/build/native"
wbx="$root/build/guest/core.wbx"
work="$root/build/gate"
rm -rf "$work"
mkdir -p "$work"

ok=0
failed=0
skipped=0
report() {
	printf "%-30s %-6s %s\n" "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok+1)) ;; SKIP) skipped=$((skipped+1)) ;; *) failed=$((failed+1)) ;; esac
}
printf "%-30s %-6s %s\n" "Check" "Result" "Detail"
printf "%-30s %-6s %s\n" "-----" "------" "------"

digests() { grep -E '^(frames|vsync|videoHash|audioHash|stepsHash|lagFrames|clock|domain\[)'; }
turboDigests() { grep -E '^(frames|vsync|tailVideoHash|audioHash|stepsHash|lagFrames|clock|domain\[)'; }
native() { timeout 900 "$nat/run-native" "$@"; }
boxed() { timeout 900 "$nat/run-wbx" "$wbx" "$@"; }
at() { awk -v s="$2" -v c="$3" '$1 == s { print $(3 + c) }' "$1"; }

# a work dir as the frontend mounts a project: the firmware under its names, the settings
workdir() {
	local d="$work/$1"
	mkdir -p "$d"
	printf '%s' "$2" > "$d/settings"
	if [ -n "${3:-}" ]; then
		python3 - "$here/waterbox.config" "$data" "$d" <<'PY'
import json, os, sys
cfg, data, d = json.load(open(sys.argv[1])), sys.argv[2], sys.argv[3]
have = {f.upper(): f for f in os.listdir(data)}
for fw in cfg["firmware"]:
    src = os.path.join(data, have[fw["name"]])
    dst = os.path.join(d, fw["name"])
    if not os.path.exists(dst): os.symlink(os.path.abspath(src), dst)
PY
	fi
	echo "$d"
}

# ------------------------------------------------------------------ 1. build
if [ "$quick" -eq 0 ]; then
	if make -C "$here" -f native.mk MB="$mb" -j"$(nproc)" > "$work/native-make.log" 2>&1 &&
	   make -C "$here" -f guest.mk MB="$mb" -j"$(nproc)" > "$work/guest-make.log" 2>&1; then
		report "build" PASS "native reference, harnesses and core.wbx (check-wbx clean)"
	else
		report "build" FAIL "see build/gate/*-make.log"
	fi
fi
[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] && [ -f "$wbx" ] || { echo "nothing built to test" >&2; exit 1; }
stale="$(find "$here" -maxdepth 1 \( -name '*.c' -o -name '*.h' -o -name '*.mk' \) ! -name 'run-*.c' ! -name 'gate-harness.h' -newer "$wbx" | head -3;
	find "$root/patches" "$root/i386" "$root/xlat" -newer "$wbx" -type f | head -1)"
[ -z "$stale" ] || report "build:fresh" FAIL "core.wbx is older than $(echo $stale | tr '\n' ' ')"
if nm "$root/build/guest/core/coro.o" 2>/dev/null | grep -q ' U mmap$'; then
	report "stacks:map-stack" PASS "the game's stack is mmap'd (MAP_STACK), not malloc'd"
else
	report "stacks:map-stack" FAIL "coro.o does not take its stack from mmap"
fi

# ------------------------------------------------------------------ 2. the declarations
before="$(cat "$here/waterbox.config" "$here/default_keybinds.json" "$here/sfx-tables.h" | sha1sum)"
python3 "$here/tables.py" > /dev/null
after="$(cat "$here/waterbox.config" "$here/default_keybinds.json" "$here/sfx-tables.h" | sha1sum)"
if [ "$before" = "$after" ]; then
	report "declarations:wire" PASS "waterbox.config, keybinds and sfx-tables.h are what tables.py makes"
else
	report "declarations:wire" FAIL "tables.py changes them: regenerate and commit"
fi
nfw="$(python3 -c "import json;c=json.load(open('$here/waterbox.config'));print(len(c['firmware']), c['kind'], len(c['input']['buttons']), len(c['input']['axes']))")"
report "declarations:config" PASS "firmware, kind, buttons, axes: $nfw"

# ------------------------------------------------------------------ 3. no files
wd="$(workdir nofiles '{}')"
out="$(boxed "$wd" --frames 1 2>&1 | grep '^loadError=')"
case "$out" in
	*"missing game file"*) report "refusal:no-files" PASS "${out#loadError=}" ;;
	*) report "refusal:no-files" FAIL "Init did not refuse: $out" ;;
esac

if [ ! -d "$data" ] || [ ! -f "$data/GAME01.DAT" ] && [ ! -f "$data/game01.dat" ]; then
	report "runs" SKIP "no game files in $data (-d <SYNDICAT\\DATA dir>)"
	echo; echo "$ok ok, $failed failed, $skipped skipped"; [ "$failed" -eq 0 ]; exit
fi

# ------------------------------------------------------------------ 4. refusals
wd="$(workdir damaged '{}' 1)"
src="$(python3 -c "import os;d='$data';print(os.path.join(d,{f.upper():f for f in os.listdir(d)}['GAME01.DAT']))")"
rm "$wd/GAME01.DAT"; cp "$src" "$wd/GAME01.DAT"
chmod u+w "$wd/GAME01.DAT"; printf '\x55' | dd of="$wd/GAME01.DAT" bs=1 seek=100 conv=notrunc 2>/dev/null
out="$(boxed "$wd" --frames 1 2>&1 | grep '^loadError=')"
case "$out" in
	*"GAME01.DAT is not the expected file"*expected*) report "refusal:damaged" PASS "${out#loadError=}" ;;
	*) report "refusal:damaged" FAIL "Init did not refuse: $out" ;;
esac

# ------------------------------------------------------------------ 5. the runs
movie="$root/tests/movies/idle-m1.movie"
frames=400
wd="$(workdir run '{"language":"English"}' 1)"
native "$wd" --frames $frames --movie "$movie" --props-json "$work/props.json" > "$work/native.txt" 2>&1
boxed "$wd" --frames $frames --movie "$movie" > "$work/box.txt" 2>&1
if [ -n "$(digests < "$work/box.txt")" ] && diff <(digests < "$work/native.txt") <(digests < "$work/box.txt") > "$work/diff.txt"; then
	report "equivalence" PASS "native == sandbox over $frames steps into mission 1 ($(grep '^lagFrames' "$work/box.txt"))"
else
	report "equivalence" FAIL "$(head -3 "$work/diff.txt" | tr '\n' ' ')"
fi
boxed "$wd" --frames $frames > "$work/noinput.txt" 2>&1
if ! diff -q <(digests < "$work/box.txt") <(digests < "$work/noinput.txt") > /dev/null; then
	report "equivalence:teeth" PASS "the same run without the movie differs"
else
	report "equivalence:teeth" FAIL "input made no difference"
fi
boxed "$wd" --frames $frames --movie "$movie" --rerecord > "$work/rerecord.txt" 2>&1
if diff -q <(digests < "$work/box.txt") <(digests < "$work/rerecord.txt") > /dev/null; then
	report "savestate:rerecord" PASS "save and load before every step: the same run"
else
	report "savestate:rerecord" FAIL "see build/gate/rerecord.txt"
fi
boxed "$wd" --frames $frames --movie "$movie" --session > "$work/session.txt" 2>&1
if diff -q <(digests < "$work/box.txt") <(digests < "$work/session.txt") > /dev/null; then
	report "savestate:session" PASS "saved at step $((frames / 2)), finished in a new host: the same run"
else
	report "savestate:session" FAIL "see build/gate/session.txt"
fi
boxed "$wd" --frames $frames --movie "$movie" --turbo > "$work/turbo.txt" 2>&1
if diff -q <(turboDigests < "$work/box.txt") <(turboDigests < "$work/turbo.txt") > /dev/null; then
	report "turbo" PASS "half the run undrawn: the same machine, the same pictures after"
else
	report "turbo" FAIL "see build/gate/turbo.txt"
fi
# the sound: the Sound Blaster's (the default) is heard; without a card there is none, and the game is the same
boxed "$wd" --frames $frames --movie "$movie" --audio "$work/sound.raw" > /dev/null 2>&1
wq="$(workdir quiet '{"language":"English","sound":"None"}' 1)"
native "$wq" --frames $frames --movie "$movie" --audio "$work/quiet.raw" > "$work/quiet.txt" 2>&1
game() { grep -E '^(frames|videoHash|domain\[Game State\]|domain\[Level\])'; }
heard() {
	python3 - "$1" <<'PY'
import array, sys
a = array.array('h', open(sys.argv[1], 'rb').read())
w = 2 * 2756   # 1/16 s of 44100 Hz stereo
print(sum(1 for i in range(0, len(a), w) if any(a[i:i + w])))
PY
}
loud="$(heard "$work/sound.raw" 2>/dev/null)"; quiet="$(heard "$work/quiet.raw" 2>/dev/null)"
if [ "${loud:-0}" -ge 16 ] && [ "$quiet" = 0 ] && diff -q <(game < "$work/box.txt") <(game < "$work/quiet.txt") > /dev/null; then
	report "sound" PASS "$loud of $frames steps heard; with no card none, and the same game"
else
	report "sound" FAIL "heard ${loud:-?} steps, ${quiet:-?} without a card; see build/gate/quiet.txt"
fi

# ------------------------------------------------------------------ 6. the properties
if python3 "$here/tests/check-properties.py" "$work/props.json" "$work/box.txt" > "$work/props.txt" 2>&1; then
	report "properties:table" PASS "$(cat "$work/props.txt")"
else
	report "properties:table" FAIL "$(tail -1 "$work/props.txt")"
fi
boxed "$wd" --frames $frames --movie "$movie" --poke "300:Level.Seed=4660" --trace "$work/poke.trace" --trace-props "Level.Seed,Turns" > "$work/poke.txt" 2>&1
boxed "$wd" --frames $frames --movie "$movie" --freeze "300-399:Level.Seed=4660" --trace "$work/freeze.trace" --trace-props "Level.Seed" > /dev/null 2>&1
# a turn draws the random number at least once, so the seed after a step is never the one poked before it;
# frozen, it is the same after every step (one draw's worth from the frozen value)
f1="$(at "$work/freeze.trace" 300 1)"
if [ -n "$f1" ] && [ "$(awk -v v="$f1" '$1 ~ /^[0-9]+$/ && $1 >= 300 && $4 != v' "$work/freeze.trace" | wc -l)" = 0 ] &&
   [ "$(at "$work/poke.trace" 350 1)" != "$(at "$work/freeze.trace" 350 1)" ] &&
   ! diff -q <(grep '^domain\[Level\]' "$work/box.txt") <(grep '^domain\[Level\]' "$work/poke.txt") > /dev/null; then
	report "properties:poke-freeze" PASS "Level.Seed poked at step 300 changes the run; frozen, it reads $f1 after every step"
else
	report "properties:poke-freeze" FAIL "freeze $f1 off on $(awk -v v="$f1" '$1 ~ /^[0-9]+$/ && $1 >= 300 && $4 != v' "$work/freeze.trace" | wc -l) steps"
fi

# ------------------------------------------------------------------ 7. the package
if sh "$here/build-package.sh" -m "$mb" -o "$work/pkg1" > "$work/pkg1.log" 2>&1 &&
   sh "$here/build-package.sh" -m "$mb" -o "$work/pkg2" > "$work/pkg2.log" 2>&1 &&
   cmp -s "$work/pkg1/syndicatfx.chimeraCore" "$work/pkg2/syndicatfx.chimeraCore"; then
	report "package" PASS "$(grep 'package sha1' "$work/pkg1.log"), the same twice"
else
	report "package" FAIL "see build/gate/pkg*.log"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
