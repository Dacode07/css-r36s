#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "osk.h"
#include "util.h"
#include "config.h"
#include "sdl2_min.h"
#include "font8x8_basic.h"
#include "cursor_img.h"

typedef unsigned GLenum; typedef unsigned GLuint; typedef int GLint;
typedef int GLsizei; typedef float GLfloat; typedef unsigned char GLboolean;
typedef char GLchar; typedef signed char GLbyte; typedef intptr_t GLintptr;

#define GLC_TEXTURE_2D 0x0DE1
#define GLC_BLEND 0x0BE2
#define GLC_DEPTH_TEST 0x0B71
#define GLC_CULL_FACE 0x0B44
#define GLC_SCISSOR_TEST 0x0C11
#define GLC_STENCIL_TEST 0x0B90
#define GLC_SRC_ALPHA 0x0302
#define GLC_ONE_MINUS_SRC_ALPHA 0x0303
#define GLC_TRIANGLES 0x0004
#define GLC_FLOAT 0x1406
#define GLC_UNSIGNED_BYTE 0x1401
#define GLC_ALPHA 0x1906
#define GLC_TEXTURE0 0x84C0
#define GLC_FRAGMENT_SHADER 0x8B30
#define GLC_VERTEX_SHADER 0x8B31
#define GLC_COMPILE_STATUS 0x8B81
#define GLC_LINK_STATUS 0x8B82
#define GLC_CURRENT_PROGRAM 0x8B8D
#define GLC_ACTIVE_TEXTURE 0x84E0
#define GLC_TEXTURE_BINDING_2D 0x8069
#define GLC_ARRAY_BUFFER 0x8892
#define GLC_ARRAY_BUFFER_BINDING 0x8894
#define GLC_VIEWPORT 0x0BA2
#define GLC_VERTEX_ARRAY_BINDING 0x85B5
#define GLC_TEXTURE_MIN_FILTER 0x2801
#define GLC_TEXTURE_MAG_FILTER 0x2800
#define GLC_NEAREST 0x2600
#define GLC_LINEAR 0x2601
#define GLC_RGBA 0x1908
#define GLC_DEPTH_WRITEMASK 0x0B72
#define GLC_BLEND_SRC_RGB 0x80C9
#define GLC_BLEND_DST_RGB 0x80C8
#define GLC_BLEND_SRC_ALPHA 0x80CB
#define GLC_BLEND_DST_ALPHA 0x80CA
#define GLC_VA_ENABLED 0x8622
#define GLC_VA_SIZE 0x8623
#define GLC_VA_STRIDE 0x8624
#define GLC_VA_TYPE 0x8625
#define GLC_VA_NORMALIZED 0x886A
#define GLC_VA_BUFFER_BINDING 0x889F
#define GLC_VA_POINTER 0x8645

