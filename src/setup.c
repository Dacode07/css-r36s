#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "setup.h"
#include "zipx.h"
#include "config.h"
#include "sdl2_min.h"
#include "util.h"

extern char font8x8_basic[128][8];

typedef unsigned int GLuint;
typedef int GLint;

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TRIANGLES 0x0004
#define GL_FLOAT 0x1406
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_ALPHA 0x1906
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_UNPACK_ALIGNMENT 0x0CF5

static void (*p_glClearColor)(float, float, float, float);
static void (*p_glClear)(unsigned);
static void (*p_glViewport)(int, int, int, int);
static void (*p_glEnable)(unsigned);
static void (*p_glBlendFunc)(unsigned, unsigned);
static GLuint (*p_glCreateShader)(unsigned);
static void (*p_glShaderSource)(GLuint, int, const char *const *, const int *);
static void (*p_glCompileShader)(GLuint);
static void (*p_glGetShaderiv)(GLuint, unsigned, GLint *);
static GLuint (*p_glCreateProgram)(void);
static void (*p_glAttachShader)(GLuint, GLuint);
static void (*p_glLinkProgram)(GLuint);
static void (*p_glGetProgramiv)(GLuint, unsigned, GLint *);
static void (*p_glUseProgram)(GLuint);
static GLint (*p_glGetAttribLocation)(GLuint, const char *);
static GLint (*p_glGetUniformLocation)(GLuint, const char *);
static void (*p_glUniform4f)(GLint, float, float, float, float);
static void (*p_glUniform1i)(GLint, int);
static void (*p_glEnableVertexAttribArray)(GLuint);
static void (*p_glVertexAttribPointer)(GLuint, int, unsigned, unsigned char, int, const void *);
static void (*p_glDrawArrays)(unsigned, int, int);
static void (*p_glGenTextures)(int, GLuint *);
static void (*p_glBindTexture)(unsigned, GLuint);
static void (*p_glTexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
static void (*p_glTexParameteri)(unsigned, unsigned, int);
static void (*p_glActiveTexture)(unsigned);
static void (*p_glPixelStorei)(unsigned, int);

static void *g_win, *g_ctx;
static int g_gui;
static int g_vw, g_vh;
static GLuint g_prog, g_font_tex;
static GLint g_u_color, g_u_texon, g_u_tex, g_a_pos, g_a_uv;

#define MAX_VERTS 12288
static float g_vb[MAX_VERTS * 4];
static int g_vn;

#define FONT_CELL 10
#define ATLAS_W (16 * FONT_CELL)
#define ATLAS_H (8 * FONT_CELL)

static const char *VS_SRC =
  "attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv;\n"
  "void main(){ v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static const char *FS_SRC =
  "precision mediump float; varying vec2 v_uv;\n"
  "uniform vec4 u_color; uniform int u_texon; uniform sampler2D u_tex;\n"
  "void main(){\n"
  "  float a = (u_texon == 1) ? texture2D(u_tex, v_uv).a : 1.0;\n"
  "  gl_FragColor = vec4(u_color.rgb, u_color.a * a); }\n";

static int resolve_gl(void) {
  #define GP(v, n) do { *(void **)&v = SDL_GL_GetProcAddress(n); if (!v) return 0; } while (0)
  GP(p_glClearColor, "glClearColor");
  GP(p_glClear, "glClear");
  GP(p_glViewport, "glViewport");
  GP(p_glEnable, "glEnable");
  GP(p_glBlendFunc, "glBlendFunc");
  GP(p_glCreateShader, "glCreateShader");
  GP(p_glShaderSource, "glShaderSource");
  GP(p_glCompileShader, "glCompileShader");
  GP(p_glGetShaderiv, "glGetShaderiv");
  GP(p_glCreateProgram, "glCreateProgram");
  GP(p_glAttachShader, "glAttachShader");
  GP(p_glLinkProgram, "glLinkProgram");
  GP(p_glGetProgramiv, "glGetProgramiv");
  GP(p_glUseProgram, "glUseProgram");
  GP(p_glGetAttribLocation, "glGetAttribLocation");
  GP(p_glGetUniformLocation, "glGetUniformLocation");
  GP(p_glUniform4f, "glUniform4f");
  GP(p_glUniform1i, "glUniform1i");
  GP(p_glEnableVertexAttribArray, "glEnableVertexAttribArray");
  GP(p_glVertexAttribPointer, "glVertexAttribPointer");
  GP(p_glDrawArrays, "glDrawArrays");
  GP(p_glGenTextures, "glGenTextures");
  GP(p_glBindTexture, "glBindTexture");
  GP(p_glTexImage2D, "glTexImage2D");
  GP(p_glTexParameteri, "glTexParameteri");
  GP(p_glActiveTexture, "glActiveTexture");
  GP(p_glPixelStorei, "glPixelStorei");
  #undef GP
  return 1;
}

static GLuint compile(unsigned type, const char *src) {
  const GLuint s = p_glCreateShader(type);
  p_glShaderSource(s, 1, &src, NULL);
  p_glCompileShader(s);
  GLint ok = 0;
  p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  return ok ? s : 0;
}

static int build_font(void) {
  unsigned char *px = calloc(ATLAS_W * ATLAS_H, 1);
  if (!px)
    return 0;
  for (int ch = 0; ch < 128; ch++) {
    const int cx = (ch % 16) * FONT_CELL + 1;
    const int cy = (ch / 16) * FONT_CELL + 1;
    for (int r = 0; r < 8; r++) {
      const unsigned char bits = (unsigned char)font8x8_basic[ch][r];
      for (int c = 0; c < 8; c++)
        if (bits & (1 << c))
          px[(cy + r) * ATLAS_W + cx + c] = 255;
    }
  }
  p_glGenTextures(1, &g_font_tex);
  p_glBindTexture(GL_TEXTURE_2D, g_font_tex);
  p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  p_glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_W, ATLAS_H, 0, GL_ALPHA, GL_UNSIGNED_BYTE, px);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  free(px);
  return 1;
}

