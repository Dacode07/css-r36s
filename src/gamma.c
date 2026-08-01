#include <string.h>
#include <stdint.h>

#include "gamma.h"
#include "util.h"
#include "config.h"
#include "sdl2_min.h"

typedef unsigned GLenum; typedef unsigned GLuint; typedef int GLint;
typedef int GLsizei; typedef float GLfloat; typedef unsigned char GLboolean;
typedef char GLchar;

#define G_TEXTURE_2D 0x0DE1
#define G_RGB 0x1907
#define G_UNSIGNED_BYTE 0x1401
#define G_TEXTURE_MIN_FILTER 0x2801
#define G_TEXTURE_MAG_FILTER 0x2800
#define G_TEXTURE_WRAP_S 0x2802
#define G_TEXTURE_WRAP_T 0x2803
#define G_LINEAR 0x2601
#define G_CLAMP_TO_EDGE 0x812F
#define G_FRAGMENT_SHADER 0x8B30
#define G_VERTEX_SHADER 0x8B31
#define G_LINK_STATUS 0x8B82
#define G_TRIANGLES 0x0004
#define G_FLOAT 0x1406
#define G_TEXTURE0 0x84C0
#define G_ARRAY_BUFFER 0x8892
#define G_DEPTH_TEST 0x0B71
#define G_BLEND 0x0BE2
#define G_CULL_FACE 0x0B44
#define G_SCISSOR_TEST 0x0C11
#define G_STENCIL_TEST 0x0B90
#define G_CURRENT_PROGRAM 0x8B8D
#define G_ACTIVE_TEXTURE 0x84E0
#define G_TEXTURE_BINDING_2D 0x8069
#define G_ARRAY_BUFFER_BINDING 0x8894
#define G_VIEWPORT 0x0BA2
#define G_FRAMEBUFFER 0x8D40
#define G_FRAMEBUFFER_BINDING 0x8CA6
#define G_VERTEX_ARRAY_BINDING 0x85B5
#define G_VA_ENABLED 0x8622
#define G_VA_SIZE 0x8623
#define G_VA_STRIDE 0x8624
#define G_VA_TYPE 0x8625
#define G_VA_NORMALIZED 0x886A
#define G_VA_BUFFER_BINDING 0x889F
#define G_VA_POINTER 0x8645
#define G_DEPTH_WRITEMASK 0x0B72
#define G_COLOR_WRITEMASK 0x0C23

