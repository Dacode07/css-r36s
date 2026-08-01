# Counter-Strike: Source — R36S / ArkOS loader

A native aarch64 so-loader that runs the Android Source Engine `.so` modules on
RK3326 handhelds (R36S) under ArkOS. No emulator: the engine runs as real ARM64
code and renders on the Mali GPU through a GLES pipeline.

This repository contains **only the loader/wrapper source**. It does not include
Counter-Strike: Source game data or the engine modules — you must provide those
yourself from a copy you own.

## What it does
- Linux mmap ELF loader for the Android `.so` modules (`so_util`)
- Bionic → glibc shims (libc, pthread futex-based mutex/cond/sem)
- Fake `dlopen` / JNI / EGL environment (`dl_emu`, `jni_fake`)
- SDL2 / KMSDRM + GLES: gamepad input, on-screen keyboard, mouse-cursor mode,
  gamma/brightness pass, shader-binary cache
- Optional controller aim assist

## Build
Needs the Bootlin aarch64 glibc-2.31 toolchain (see the note at the top of
`build-arkos.sh`), then:

    ./build-arkos.sh

Produces the `css` loader binary.

## Layout
    src/            loader source
    build-arkos.sh  cross-compile script

## License
MIT — see `LICENSE`.