static void gui_close(void) {
  if (g_ctx) { SDL_GL_DeleteContext(g_ctx); g_ctx = NULL; }
  if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; }
  if (g_gui) { SDL_QuitSubSystem(SDLM_INIT_VIDEO); g_gui = 0; }
}

static int gui_open(void) {
  if (g_gui)
    return 1;
  if (SDL_Init(SDLM_INIT_VIDEO) != 0) {
    debugPrintf("setup: SDL video init failed: %s\n", SDL_GetError());
    return 0;
  }
  g_gui = 1;
  SDL_GL_SetAttribute(SDLM_GL_CONTEXT_PROFILE_MASK, SDLM_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDLM_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDLM_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDLM_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDLM_GL_DEPTH_SIZE, 0);

  g_win = SDL_CreateWindow("css setup", SDLM_WINDOWPOS_CENTERED, SDLM_WINDOWPOS_CENTERED,
                           screen_width, screen_height,
                           SDLM_WINDOW_OPENGL | SDLM_WINDOW_FULLSCREEN);
  if (!g_win) {
    debugPrintf("setup: window failed: %s\n", SDL_GetError());
    gui_close();
    return 0;
  }
  g_ctx = SDL_GL_CreateContext(g_win);
  if (!g_ctx) {
    debugPrintf("setup: GL context failed: %s\n", SDL_GetError());
    gui_close();
    return 0;
  }
  SDL_GL_SetSwapInterval(1);
  SDL_ShowCursor(0);

  g_vw = screen_width;
  g_vh = screen_height;
  SDL_GL_GetDrawableSize(g_win, &g_vw, &g_vh);
  if (g_vw <= 0 || g_vh <= 0) { g_vw = screen_width; g_vh = screen_height; }

  if (!resolve_gl()) {
    debugPrintf("setup: GLES2 entry points missing\n");
    gui_close();
    return 0;
  }

  const GLuint vs = compile(GL_VERTEX_SHADER, VS_SRC);
  const GLuint fs = compile(GL_FRAGMENT_SHADER, FS_SRC);
  if (!vs || !fs) { debugPrintf("setup: shader compile failed\n"); gui_close(); return 0; }
  g_prog = p_glCreateProgram();
  p_glAttachShader(g_prog, vs);
  p_glAttachShader(g_prog, fs);
  p_glLinkProgram(g_prog);
  GLint ok = 0;
  p_glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
  if (!ok) { debugPrintf("setup: shader link failed\n"); gui_close(); return 0; }

  g_a_pos = p_glGetAttribLocation(g_prog, "a_pos");
  g_a_uv = p_glGetAttribLocation(g_prog, "a_uv");
  g_u_color = p_glGetUniformLocation(g_prog, "u_color");
  g_u_texon = p_glGetUniformLocation(g_prog, "u_texon");
  g_u_tex = p_glGetUniformLocation(g_prog, "u_tex");

  if (!build_font()) { gui_close(); return 0; }

  p_glViewport(0, 0, g_vw, g_vh);
  p_glEnable(GL_BLEND);
  p_glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  return 1;
}

