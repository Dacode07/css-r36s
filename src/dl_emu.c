#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "so_util.h"
#include "dl_emu.h"
#include "util.h"
#include "imports.h"
#include "glcache.h"

#define HANDLE_HOST ((void *)0x484f5354)
#define HANDLE_EGL  ((void *)0x45474c00)
#define HANDLE_GLES ((void *)0x474c4553)

static char dl_err[256];
static int dl_err_set = 0;

static void set_dl_error(const char *fmt, const char *arg) {
  snprintf(dl_err, sizeof(dl_err), fmt, arg);
  dl_err_set = 1;
}

extern void *SDL_GL_GetProcAddress(const char *proc);
extern void *SDL_GL_GetCurrentContext(void);

int g_gl_context_ready;

void *eglGetProcAddress_fake(const char *name) {
  const char *src = "sdl";
  void *addr = SDL_GL_GetProcAddress(name);

  if (!addr && g_gl_context_ready) {

    addr = dlsym(RTLD_DEFAULT, name);
    src = "rtld";
    if (!addr) {
      static void *gles_handle;
      if (!gles_handle) {
        static const char *const names[] =
            { "libGLESv2.so.2", "libGLESv2.so", "libGLESv3.so", NULL };
        for (int i = 0; !gles_handle && names[i]; i++)
          gles_handle = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
      }
      if (gles_handle) {
        addr = dlsym(gles_handle, name);
        src = "gles";
      }
    }
  }

  static int logged;
  if (logged < 48) {
    tracePrintf("gpa(%s) -> %p [%s]\n", name, addr, addr ? src : "MISS");
    logged++;
  }

  return glcache_intercept(name, addr);
}

typedef struct {
  const char *name;
  void *handle;
} PseudoMap;

static const PseudoMap pseudo_map[] = {
  { "libSDL2.so", HANDLE_HOST },
  { "libSDL2-2.0.so", HANDLE_HOST },
  { "libdl.so", HANDLE_HOST },
  { "liblog.so", HANDLE_HOST },
  { "libc.so", HANDLE_HOST },
  { "libm.so", HANDLE_HOST },
  { "libz.so", HANDLE_HOST },
  { "libEGL.so", HANDLE_EGL },
  { "libGLESv3.so", HANDLE_GLES },
  { "libGLESv2.so", HANDLE_GLES },
  { "libGLESv1_CM.so", HANDLE_GLES },
};

void *dlopen_fake(const char *filename, int flags) {
  (void)flags;

  if (!filename)
    return HANDLE_HOST;

  const char *base = strrchr(filename, '/');
  base = base ? base + 1 : filename;

  char norm[128];
  if (strncmp(base, "lib", 3) != 0)
    snprintf(norm, sizeof(norm), "lib%s", base);
  else
    snprintf(norm, sizeof(norm), "%s", base);

  for (unsigned int i = 0; i < sizeof(pseudo_map) / sizeof(*pseudo_map); i++) {
    if (!strcmp(norm, pseudo_map[i].name)) {
      tracePrintf("dlopen(%s) -> pseudo %p\n", filename, pseudo_map[i].handle);
      return pseudo_map[i].handle;
    }
  }

  so_module *mod = so_find_module(norm);
  if (mod) {
    tracePrintf("dlopen(%s) -> %s\n", filename, mod->name);
    return mod;
  }

  debugPrintf("dlopen(%s): NOT FOUND\n", filename);
  set_dl_error("dlopen failed: %s not preloaded", base);
  return NULL;
}

void *dlsym_fake(void *handle, const char *symbol) {
  void *addr = NULL;

  if (!symbol) {
    set_dl_error("dlsym: null symbol%s", "");
    return NULL;
  }

  if (handle == HANDLE_HOST || handle == NULL) {

    if (!strcmp(symbol, "SDL_GL_GetProcAddress") || !strcmp(symbol, "eglGetProcAddress")) {
      tracePrintf("dlsym(HOST, %s) -> eglGetProcAddress_fake\n", symbol);
      return (void *)&eglGetProcAddress_fake;
    }

    DynLibFunction *f = so_find_import(dynlib_functions, dynlib_numfunctions, symbol);
    if (f)
      addr = (void *)f->func;
    if (!addr)
      addr = (void *)so_lookup_export_all(symbol);
    if (!addr) {
      addr = dlsym(RTLD_DEFAULT, symbol);
      if (addr)
        tracePrintf("dlsym(HOST, %s) -> %p [rtld]\n", symbol, addr);
    }
  } else if (handle == HANDLE_EGL || handle == HANDLE_GLES) {

    if (!strcmp(symbol, "eglGetProcAddress")) {
      tracePrintf("dlsym(%s, eglGetProcAddress) -> fake resolver\n",
                  handle == HANDLE_EGL ? "EGL" : "GLES");
      return (void *)&eglGetProcAddress_fake;
    }
    addr = eglGetProcAddress_fake(symbol);
    tracePrintf("dlsym(%s, %s) -> %p\n",
                handle == HANDLE_EGL ? "EGL" : "GLES", symbol, addr);
    if (!addr)
      return NULL;
  } else if (so_is_module(handle)) {
    addr = (void *)so_lookup_export((so_module *)handle, symbol);
  } else {
    debugPrintf("dlsym(%p, %s): bad handle\n", handle, symbol);
    set_dl_error("dlsym: invalid handle%s", "");
    return NULL;
  }

  if (!addr) {
    tracePrintf("dlsym(%p, %s): NOT FOUND\n", handle, symbol);
    set_dl_error("dlsym: undefined symbol %s", symbol);
  }

  return addr;
}

int dlclose_fake(void *handle) {
  (void)handle;
  return 0;
}

char *dlerror_fake(void) {
  if (!dl_err_set)
    return NULL;
  dl_err_set = 0;
  return dl_err;
}

int dladdr_fake(const void *addr, DlInfoFake *info) {
  for (so_module *mod = so_first(); mod; mod = mod->next) {
    const uintptr_t base = (uintptr_t)mod->load_virtbase;
    if ((uintptr_t)addr < base || (uintptr_t)addr >= base + mod->load_size)
      continue;
    info->dli_fname = mod->name;
    info->dli_fbase = mod->load_virtbase;
    info->dli_sname = NULL;
    info->dli_saddr = NULL;

    uintptr_t best = 0;
    int best_idx = -1;
    for (int i = 0; i < mod->num_syms; i++) {
      if (mod->syms[i].st_shndx == SHN_UNDEF)
        continue;
      const uintptr_t s = base + mod->syms[i].st_value;
      if (s <= (uintptr_t)addr && s > best) {
        best = s;
        best_idx = i;
      }
    }
    if (best_idx >= 0) {
      info->dli_sname = mod->dynstrtab + mod->syms[best_idx].st_name;
      info->dli_saddr = (void *)best;
    }
    return 1;
  }
  return 0;
}
