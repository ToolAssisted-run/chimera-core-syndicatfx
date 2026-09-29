# Checking the core against the original game

The gate holds the sandboxed core to the native reference. This procedure holds the core to the
**original**: Syndicate's DOS `MAIN.EXE` (Syndicate Plus CD, `SYNDICAT\MAIN.EXE`, md5 67111e44...)
running in DOSBox-X, the Chimera DOSBox-X core's scripted runner (`oracle-run`, branch `pop2-tracer`:
`mouse`/`button` script commands and `probe32`, a full-EIP probe for DOS/4GW flat code).

## What is compared

The **level block**: the 116,010 bytes the game loads each mission from `GAMExx.DAT` and runs every turn
on (0x80108..0x9C632 in `MAIN.EXE`: the random seed, the map of who stands where, the people, vehicles,
objects, weapons, effects, commands and objectives), at the head of the game loop (0x10214), every turn.

- The oracle: a `probe32` at the loop head's linear address (DOS/4GW loads the code object at physical
  0x1C4030, the level block's object at 0x218030) samples the block each turn.
- The core: `build/native/sfx-run --turns FILE` writes the same block at the same point (the
  `core_game_turn` hook, patches/0002), in the same format.

## The runs (2026-09-29)

| Mission 1, Western Europe | Turns | Result |
|---|---|---|
| No input after entering (`tests/scripts/idle-m1.script`) | 3,851 (3 in-game day rollovers) | identical |
| Select all, arm pistols, assault the base (`tests/scripts/combat-m1.script`) | 2,120 (a firefight, 3,680 random draws) | identical |

Both runs match with the Sound Blaster (`sfx-run --sound`, the core's default: sounds and music) and
without a card alike.

## The music

The FM chip's register writes: the original with its sound on (`main /c0`, the Sound Blaster at its
defaults, as DOSBox-X's SB16) through `tests/scripts/idle-m1.script`'s route into mission 1, logged by
`oracle-run --fmtrace PATH` (every byte written to the chip's ports, with the emulated time), against
`sfx-run --sound --fmtrace PATH` (every register write, with its PIT clock), by `tools/cmp_fm.py`:

| Mission 1, 4,000 oracle frames | Writes | Result |
|---|---|---|
| The driver's detection of the chip, its initialization, the timbres and song 1's notes | 1,565 | identical, in order |
| Their times after the first key-on, relative to it | 1,302 | within 1.1 ms (the timer's phase; its period is 8.33 ms) |

The menus before a mission advance the game's clock once per pass of the briefing and team-selection
screens (`process_day`), so a run matches the oracle only with the same number of passes there:
50 on the world map, 34 in the briefing, 27 in team selection for the oracle scripts. A build translated
with `HOOKS=process_day tools/translate.sh` and `sfx-run --pday` prints them.

Input timing: an oracle mouse event at frame F lands in the game-loop iteration after the loop head
before F; the core's step s delivers its input at the start of step s, so the same iteration is step
(first turn's step) + iteration. `tests/scripts/*.script` are already mapped.

Before the core existed, SyndicatFX's own i386 build (autotools, SDL2, with a harness hooking the same
loop head) passed the same two runs against the oracle; the core's translation was then compared to the
oracle directly. The one difference between SyndicatFX and the original found on the way is
patches/0001: its C `SetBFSampleStatus` lost the original's check for a missing sample table, which
crashes a game started without sound (`-s`) when the first mission begins.