static float cx_(float x) { return x / (float)screen_width * 2.0f - 1.0f; }
static float cy_(float y) { return 1.0f - y / (float)screen_height * 2.0f; }

static void push_quad(float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1) {
  if (g_vn + 6 > MAX_VERTS)
    return;
  const float px[6] = { x, x + w, x + w, x, x, x + w };
  const float py[6] = { y, y, y + h, y, y + h, y + h };
  const float pu[6] = { u0, u1, u1, u0, u0, u1 };
  const float pv[6] = { v0, v0, v1, v0, v1, v1 };
  for (int i = 0; i < 6; i++) {
    float *v = &g_vb[(g_vn + i) * 4];
    v[0] = cx_(px[i]); v[1] = cy_(py[i]); v[2] = pu[i]; v[3] = pv[i];
  }
  g_vn += 6;
}

static void flush(float r, float g, float b, float a, int textured) {
  if (!g_vn)
    return;
  p_glUseProgram(g_prog);
  p_glUniform4f(g_u_color, r, g, b, a);
  p_glUniform1i(g_u_texon, textured);
  if (textured) {
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, g_font_tex);
    p_glUniform1i(g_u_tex, 0);
  }
  p_glEnableVertexAttribArray((GLuint)g_a_pos);
  p_glEnableVertexAttribArray((GLuint)g_a_uv);
  p_glVertexAttribPointer((GLuint)g_a_pos, 2, GL_FLOAT, 0, 16, g_vb);
  p_glVertexAttribPointer((GLuint)g_a_uv, 2, GL_FLOAT, 0, 16, g_vb + 2);
  p_glDrawArrays(GL_TRIANGLES, 0, g_vn);
  g_vn = 0;
}

static void rect(float x, float y, float w, float h, float r, float g, float b, float a) {
  push_quad(x, y, w, h, 0, 0, 1, 1);
  flush(r, g, b, a, 0);
}

static float text_w(const char *s, float sc) { return (float)strlen(s) * 6.0f * sc; }

static void text(float x, float y, float sc, const char *s,
                 float r, float g, float b, float a) {
  const float cw = FONT_CELL / (float)ATLAS_W;
  const float chh = FONT_CELL / (float)ATLAS_H;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    const int ch = *p < 128 ? *p : '?';
    const float u0 = (ch % 16) * cw;
    const float v0 = (ch / 16) * chh;
    push_quad(x, y, 8 * sc, 8 * sc, u0, v0, u0 + cw, v0 + chh);
    x += 6.0f * sc;
  }
  flush(r, g, b, a, 1);
}

static void text_center(float y, float sc, const char *s, float r, float g, float b, float a) {
  text((screen_width - text_w(s, sc)) / 2.0f, y, sc, s, r, g, b, a);
}

static void frame_begin(void) {
  p_glViewport(0, 0, g_vw, g_vh);
  p_glClearColor(0.043f, 0.047f, 0.039f, 1.0f);
  p_glClear(GL_COLOR_BUFFER_BIT);
  g_vn = 0;
}

static void frame_end(void) { SDL_GL_SwapWindow(g_win); }

static void draw_header(const char *title) {
  const float h = screen_height * 0.16f;
  rect(0, 0, (float)screen_width, h, 0.09f, 0.10f, 0.08f, 1.0f);
  rect(0, h - 2, (float)screen_width, 2, 0.85f, 0.47f, 0.13f, 1.0f);
  text_center(h * 0.36f, screen_height / 150.0f + 0.9f, title, 0.94f, 0.92f, 0.84f, 1.0f);
}

static void draw_progress(const char *stage, const char *detail, float frac) {
  if (!g_gui)
    return;
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;

  frame_begin();
  draw_header("COUNTER-STRIKE: SOURCE");

  const float sc = screen_height / 220.0f + 0.4f;
  text_center(screen_height * 0.30f, sc, stage, 0.85f, 0.47f, 0.13f, 1.0f);

  const float bw = screen_width * 0.78f;
  const float bx = (screen_width - bw) / 2.0f;
  const float by = screen_height * 0.46f;
  const float bh = screen_height * 0.075f;
  rect(bx - 2, by - 2, bw + 4, bh + 4, 0.20f, 0.20f, 0.17f, 1.0f);
  rect(bx, by, bw, bh, 0.07f, 0.08f, 0.06f, 1.0f);
  rect(bx, by, bw * frac, bh, 0.85f, 0.47f, 0.13f, 1.0f);

  char pct[16];
  snprintf(pct, sizeof(pct), "%d%%", (int)(frac * 100.0f + 0.5f));
  text_center(by + bh + screen_height * 0.045f, sc, pct, 0.94f, 0.92f, 0.84f, 1.0f);

  if (detail && detail[0]) {
    char cut[40];
    snprintf(cut, sizeof(cut), "%s", detail);
    text_center(screen_height * 0.72f, sc * 0.8f, cut, 0.62f, 0.60f, 0.53f, 1.0f);
  }
  text_center(screen_height * 0.87f, sc * 0.75f, "PLEASE WAIT - DO NOT POWER OFF",
              0.45f, 0.44f, 0.38f, 1.0f);
  frame_end();
}

