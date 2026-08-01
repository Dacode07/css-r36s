#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "glcache.h"
#include "config.h"
#include "util.h"

typedef unsigned GLenum; typedef unsigned GLuint; typedef int GLint;
typedef int GLsizei; typedef char GLchar;

#define GLC_LINK_STATUS 0x8B82
#define GLC_COMPILE_STATUS 0x8B81
#define GLC_INFO_LOG_LENGTH 0x8B84
#define GLC_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#define GLC_NUM_PROGRAM_BINARY_FORMATS 0x87FE
#define GLC_PROGRAM_BINARY_LENGTH 0x8741
#define GLC_VERSION 0x1F02
#define GLC_RENDERER 0x1F01

extern void *SDL_GL_GetProcAddress(const char *proc);

static void (*real_ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
static void (*real_CompileShader)(GLuint);
static void (*real_GetShaderiv)(GLuint, GLenum, GLint *);
static void (*real_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static void (*real_AttachShader)(GLuint, GLuint);
static void (*real_DetachShader)(GLuint, GLuint);
static void (*real_BindAttribLocation)(GLuint, GLuint, const GLchar *);
static void (*real_LinkProgram)(GLuint);
static void (*real_DeleteShader)(GLuint);
static void (*real_DeleteProgram)(GLuint);
static void (*p_GetProgramiv)(GLuint, GLenum, GLint *);
static void (*p_ProgramBinary)(GLuint, GLenum, const void *, GLsizei);
static void (*p_GetProgramBinary)(GLuint, GLsizei, GLsizei *, GLenum *, void *);
static void (*p_ProgramParameteri)(GLuint, GLenum, GLint);
static void (*p_GetIntegerv)(GLenum, GLint *);
static const unsigned char *(*p_GetString)(GLenum);

#define MAX_ID 65536
#define MAX_ATTACH 8
static uint64_t sh_hash[MAX_ID];
static uint64_t pr_shaders[MAX_ID];
static uint64_t pr_attribs[MAX_ID];

static unsigned char sh_deferred[MAX_ID];
static unsigned char sh_compiled[MAX_ID];
static GLuint pr_attached[MAX_ID][MAX_ATTACH];
static unsigned char pr_nattached[MAX_ID];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_driver_hash;
static int g_binary_ok = -1;
static int g_hits, g_misses, g_fails;

#define FNV64_BASIS 0xcbf29ce484222325ull
#define FNV64_PRIME 0x100000001b3ull

static uint64_t fnv64(uint64_t h, const void *data, size_t len) {
  const unsigned char *p = data;
  while (len--) { h ^= *p++; h *= FNV64_PRIME; }
  return h;
}

static uint64_t fnv64_u64(uint64_t h, uint64_t v) { return fnv64(h, &v, sizeof(v)); }

static void ensure_driver_hash(void) {
  if (g_driver_hash)
    return;
  if (!p_GetString) p_GetString = SDL_GL_GetProcAddress("glGetString");
  if (!p_GetIntegerv) p_GetIntegerv = SDL_GL_GetProcAddress("glGetIntegerv");
  uint64_t h = FNV64_BASIS;
  if (p_GetString) {
    const unsigned char *s;
    if ((s = p_GetString(GLC_VERSION))) h = fnv64(h, s, strlen((const char *)s));
    if ((s = p_GetString(GLC_RENDERER))) h = fnv64(h, s, strlen((const char *)s));
  }
  g_driver_hash = h ? h : 1;
  if (g_binary_ok < 0) {
    GLint nfmt = 0;
    if (p_GetIntegerv) p_GetIntegerv(GLC_NUM_PROGRAM_BINARY_FORMATS, &nfmt);
    g_binary_ok = nfmt > 0;
    debugPrintf("glcache: %d program binary format(s) -> %s\n",
                nfmt, g_binary_ok ? "enabled" : "UNSUPPORTED, disabled");
    if (g_binary_ok)
      mkdir("glcache", 0777);
  }
}

static void cache_path(char *out, size_t n, uint64_t key) {
  snprintf(out, n, "glcache/%016llx.bin", (unsigned long long)key);
}

static void wrap_ShaderSource(GLuint sh, GLsizei count,
                              const GLchar *const *strs, const GLint *lens) {
  uint64_t h = FNV64_BASIS;
  for (GLsizei i = 0; i < count && strs; i++) {
    if (!strs[i]) continue;
    const size_t l = (lens && lens[i] >= 0) ? (size_t)lens[i] : strlen(strs[i]);
    h = fnv64(h, strs[i], l);
  }
  if (sh < MAX_ID) {
    pthread_mutex_lock(&g_lock);
    sh_hash[sh] = h ? h : 1;
    sh_deferred[sh] = 0;
    sh_compiled[sh] = 0;
    pthread_mutex_unlock(&g_lock);
  }
  real_ShaderSource(sh, count, strs, lens);
}

static void wrap_CompileShader(GLuint sh) {
  if (sh < MAX_ID && config.glcache) {
    pthread_mutex_lock(&g_lock);
    sh_deferred[sh] = 1;
    sh_compiled[sh] = 0;
    pthread_mutex_unlock(&g_lock);
    return;
  }
  real_CompileShader(sh);
}

static void wrap_GetShaderiv(GLuint sh, GLenum pname, GLint *out) {
  if (sh < MAX_ID && out) {
    pthread_mutex_lock(&g_lock);
    const int lie = sh_deferred[sh] && !sh_compiled[sh];
    pthread_mutex_unlock(&g_lock);
    if (lie && pname == GLC_COMPILE_STATUS) { *out = 1; return; }
    if (lie && pname == GLC_INFO_LOG_LENGTH) { *out = 0; return; }
  }
  real_GetShaderiv(sh, pname, out);
}

static void wrap_GetShaderInfoLog(GLuint sh, GLsizei bufsz, GLsizei *len, GLchar *log) {
  if (sh < MAX_ID) {
    pthread_mutex_lock(&g_lock);
    const int lie = sh_deferred[sh] && !sh_compiled[sh];
    pthread_mutex_unlock(&g_lock);
    if (lie) {
      if (len) *len = 0;
      if (log && bufsz > 0) log[0] = 0;
      return;
    }
  }
  real_GetShaderInfoLog(sh, bufsz, len, log);
}

static void ensure_compiled(GLuint prog) {
  if (prog >= MAX_ID)
    return;
  pthread_mutex_lock(&g_lock);
  GLuint list[MAX_ATTACH];
  const int n = pr_nattached[prog];
  memcpy(list, pr_attached[prog], sizeof(list));
  pthread_mutex_unlock(&g_lock);
  for (int i = 0; i < n; i++) {
    const GLuint sh = list[i];
    if (sh >= MAX_ID)
      continue;
    pthread_mutex_lock(&g_lock);
    const int need = sh_deferred[sh] && !sh_compiled[sh];
    if (need) sh_compiled[sh] = 1;
    pthread_mutex_unlock(&g_lock);
    if (need)
      real_CompileShader(sh);
  }
}

static void wrap_AttachShader(GLuint prog, GLuint sh) {
  if (prog < MAX_ID) {
    pthread_mutex_lock(&g_lock);
    const uint64_t shh = (sh < MAX_ID) ? sh_hash[sh] : 0;
    if (!pr_shaders[prog]) pr_shaders[prog] = FNV64_BASIS;

    pr_shaders[prog] = shh ? fnv64_u64(pr_shaders[prog], shh) : UINT64_MAX;
    if (pr_nattached[prog] < MAX_ATTACH)
      pr_attached[prog][pr_nattached[prog]++] = sh;
    else
      pr_shaders[prog] = UINT64_MAX;
    pthread_mutex_unlock(&g_lock);
  }
  real_AttachShader(prog, sh);
}

static void wrap_DetachShader(GLuint prog, GLuint sh) {
  if (prog < MAX_ID) {
    pthread_mutex_lock(&g_lock);
    pr_shaders[prog] = UINT64_MAX;
    pthread_mutex_unlock(&g_lock);
  }
  real_DetachShader(prog, sh);
}

static void wrap_BindAttribLocation(GLuint prog, GLuint idx, const GLchar *name) {
  if (prog < MAX_ID && name) {
    pthread_mutex_lock(&g_lock);
    if (!pr_attribs[prog]) pr_attribs[prog] = FNV64_BASIS;
    pr_attribs[prog] = fnv64(fnv64_u64(pr_attribs[prog], idx), name, strlen(name));
    pthread_mutex_unlock(&g_lock);
  }
  real_BindAttribLocation(prog, idx, name);
}

static void wrap_DeleteShader(GLuint sh) {
  if (sh < MAX_ID) {
    pthread_mutex_lock(&g_lock);
    sh_hash[sh] = 0;
    sh_deferred[sh] = 0;
    sh_compiled[sh] = 0;
    pthread_mutex_unlock(&g_lock);
  }
  real_DeleteShader(sh);
}

static void wrap_DeleteProgram(GLuint prog) {
  if (prog < MAX_ID) {
    pthread_mutex_lock(&g_lock);
    pr_shaders[prog] = 0; pr_attribs[prog] = 0;
    pr_nattached[prog] = 0;
    pthread_mutex_unlock(&g_lock);
  }
  real_DeleteProgram(prog);
}

static int link_ok(GLuint prog) {
  GLint ok = 0;
  p_GetProgramiv(prog, GLC_LINK_STATUS, &ok);
  return ok == 1;
}

static void wrap_LinkProgram(GLuint prog) {
  if (!config.glcache || prog >= MAX_ID)
    { ensure_compiled(prog); real_LinkProgram(prog); return; }

  if (!p_GetProgramiv) p_GetProgramiv = SDL_GL_GetProcAddress("glGetProgramiv");
  if (!p_ProgramBinary) p_ProgramBinary = SDL_GL_GetProcAddress("glProgramBinary");
  if (!p_GetProgramBinary) p_GetProgramBinary = SDL_GL_GetProcAddress("glGetProgramBinary");
  if (!p_ProgramParameteri) p_ProgramParameteri = SDL_GL_GetProcAddress("glProgramParameteri");
  ensure_driver_hash();
  if (!g_binary_ok || !p_GetProgramiv || !p_ProgramBinary || !p_GetProgramBinary)
    { ensure_compiled(prog); real_LinkProgram(prog); return; }

  pthread_mutex_lock(&g_lock);
  const uint64_t sh = pr_shaders[prog], at = pr_attribs[prog];
  pthread_mutex_unlock(&g_lock);
  if (!sh || sh == UINT64_MAX)
    { ensure_compiled(prog); real_LinkProgram(prog); return; }

  const uint64_t key = fnv64_u64(fnv64_u64(fnv64_u64(FNV64_BASIS, sh), at), g_driver_hash);
  char path[128];
  cache_path(path, sizeof(path), key);

  FILE *f = fopen(path, "rb");
  if (f) {
    GLenum fmt = 0;
    long sz;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz > (long)sizeof(fmt)) {
      void *buf = malloc(sz - sizeof(fmt));
      if (buf && fread(&fmt, sizeof(fmt), 1, f) == 1 &&
          fread(buf, sz - sizeof(fmt), 1, f) == 1) {
        p_ProgramBinary(prog, fmt, buf, (GLsizei)(sz - sizeof(fmt)));
        if (link_ok(prog)) {

          free(buf); fclose(f);
          if ((++g_hits % 50) == 1)
            tracePrintf("glcache: %d hits / %d compiles / %d errors\n",
                        g_hits, g_misses, g_fails);
          return;
        }
        g_fails++;
      }
      free(buf);
    }
    fclose(f);
  }

  ensure_compiled(prog);
  if (p_ProgramParameteri)
    p_ProgramParameteri(prog, GLC_PROGRAM_BINARY_RETRIEVABLE_HINT, 1);
  real_LinkProgram(prog);
  g_misses++;
  if (!link_ok(prog))
    return;
  GLint blen = 0;
  p_GetProgramiv(prog, GLC_PROGRAM_BINARY_LENGTH, &blen);
  if (blen <= 0)
    return;
  void *buf = malloc(blen);
  if (!buf)
    return;
  GLsizei got = 0; GLenum fmt = 0;
  p_GetProgramBinary(prog, blen, &got, &fmt, buf);
  if (got > 0) {
    char tmp[144];
    snprintf(tmp, sizeof(tmp), "%s.tmp%d", path, (int)getpid());
    FILE *o = fopen(tmp, "wb");
    if (o) {
      const int w = fwrite(&fmt, sizeof(fmt), 1, o) == 1 &&
                    fwrite(buf, got, 1, o) == 1;
      fclose(o);
      if (w) rename(tmp, path); else remove(tmp);
    }
  }
  free(buf);
}

void *glcache_intercept(const char *name, void *real) {
  if (!real || !config.glcache)
    return real;
  #define WRAP(fn) if (!strcmp(name, "gl" #fn)) { real_##fn = real; return (void *)&wrap_##fn; }
  WRAP(ShaderSource)
  WRAP(CompileShader)
  WRAP(GetShaderiv)
  WRAP(GetShaderInfoLog)
  WRAP(AttachShader)
  WRAP(DetachShader)
  WRAP(BindAttribLocation)
  WRAP(LinkProgram)
  WRAP(DeleteShader)
  WRAP(DeleteProgram)
  #undef WRAP
  return real;
}
