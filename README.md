# chimera-core-syndicatfx

[SyndicatFX](https://github.com/swfans/syndicatfx), the port of Syndicate (Bullfrog, DOS, 1993) built on
the original game's code, as a [Chimera](https://github.com/ToolAssisted-run/chimera) **game core**
(`"kind": "game"`, see Chimera's `docs/game-cores.md`): the whole game - the menus, the world map, the
briefings, team selection and the missions - stepped one pass of its loop at a time in miniBox's sandbox,
packaged as `syndicatfx.chimeraCore`.

**Built on upstream SyndicatFX with four small patches**, and **translated, not rewritten**: SyndicatFX is
the original `MAIN.EXE` as i386 assembly plus a C library layer, which only builds for 32-bit x86, while
Chimera's cores are x86-64. The core builds SyndicatFX as one static i386 ELF (musl, and an SDL2 shim of its
own in place of SDL, and a small OpenAL of its own) and translates that ELF's machine code to portable C
(`xlat/elf2c.py`): every instruction over a 32-bit address space kept in guest memory, the game's own
pointers staying 32-bit. Nothing of the game is reimplemented by hand.

## What it is

- **Syndicate from the Syndicate Plus CD** (`SYNDICAT\DATA`, the base game; American Revolt is not in
  SyndicatFX). The package carries none of the game's data: its 438 files are the project's **firmware**.
  A missing one is named. A file of your own - a modified one - may take an original's place: the core
  takes it as it is, and the project pins its hash. The original sprites are used (`MSPR-0.DAT`), not
  SyndicatFX's fan pack.
- **The same game as the original, turn by turn**: the level block (the seed, the people, vehicles,
  weapons, effects, commands, objectives) matches the DOS executable running in DOSBox-X at every turn of
  the runs in `tests/oracle/README.md` - 3,851 turns without input and a 2,120-turn firefight.
- **A step is one pass of the game's loop** (SyndicatFX's frame, 1/16 s): a mission turn, a menu's pass,
  a frame of an animation. The step ends where the game reads its input. The clock is virtual: the game's
  logic never reads it, and a wait that does (the animations' frame delay) sees 1 ms go by at each look,
  so a step that waits lasts as long as the wait (its length is reported per step).
- **The controls are the mouse and the keyboard**: the pointer is two axes over the picture shown (320x200
  in the menus, 640x480 in a mission), with the left and right buttons; the keys are the PC keyboard's
  (letters and digits for the company and agent names and the agent keys, F1-F12, arrows, Esc, Enter,
  Space and the rest).
- **Settings**: the language (English, French, Italian - the original's `-c`): the menus from SyndicatFX's
  translations, the briefings from the CD's own sets. The sound card: the Sound Blaster (the default, the
  only card the game supports) or none (the original's `-s`).
- **Sound**: the Sound Blaster's, rendered at 44100 Hz for exactly the time each step covers.
  - The digitized sounds, one at a time as the card played them (patches/0003). SyndicatFX's bfsoundlib
    plays them through OpenAL; the core's OpenAL (`i386/oal.c`) is a mixer inside the translated program
    that plays its sources only when a step ends, so the sound is part of the machine's state.
  - The FM music is the original's own: its music driver - `GAMEFM.DLL`, AIL/32's Ad Lib driver, from the
    game's files - loaded and run as it is by an interpreter in the translation runtime
    (`xlat/xl_interp.c`), on a C port of the AIL/32 layer `MAIN.EXE` drives it with (`i386/ail32.c`,
    patches/0004). Its timer interrupt runs at the PIT's own period (120 Hz), each at its time; the chip is
    Nuked OPL3 in OPL2 mode, every write rendered where it happened. SyndicatFX itself has no path to this
    music (its OpenAL build needs WildMIDI and plays other instruments). The register writes are the
    original's, write for write (`tests/oracle/README.md`).
  - The music is not only sound: when a mission is won or lost, the game plays a song and ends the mission
    when it has finished (0x10D9E), so the music decides how many turns that takes. The game is otherwise
    the same with sound and without: the oracle runs match either way.
- **Memory**: `Level` (the level block, in place), `Arena` (the translated program's whole 64 MiB address
  space) and a small `Game State` block (steps, turns, mission, ended). **Properties** by name: the level's
  random seed and timer, the steps, turns and mission.

## The patches

- `patches/0001`: SyndicatFX's C remake of `SetBFSampleStatus` dropped the original's check for a missing
  sample table (0x387E0 in `MAIN.EXE`), so a game without sound crashed when the first mission started.
- `patches/0002`: the core's hooks - no pacing to the wall clock (the core steps the game), the flag that
  makes `game_update`'s input read the step boundary, the game loop's turn hook, and no call of the
  separate intro program.
- `patches/0003`: SyndicatFX plays every sound the game asks for at once, over a mixer; the original's
  card driver (AIL 2) plays one digitized sample at a time - a new one replaces what plays
  (`AIL_play_VOC_file`), and the game's sound priorities (`BFSonundUnkn1`, 0x388F0) hold only until the
  card is done. The patch restores that.
- `patches/0004`: the music as the original plays it (above): `InitMIDI` and the `BFMidi*` functions as
  `ASM_InitMIDI` (0x3B5B0) and the routines after it, on the core's AIL/32 layer and the game's driver.

## Building

```
git submodule update --init
pip install capstone pyelftools polib
make -C waterbox -f native.mk -j$(nproc)    # the native reference, the harnesses, sfx-run
make -C waterbox -f guest.mk -j$(nproc)     # core.wbx
./waterbox/build-package.sh                 # build/package/syndicatfx.chimeraCore
```

The first build also sets up the rootless i386 toolchain (`tools/setup-i386-toolchain.sh`: Ubuntu's 32-bit
libgcc and a static musl, no root and no multilib install needed), builds SyndicatFX for i386 (`i386/`) and
translates it (`tools/translate.sh`, about 390 thousand lines of C). miniBox is taken from `MB=`/
`MINIBOX_DIR`, else `~/chimera/extern/chimera-common-minibox`, with its guest toolchain built.

## The gate

`./waterbox/run-gate.sh [-d <SYNDICAT\DATA dir>]` (or the files in `tests/roms-local`): the build, the
declarations, the refusal of a project without the game's files, a file of the project's own in an
original's place, native == sandbox over a run into the first mission (with teeth), a savestate
before every step, a new host in the middle, turbo, the sound (heard; none without a card, and the same
game), the property table with a poke and a freeze, and a deterministic package.

## Where things are

- `xlat/`: the translator (`elf2c.py`), its runtime (`xlat.h`, `xlat_rt.c`: flags, x87, dispatch), the
  interpreter for code the program loads at run time (`xl_interp.c`: the music driver) and a native test
  runner for any static i386 musl program (`xlrun.c`).
- `i386/`: the i386 build of SyndicatFX: the SDL2 shim, the host calls, the OpenAL mixer (`oal.c`), the
  AIL/32 layer (`ail32.c`), the vorbisfile stubs, the C++ header shim, the config headers.
- `waterbox/`: the machine (`sfx-machine.c`: the arena, the Linux i386 syscalls over an in-memory file
  system, the coroutine the game runs on, the step boundary, the PIT and the FM chip; `opl3.c` is Nuked
  OPL3), the driver, the exports, the domains and
  properties, the tables (`tables.py` makes `waterbox.config`, the keybinds and `sfx-tables.h`), the
  harnesses and the gate.
- `tests/`: movies and step scripts, `sfx-run.c`, and the oracle procedure.

## Licence

This repository is GPL-3.0-or-later. SyndicatFX's C sources are GPL-3.0-or-later; **its `src/syndre.sx`
is Bullfrog's own `MAIN.EXE`, disassembled, which carries no licence** - SyndicatFX distributes it in good
faith for owners of the game and will take it down if the copyright holders object, and a package of this
core carries that code, translated, the same way (`waterbox/package-licenses.json`). musl libc is MIT;
Nuked OPL3 (`waterbox/opl3.c`) is LGPL-2.1-or-later.