static void draw_message(const char *title, const char *const *lines, int nlines,
                         float r, float g, float b) {
  if (!g_gui)
    return;
  frame_begin();
  draw_header("COUNTER-STRIKE: SOURCE");
  const float sc = screen_height / 220.0f + 0.4f;
  text_center(screen_height * 0.26f, sc, title, r, g, b, 1.0f);
  float y = screen_height * 0.40f;
  const float ls = sc * 0.8f;
  for (int i = 0; i < nlines; i++) {
    if (lines[i][0])
      text_center(y, ls, lines[i], 0.80f, 0.78f, 0.70f, 1.0f);
    y += 10.0f * ls;
  }
  frame_end();
}

static void wait_for_key(int ms) {
  if (!g_gui) { SDL_Delay((uint32_t)ms); return; }
  SDL_InitSubSystem(SDLM_INIT_JOYSTICK);
  const int n = SDL_NumJoysticks();
  for (int i = 0; i < n && i < 4; i++)
    SDL_JoystickOpen(i);
  const uint32_t t0 = SDL_GetTicks();
  for (;;) {
    SDL_Event_min e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDLM_JOYBUTTONDOWN || e.type == SDLM_KEYDOWN || e.type == SDLM_QUIT)
        return;
    }
    if (SDL_GetTicks() - t0 > (uint32_t)ms)
      return;
    SDL_Delay(16);
  }
}

void setup_show_error(const char *title, const char *const *lines, int nlines) {
  debugPrintf("setup: %s\n", title);
  for (int i = 0; i < nlines; i++)
    if (lines[i][0])
      debugPrintf("setup:   %s\n", lines[i]);
  const int had_gui = g_gui || gui_open();
  if (!had_gui)
    return;
  draw_message(title, lines, nlines, 0.80f, 0.22f, 0.22f);
  wait_for_key(20000);
  gui_close();
}

typedef struct { char path[512]; long size; } ApkFile;

static int ends_with(const char *s, const char *suf) {
  const size_t ls = strlen(s), lf = strlen(suf);
  return ls >= lf && !strcasecmp(s + ls - lf, suf);
}

static int find_apks(const char *root, ApkFile *out, int max) {
  DIR *d = opendir(root);
  if (!d)
    return 0;
  int n = 0;
  struct dirent *de;
  while ((de = readdir(d)) && n < max) {
    if (!ends_with(de->d_name, ".apk"))
      continue;
    ApkFile a;
    snprintf(a.path, sizeof(a.path), "%s/%s", root, de->d_name);
    struct stat st;
    if (stat(a.path, &st) != 0 || !S_ISREG(st.st_mode))
      continue;
    a.size = (long)st.st_size;
    out[n++] = a;
  }
  closedir(d);
  for (int i = 1; i < n; i++) {
    ApkFile k = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].size < k.size) { out[j + 1] = out[j]; j--; }
    out[j + 1] = k;
  }
  return n;
}

enum { PICK_ENGINE_LIBS, PICK_FONTS, PICK_CSS_LIBS, PICK_EXTRAS };

static int pick(const char *name, void *ud) {
  const int mode = (int)(intptr_t)ud;
  const char *base = strrchr(name, '/');
  base = base ? base + 1 : name;
  switch (mode) {
    case PICK_ENGINE_LIBS:
      return !strncmp(name, "lib/arm64-v8a/", 14) && ends_with(name, ".so");
    case PICK_FONTS:
      return !strncmp(name, "assets/", 7) && (ends_with(name, ".ttf") || ends_with(name, ".otf"));
    case PICK_CSS_LIBS:
      return !strncmp(name, "lib/arm64-v8a/", 14) &&
             (!strcmp(base, "libclient.so") || !strcmp(base, "libserver.so"));
    case PICK_EXTRAS:
      return !strcmp(name, "assets/extras_dir.vpk");
  }
  return 0;
}

typedef struct { const char *stage; uint32_t last_draw; } Progress;

static void on_progress(uint64_t done, uint64_t total, const char *name, void *ud) {
  Progress *p = ud;
  const uint32_t now = SDL_GetTicks();
  if (now - p->last_draw < 60)
    return;
  p->last_draw = now;
  draw_progress(p->stage, name, total ? (float)((double)done / (double)total) : 0.0f);
}

