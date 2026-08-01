#!/bin/bash
# Build the loader against an OLD glibc (2.31) so it runs on ArkOS/R36S.
#
# - Bootlin aarch64 glibc-2.31 toolchain, so the binary has no GLIBC_2.34/2.38
#   symbols (loads fine against the device's glibc 2.31).
# - SDL2 and zlib are only needed at link time for symbol resolution; there is
#   no aarch64 copy on the host. Solution: generate STUB .so files (empty
#   functions, correct SONAME) and link against those. The real
#   libSDL2-2.0.so.0 / libz.so.1 are bound by ld.so on the device.
#
# Toolchain install (once, skip if already present):
#   curl -o /tmp/tc.tar.bz2 https://toolchains.bootlin.com/downloads/releases/toolchains/aarch64/tarballs/aarch64--glibc--stable-2020.08-1.tar.bz2
#   sudo tar xf /tmp/tc.tar.bz2 -C /opt/toolchains/
set -e
cd "$(dirname "$0")"

TC=""
for d in /opt/toolchains/aarch64--glibc--stable-2020.08-1 /opt/aarch64--glibc--stable-2020.08-1; do
  [ -d "$d" ] && TC=$d && break
done
[ -n "$TC" ] || { echo "aarch64 toolchain not found (see the install note at the top)"; exit 1; }

CC=""
for c in "$TC"/bin/aarch64-buildroot-linux-gnu-gcc "$TC"/bin/aarch64-linux-gcc; do
  [ -x "$c" ] && CC=$c && break
done
[ -n "$CC" ] || { echo "toolchain gcc not found: $TC/bin"; exit 1; }
OBJDUMP=$(dirname "$CC")/$(basename "$CC" gcc)objdump

# ---------------------------------------------------------------------------
# stub libraries: empty SDL2 + zlib for link-time symbol resolution
# ---------------------------------------------------------------------------
EXTLIBS=/tmp/css_arkos_extlibs
mkdir -p "$EXTLIBS"

SDL_SYMS="SDL_AddEventWatch SDL_DelEventWatch SDL_CloseAudioDevice
SDL_CreateSystemCursor SDL_CreateWindow SDL_DestroyWindow SDL_FreeSurface
SDL_GL_CreateContext SDL_GL_DeleteContext SDL_GL_LoadLibrary SDL_GL_MakeCurrent
SDL_GL_SetAttribute SDL_GL_SetSwapInterval SDL_GL_SwapWindow SDL_GL_UnloadLibrary
SDL_GL_GetProcAddress SDL_GL_GetCurrentContext SDL_GetTicks SDL_GL_GetDrawableSize
SDL_GameControllerClose SDL_GameControllerGetJoystick SDL_GameControllerOpen
SDL_GetClipboardText SDL_GetCurrentAudioDriver SDL_GetCurrentVideoDriver
SDL_GetCurrentDisplayMode SDL_GetDesktopDisplayMode SDL_GetDisplayBounds
SDL_GetError SDL_GetKeyName SDL_GetMouseState SDL_GetNumVideoDisplays
SDL_GetRevision SDL_GetVersion SDL_GetWindowGrab SDL_GetWindowID
SDL_GetWindowSize SDL_HapticClose SDL_HapticOpenFromJoystick
SDL_HapticRumbleInit SDL_HapticRumblePlay SDL_HapticRumbleStop
SDL_HasClipboardText SDL_HideWindow SDL_Init SDL_InitSubSystem SDL_Delay
SDL_IsGameController SDL_JoystickClose SDL_JoystickGetDeviceGUID
SDL_JoystickGetGUIDString SDL_JoystickInstanceID SDL_JoystickNameForIndex
SDL_JoystickOpen SDL_LoadBMP_RW SDL_NumJoysticks SDL_OpenAudioDevice
SDL_OpenURL SDL_PauseAudioDevice SDL_PollEvent SDL_PushEvent SDL_QuitSubSystem
SDL_RWFromFile SDL_RaiseWindow SDL_SetClipboardText SDL_SetCursor SDL_SetHint
SDL_SetRelativeMouseMode SDL_SetWindowBordered SDL_SetWindowDisplayMode
SDL_SetWindowGrab SDL_SetWindowIcon SDL_SetWindowPosition SDL_SetWindowSize
SDL_SetWindowTitle SDL_ShowCursor SDL_ShowMessageBox SDL_ShowSimpleMessageBox
SDL_ShowWindow SDL_StartTextInput SDL_WaitEventTimeout SDL_WarpMouseInWindow
SDL_WasInit SDL_free SDL_memset"

Z_SYMS="adler32 crc32 deflate deflateEnd deflateInit2_ deflateReset gzclose
gzopen gzwrite inflate inflateEnd inflateInit2_ inflateInit_ inflateReset
inflateReset2 zlibVersion"

gen_stub() { # $1=output name (SONAME), $2=linker name, $3=symbols
  local src="$EXTLIBS/$(basename "$1" .so.0).stub.c"
  : > "$src"
  # return 0: stubs must not hand out garbage; e.g. SDL_GetError must not point
  # at junk (glibc prints NULL as "(null)"), so the engine's error path stays clean
  for s in $3; do echo "long $s(void) { return 0; }" >> "$src"; done
  "$CC" -shared -fPIC -nostdlib -Wl,-soname,"$1" -o "$EXTLIBS/$1" "$src"
  ln -sf "$1" "$EXTLIBS/$2"
}

gen_stub libSDL2-2.0.so.0 libSDL2.so "$SDL_SYMS"
gen_stub libz.so.1 libz.so "$Z_SYMS"

# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------
SRCS="src/main.c src/so_util.c src/dl_emu.c src/imports.c src/libc_shim.c \
      src/pthread_shim.c src/jni_fake.c src/osk.c src/gamma.c src/glcache.c src/config.c src/util.c src/error.c \
      src/aim_assist.c src/zipx.c src/setup.c"

"$CC" -march=armv8-a+crc -mtune=cortex-a35 \
    -I src ${CSS_OPT:--O2} -fPIE -pie \
    -Wall -Wno-unused-parameter -Wno-unused-function \
    -Wl,--export-dynamic \
    -L"$EXTLIBS" -Wl,-rpath-link,"$EXTLIBS" \
    -o css $SRCS \
    -lSDL2 -lz -ldl -lm -lpthread

echo "BUILD OK -> $(file css | cut -d, -f1-3)"
echo "max GLIBC symbol (should be <= 2.31 for ArkOS):"
"$OBJDUMP" -T css | grep -oE 'GLIBC_[0-9.]+' | sort -V | uniq -c | tail -6
