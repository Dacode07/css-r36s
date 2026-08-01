#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>

#include "util.h"
#include "config.h"
#include "so_util.h"

void resolve_code_addr(uintptr_t pc, char *out, size_t n) {
  for (so_module *m = so_first(); m; m = m->next) {
    const uintptr_t base = (uintptr_t)m->load_virtbase;
    if (pc >= base && pc < base + m->load_size) {
      snprintf(out, n, "%s+0x%lx", m->name, (unsigned long)(pc - base));
      return;
    }
  }
  Dl_info di;
  if (dladdr((void *)pc, &di) && di.dli_fname) {
    const char *b = strrchr(di.dli_fname, '/');
    snprintf(out, n, "%s+0x%lx", b ? b + 1 : di.dli_fname,
             (unsigned long)(pc - (uintptr_t)di.dli_fbase));
    return;
  }
  snprintf(out, n, "0x%lx", (unsigned long)pc);
}

int log_verbose;

int debugPrintf(const char *text, ...) {
  va_list list;
  va_start(list, text);
  vfprintf(stderr, text, list);
  va_end(list);
  return 0;
}

int tracePrintf(const char *text, ...) {
  if (!log_verbose)
    return 0;
  va_list list;
  va_start(list, text);
  vfprintf(stderr, text, list);
  va_end(list);
  return 0;
}

static const char *const log_noise[] = {
  "This system supports the OpenGL extension",
  "This system DOES NOT support the OpenGL extension",
  "GL_EXTENSIONS=",
  "centroid mask",
  "which is not a texture value",
  "m_face->glyph->bitmap.width",
  "Couldn't load combo",
  "uses unknown shader",
  "Found font:",
  "Failed to load custom font file",
  "Blocking HTML info panel",
  "Video configuration ignoring",
  "JOY_AXIS_",
  "Advanced Joystick settings initialized",
  "LoadLibrary: ",
  "Can't find module",
  "Unable to load module",
  "Missing Vgui material",
  "not found.",
  "Unknown command \"",
  "Parent cvar in server.dll not allowed",
  "multiple help strings",
  "conflicting FCVAR_CHEAT flags",
  "doesn't point to an existing ConVar",
  "doesn't have an entry",
  "artificially incrementing refcount",
  "Steamworks Stats:",
  "Using Breakpad minidump",
  "Redownloading all lightmaps",
  "IDirect3DDevice9::",
};

static int line_is_noise(const char *s) {
  if (log_verbose)
    return 0;
  if (*s == '\t')
    return 1;
  const char *p = s;
  while (*p == '*' || *p == ' ')
    p++;
  if (!*p && p != s)
    return 1;
  for (unsigned i = 0; i < sizeof(log_noise) / sizeof(*log_noise); i++)
    if (strstr(s, log_noise[i]))
      return 1;
  return 0;
}

static void emit_engine_line(const char *line) {
  static char last[512];
  if (!line[0] || line_is_noise(line))
    return;
  if (!strcmp(line, last))
    return;
  snprintf(last, sizeof(last), "%s", line);
  debugPrintf("engine: %s\n", line);
}

static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

void engineLine(const char *text) {
  if (!text)
    return;
  char buf[1024];
  size_t len = 0;
  pthread_mutex_lock(&log_mtx);
  for (const char *p = text;; p++) {
    if (*p == '\n' || *p == '\r' || !*p) {
      while (len && (buf[len - 1] == ' ' || buf[len - 1] == '\t'))
        len--;
      buf[len] = '\0';
      emit_engine_line(buf);
      len = 0;
      if (!*p)
        break;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = *p;
    }
  }
  pthread_mutex_unlock(&log_mtx);
}

void engineStream(const char *text) {
  static char buf[1024];
  static size_t len;
  if (!text)
    return;
  pthread_mutex_lock(&log_mtx);
  for (const char *p = text; *p; p++) {
    if (*p == '\n' || *p == '\r') {
      while (len && (buf[len - 1] == ' ' || buf[len - 1] == '\t'))
        len--;
      buf[len] = '\0';
      emit_engine_line(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = *p;
    }
  }
  pthread_mutex_unlock(&log_mtx);
}

int enginePrintf(const char *text, ...) {
  char buf[1024];
  va_list list;
  va_start(list, text);
  const int n = vsnprintf(buf, sizeof(buf), text, list);
  va_end(list);
  engineStream(buf);
  return n;
}

int enginePuts(const char *text) {
  engineLine(text ? text : "(null)");
  return 0;
}

int ret0(void) { return 0; }

int retm1(void) { return -1; }