static int extract_set(const char *apk, const char *out_dir, int mode,
                       const char *stage, int expect_files) {
  Zip z;
  const int oz = zip_open(&z, apk);
  if (oz != 0) {
    debugPrintf("setup: cannot read %s (%d)\n", apk, oz);
    return -1;
  }
  Progress pr = { stage, 0 };
  draw_progress(stage, "", 0.0f);
  const int n = zip_extract(&z, out_dir, pick, (void *)(intptr_t)mode, on_progress, &pr);
  zip_close(&z);
  if (n < 0)
    return -2;
  if (n < expect_files) {
    debugPrintf("setup: %s yielded %d files, expected at least %d\n", apk, n, expect_files);
    return -3;
  }
  draw_progress(stage, "", 1.0f);
  return n;
}

int setup_needed(const char *root) {
  char p[512];
  struct stat st;
  snprintf(p, sizeof(p), "%s/lib/liblauncher.so", root);
  return stat(p, &st) != 0;
}

static void drop_setup_marker(const char *root) {
  char p[512];
  snprintf(p, sizeof(p), "%s/lib/liblauncher.so", root);
  remove(p);
}

int setup_run(const char *root) {
  ApkFile apks[8];
  const int napk = find_apks(root, apks, 8);

  if (napk < 2) {
    static const char *const lines[] = {
      "Put BOTH APK files into the css folder:",
      "",
      "  - Source Engine APK   (engine, ~80 MB)",
      "  - Counter-Strike: Source APK  (~28 MB)",
      "",
      "Also copy your game data into css:",
      "  cstrike/   hl2/   platform/",
      "",
      "Then start the port again.",
    };
    setup_show_error("SETUP: APK FILES MISSING", lines, 9);
    return -1;
  }

  gui_open();
  debugPrintf("setup: engine apk=%s (%ld bytes), game apk=%s (%ld bytes)\n",
              apks[0].path, apks[0].size, apks[1].path, apks[1].size);

  char lib_dir[512], assets_dir[512], files_dir[512];
  snprintf(lib_dir, sizeof(lib_dir), "%s/lib", root);
  snprintf(assets_dir, sizeof(assets_dir), "%s/assets", root);
  snprintf(files_dir, sizeof(files_dir), "%s/files", root);
  mkdir(lib_dir, 0777);
  mkdir(assets_dir, 0777);
  mkdir(files_dir, 0777);

  int rc = extract_set(apks[0].path, lib_dir, PICK_ENGINE_LIBS, "STEP 1 OF 3: ENGINE MODULES", 10);
  if (rc < 0) goto fail;
  rc = extract_set(apks[0].path, files_dir, PICK_FONTS, "STEP 2 OF 3: FONTS", 1);
  if (rc < 0) goto fail;
  rc = extract_set(apks[1].path, lib_dir, PICK_CSS_LIBS, "STEP 3 OF 3: GAME MODULES", 2);
  if (rc < 0) goto fail;
  rc = extract_set(apks[1].path, assets_dir, PICK_EXTRAS, "STEP 3 OF 3: GAME MODULES", 1);
  if (rc < 0) goto fail;

  char probe[512];
  struct stat st;
  snprintf(probe, sizeof(probe), "%s/liblauncher.so", lib_dir);
  const int have_launcher = stat(probe, &st) == 0;
  snprintf(probe, sizeof(probe), "%s/libclient.so", lib_dir);
  const int have_client = stat(probe, &st) == 0;
  if (!have_launcher || !have_client) {
    static const char *const lines[] = {
      "The APKs did not contain the expected files.",
      "",
      "Check that you have the arm64 Source Engine APK",
      "and the Counter-Strike: Source APK, and that",
      "neither download is truncated.",
    };
    drop_setup_marker(root);
    setup_show_error("SETUP FAILED", lines, 5);
    return -2;
  }

  {
    static const char *const lines[] = { "Starting the game...", "", "The first launch also builds the",
                                         "shader cache, so it is slower than usual." };
    draw_message("SETUP COMPLETE", lines, 4, 0.35f, 0.72f, 0.35f);
    SDL_Delay(1500);
  }
  gui_close();
  return 0;

fail:
  {
    static const char *const lines[] = {
      "Unpacking the APKs failed.",
      "",
      "The SD card may be full or the APK damaged.",
      "See css.log for details.",
    };
    drop_setup_marker(root);
    setup_show_error("SETUP FAILED", lines, 4);
  }
  return -3;
}
