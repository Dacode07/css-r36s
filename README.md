# Counter-Strike: Source — R36S / ArkOS loader

A native aarch64 so-loader that runs the Android Source Engine `.so` modules on
RK3326 handhelds (R36S) under ArkOS. No emulator: the engine runs as real ARM64
code and renders on the Mali GPU through a GLES pipeline.

This repository contains **only the loader/wrapper source**. It does not include
Counter-Strike: Source game data or the engine modules — you must provide those
yourself from a copy you own.

## 📖 Full guide & download

Installation, controls, troubleshooting and the ready-to-use package live on the
**R36S Wiki**:

### 👉 https://r36swiki.com/wiki-css.html

The R36S Wiki also covers many other native ports, plus performance, firmware
and hardware guides for the R36S and its clones.

## What it does
- Linux mmap ELF loader for the Android `.so` modules (`so_util`)
- First-run setup: unpacks the engine and game APKs itself (`setup`, `zipx`)
  behind an SDL/GLES progress screen — no shell tooling needed
- Bionic → glibc shims (libc, pthread futex-based mutex/cond/sem)
- Fake `dlopen` / JNI / EGL environment (`dl_emu`, `jni_fake`)
- SDL2 / KMSDRM + GLES: gamepad input, on-screen keyboard, mouse-cursor mode,
  gamma/brightness pass, shader-binary cache
- Optional controller aim assist

## Controls

| Button        | Action                                   |
|---------------|------------------------------------------|
| Left Stick    | Move                                     |
| Right Stick   | Look / Aim                               |
| R1            | Fire                                     |
| R2            | Secondary fire / Scope                   |
| L1            | Aim assist (hold — soft lock onto bots)  |
| L2            | Reload                                   |
| Y             | Switch weapon                            |
| X             | Buy menu                                 |
| L1 + X        | Autobuy                                  |
| L1 + Y        | Rebuy                                    |
| B             | Jump                                     |
| A             | Use                                      |
| L3            | Crouch                                   |
| R3            | Toggle mouse cursor (menus / buy)        |
| D-Pad Up      | Primary weapon slot                      |
| D-Pad Down    | Pistol slot                              |
| D-Pad Left    | Drop weapon                              |
| D-Pad Right   | Scoreboard (TAB)                         |
| Start         | Game menu                                |
| Select        | On-screen keyboard                       |

In mouse-cursor mode (R3): the Right Stick moves the cursor and R1 is left-click,
so you can navigate the buy menu and menus. Keys with no menu action fall through
to their in-game function.

Button ids, aim-assist FOV/speed, brightness and resolution are all configurable
in `config.txt`.

## Build
Needs the Bootlin aarch64 glibc-2.31 toolchain (see the note at the top of
`build-arkos.sh`), then:

    ./build-arkos.sh

Produces the `css` loader binary.

## Logging
The loader writes everything to stderr, so the launch script can keep a single
log file. Engine spew is filtered (missing vgui materials, unsupported GL
extension probes, shader combo warnings, back-to-back duplicates) and the
loader's own tracing is off by default. Set `debug 1` in `config.txt` for the
unfiltered log — that also passes `-dev 2` to the engine.

## Layout
    src/            loader source
    build-arkos.sh  cross-compile script

## Credits
Built on [nillerusr's Source Engine](https://github.com/nillerusr/source-engine).
Not affiliated with Valve. You must own Counter-Strike: Source.

## License
MIT — see `LICENSE`.
