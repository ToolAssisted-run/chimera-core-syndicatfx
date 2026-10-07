# AGENTS.md - SyndicatFX core for Chimera

This repository builds SyndicatFX, the port of Syndicate built on the original
game's code, as a GAME core (one game built as a core, not an emulator) for
Chimera (https://github.com/ToolAssisted-run/chimera), a frontend for
tool-assisted speedruns. It produces one file, `syndicatfx.chimeraCore`.
SyndicatFX only builds for 32-bit x86, so the build makes it as a static i386
program, translates that program's machine code to C, and compiles the C as a
sandboxed guest (`core.wbx`). The game's own files are never in the repository
or the package: the user provides them.

## Layout

- `extern/syndicatfx`: upstream SyndicatFX, a submodule. Never edited in place.
- `patches/`: the four numbered patches for `extern/syndicatfx`, applied by
  `tools/apply-patches.sh` (in `tools/`, not `waterbox/`).
- `tools/setup-i386-toolchain.sh`: fetches and builds the i386 toolchain into
  `build/i386tc`.
- `i386/`: the i386 build of SyndicatFX (its `Makefile`, the SDL2 shim, the
  OpenAL mixer, the AIL/32 layer).
- `xlat/`: the translator (`elf2c.py`) and its runtime. `tools/translate.sh`
  runs it into `build/xlat`.
- `waterbox/sources.mk`: the stages and sources both builds share.
- `waterbox/guest.mk`: builds `build/guest/core.wbx` (the sandboxed core).
- `waterbox/native.mk`: builds `build/native/run-native`, `run-wbx` and
  `sfx-run` (the native reference and the harnesses).
- `waterbox/sfx-machine.c`: the machine. `syndicatfx-driver.c`, `wbx-entry.c`:
  the driver and the guest's exports.
- `waterbox/tables.py`: GENERATES `waterbox/waterbox.config`,
  `default_keybinds.json` and `sfx-tables.h`. `file_slots.json`: no slots.
- `waterbox/build-package.sh`: builds the package. `run-gate.sh`: the gate.
- `tests/`: movies, step scripts, `sfx-run.c`, `oracle/README.md`.
- `.github/workflows/chimera.yml`: CI. It gates, packages and publishes.
- `build/`: every output. Ignored by git.

## Set up the build environment

Linux only (CI: `ubuntu-latest`). `<chimera>` is a Chimera checkout.

```
sudo apt-get update
sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev python3-pip
pip install --break-system-packages capstone pyelftools polib

git submodule update --init --recursive
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>

# Chimera itself, for the contract tests. The dotnet line needs the .NET SDK 8.0.
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false

# miniBox: the sandbox host and the guest toolchain
mb=<chimera>/extern/chimera-common-minibox
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

The scripts take miniBox from `-m <dir>` (or `MB=<dir>` for `make`), else
`MINIBOX_DIR`, else `~/chimera/extern/chimera-common-minibox`. The first build
downloads into `build/i386tc` (musl 1.2.5, SDL2 and OpenAL Soft headers,
Ubuntu's 32-bit libgcc if the system has none), each file checked by SHA-256:
it needs `curl` and network access, once.

## Build

From the repository root:

```
make -C waterbox -f guest.mk MB=<chimera>/extern/chimera-common-minibox -j"$(nproc)"
./waterbox/build-package.sh -r <chimera>
```

The `make` line applies the patches, sets up the i386 toolchain, builds the
i386 program, translates it and builds `build/guest/core.wbx`;
`build-package.sh` runs the same line itself. The same line with
`-f native.mk` builds the native reference and the harnesses.

`-r <chimera>` writes `<chimera>/build/Cores/syndicatfx.chimeraCore` and uses
that checkout's miniBox. `-o <dir>` writes `<dir>/syndicatfx.chimeraCore`.
With neither the package is `build/package/syndicatfx.chimeraCore`.
`-m <dir>` names miniBox. A hand-built package stamps its version
`<commit>+local` (`-dirty` when the tree has changes) and is for testing. CI
stamps the commit through `CORE_VERSION` and publishes the releases.

## Install the core into Chimera

Chimera ships no cores and downloads nothing: a core is a file in its cores
folder. In a source checkout that is `<chimera>/build/Cores/`, where
`build-package.sh -r <chimera>` writes. In a release bundle it is the `Cores`
folder beside `Chimera.exe`, or the one chosen in File > Core Manager >
Change folder... File > Core Manager lists the folder; Refresh List rescans
it. The same package works on Linux and on Windows.

## Test before you commit

```
MINIBOX_DIR=<chimera>/extern/chimera-common-minibox ./waterbox/run-gate.sh
```

The last line must read `0 failed`. Without the game's files only the build,
the declarations and the refusal of a project with no files run; the rest is
one `SKIP` line. That is all CI can run. The checks that run the game need the
user's `SYNDICAT\DATA` folder (`-d <dir>`, or `SYNDICATE_DIR`, or the files in
`tests/roms-local`). They are run where the game's files are, before anything
is pushed. `-q` skips the build.

Then, with the package in `<chimera>/build/Cores`, Chimera's contract tests:

```
cd <chimera>
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

## Rules of this repository

- `extern/syndicatfx` is a submodule. A change to it is a numbered patch in
  `patches/`, applied by `tools/apply-patches.sh`. Never commit inside the
  submodule. The script applies the series to a clean tree and takes a tree
  with any change to have it already. So after changing a patch, return the
  submodule to its pinned, clean state and delete `build/patches.stamp`.
- Nothing of the game is rewritten by hand: it is upstream's code, translated.
  Fix the translator, the runtime or a patch, not the C in `build/xlat`.
- `waterbox.config`, `default_keybinds.json` and `sfx-tables.h` are generated.
  Edit `waterbox/tables.py`, run `python3 waterbox/tables.py`, commit all of
  it. The gate runs `tables.py` and fails when the committed files differ.
- Determinism is the product. The guest must not read host time, host
  randomness or anything else that differs between runs, and a savestate must
  round-trip. The gate checks it when the game's files are there.
- Run the gate before committing. A new check needs a negative control: show
  it fails when the thing it checks is broken (the gate's `equivalence:teeth`
  is the model; Chimera's `docs/gates.md` says why).
- Never commit game files. `tests/roms-local/` is ignored by git. Never add
  network access to the core.
- Shell scripts stay executable (git mode 100755): `waterbox/*.sh`, and
  `tools/{apply-patches,setup-i386-toolchain,translate,menu-script}.sh`.
- Documentation prose is plain ASCII.
- Commit messages: the subject states what the core now does, sometimes after
  a topic and a colon ("Music: the original's own FM music driver, run as it
  is"). The body says what changed, why, how it was checked, the gate's count.
- Do not edit `.github/workflows` unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md`: every stage and option, the files a user provides,
  troubleshooting.
- `README.md`: what the core is, the patches, where things are.
- `tests/oracle/README.md`: how the core is compared with the original game.
- The header comments of the scripts and makefiles in `waterbox/` and `tools/`.
- Chimera's `docs/`: `game-cores.md`, `porting-a-core.md`, `core-manager.md`,
  `gates.md`.