static void (*p_glEnable)(GLenum), (*p_glDisable)(GLenum);
static GLboolean (*p_glIsEnabled)(GLenum);
static void (*p_glGetIntegerv)(GLenum, GLint *);
static void (*p_glGetBooleanv)(GLenum, GLboolean *);
static void (*p_glViewport)(GLint, GLint, GLsizei, GLsizei);
static GLuint (*p_glCreateShader)(GLenum);
static void (*p_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
static void (*p_glCompileShader)(GLuint);
static void (*p_glGetShaderiv)(GLuint, GLenum, GLint *);
static void (*p_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static GLuint (*p_glCreateProgram)(void);
static void (*p_glAttachShader)(GLuint, GLuint);
static void (*p_glLinkProgram)(GLuint);
static void (*p_glGetProgramiv)(GLuint, GLenum, GLint *);
static void (*p_glUseProgram)(GLuint);
static GLint (*p_glGetUniformLocation)(GLuint, const GLchar *);
static GLint (*p_glGetAttribLocation)(GLuint, const GLchar *);
static void (*p_glUniform1i)(GLint, GLint);
static void (*p_glUniform1f)(GLint, GLfloat);
static void (*p_glGenTextures)(GLsizei, GLuint *);
static void (*p_glBindTexture)(GLenum, GLuint);
static void (*p_glCopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
static void (*p_glCopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
static void (*p_glTexParameteri)(GLenum, GLenum, GLint);
static void (*p_glActiveTexture)(GLenum);
static void (*p_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
static void (*p_glEnableVertexAttribArray)(GLuint);
static void (*p_glDisableVertexAttribArray)(GLuint);
static void (*p_glDrawArrays)(GLenum, GLint, GLsizei);
static void (*p_glBindBuffer)(GLenum, GLuint);
static void (*p_glBindFramebuffer)(GLenum, GLuint);
static void (*p_glBindVertexArray)(GLuint);
static void (*p_glGetVertexAttribiv)(GLuint, GLenum, GLint *);
static void (*p_glGetVertexAttribPointerv)(GLuint, GLenum, void **);
static void (*p_glDepthMask)(GLboolean);
static void (*p_glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);

static volatile int g_have_ramp, g_identity = 1;
static float g_gamma = 1.0f;

static GLuint g_prog, g_scene_tex;
static GLint g_u_scene, g_u_gamma, g_u_lift, g_a_pos, g_a_uv;
static float g_lift = 0.0f;
static int g_ready = -1, g_tex_w, g_tex_h;

void gamma_init_from_config(int bright100) {
  if (bright100 <= 0) bright100 = 100;
  if (bright100 == 100) { g_identity = 1; g_have_ramp = 0; return; }
  g_gamma = 100.0f / (float)bright100;

  g_lift = (bright100 - 100) / 1000.0f;
  if (g_lift < 0.0f) g_lift = 0.0f;
  if (g_lift > 0.12f) g_lift = 0.12f;
  g_identity = 0; g_have_ramp = 1;
  tracePrintf("gamma: brightness=%d gamma=%.3f lift=%.3f\n", bright100, g_gamma, g_lift);
}

void gamma_set_ramp(const uint16_t *r, const uint16_t *g, const uint16_t *b) {
  (void)r; (void)g; (void)b;
}

static int gl_init(void) {
  #define GP(v, n) v = SDL_GL_GetProcAddress(n); if (!(v)) { debugPrintf("gamma: missing %s\n", n); return 0; }
  GP(p_glEnable, "glEnable"); GP(p_glDisable, "glDisable");
  GP(p_glIsEnabled, "glIsEnabled"); GP(p_glGetIntegerv, "glGetIntegerv");
  GP(p_glGetBooleanv, "glGetBooleanv"); GP(p_glViewport, "glViewport");
  GP(p_glCreateShader, "glCreateShader"); GP(p_glShaderSource, "glShaderSource");
  GP(p_glCompileShader, "glCompileShader"); GP(p_glGetShaderiv, "glGetShaderiv");
  GP(p_glGetShaderInfoLog, "glGetShaderInfoLog");
  GP(p_glCreateProgram, "glCreateProgram"); GP(p_glAttachShader, "glAttachShader");
  GP(p_glLinkProgram, "glLinkProgram"); GP(p_glGetProgramiv, "glGetProgramiv");
  GP(p_glUseProgram, "glUseProgram"); GP(p_glGetUniformLocation, "glGetUniformLocation");
  GP(p_glGetAttribLocation, "glGetAttribLocation"); GP(p_glUniform1i, "glUniform1i");
  GP(p_glUniform1f, "glUniform1f");
  GP(p_glGenTextures, "glGenTextures"); GP(p_glBindTexture, "glBindTexture");
  GP(p_glCopyTexImage2D, "glCopyTexImage2D"); GP(p_glCopyTexSubImage2D, "glCopyTexSubImage2D");
  GP(p_glTexParameteri, "glTexParameteri"); GP(p_glActiveTexture, "glActiveTexture");
  GP(p_glVertexAttribPointer, "glVertexAttribPointer");
  GP(p_glEnableVertexAttribArray, "glEnableVertexAttribArray");
  GP(p_glDisableVertexAttribArray, "glDisableVertexAttribArray");
  GP(p_glDrawArrays, "glDrawArrays"); GP(p_glBindBuffer, "glBindBuffer");
  GP(p_glBindFramebuffer, "glBindFramebuffer");
  GP(p_glGetVertexAttribiv, "glGetVertexAttribiv");
  GP(p_glGetVertexAttribPointerv, "glGetVertexAttribPointerv");
  GP(p_glDepthMask, "glDepthMask"); GP(p_glColorMask, "glColorMask");
  #undef GP
  p_glBindVertexArray = SDL_GL_GetProcAddress("glBindVertexArray");

  static const char *vs =
    "attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv;\n"
    "void main(){ v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

  static const char *fs =
    "precision mediump float; varying vec2 v_uv;\n"
    "uniform sampler2D u_scene; uniform float u_gamma; uniform float u_lift;\n"
    "void main(){\n"
    "  vec3 c = texture2D(u_scene, v_uv).rgb;\n"
    "  vec3 o = pow(c, vec3(u_gamma)) * (1.0 - u_lift) + u_lift;\n"
    "  gl_FragColor = vec4(o, 1.0);\n"
    "}\n";
  GLint ok = 0;
  GLuint v = p_glCreateShader(G_VERTEX_SHADER);
  p_glShaderSource(v, 1, &vs, 0); p_glCompileShader(v);
  p_glGetShaderiv(v, 0x8B81, &ok);
  if (!ok) { char log[256]; p_glGetShaderInfoLog(v, 256, 0, log); debugPrintf("gamma: vs FAIL %s\n", log); return 0; }
  GLuint f = p_glCreateShader(G_FRAGMENT_SHADER);
  p_glShaderSource(f, 1, &fs, 0); p_glCompileShader(f);
  p_glGetShaderiv(f, 0x8B81, &ok);
  if (!ok) { char log[256]; p_glGetShaderInfoLog(f, 256, 0, log); debugPrintf("gamma: fs FAIL %s\n", log); return 0; }
  g_prog = p_glCreateProgram();
  p_glAttachShader(g_prog, v); p_glAttachShader(g_prog, f);
  p_glLinkProgram(g_prog);
  p_glGetProgramiv(g_prog, G_LINK_STATUS, &ok);
  if (!ok) { debugPrintf("gamma: link FAIL\n"); return 0; }
  g_u_scene = p_glGetUniformLocation(g_prog, "u_scene");
  g_u_gamma = p_glGetUniformLocation(g_prog, "u_gamma");
  g_u_lift = p_glGetUniformLocation(g_prog, "u_lift");
  g_a_pos = p_glGetAttribLocation(g_prog, "a_pos");
  g_a_uv = p_glGetAttribLocation(g_prog, "a_uv");
  p_glGenTextures(1, &g_scene_tex);
  p_glBindTexture(G_TEXTURE_2D, g_scene_tex);
  p_glTexParameteri(G_TEXTURE_2D, G_TEXTURE_MIN_FILTER, G_LINEAR);
  p_glTexParameteri(G_TEXTURE_2D, G_TEXTURE_MAG_FILTER, G_LINEAR);
  p_glTexParameteri(G_TEXTURE_2D, G_TEXTURE_WRAP_S, G_CLAMP_TO_EDGE);
  p_glTexParameteri(G_TEXTURE_2D, G_TEXTURE_WRAP_T, G_CLAMP_TO_EDGE);
  tracePrintf("gamma: gl_init OK (a_pos=%d a_uv=%d)\n", g_a_pos, g_a_uv);
  return 1;
}

typedef struct { GLint en, size, type, norm, stride, buf; void *ptr; } AttrSave;
static void asave(GLuint i, AttrSave *o) {
  p_glGetVertexAttribiv(i, G_VA_ENABLED, &o->en);
  p_glGetVertexAttribiv(i, G_VA_SIZE, &o->size);
  p_glGetVertexAttribiv(i, G_VA_TYPE, &o->type);
  p_glGetVertexAttribiv(i, G_VA_NORMALIZED, &o->norm);
  p_glGetVertexAttribiv(i, G_VA_STRIDE, &o->stride);
  p_glGetVertexAttribiv(i, G_VA_BUFFER_BINDING, &o->buf);
  p_glGetVertexAttribPointerv(i, G_VA_POINTER, &o->ptr);
}
static void arestore(GLuint i, const AttrSave *o, GLint prev_ab) {
  p_glBindBuffer(G_ARRAY_BUFFER, o->buf);
  if (o->size)
    p_glVertexAttribPointer(i, o->size, o->type, (GLboolean)o->norm, o->stride, o->ptr);
  p_glBindBuffer(G_ARRAY_BUFFER, prev_ab);
  if (o->en) p_glEnableVertexAttribArray(i); else p_glDisableVertexAttribArray(i);
}

void gamma_apply(void *window) {
  static int logged;
  if (!g_have_ramp || g_identity)
    return;
  if (g_ready < 0)
    g_ready = gl_init();
  if (!g_ready) {
    if (!logged) { debugPrintf("gamma: gl_init FAILED, brightness not applied\n"); logged = 1; }
    return;
  }
  if (logged < 2) { tracePrintf("gamma: active, gamma=%.2f\n", g_gamma); logged = 2; }

  int w = screen_width, h = screen_height;
  SDL_GL_GetDrawableSize(window, &w, &h);
  if (w <= 0) { w = screen_width; h = screen_height; }

  GLint prev_prog, prev_active, prev_tex0, prev_ab, prev_fbo, prev_vao = 0, vp[4];
  p_glGetIntegerv(G_CURRENT_PROGRAM, &prev_prog);
  p_glGetIntegerv(G_ACTIVE_TEXTURE, &prev_active);
  p_glGetIntegerv(G_ARRAY_BUFFER_BINDING, &prev_ab);
  p_glGetIntegerv(G_FRAMEBUFFER_BINDING, &prev_fbo);
  p_glGetIntegerv(G_VIEWPORT, vp);
  if (p_glBindVertexArray) p_glGetIntegerv(G_VERTEX_ARRAY_BINDING, &prev_vao);
  const GLboolean bD = p_glIsEnabled(G_DEPTH_TEST), bB = p_glIsEnabled(G_BLEND),
                  bC = p_glIsEnabled(G_CULL_FACE), bS = p_glIsEnabled(G_SCISSOR_TEST),
                  bT = p_glIsEnabled(G_STENCIL_TEST);
  GLboolean dmask; p_glGetBooleanv(G_DEPTH_WRITEMASK, &dmask);
  GLboolean cmask[4]; p_glGetBooleanv(G_COLOR_WRITEMASK, cmask);
  p_glActiveTexture(G_TEXTURE0);
  p_glGetIntegerv(G_TEXTURE_BINDING_2D, &prev_tex0);
  if (p_glBindVertexArray) p_glBindVertexArray(0);
  AttrSave a0, a1; asave((GLuint)g_a_pos, &a0); asave((GLuint)g_a_uv, &a1);

  p_glBindFramebuffer(G_FRAMEBUFFER, 0);

  p_glBindTexture(G_TEXTURE_2D, g_scene_tex);
  if (w != g_tex_w || h != g_tex_h) {
    p_glCopyTexImage2D(G_TEXTURE_2D, 0, G_RGB, 0, 0, w, h, 0);
    g_tex_w = w; g_tex_h = h;
  } else {
    p_glCopyTexSubImage2D(G_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
  }

  p_glDisable(G_DEPTH_TEST); p_glDisable(G_BLEND); p_glDisable(G_CULL_FACE);
  p_glDisable(G_SCISSOR_TEST); p_glDisable(G_STENCIL_TEST);
  p_glDepthMask(0); p_glColorMask(1, 1, 1, 1);
  p_glViewport(0, 0, w, h);
  p_glUseProgram(g_prog);
  p_glBindBuffer(G_ARRAY_BUFFER, 0);
  static const float quad[] = { -1,-1, 0,0,  3,-1, 2,0,  -1,3, 0,2 };
  p_glEnableVertexAttribArray(g_a_pos);
  p_glEnableVertexAttribArray(g_a_uv);
  p_glVertexAttribPointer(g_a_pos, 2, G_FLOAT, 0, 16, quad);
  p_glVertexAttribPointer(g_a_uv, 2, G_FLOAT, 0, 16, quad + 2);
  p_glActiveTexture(G_TEXTURE0); p_glBindTexture(G_TEXTURE_2D, g_scene_tex);
  p_glUniform1i(g_u_scene, 0);
  p_glUniform1f(g_u_gamma, g_gamma);
  p_glUniform1f(g_u_lift, g_lift);
  p_glDrawArrays(G_TRIANGLES, 0, 3);

  arestore((GLuint)g_a_pos, &a0, prev_ab);
  arestore((GLuint)g_a_uv, &a1, prev_ab);
  if (p_glBindVertexArray) p_glBindVertexArray((GLuint)prev_vao);
  p_glBindBuffer(G_ARRAY_BUFFER, prev_ab);
  p_glBindFramebuffer(G_FRAMEBUFFER, (GLuint)prev_fbo);
  p_glActiveTexture(G_TEXTURE0); p_glBindTexture(G_TEXTURE_2D, (GLuint)prev_tex0);
  p_glActiveTexture((GLenum)prev_active);
  p_glUseProgram((GLuint)prev_prog);
  p_glViewport(vp[0], vp[1], vp[2], vp[3]);
  if (bD) p_glEnable(G_DEPTH_TEST);
  if (bB) p_glEnable(G_BLEND);
  if (bC) p_glEnable(G_CULL_FACE);
  if (bS) p_glEnable(G_SCISSOR_TEST);
  if (bT) p_glEnable(G_STENCIL_TEST);
  p_glDepthMask(dmask);
  p_glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
}
