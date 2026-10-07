# Building the SyndicatFX core

This repository builds SyndicatFX, the port of Syndicate built on the original
game's code, as a Chimera game core: one game built as a core, not an emulator
(Chimera's `docs/game-cores.md`). The result is one file,
`syndicatfx.chimeraCore`, which Chimera loads. The steps below follow
`.github/workflows/chimera.yml`, which builds and gates the core from a fresh
clone on a public Ubuntu runner.

The build is unusual in one way. SyndicatFX only builds for 32-bit x86, and
Chimera's cores are x86-64. So the build first makes SyndicatFX as one static
i386 program, then translates that program's machine code to C
(`xlat/elf2c.py`), and compiles the C as the core. The makefiles do all of it.

Placeholders used below:

- `<core>`: the checkout of this repository.
- `<chimera>`: a checkout of Chimera (https://github.com/ToolAssisted-run/chimera).
- `<minibox>`: `<chimera>/extern/chimera-common-minibox`, Chimera's miniBox
  submodule (the sandbox host and the guest toolchain).

## Requirements

- Linux. CI runs on GitHub's `ubuntu-latest` runner. Cores are built on Linux;
  the package they produce runs on Linux and on Windows.
- The apt packages the workflow installs:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential cmake pkg-config python3 mono-complete xvfb libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev python3-pip
  ```

- Three Python packages, which the workflow installs with pip: `capstone` and
  `pyelftools` for the translator, `polib` for SyndicatFX's text tool.

  ```
  pip install --break-system-packages capstone pyelftools polib
  ```

  Another Python that has them can be named with `PY=<python>` on the `make`
  command line (`waterbox/native.mk`).
- The .NET SDK 8.0 (the workflow uses `actions/setup-dotnet@v4` with
  `dotnet-version: '8.0'`). It is needed to build the Chimera solution and to
  run Chimera's contract tests, not to build the package. Chimera's README
  installs it with
  `curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`.
- The compiler is the system's `gcc` (from `build-essential`). The workflow
  pins no compiler version. The same `gcc` compiles the i386 program (`-m32`),
  the native reference, and the guest over miniBox's guest sysroot.
- No Rust toolchain.
- What the build fetches and builds itself, on the first build, into
  `build/i386tc` (`tools/setup-i386-toolchain.sh`; no root, no multilib
  install):
  - the 32-bit `libgcc.a`: copied from the system if it is there, else taken
    out of Ubuntu's `lib32gcc-<N>-dev` package (`apt-get download`,
    `dpkg-deb -x`), `<N>` being the major version of `gcc`;
  - musl 1.2.5, downloaded from musl.libc.org and built as a static i386 libc;
  - headers only of SDL2 2.30.12 and OpenAL Soft 1.23.1, downloaded from their
    GitHub releases.

  Each download is checked against a SHA-256 the script holds. The script
  uses `curl`, `sha256sum`, `tar`, `apt-get download` and `dpkg-deb`, and so
  the first build needs network access. It is idempotent: what is already in
  `build/i386tc` is kept.

## Get the sources

The workflow checks out both repositories with their submodules, recursively
(`actions/checkout@v6`, `submodules: recursive`), and takes Chimera at `main`.
By hand:

```
git clone --recursive https://github.com/ToolAssisted-run/chimera-core-syndicatfx.git <core>
git clone --recursive https://github.com/ToolAssisted-run/chimera.git <chimera>
```

In a clone made without `--recursive`:

```
git -C <core> submodule update --init --recursive
git -C <chimera> submodule update --init --recursive
```

This repository has one submodule, `extern/syndicatfx` (upstream SyndicatFX).

CI puts the Chimera checkout at `<core>/chimera-checkout`. Any place works:
the scripts find miniBox in this order.

1. `-m <miniBox dir>` on `run-gate.sh` and `build-package.sh`, or `MB=<dir>`
   on the `make` command line.
2. The `MINIBOX_DIR` environment variable (what CI sets).
3. For `build-package.sh -r <chimera>`: `<chimera>/extern/chimera-common-minibox`.
4. `~/chimera/extern/chimera-common-minibox`.

## Build miniBox

The workflow builds Chimera itself first, then miniBox. Chimera's own build
gives the managed solution, which the contract tests use. The package does not
need it: `build-package.sh` uses only miniBox.

```
cd <chimera>
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
meson compile -C build/meson-linux
meson install -C build/meson-linux
dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false
```

Then miniBox: the host and the guest toolchain.

```
mb=<chimera>/extern/chimera-common-minibox
[ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
meson compile -C "$mb/build/meson-linux"
[ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
meson compile -C "$mb/build/meson-cpp"
```

- `build/meson-linux` holds the miniBox host library
  (`source/host/libminiboxhost.so`), which the `run-wbx` harness links.
- The guest is C. `waterbox/guest.mk` takes the guest sysroot from
  `build/meson-cpp` when that directory exists, else from `build/meson-linux`.
  The workflow builds both.

CI caches these two build directories (`actions/cache@v4`). By hand there is
nothing to do: the directories stay where they are, and the `[ -f ... ] ||`
guards skip `meson setup` when one is already configured.

## Build the core

`run-gate.sh` runs both builds itself. To build without gating:

```
cd <core>
make -C waterbox -f native.mk MB=<minibox> -j"$(nproc)"
make -C waterbox -f guest.mk MB=<minibox> -j"$(nproc)"
```

Either makefile, on its first run, goes through these stages
(`waterbox/sources.mk`):

1. **Patches.** `patches/` holds four numbered patches for
   `extern/syndicatfx`. `tools/apply-patches.sh` applies them
   (`build/patches.stamp` records that it ran). The script takes the series as
   a whole: a clean submodule tree gets every patch, and a tree with any
   change in it is taken to have the series already and is left alone. It does
   not check a changed tree against the series.
2. **The i386 toolchain.** `tools/setup-i386-toolchain.sh` fills
   `build/i386tc` (see Requirements).
3. **The i386 program.** `i386/Makefile` builds SyndicatFX, with the core's
   SDL2 shim, OpenAL mixer and AIL/32 layer, as one static i386 ELF,
   `build/i386/syndicatfx.elf`, and the menu texts
   (`build/i386/language/{eng,fre,ita}/guitext.dat`).
4. **The translation.** `tools/translate.sh` runs `xlat/elf2c.py` on the ELF
   and writes the generated C to `build/xlat` (it empties that directory
   first).
5. **The compile.** The generated C, the translation runtime (`xlat/`) and the
   core's own sources (`waterbox/`).

What comes out:

- **The guest core** (`guest.mk`): `build/guest/core.wbx`, built over
  miniBox's guest sysroot and checked by miniBox's `check-wbx.sh`.
- **The native reference** (`native.mk`): `build/native/run-native`,
  `build/native/run-wbx` and `build/native/sfx-run`. `run-native` is the same
  translated game and core sources built for the host with no sandbox.
  `run-wbx` drives `core.wbx` through the miniBox host as the frontend does.
  The gate compares the two step by step. `sfx-run` is the machine alone, for
  the comparison with the original game (`tests/oracle/README.md`). None of
  them is part of the package.

Each object depends on the flags it was built with: a change of flags rebuilds
it. `make -C waterbox -f guest.mk clean` and `make -C waterbox -f native.mk
clean` remove `build/guest` and `build/native`.

`waterbox/waterbox.config`, `waterbox/default_keybinds.json` and
`waterbox/sfx-tables.h` are generated by `waterbox/tables.py` and committed.
Change `tables.py` and run `python3 waterbox/tables.py`, never the three files.

## Build the package

```
cd <core>
./waterbox/build-package.sh -r <chimera>
```

Run it after the gate or after one of the `make` commands above, as CI does:
see Troubleshooting for the fresh-clone case.

`waterbox/build-package.sh [-m <miniBox dir>] [-r <chimera root>] [-o <out dir>]`:

- `-r <chimera root>` writes
  `<chimera root>/build/Cores/syndicatfx.chimeraCore` and takes miniBox from
  that checkout unless `-m` or `MINIBOX_DIR` names another. It also removes
  `<chimera root>/build/CoreCache/syndicatfx-*`. This is what CI runs
  (`-r chimera-checkout`).
- `-o <out dir>` writes `<out dir>/syndicatfx.chimeraCore` instead.
- With neither, the package is `<core>/build/package/syndicatfx.chimeraCore`.

The script builds the guest (`guest.mk`; the log is `build/package-make.log`),
checks `core.wbx`, and packs `core.wbx`, `waterbox.config`,
`default_keybinds.json`, `file_slots.json`, the licence texts and a
`build.json` that records what built the package. The packing is
deterministic: the script packs twice and stops if the two SHA-1s differ, then
prints `package sha1 <hash>`.

The version is the commit the package was built from.

- CI sets `CORE_VERSION` to the commit's full hash.
- By hand, with `CORE_VERSION` unset, the script stamps `<commit>+local`
  (twelve hex digits), or `<commit>-dirty+local` when the tree has changes.
  The patches applied inside `extern/syndicatfx` do not count as changes.

A hand-built package is for testing. Chimera's publish script refuses a
version that carries `+local` or `-dirty`.

CI publishes what its gate passed as this repository's own releases: a rolling
`dev` release on every green push to `main`, and a dated `nightly-YYYY-MM-DD`
release from the scheduled run (04:00 UTC, only when `main` moved since the
last one). The gate job uploads the package as the artifact
`syndicatfx-<commit>` (`actions/upload-artifact@v7`), and the publish job
hands it to Chimera's reusable workflow `publish-core.yml`. There is no manual
equivalent.

## Install it into Chimera

Chimera ships no cores and downloads nothing: it has no network code. A core
gets into Chimera because somebody put its package file in the cores folder.

- **A release bundle of Chimera**: download the core's `.chimeraCore` package
  from this repository's Releases page
  (https://github.com/ToolAssisted-run/chimera-core-syndicatfx/releases), or
  build it, and put it in the `Cores` folder beside `Chimera.exe`. Another
  folder can be chosen in File > Core Manager > Change folder...
- **A Chimera source checkout**: the cores folder is `<chimera>/build/Cores/`.
  `./waterbox/build-package.sh -r <chimera>` writes the package straight there.

File > Core Manager lists what is in the folder. Refresh List rescans it. The
same package file works on Linux and on Windows: the guest inside it is run by
Chimera's sandbox (miniBox) on either.

## Run the gates

### The core gate

```
cd <core>
MINIBOX_DIR=<minibox> ./waterbox/run-gate.sh
```

This is the command CI runs. The gate builds the native reference and
`core.wbx` first (logs: `build/gate/native-make.log`,
`build/gate/guest-make.log`), works in `build/gate` (emptied at each run),
prints a table of checks with `PASS`, `FAIL` or `SKIP`, and ends with
`N ok, N failed, N skipped`. It exits non-zero when a check failed.

Usage: `run-gate.sh [-q] [-m <miniBox dir>] [-d <game dir>]`

- `-d <game dir>` names the folder with the game's files (`SYNDICAT\DATA`).
  Without it the gate looks in the `SYNDICATE_DIR` environment variable's
  folder, else in `tests/roms-local` (ignored by git).
- `-q` skips the build and tests what is built.

Without the game's files, which is how CI runs it, these checks run:

- `build`: the native reference, the harnesses and `core.wbx` build.
- `stacks:map-stack`: the game's stack is taken from `mmap`.
- `declarations:wire`, `declarations:config`: `waterbox.config`, the keybinds
  and `sfx-tables.h` are what `tables.py` makes.
- `refusal:no-files`: a project without the game's files is refused, naming a
  missing file.

Every check that runs the game is then skipped, as one `runs SKIP` line. With
the game's files the gate goes on:

- `firmware:custom`: a file of the project's own is taken in an original's
  place.
- `equivalence` and `equivalence:teeth`: native == sandbox over 400 steps into
  the first mission (picture, sound, every step's length, the clock, every
  memory domain), and the same run without the movie differs.
- `savestate:rerecord`, `savestate:session`: a savestate before every step,
  and a run saved, loaded in a new host and finished there.
- `turbo`: half the run not drawn, the same machine after.
- `sound`: the Sound Blaster is heard; with no card there is no sound and the
  game is the same.
- `properties:table`, `properties:poke-freeze`: the property table holds to
  Chimera's `docs/game-cores.md`, a poke changes the run, a freeze holds.
- `package`: the package builds twice to the same bytes.

Note that the `declarations:wire` check runs `tables.py`, which rewrites the
three generated files in place. If `git status` shows them changed after a
gate run, they were out of date: commit the regenerated files.

The step-by-step comparison with the original game is not this gate's. It is a
separate procedure, described in `tests/oracle/README.md`.

### Chimera's contract tests

Run after the package is in `<chimera>/build/Cores`:

```
cd <chimera>
CHIMERA_CORES_DIR=<chimera>/build/Cores dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
  -c Release --nologo \
  --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

They run Chimera's own checks against this package: it is readable, it is
built for an ABI this frontend runs, it makes a working factory, it binds only
buttons its controller declares, and it stamps a version. They need the
Chimera solution built and no game files.

## Files the core needs at run time

The package carries none of the game's data. The user provides the game's
original files from the Syndicate Plus CD, folder `SYNDICAT\DATA` (the base
game), and a project brings them as firmware.

- `waterbox/waterbox.config` declares 438 firmware files, each by name, size
  and SHA-1: the game files SyndicatFX's installer takes from the CD, plus the
  original sprites `MSPR-0.DAT` and `MSPR-0.TAB` (`waterbox/tables.py`). They
  are `.DAT`, `.TAB`, `.ANI`, `.PAL`, `.AD`, `.OPL`, `.XMI` and `.DLL` files.
  One of them, `GAMEFM.DLL`, is the game's own music driver, which the core
  loads and runs.
- A missing file is refused, and named.
- A file of the user's own (a modified one) may take an original's place. The
  project pins its hash.
- `waterbox/file_slots.json` declares no file slots: a project needs no file
  of its own.

No BIOS and no other firmware is needed.

## Troubleshooting

- `miniBox not found at ...; pass -m <path> or set MINIBOX_DIR`
  (`build-package.sh`), or `miniBox not found; pass -m or set MINIBOX_DIR`
  (`run-gate.sh`): no miniBox was named and none is at
  `~/chimera/extern/chimera-common-minibox`.
- `extern/syndicatfx is not checked out (git submodule update --init)`
  (`tools/apply-patches.sh`): the clone was made without its submodule.
- `extern/syndicatfx already patched` when the series was not applied, or
  after a patch was changed: the script takes any change in the submodule's
  tree to be the whole series, and `build/patches.stamp` then says it ran.
  Return the submodule to its pinned, clean state, delete
  `build/patches.stamp`, and build again.
- A download in `tools/setup-i386-toolchain.sh` fails its `sha256sum -c`
  check: the file fetched is not the one the script expects. The downloads are
  kept in `build/i386tc/src`; remove the bad file and build again.
- The translator fails on `import capstone` or `import elftools`, or
  SyndicatFX's text tool on `import polib`: install the three Python packages
  (Requirements), or name a Python that has them with `PY=`.
- `build:fresh FAIL core.wbx is older than ...` in the gate: a source, a
  patch, or a file in `i386/` or `xlat/` is newer than `core.wbx`. Run the
  gate without `-q`, so that it rebuilds.
- `the guest build failed (build/package-make.log)` (`build-package.sh`): read
  that log. The gate's build logs are in `build/gate/`.
- `git status` shows `extern/syndicatfx` as modified after a build: the
  patches are applied in the submodule's working tree. That is expected, and
  it does not mark the package `-dirty`.