static void (*p_glEnable)(GLenum), (*p_glDisable)(GLenum);
static GLboolean (*p_glIsEnabled)(GLenum);
static void (*p_glGetIntegerv)(GLenum, GLint *);
static void (*p_glViewport)(GLint, GLint, GLsizei, GLsizei);
static GLuint (*p_glCreateShader)(GLenum);
static void (*p_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
static void (*p_glCompileShader)(GLuint);
static void (*p_glGetShaderiv)(GLuint, GLenum, GLint *);
static GLuint (*p_glCreateProgram)(void);
static void (*p_glAttachShader)(GLuint, GLuint);
static void (*p_glLinkProgram)(GLuint);
static void (*p_glGetProgramiv)(GLuint, GLenum, GLint *);
static void (*p_glUseProgram)(GLuint);
static GLint (*p_glGetUniformLocation)(GLuint, const GLchar *);
static GLint (*p_glGetAttribLocation)(GLuint, const GLchar *);
static void (*p_glUniform1i)(GLint, GLint);
static void (*p_glUniform1f)(GLint, GLfloat);
static void (*p_glUniform2f)(GLint, GLfloat, GLfloat);
static void (*p_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
static void (*p_glGenTextures)(GLsizei, GLuint *);
static void (*p_glBindTexture)(GLenum, GLuint);
static void (*p_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*p_glTexParameteri)(GLenum, GLenum, GLint);
static void (*p_glActiveTexture)(GLenum);
static void (*p_glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
static void (*p_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
static void (*p_glEnableVertexAttribArray)(GLuint);
static void (*p_glDisableVertexAttribArray)(GLuint);
static void (*p_glDrawArrays)(GLenum, GLint, GLsizei);
static void (*p_glBindBuffer)(GLenum, GLuint);
static void (*p_glBindVertexArray)(GLuint);
static void (*p_glGetVertexAttribiv)(GLuint, GLenum, GLint *);
static void (*p_glGetVertexAttribPointerv)(GLuint, GLenum, void **);
static void (*p_glDepthMask)(GLboolean);
static void (*p_glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
static void (*p_glGetBooleanv)(GLenum, GLboolean *);

static int g_active;
static GLuint g_prog, g_font_tex, g_cursor_tex;
static GLint g_u_color, g_u_solid, g_u_tex, g_a_pos, g_a_uv, g_u_half, g_u_radius;
static int g_ready = -1;

#define OSK_CELL 12
#define OSK_ATLAS_W (16 * OSK_CELL)
#define OSK_ATLAS_H (8 * OSK_CELL)
#define OSK_GLYPH_OFF 2

typedef struct { float x, y, w, h; char label[8]; int action; } OskKey;

#define MAX_KEYS 48
static OskKey g_keys[MAX_KEYS];
static int g_num_keys;
static int g_kb_x, g_kb_y, g_kb_w, g_kb_h;

int osk_is_active(void) { return g_active; }
void osk_close(void) { g_active = 0; }

static void layout_keys(void) {
  static const char *rows[4] = { "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._:" };
  g_num_keys = 0;
  g_kb_w = screen_width * 94 / 100;
  g_kb_h = screen_height * 42 / 100;
  g_kb_x = (screen_width - g_kb_w) / 2;
  g_kb_y = screen_height - g_kb_h - screen_height / 50;

  const float kw = g_kb_w / 10.0f;
  const float kh = g_kb_h / 5.0f;
  for (int r = 0; r < 4; r++) {
    for (int c = 0; rows[r][c]; c++) {
      OskKey *k = &g_keys[g_num_keys++];
      k->x = g_kb_x + c * kw; k->y = g_kb_y + r * kh;
      k->w = kw; k->h = kh;
      k->label[0] = rows[r][c] >= 'a' && rows[r][c] <= 'z' ? rows[r][c] - 32 : rows[r][c];
      k->label[1] = 0;
      k->action = rows[r][c];
    }
  }

  OskKey *k = &g_keys[g_num_keys++];
  k->x = g_kb_x; k->y = g_kb_y + 4 * kh; k->w = kw * 2.5f; k->h = kh;
  strcpy(k->label, "<DEL"); k->action = 1;
  k = &g_keys[g_num_keys++];
  k->x = g_kb_x + kw * 2.5f; k->y = g_kb_y + 4 * kh; k->w = kw * 5.0f; k->h = kh;
  strcpy(k->label, "SPACE"); k->action = 3;
  k = &g_keys[g_num_keys++];
  k->x = g_kb_x + kw * 7.5f; k->y = g_kb_y + 4 * kh; k->w = kw * 2.5f; k->h = kh;
  strcpy(k->label, "ENTER"); k->action = 2;
}

void osk_open(void *window) {
  (void)window;
  layout_keys();
  g_active = 1;
  tracePrintf("OSK opened\n");
}

void osk_toggle(void *window) {
  if (g_active) osk_close(); else osk_open(window);
}

static void *g_win;
static void push_key(int scancode, int sym) {
  SDL_Event_min e;
  for (int down = 1; down >= 0; down--) {
    memset(&e, 0, sizeof(e));
    e.key.type = down ? SDLM_KEYDOWN : SDLM_KEYUP;
    e.key.timestamp = SDL_GetTicks();
    e.key.windowID = SDL_GetWindowID(g_win);
    e.key.state = down;
    e.key.scancode = scancode;
    e.key.sym = sym;
    SDL_PushEvent(&e);
  }
}

static void push_text(char ch) {
  SDL_Event_min e;
  memset(&e, 0, sizeof(e));
  e.type = 0x303;

  ((uint32_t *)&e)[1] = SDL_GetTicks();
  ((uint32_t *)&e)[2] = SDL_GetWindowID(g_win);
  ((char *)&e)[12] = ch;
  ((char *)&e)[13] = 0;
  SDL_PushEvent(&e);
}

int osk_handle_click(int x, int y, int down) {
  if (!g_active)
    return 0;
  if (x < g_kb_x || x >= g_kb_x + g_kb_w || y < g_kb_y || y >= g_kb_y + g_kb_h)
    return 0;
  if (!down)
    return 1;
  for (int i = 0; i < g_num_keys; i++) {
    OskKey *k = &g_keys[i];
    if (x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h) {
      switch (k->action) {
        case 1: push_key(42, 8); break;
        case 2: push_key(40, 13); osk_close(); break;
        case 3: push_text(' '); break;
        default: push_text((char)k->action); break;
      }
      break;
    }
  }
  return 1;
}

static int gl_init(void) {
  #define GP(v, n) v = SDL_GL_GetProcAddress(n); if (!(v)) return 0
  GP(p_glEnable, "glEnable"); GP(p_glDisable, "glDisable");
  GP(p_glIsEnabled, "glIsEnabled"); GP(p_glGetIntegerv, "glGetIntegerv");
  GP(p_glViewport, "glViewport"); GP(p_glCreateShader, "glCreateShader");
  GP(p_glShaderSource, "glShaderSource"); GP(p_glCompileShader, "glCompileShader");
  GP(p_glGetShaderiv, "glGetShaderiv"); GP(p_glCreateProgram, "glCreateProgram");
  GP(p_glAttachShader, "glAttachShader"); GP(p_glLinkProgram, "glLinkProgram");
  GP(p_glGetProgramiv, "glGetProgramiv"); GP(p_glUseProgram, "glUseProgram");
  GP(p_glGetUniformLocation, "glGetUniformLocation");
  GP(p_glGetAttribLocation, "glGetAttribLocation");
  GP(p_glUniform1i, "glUniform1i"); GP(p_glUniform4f, "glUniform4f");
  GP(p_glUniform1f, "glUniform1f"); GP(p_glUniform2f, "glUniform2f");
  GP(p_glGenTextures, "glGenTextures"); GP(p_glBindTexture, "glBindTexture");
  GP(p_glTexImage2D, "glTexImage2D"); GP(p_glTexParameteri, "glTexParameteri");
  GP(p_glActiveTexture, "glActiveTexture");
  GP(p_glBlendFuncSeparate, "glBlendFuncSeparate");
  GP(p_glVertexAttribPointer, "glVertexAttribPointer");
  GP(p_glEnableVertexAttribArray, "glEnableVertexAttribArray");
  GP(p_glDisableVertexAttribArray, "glDisableVertexAttribArray");
  GP(p_glDrawArrays, "glDrawArrays"); GP(p_glBindBuffer, "glBindBuffer");
  GP(p_glGetVertexAttribiv, "glGetVertexAttribiv");
  GP(p_glGetVertexAttribPointerv, "glGetVertexAttribPointerv");
  GP(p_glDepthMask, "glDepthMask"); GP(p_glColorMask, "glColorMask");
  GP(p_glGetBooleanv, "glGetBooleanv");
  p_glBindVertexArray = SDL_GL_GetProcAddress("glBindVertexArray");
  #undef GP

  static const char *vs =
    "attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv;\n"
    "void main(){ v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

  static const char *fs =
    "precision mediump float; varying vec2 v_uv;\n"
    "uniform sampler2D u_tex; uniform vec4 u_color; uniform int u_solid;\n"
    "uniform vec2 u_half; uniform float u_radius;\n"
    "void main(){\n"
    "  if (u_solid == 2) { vec4 t = texture2D(u_tex, v_uv);\n"
    "    gl_FragColor = vec4(t.rgb, t.a * u_color.a); return; }\n"
    "  if (u_solid == 3) {\n"
    "    vec2 p = (v_uv * 2.0 - 1.0) * u_half;\n"
    "    vec2 q = abs(p) - (u_half - vec2(u_radius));\n"
    "    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_radius;\n"
    "    float a = 1.0 - smoothstep(-1.0, 1.0, d);\n"
    "    gl_FragColor = vec4(u_color.rgb, u_color.a * a); return; }\n"
    "  float a = (u_solid == 1) ? 1.0 : texture2D(u_tex, v_uv).a;\n"
    "  gl_FragColor = vec4(u_color.rgb, u_color.a * a); }\n";

  GLuint v = p_glCreateShader(GLC_VERTEX_SHADER);
  p_glShaderSource(v, 1, &vs, 0); p_glCompileShader(v);
  GLuint f = p_glCreateShader(GLC_FRAGMENT_SHADER);
  p_glShaderSource(f, 1, &fs, 0); p_glCompileShader(f);
  GLint ok = 0;
  g_prog = p_glCreateProgram();
  p_glAttachShader(g_prog, v); p_glAttachShader(g_prog, f);
  p_glLinkProgram(g_prog);
  p_glGetProgramiv(g_prog, GLC_LINK_STATUS, &ok);
  if (!ok) { debugPrintf("OSK: shader link FAIL\n"); return 0; }
  g_u_color = p_glGetUniformLocation(g_prog, "u_color");
  g_u_solid = p_glGetUniformLocation(g_prog, "u_solid");
  g_u_tex = p_glGetUniformLocation(g_prog, "u_tex");
  g_u_half = p_glGetUniformLocation(g_prog, "u_half");
  g_u_radius = p_glGetUniformLocation(g_prog, "u_radius");
  g_a_pos = p_glGetAttribLocation(g_prog, "a_pos");
  g_a_uv = p_glGetAttribLocation(g_prog, "a_uv");

  static uint8_t atlas[OSK_ATLAS_H * OSK_ATLAS_W];
  memset(atlas, 0, sizeof(atlas));
  for (int ch = 0; ch < 128; ch++) {
    const int cx = (ch % 16) * OSK_CELL + OSK_GLYPH_OFF;
    const int cy = (ch / 16) * OSK_CELL + OSK_GLYPH_OFF;
    for (int row = 0; row < 8; row++) {
      const uint8_t bits = (uint8_t)font8x8_basic[ch][row];
      for (int col = 0; col < 8; col++)
        if ((bits >> col) & 1)
          atlas[(cy + row) * OSK_ATLAS_W + cx + col] = 255;
    }
  }
  p_glGenTextures(1, &g_font_tex);
  p_glBindTexture(GLC_TEXTURE_2D, g_font_tex);
  p_glTexImage2D(GLC_TEXTURE_2D, 0, GLC_ALPHA, OSK_ATLAS_W, OSK_ATLAS_H, 0,
                 GLC_ALPHA, GLC_UNSIGNED_BYTE, atlas);
  p_glTexParameteri(GLC_TEXTURE_2D, GLC_TEXTURE_MIN_FILTER, GLC_LINEAR);
  p_glTexParameteri(GLC_TEXTURE_2D, GLC_TEXTURE_MAG_FILTER, GLC_LINEAR);

  p_glGenTextures(1, &g_cursor_tex);
  p_glBindTexture(GLC_TEXTURE_2D, g_cursor_tex);
  p_glTexImage2D(GLC_TEXTURE_2D, 0, GLC_RGBA, CURSOR_IMG_W, CURSOR_IMG_H_, 0,
                 GLC_RGBA, GLC_UNSIGNED_BYTE, cursor_img_rgba);
  p_glTexParameteri(GLC_TEXTURE_2D, GLC_TEXTURE_MIN_FILTER, GLC_LINEAR);
  p_glTexParameteri(GLC_TEXTURE_2D, GLC_TEXTURE_MAG_FILTER, GLC_LINEAR);
  return 1;
}

static int g_dw, g_dh, g_rdw, g_rdh;
static float *g_vb; static int g_vb_n, g_vb_cap;

static void vb_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
  if (g_vb_n + 24 > g_vb_cap) return;

  const float x0 = 2.0f * x / g_dw - 1.0f, y0 = 1.0f - 2.0f * y / g_dh;
  const float x1 = 2.0f * (x + w) / g_dw - 1.0f, y1 = 1.0f - 2.0f * (y + h) / g_dh;
  const float q[24] = { x0,y0,u0,v0, x1,y0,u1,v0, x1,y1,u1,v1,
                        x0,y0,u0,v0, x1,y1,u1,v1, x0,y1,u0,v1 };
  memcpy(g_vb + g_vb_n, q, sizeof(q));
  g_vb_n += 24;
}

static void vb_flush(float r, float g, float b, float a, int solid) {
  if (!g_vb_n) return;
  p_glUniform4f(g_u_color, r, g, b, a);
  p_glUniform1i(g_u_solid, solid);
  p_glVertexAttribPointer(g_a_pos, 2, GLC_FLOAT, 0, 16, g_vb);
  p_glVertexAttribPointer(g_a_uv, 2, GLC_FLOAT, 0, 16, g_vb + 2);
  p_glDrawArrays(GLC_TRIANGLES, 0, g_vb_n / 4);
  g_vb_n = 0;
}

static void vb_text(float x, float y, float scale, const char *s) {

  const float cell = OSK_CELL * scale, off = OSK_GLYPH_OFF * scale;
  const float cu = OSK_CELL / (float)OSK_ATLAS_W, cv = OSK_CELL / (float)OSK_ATLAS_H;
  for (; *s; s++, x += 8 * scale) {
    const int ch = (unsigned char)*s;
    if (ch <= 32 || ch > 127) continue;
    const float u0 = (ch % 16) * cu, v0 = (ch / 16) * cv;
    vb_quad(x - off, y - off, cell, cell, u0, v0, u0 + cu, v0 + cv);
  }
}

static void draw_round_rect(float x, float y, float w, float h, float radius,
                            float r, float g, float b, float a) {
  const float px = (float)g_rdw / (float)g_dw;
  const float py = (float)g_rdh / (float)g_dh;
  g_vb_n = 0;
  vb_quad(x, y, w, h, 0, 0, 1, 1);
  p_glUniform2f(g_u_half, w * 0.5f * px, h * 0.5f * py);
  p_glUniform1f(g_u_radius, radius * px);
  vb_flush(r, g, b, a, 3);
}

typedef struct { GLint en, size, type, norm, stride, buf; void *ptr; } AttrSave;
static void attr_save(GLuint i, AttrSave *o) {
  p_glGetVertexAttribiv(i, GLC_VA_ENABLED, &o->en);
  p_glGetVertexAttribiv(i, GLC_VA_SIZE, &o->size);
  p_glGetVertexAttribiv(i, GLC_VA_TYPE, &o->type);
  p_glGetVertexAttribiv(i, GLC_VA_NORMALIZED, &o->norm);
  p_glGetVertexAttribiv(i, GLC_VA_STRIDE, &o->stride);
  p_glGetVertexAttribiv(i, GLC_VA_BUFFER_BINDING, &o->buf);
  p_glGetVertexAttribPointerv(i, GLC_VA_POINTER, &o->ptr);
}
static void attr_restore(GLuint i, const AttrSave *o, GLint prev_ab) {
  p_glBindBuffer(GLC_ARRAY_BUFFER, o->buf);
  if (o->size)
    p_glVertexAttribPointer(i, o->size, o->type, (GLboolean)o->norm, o->stride, o->ptr);
  p_glBindBuffer(GLC_ARRAY_BUFFER, prev_ab);
  if (o->en) p_glEnableVertexAttribArray(i); else p_glDisableVertexAttribArray(i);
}

typedef struct {
  GLint prev_prog, prev_active, prev_tex0, prev_ab, prev_vao, vp[4];
  GLint bsr, bdr, bsa, bda;
  GLboolean bDepth, bBlend, bCull, bScis, bSten, dmask, cmask[4];
  AttrSave a0, a1;
} GLStateSave;

static void draw_end(const GLStateSave *s);

static void draw_begin(void *window, GLuint tex, GLStateSave *s) {
  static float vbuf[24 * 512];
  g_vb = vbuf; g_vb_cap = 24 * 512; g_vb_n = 0;

  g_dw = screen_width; g_dh = screen_height;
  int rdw = 0, rdh = 0;
  SDL_GL_GetDrawableSize(window, &rdw, &rdh);
  if (rdw <= 0) { rdw = screen_width; rdh = screen_height; }
  g_rdw = rdw; g_rdh = rdh;

  s->prev_vao = 0;
  p_glGetIntegerv(GLC_CURRENT_PROGRAM, &s->prev_prog);
  p_glGetIntegerv(GLC_ACTIVE_TEXTURE, &s->prev_active);
  p_glGetIntegerv(GLC_ARRAY_BUFFER_BINDING, &s->prev_ab);
  p_glGetIntegerv(GLC_VIEWPORT, s->vp);
  p_glGetIntegerv(GLC_BLEND_SRC_RGB, &s->bsr); p_glGetIntegerv(GLC_BLEND_DST_RGB, &s->bdr);
  p_glGetIntegerv(GLC_BLEND_SRC_ALPHA, &s->bsa); p_glGetIntegerv(GLC_BLEND_DST_ALPHA, &s->bda);
  if (p_glBindVertexArray) p_glGetIntegerv(GLC_VERTEX_ARRAY_BINDING, &s->prev_vao);
  s->bDepth = p_glIsEnabled(GLC_DEPTH_TEST); s->bBlend = p_glIsEnabled(GLC_BLEND);
  s->bCull = p_glIsEnabled(GLC_CULL_FACE); s->bScis = p_glIsEnabled(GLC_SCISSOR_TEST);
  s->bSten = p_glIsEnabled(GLC_STENCIL_TEST);
  p_glGetBooleanv(GLC_DEPTH_WRITEMASK, &s->dmask);
  p_glGetBooleanv(0x0C23, s->cmask);
  p_glActiveTexture(GLC_TEXTURE0);
  p_glGetIntegerv(GLC_TEXTURE_BINDING_2D, &s->prev_tex0);
  if (p_glBindVertexArray) p_glBindVertexArray(0);
  attr_save((GLuint)g_a_pos, &s->a0);
  attr_save((GLuint)g_a_uv, &s->a1);

  p_glDisable(GLC_DEPTH_TEST); p_glDisable(GLC_CULL_FACE);
  p_glDisable(GLC_SCISSOR_TEST); p_glDisable(GLC_STENCIL_TEST);
  p_glEnable(GLC_BLEND);
  p_glBlendFuncSeparate(GLC_SRC_ALPHA, GLC_ONE_MINUS_SRC_ALPHA, GLC_SRC_ALPHA, GLC_ONE_MINUS_SRC_ALPHA);
  p_glDepthMask(0);
  p_glColorMask(1, 1, 1, 1);
  p_glViewport(0, 0, rdw, rdh);
  p_glUseProgram(g_prog);
  p_glBindBuffer(GLC_ARRAY_BUFFER, 0);
  p_glBindTexture(GLC_TEXTURE_2D, tex);
  p_glUniform1i(g_u_tex, 0);
  p_glEnableVertexAttribArray(g_a_pos);
  p_glEnableVertexAttribArray(g_a_uv);
}

void osk_draw(void *window) {
  if (!g_active)
    return;
  g_win = window;
  if (g_ready < 0)
    g_ready = gl_init();
  if (!g_ready)
    return;

  GLStateSave st;
  draw_begin(window, g_font_tex, &st);

  const float pad = 3.0f;
  const float key_r = g_kb_h / 5.0f * 0.28f;

  draw_round_rect(g_kb_x - 6, g_kb_y - 6, g_kb_w + 12, g_kb_h + 12,
                  14.0f, 0.11f, 0.12f, 0.14f, 0.90f);

  for (int i = 0; i < g_num_keys; i++) {
    OskKey *k = &g_keys[i];

    float cr = 0.30f, cg = 0.32f, cb = 0.37f;
    if (k->action == 2) { cr = 0.20f; cg = 0.55f; cb = 0.42f; }
    else if (k->action == 1) { cr = 0.48f; cg = 0.26f; cb = 0.30f; }
    draw_round_rect(k->x + pad, k->y + pad, k->w - 2 * pad, k->h - 2 * pad,
                    key_r, cr, cg, cb, 0.96f);
  }

  g_vb_n = 0;
  p_glBindTexture(GLC_TEXTURE_2D, g_font_tex);
  for (int i = 0; i < g_num_keys; i++) {
    OskKey *k = &g_keys[i];
    float scale = (k->h - 10) / 8.0f;
    if (scale > 2.2f) scale = 2.2f;
    if (scale < 1.0f) scale = 1.0f;
    const float tw = 8 * scale * (float)strlen(k->label);
    vb_text(k->x + (k->w - tw) / 2, k->y + (k->h - 8 * scale) / 2, scale, k->label);
  }
  vb_flush(0.97f, 0.97f, 1.0f, 1.0f, 0);

  draw_end(&st);
}

static void draw_end(const GLStateSave *s) {
  attr_restore((GLuint)g_a_pos, &s->a0, s->prev_ab);
  attr_restore((GLuint)g_a_uv, &s->a1, s->prev_ab);
  if (p_glBindVertexArray) p_glBindVertexArray((GLuint)s->prev_vao);
  p_glBindBuffer(GLC_ARRAY_BUFFER, s->prev_ab);
  p_glBindTexture(GLC_TEXTURE_2D, (GLuint)s->prev_tex0);
  p_glActiveTexture((GLenum)s->prev_active);
  p_glUseProgram((GLuint)s->prev_prog);
  p_glViewport(s->vp[0], s->vp[1], s->vp[2], s->vp[3]);
  p_glBlendFuncSeparate((GLenum)s->bsr, (GLenum)s->bdr, (GLenum)s->bsa, (GLenum)s->bda);
  if (s->bDepth) p_glEnable(GLC_DEPTH_TEST); else p_glDisable(GLC_DEPTH_TEST);
  if (s->bBlend) p_glEnable(GLC_BLEND); else p_glDisable(GLC_BLEND);
  if (s->bCull) p_glEnable(GLC_CULL_FACE); else p_glDisable(GLC_CULL_FACE);
  if (s->bScis) p_glEnable(GLC_SCISSOR_TEST); else p_glDisable(GLC_SCISSOR_TEST);
  if (s->bSten) p_glEnable(GLC_STENCIL_TEST); else p_glDisable(GLC_STENCIL_TEST);
  p_glDepthMask(s->dmask);
  p_glColorMask(s->cmask[0], s->cmask[1], s->cmask[2], s->cmask[3]);
}

int osk_cursor_draw(void *window, float x, float y) {
  if (g_ready < 0)
    g_ready = gl_init();
  if (!g_ready || !g_cursor_tex)
    return 0;
  GLStateSave st;
  draw_begin(window, g_cursor_tex, &st);

  vb_quad(x - 3, y - 2, CURSOR_IMG_W, CURSOR_IMG_H_, 0, 0, 1, 1);
  vb_flush(1.0f, 1.0f, 1.0f, 1.0f, 2);
  draw_end(&st);
  return 1;
}
