# MPS Recomp

A native Windows port of **Mario Party Superstars** (v1.0.0, title 01006FE013472000), made by **static
recompilation**. The game's AArch64 code (its executable and its 126 NROs) is translated to C ahead of time and
compiled for your machine. A native runtime stands in for the Switch's system services, and the game's graphics run
on Vulkan. Nothing is emulated at run time.

On top of the original game it adds:

- **Internal resolution up to 4x**, SMAA, and FSR 1 when upscaling
- **Button icons that follow your controller**: Xbox, PlayStation or Nintendo, picked automatically or in the menu
- **No keys or firmware needed**: setup reads the game files you extracted yourself

> **Offline only.** Only the offline game works: online play and local wireless don't (see [Status](#status)).

> **Very heavily AI-assisted.** Most of this project's code, tools and documentation were written with AI coding
> assistants, under a human's direction and tested by hand on the real game.

> **You need your own copy of the game.** This repository and its downloads contain no code, assets, text, keys or
> recompiled output from Mario Party Superstars. You point the program at your own extracted game files, and
> everything made from them is generated on your machine.

## Play it

A download is a **launcher plus the recompiler**, not the game: on first start it takes your game files, checks
them, recompiles the game code on your computer, and then runs it.

1. **Get the package** `MPSRecomp-windows-x86_64.zip` (or build it, see [Building](#building)).

   | System | Package | Also needs |
   |---|---|---|
   | Windows 10 / 11, x64 | `MPSRecomp-windows-x86_64.zip` | a CPU with x86-64-v2 (AVX2 used when available), a Vulkan 1.3 graphics driver |

2. **Unpack it and start `mpsrecomp.exe`.** Windows keeps everything in the unpacked folder, so put it somewhere
   writable (Documents or Desktop, not Program Files).
3. **Point it at your game files**: one folder with the **ExeFS and RomFS of version 1.0.0** (no update), extracted
   from your own copy. NSP and XCI files aren't read, so no keys or firmware are needed. The version is checked
   against the known 1.0.0 files (build ids and hashes of your own files).
4. **Set up and play.** The first time it lays out your files and recompiles the game code with the bundled Python
   and compiler: a few minutes to half an hour. Then the game starts, and later starts go straight to it.

**If something goes wrong,** attach the log files: `mpsrecomp.log` next to `mpsrecomp.exe` (the program) and
`logs\setup.log` in the data folder (the setup's steps). After a crash, the next start keeps the crashed session's log
as `mpsrecomp.previous.log`: attach that one. The data folder holds your game files, saves, settings and caches:

| Started from | Data folder |
|---|---|
| the package | the unpacked folder, next to `mpsrecomp.exe` |
| a folder that has `extracted\` in it (a source checkout, or a shortcut whose "Start in" is such a folder) | that folder |
| anywhere, with `LARECOMP_DATA_ROOT=<folder>` set | that folder |

To update, unpack the new release over the old folder (replace the files): your game files, saves and settings stay.

## Console fonts (optional)

The game asks the console for its system fonts, which aren't part of the game files. Without them it uses Windows
fonts instead: Yu Gothic; Microsoft YaHei, Microsoft JhengHei and Malgun Gothic for Chinese and Korean; Arial when the
game plays in Russian. The game draws its menus with its own fonts in every language, so the difference is small.

If you have the fonts from your own console, put them in a `fonts\` folder inside the data folder (for the package:
`fonts\` next to `mpsrecomp.exe`), named as on the console:

| File | Font |
|---|---|
| `nintendo_udsg-r_std_003.bfttf` | Standard |
| `nintendo_udsg-r_org_zh-cn_003.bfttf` | Chinese (Simplified) |
| `nintendo_udsg-r_ext_zh-cn_003.bfttf` | Chinese (Simplified), extension |
| `nintendo_udjxh-db_zh-tw_003.bfttf` | Chinese (Traditional) |
| `nintendo_udsg-r_ko_003.bfttf` | Korean |
| `nintendo_ext_003.bfttf` | Nintendo button symbols |

A plain `.ttf` with the same name works too. The launcher doesn't need them and doesn't copy them: the game reads them
from there at every start (`runtime/hle/pl.c`). `LARECOMP_FONT=<file>` (.ttf / .otf / .ttc / .bfttf) replaces the
Standard font.

## Troubleshooting

- **"vulkan-1.dll was not found", or "needs a graphics driver with Vulkan 1.3".** The game draws with Vulkan, which
  comes with your graphics driver. Install the latest driver from [NVIDIA](https://www.nvidia.com/drivers),
  [AMD](https://www.amd.com/support) or [Intel](https://www.intel.com/support) (the one from Windows Update or the PC
  maker is often too old). Remote Desktop and most virtual machines have no Vulkan driver: start the game on the PC
  itself.
- **Setup says "a packed game".** It reads extracted folders only: extract the game's ExeFS and RomFS into one folder
  (see [Play it](#play-it)) and choose that folder.
- **Setup says a file "is not the v1.0.0 main" (or sdk, subsdk...).** Those files come from an update: extract the
  base game without its update.
- **Where are the settings?** In the overlay menu: F1 or Esc (a controller: the Home / Guide button, or − and +
  together). Video, audio, controls, button icons and game files are there.
- **Anything else:** look at `mpsrecomp.log` (and `mpsrecomp.previous.log` after a crash), see above.

## Status

**Early development.** Windows only for now, and only the offline game works.

| Platform | Graphics | Status |
|---|---|---|
| **Windows** (x64) | Vulkan | Boots and plays offline: menus and boards. The player package sets up from an empty folder, recompiles the game code with its own Python and compiler, and runs. |
| **macOS, iOS, Linux, Android** | Metal / Vulkan | The code paths come from the upstream projects; not built or tested for this game. |

| Feature | Status |
|---|---|
| Title, menus, boards (offline) | Working |
| Button icons for your controller (Xbox / PlayStation / Nintendo) | Working: automatic, or set in the menu |
| Players 2-4 | Each further controller is the next player; not tested with this game yet |
| Online play | Not supported (the game also asks for an update it doesn't have) |
| Local wireless | Not supported |
| Shaders that store to images (SUST) | Two are skipped; no visible difference found so far |

## How it works

1. **Recompile.** The recompiler (`tools/`, Python) reads the game's executable and NROs from your files and
   translates every function to C.
2. **Build.** That C is compiled together with the runtime (`runtime/`, C, C++ and Objective-C), which implements the
   Switch's kernel, system services, audio, input and the NVN graphics API on Vulkan. The game's NROs are loaded the
   way the console loads them (nn::ro).
3. **Play.** The result is an ordinary native program that loads the game's assets straight from your files.

`games/mps/` holds what is specific to this game: its identity, the known 1.0.0 files, setup, the HLE list, the
launcher's text and the button icons.

This project is derived from [SMM2 Recomp](https://github.com/smm2recomp/smm2recomp) (GPL-3.0-or-later), whose
recompiler is a fork of [LAS Recomp](https://github.com/peiikai/LASRecomp) (MIT).

## Building

### What you need

- Your extracted game files: the **ExeFS and RomFS of version 1.0.0**, in one folder
- WSL (Ubuntu) or Linux, with `make`, `curl` and Python 3.12 or newer: the runtime is cross-compiled for Windows
  with zig
- About 2 GB of free disk space

### Windows (cross-built from WSL / Linux)

```sh
tools/fetch_zig.sh && tools/fetch_deps.sh windows-x86_64                 # once: zig, SDL3 and shaderc for Windows
make TARGET=windows-x86_64 DEPS=build/deps-windows-x86_64 all            # runtime (+ game code if generated/ exists)
make venv && make mps DUMP=<folder with exefs/ and romfs/>              # setup + gen + build from your files
```

The game code can also be built on the player's machine by the setup screen (`runtime/gamebuild.c`), as the package
does. A source checkout started in a folder that has `extracted/` uses that folder as its data folder.

**Controls:** a controller works out of the box (Nintendo layout by position). On the keyboard:

| Keys | Switch button |
|---|---|
| WASD | left stick |
| numpad 8 / 4 / 5 / 6 | right stick |
| arrow keys | D-pad |
| K / J / L / I | A / B / X / Y |
| Q / E | L / R |
| 1 / 3 | ZL / ZR |
| F / numpad 0 | left / right stick click |
| Enter / Backspace | + / − |
| F1 or Esc | settings menu |
| F11 | full screen |

### The player package

```sh
tools/fetch_python.sh windows-x86_64      # the Python the package brings along (needs a Python with pip)
make TARGET=windows-x86_64 DEPS=build/deps-windows-x86_64 package       # dist/MPSRecomp-windows-x86_64.zip
```

The package is checked for anything from the game before it is archived (`scripts/check_package.py`). Without a `v*`
git tag its version is 0.1.0.

## Repository layout

| Path | Contents |
|---|---|
| `runtime/` | the kernel, IPC, system services, HLE, NVN on Vulkan / Metal, the setup screen and the overlay menu |
| `tools/` | the recompiler and the packaging scripts |
| `games/mps/` | what is specific to Mario Party Superstars |
| `third_party/` | libraries with their own licenses |
| `scripts/` | the checks for proprietary files |
| `docs/branding/` | the program's icon |

## Contributing

```sh
scripts/install-hooks.sh     # pre-commit check that blocks proprietary files
```

Never commit game files or anything generated from them. Generated output stays in the gitignored `private/`,
`generated/`, `build/` and `dist/`. `scripts/check_no_proprietary.py` runs in the pre-commit hook to enforce this.

## Legal

This is an unofficial fan project, not affiliated with or endorsed by Nintendo. Mario Party Superstars and related
names are trademarks of Nintendo. No Nintendo code or assets are distributed here. You need a legally obtained copy
of the game, and you build everything from it yourself.

## License

MPS Recomp is licensed under the [GNU General Public License v3.0 or later](LICENSE).

It includes code under other licenses, which keep their own terms:

- it is derived from [SMM2 Recomp](https://github.com/smm2recomp/smm2recomp) (GPL-3.0-or-later), whose recompiler
  started as a fork of [LAS Recomp](https://github.com/peiikai/LASRecomp) (MIT, [LICENSE-LASRecomp](LICENSE-LASRecomp))
- the third-party libraries in `third_party/` each have their own license file (Dear ImGui, SMAA, bcdec, Mbed TLS and
  others)

[UPSTREAM.md](UPSTREAM.md) lists each upstream project and its license.

The license covers this project's own code only. It grants no rights to Nintendo's games, code or assets, or to
anything generated from your files.
