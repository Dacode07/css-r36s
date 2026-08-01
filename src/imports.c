#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <wchar.h>
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <time.h>
#include <dirent.h>
#include <locale.h>
#include <malloc.h>
#include <fnmatch.h>
#include <libgen.h>
#include <poll.h>
#include <sched.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <signal.h>
#include <pwd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/auxv.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "config.h"
#include "so_util.h"
#include "util.h"
#include "libc_shim.h"
#include "pthread_shim.h"
#include "dl_emu.h"
#include "imports.h"
#include "sdl2_min.h"
#include "osk.h"
#include "gamma.h"
#include "aim_assist.h"

static uint64_t __stack_chk_guard_fake = 0x4242424242424242;

#define XF(name) extern void name(void);

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  (void)prio; (void)tag;
  va_list list;
  static char string[0x1000];

  va_start(list, fmt);
  vsnprintf(string, sizeof(string), fmt, list);
  va_end(list);

  engineLine(string);
  return 0;
}

int __android_log_write(int prio, const char *tag, const char *text) {
  (void)prio; (void)tag;
  engineLine(text);
  return 0;
}

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list va) {
  (void)prio; (void)tag;
  static char string[0x1000];
  vsnprintf(string, sizeof(string), fmt, va);
  engineLine(string);
  return 0;
}

extern int *__errno_location(void);
extern int __cxa_atexit(void (*fn)(void *), void *arg, void *dso);
extern int __cxa_thread_atexit_impl(void (*fn)(void *), void *arg, void *dso);
extern void __stack_chk_fail(void);

static int sched_get_priority_max_fake(int policy) {
  (void)policy;
  return 0;
}

static int SDL_GetDesktopDisplayMode_fake(int idx, SDL_DisplayMode_min *mode) {
  const int r = SDL_GetDesktopDisplayMode(idx, mode);
  if (r == 0 && mode) { mode->w = screen_width; mode->h = screen_height; }
  return r;
}

static int SDL_GetCurrentDisplayMode_fake(int idx, SDL_DisplayMode_min *mode) {
  const int r = SDL_GetCurrentDisplayMode(idx, mode);
  if (r == 0 && mode) { mode->w = screen_width; mode->h = screen_height; }
  return r;
}

static int SDL_GetDisplayBounds_fake(int idx, SDL_Rect_min *rect) {
  const int r = SDL_GetDisplayBounds(idx, rect);
  if (r == 0 && rect) { rect->x = 0; rect->y = 0; rect->w = screen_width; rect->h = screen_height; }
  return r;
}

static void SDL_SetWindowSize_fake(void *window, int w, int h) {
  (void)w; (void)h;
  SDL_SetWindowSize(window, screen_width, screen_height);
}

static int SDL_SetWindowDisplayMode_fake(void *window, const SDL_DisplayMode_min *mode) {
  SDL_DisplayMode_min m;
  if (mode) {
    m = *mode;
    m.w = screen_width;
    m.h = screen_height;
    return SDL_SetWindowDisplayMode(window, &m);
  }
  return SDL_SetWindowDisplayMode(window, NULL);
}

static int SDL_SetWindowFullscreen_fake(void *window, uint32_t flags) {
  (void)window; (void)flags;
  return 0;
}

static int SDL_GetWindowWMInfo_fake(void *window, void *info) {
  (void)window; (void)info;
  return 1;
}

static int SDL_SetWindowGammaRamp_fake(void *window, const uint16_t *r, const uint16_t *g, const uint16_t *b) {
  (void)window;
  gamma_set_ramp(r, g, b);
  return 0;
}

static void *g_mouse_window;
static uint32_t g_mouse_last_move;

static int SDL_GL_LoadLibrary_fake(const char *path) {

  int r = SDL_GL_LoadLibrary(NULL);
  if (r != 0)
    debugPrintf("SDL_GL_LoadLibrary(default) failed: %s\n", SDL_GetError());
  if (r != 0 && path) {
    r = SDL_GL_LoadLibrary(path);
    debugPrintf("SDL_GL_LoadLibrary(%s retry) -> %d%s%s\n", path, r,
                r ? " err: " : "", r ? SDL_GetError() : "");
  }
  return r;
}

static int SDL_GL_SetAttribute_fake(int attr, int value) {
  tracePrintf("SDL_GL_SetAttribute(%d, %d)\n", attr, value);
  return SDL_GL_SetAttribute(attr, value);
}

static void *SDL_CreateWindow_fake(const char *title, int x, int y, int w, int h, uint32_t flags) {
  void *win = SDL_CreateWindow(title, x, y, w, h, flags);
  if (!win)
    debugPrintf("SDL_CreateWindow(%dx%d) failed: %s\n", w, h, SDL_GetError());
  if (win)
    g_mouse_window = win;
  return win;
}

static void *SDL_GL_CreateContext_fake(void *window) {
  void *ctx = SDL_GL_CreateContext(window);
  if (!ctx)
    debugPrintf("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
  if (ctx) {
    extern int g_gl_context_ready;
    g_gl_context_ready = 1;

    void *p = SDL_GL_GetProcAddress("glGetString");
    if (!p)
      debugPrintf("selftest: SDL cannot resolve glGetString: %s\n", SDL_GetError());
    extern void *eglGetProcAddress_fake(const char *);
    if (!eglGetProcAddress_fake("glGetString"))
      debugPrintf("selftest: resolver chain cannot find glGetString\n");

    void (*p_geti)(unsigned, int *) = SDL_GL_GetProcAddress("glGetIntegerv");
    if (p_geti) {
      int depth = -1, stencil = -1, samples = -1;
      p_geti(0x0D56 , &depth);
      p_geti(0x0D57 , &stencil);
      p_geti(0x80A9 , &samples);
      if (depth <= 0)
        debugPrintf("WARNING: no depth buffer (depth=%d stencil=%d samples=%d)\n",
                    depth, stencil, samples);
      else
        tracePrintf("framebuffer: depth=%d stencil=%d samples=%d\n", depth, stencil, samples);
    }
  }
  return ctx;
}

static volatile int g_mouse_relative;
static float g_mouse_x, g_mouse_y;
static float g_mouse_fx, g_mouse_fy;
static int g_mouse_ax, g_mouse_ay;
static int g_mouse_btn_down;
static volatile int g_aim_precision;
static volatile int g_cursor_visible = 1;
static uint32_t g_mouse_prev_tick;

#define MOUSE_DEADZONE 6000

static int g_mouse_runtime_on = 1;

static int mouse_mode_active(void) {
  return config.mouse_enable && g_mouse_runtime_on && g_mouse_window;
}

static void mouse_push_click(int down) {
  if (osk_handle_click((int)g_mouse_x, (int)g_mouse_y, down)) {
    g_mouse_btn_down = down;
    return;
  }
  SDL_Event_min e;
  memset(&e, 0, sizeof(e));
  e.button.type = down ? SDLM_MOUSEBUTTONDOWN : SDLM_MOUSEBUTTONUP;
  e.button.timestamp = SDL_GetTicks();
  e.button.windowID = SDL_GetWindowID(g_mouse_window);
  e.button.button = 1;
  e.button.state = down ? 1 : 0;
  e.button.clicks = 1;
  e.button.x = (int)g_mouse_x;
  e.button.y = (int)g_mouse_y;
  g_mouse_btn_down = down;
  SDL_PushEvent(&e);
}

static float mouse_axis_norm(int v) {
  if (v > -MOUSE_DEADZONE && v < MOUSE_DEADZONE)
    return 0.0f;
  return (float)v / 32768.0f;
}

static void mouse_tick(void) {
  if (!mouse_mode_active()) {
    g_mouse_prev_tick = 0;
    return;
  }
  const uint32_t now = SDL_GetTicks();
  if (!g_mouse_prev_tick) {
    g_mouse_prev_tick = now;
    g_mouse_x = screen_width / 2.0f;
    g_mouse_y = screen_height / 2.0f;
    return;
  }
  float dt = (now - g_mouse_prev_tick) / 1000.0f;
  g_mouse_prev_tick = now;
  if (dt > 0.1f) dt = 0.1f;

  const float fx = mouse_axis_norm(g_mouse_ax);
  const float fy = mouse_axis_norm(g_mouse_ay);
  if (fx == 0.0f && fy == 0.0f) {
    g_mouse_fx = g_mouse_fy = 0.0f;
    return;
  }

  float spd = (float)config.mouse_speed;
  if (g_aim_precision && !g_cursor_visible)
    spd = spd * (float)config.aim_slow / 100.0f;
  g_mouse_fx += fx * spd * dt;
  g_mouse_fy += fy * spd * dt;
  const int dx = (int)g_mouse_fx;
  const int dy = (int)g_mouse_fy;
  if (!dx && !dy)
    return;
  g_mouse_fx -= dx;
  g_mouse_fy -= dy;

  g_mouse_x += dx;
  g_mouse_y += dy;
  if (g_mouse_x < 0) g_mouse_x = 0;
  if (g_mouse_y < 0) g_mouse_y = 0;
  if (g_mouse_x > screen_width - 1) g_mouse_x = screen_width - 1;
  if (g_mouse_y > screen_height - 1) g_mouse_y = screen_height - 1;

  g_mouse_last_move = now;
  SDL_WarpMouseInWindow(g_mouse_window, (int)g_mouse_x, (int)g_mouse_y);
  static int warp_logged;
  if (log_verbose && warp_logged < 10) {
    tracePrintf("mouse warp -> %d,%d (dx=%d dy=%d)\n", (int)g_mouse_x, (int)g_mouse_y, dx, dy);
    warp_logged++;
  }
}

static uint32_t SDL_GetMouseState_fake(int *x, int *y) {
  uint32_t mask = SDL_GetMouseState(x, y);
  if (mouse_mode_active() && g_mouse_btn_down)
    mask |= 1;
  return mask;
}

#define BIND_KEY   0
#define BIND_MOUSE 1
#define BIND_WHEEL 2

static const struct { int btn; int kind; int a; int b; } joy_binds[] = {
  { 3,  BIND_WHEEL,   0, -1 },
  { 14, BIND_KEY,   224, 1073742048 },
  { 2,  BIND_KEY,     5, 'b' },
  { 11, BIND_KEY,    43, 9 },
  { 6,  BIND_KEY,    21, 'r' },
  { 7,  BIND_MOUSE,   3, 0 },
  { 5,  BIND_MOUSE,   1, 0 },

  { 0,  BIND_KEY,    44, ' ' },
  { 1,  BIND_KEY,     8, 'e' },
  { 8,  BIND_KEY,    30, '1' },
  { 9,  BIND_KEY,    31, '2' },
  { 10, BIND_KEY,    10, 'g' },
};

static const struct { int btn; int kind; int a; int b; } menu_binds[] = {
  { 8,  BIND_KEY, 82, 1073741906 },
  { 9,  BIND_KEY, 81, 1073741905 },
  { 10, BIND_KEY, 80, 1073741904 },
  { 11, BIND_KEY, 79, 1073741903 },
  { 0,  BIND_KEY, 40, 13 },
  { 1,  BIND_KEY, 41, 27 },
};

static int binds_emit(const void *tbl, unsigned n, SDL_Event_min *e, int down) {
  const struct { int btn; int kind; int a; int b; } *binds = tbl;
  for (unsigned i = 0; i < n; i++) {
    if (binds[i].btn != (int)e->jbutton.button)
      continue;
    SDL_Event_min out;
    memset(&out, 0, sizeof(out));
    switch (binds[i].kind) {
      case BIND_KEY:
        out.key.type = down ? SDLM_KEYDOWN : SDLM_KEYUP;
        out.key.timestamp = SDL_GetTicks();
        out.key.windowID = SDL_GetWindowID(g_mouse_window);
        out.key.state = down ? 1 : 0;
        out.key.scancode = binds[i].a;
        out.key.sym = binds[i].b;
        SDL_PushEvent(&out);
        return 1;
      case BIND_MOUSE:
        if (binds[i].a == 1 && mouse_mode_active())
          return 0;
        out.button.type = down ? SDLM_MOUSEBUTTONDOWN : SDLM_MOUSEBUTTONUP;
        out.button.timestamp = SDL_GetTicks();
        out.button.windowID = SDL_GetWindowID(g_mouse_window);
        out.button.button = binds[i].a;
        out.button.state = down ? 1 : 0;
        out.button.clicks = 1;
        out.button.x = (int)g_mouse_x;
        out.button.y = (int)g_mouse_y;
        SDL_PushEvent(&out);
        return 1;
      case BIND_WHEEL:
        if (down) {
          out.wheel.type = SDLM_MOUSEWHEEL;
          out.wheel.timestamp = SDL_GetTicks();
          out.wheel.windowID = SDL_GetWindowID(g_mouse_window);
          out.wheel.x = binds[i].a;
          out.wheel.y = binds[i].b;
          SDL_PushEvent(&out);
        }
        return 1;
    }
  }
  return 0;
}

static int joybind_handle(SDL_Event_min *e) {
  if (e->type != SDLM_JOYBUTTONDOWN && e->type != SDLM_JOYBUTTONUP)
    return 0;
  const int down = (e->type == SDLM_JOYBUTTONDOWN);

  if (g_aim_precision &&
      (e->jbutton.button == 2 || e->jbutton.button == 3)) {
    SDL_Event_min out;
    memset(&out, 0, sizeof(out));
    out.key.type = down ? SDLM_KEYDOWN : SDLM_KEYUP;
    out.key.timestamp = SDL_GetTicks();
    out.key.windowID = SDL_GetWindowID(g_mouse_window);
    out.key.state = down ? 1 : 0;
    if (e->jbutton.button == 2) { out.key.scancode = 60; out.key.sym = 1073741884; }
    else                        { out.key.scancode = 61; out.key.sym = 1073741885; }
    SDL_PushEvent(&out);
    return 1;
  }
  if (mouse_mode_active()) {

    if (binds_emit(menu_binds, sizeof(menu_binds)/sizeof(*menu_binds), e, down))
      return 1;
    return binds_emit(joy_binds, sizeof(joy_binds)/sizeof(*joy_binds), e, down);
  }
  return binds_emit(joy_binds, sizeof(joy_binds)/sizeof(*joy_binds), e, down);
}

static int event_suppressed_for_engine(SDL_Event_min *e) {
  switch (e->type) {
    case SDLM_CTRLBUTTONDOWN:
    case SDLM_CTRLBUTTONUP:
      return 1;
    case SDLM_JOYBUTTONDOWN:
    case SDLM_JOYBUTTONUP: {
      const int b = e->jbutton.button;
      if (b == config.esc_button || b == config.mouse_toggle || b == config.mouse_click || b == config.aim_btn)
        return 1;
      for (unsigned i = 0; i < sizeof(joy_binds)/sizeof(*joy_binds); i++)
        if (joy_binds[i].btn == b)
          return 1;
      return 0;
    }
    case SDLM_JOYAXISMOTION:

      return mouse_mode_active() &&
             (e->jaxis.axis == config.mouse_axis_x || e->jaxis.axis == config.mouse_axis_y);
    case SDLM_CTRLAXISMOTION:
      if (e->jaxis.axis == 4 || e->jaxis.axis == 5)
        return 1;
      return mouse_mode_active() && (e->jaxis.axis == 2 || e->jaxis.axis == 3);
  }
  return 0;
}

#define MAX_EVENT_WATCHES 8
typedef int (*sdl_event_filter)(void *userdata, SDL_Event_min *event);
static struct { sdl_event_filter fn; void *ud; } g_watches[MAX_EVENT_WATCHES];

static int watch_trampoline(void *slot_p, SDL_Event_min *e) {
  const int slot = (int)(intptr_t)slot_p;
  if (e && event_suppressed_for_engine(e))
    return 0;
  if (log_verbose && e && slot == 0 && e->type >= 0x300 && e->type < 0x800 &&
      e->type != SDLM_MOUSEMOTION) {
    static int wlog;
    if (wlog < 80) {
      tracePrintf("engine<- event t=%x axis=%d btn=%d val=%d\n", e->type,
                  e->jaxis.axis, e->jbutton.button, e->jaxis.value);
      wlog++;
    }
  }
  return g_watches[slot].fn ? g_watches[slot].fn(g_watches[slot].ud, e) : 0;
}

static void SDL_AddEventWatch_fake(sdl_event_filter fn, void *ud) {
  extern void SDL_AddEventWatch(void *, void *);
  for (int i = 0; i < MAX_EVENT_WATCHES; i++) {
    if (!g_watches[i].fn) {
      g_watches[i].fn = fn;
      g_watches[i].ud = ud;
      tracePrintf("SDL_AddEventWatch wrapped (slot %d)\n", i);
      SDL_AddEventWatch((void *)watch_trampoline, (void *)(intptr_t)i);
      return;
    }
  }
  SDL_AddEventWatch((void *)fn, ud);
}

static void SDL_DelEventWatch_fake(sdl_event_filter fn, void *ud) {
  extern void SDL_DelEventWatch(void *, void *);
  for (int i = 0; i < MAX_EVENT_WATCHES; i++) {
    if (g_watches[i].fn == fn && g_watches[i].ud == ud) {
      SDL_DelEventWatch((void *)watch_trampoline, (void *)(intptr_t)i);
      g_watches[i].fn = NULL;
      g_watches[i].ud = NULL;
      return;
    }
  }
  SDL_DelEventWatch((void *)fn, ud);
}

static int mouse_filter_event(SDL_Event_min *e) {
  if (log_verbose && e->type == SDLM_JOYBUTTONDOWN) {
    static int blog;
    if (blog < 200) { tracePrintf("joybtn %d down\n", e->jbutton.button); blog++; }
  }

  if (g_mouse_window &&
      (e->type == SDLM_JOYBUTTONDOWN || e->type == SDLM_JOYBUTTONUP) &&
      e->jbutton.button == config.esc_button) {
    if (e->type == SDLM_JOYBUTTONDOWN) {

      const int visible = engine_gameui_visible();
      const char *cmd = visible > 0 ? "gameui_hide" : "gameui_activate";
      const int ok = engine_run_command(cmd);
      tracePrintf("Start -> %s (visible=%d) ret=%d\n", cmd, visible, ok);
      if (!ok) {

        SDL_Event_min k;
        memset(&k, 0, sizeof(k));
        k.key.type = SDLM_KEYDOWN;
        k.key.timestamp = SDL_GetTicks();
        k.key.windowID = SDL_GetWindowID(g_mouse_window);
        k.key.state = 1;
        k.key.scancode = 41;
        k.key.sym = 27;
        SDL_PushEvent(&k);
        k.key.type = SDLM_KEYUP; k.key.state = 0;
        SDL_PushEvent(&k);
      }
    }
    return 1;
  }

  if (g_mouse_window &&
      (e->type == SDLM_JOYBUTTONDOWN || e->type == SDLM_JOYBUTTONUP) &&
      e->jbutton.button == 12) {
    if (e->type == SDLM_JOYBUTTONDOWN)
      osk_toggle(g_mouse_window);
    return 1;
  }

  if (g_mouse_window &&
      (e->type == SDLM_JOYBUTTONDOWN || e->type == SDLM_JOYBUTTONUP) &&
      e->jbutton.button == config.aim_btn) {
    g_aim_precision = (e->type == SDLM_JOYBUTTONDOWN);
    return 1;
  }

  if (config.mouse_enable && g_mouse_window &&
      (e->type == SDLM_JOYBUTTONDOWN || e->type == SDLM_JOYBUTTONUP) &&
      e->jbutton.button == config.mouse_toggle) {
    if (e->type == SDLM_JOYBUTTONDOWN) {
      g_mouse_runtime_on = !g_mouse_runtime_on;
      debugPrintf("mouse mode: %s\n", g_mouse_runtime_on ? "on" : "off");
    }
    return 1;
  }

  if (g_mouse_window && joybind_handle(e))
    return 1;

  if (!mouse_mode_active())
    return 0;
  switch (e->type) {
    case SDLM_JOYAXISMOTION: {

      static int axis_logged;
      if (log_verbose && axis_logged < 16 &&
          (e->jaxis.value > 20000 || e->jaxis.value < -20000)) {
        tracePrintf("joyaxis %d val %d (mouse axes: x=%d y=%d)\n",
                    e->jaxis.axis, e->jaxis.value, config.mouse_axis_x, config.mouse_axis_y);
        axis_logged++;
      }
      if (e->jaxis.axis == config.mouse_axis_x) { g_mouse_ax = e->jaxis.value; return 1; }
      if (e->jaxis.axis == config.mouse_axis_y) { g_mouse_ay = e->jaxis.value; return 1; }
      return 0;
    }
    case SDLM_CTRLAXISMOTION:
      if (e->jaxis.axis == 2) { g_mouse_ax = e->jaxis.value; return 1; }
      if (e->jaxis.axis == 3) { g_mouse_ay = e->jaxis.value; return 1; }
      return 0;
    case SDLM_JOYBUTTONDOWN:
    case SDLM_JOYBUTTONUP:
      if (e->jbutton.button == config.mouse_toggle) {
        if (e->type == SDLM_JOYBUTTONDOWN) {
          g_mouse_runtime_on = !g_mouse_runtime_on;
          debugPrintf("mouse mode: %s\n", g_mouse_runtime_on ? "on" : "off");
        }
        return 1;
      }
      if (e->jbutton.button == config.mouse_click) {
        mouse_push_click(e->type == SDLM_JOYBUTTONDOWN);
        return 1;
      }
      return 0;
  }
  return 0;
}

static int SDL_PollEvent_fake(SDL_Event_min *ev) {
  mouse_tick();
  aim_assist_tick(g_aim_precision && !g_cursor_visible);
  for (;;) {
    const int r = SDL_PollEvent(ev);
    if (!r)
      return 0;
    if (ev && mouse_filter_event(ev))
      continue;
    return r;
  }
}

static int SDL_WaitEventTimeout_fake(SDL_Event_min *ev, int timeout) {
  mouse_tick();
  aim_assist_tick(g_aim_precision && !g_cursor_visible);
  for (;;) {
    const int r = SDL_WaitEventTimeout(ev, timeout);
    if (!r)
      return 0;
    if (ev && mouse_filter_event(ev)) {
      timeout = 0;
      continue;
    }
    return r;
  }
}

static int SDL_SetRelativeMouseMode_fake(int enabled) {
  if (g_mouse_relative != enabled)
    tracePrintf("SDL_SetRelativeMouseMode(%d) requested; kept off\n", enabled);
  g_mouse_relative = enabled;

  return SDL_SetRelativeMouseMode(0);
}

static void SDL_SetWindowGrab_fake(void *window, int grabbed) {
  extern void SDL_SetWindowGrab(void *, int);
  tracePrintf("SDL_SetWindowGrab(%d)\n", grabbed);
  SDL_SetWindowGrab(window, grabbed);
}

extern void SDL_StartTextInput(void);
static void SDL_StartTextInput_fake(void) {
  SDL_StartTextInput();
  if (config.mouse_enable && g_mouse_window)
    osk_open(g_mouse_window);
}

static int SDL_ShowCursor_fake(int toggle) {

  if (toggle >= 0) {
    if (g_cursor_visible != toggle)
      tracePrintf("SDL_ShowCursor(%d) requested; forced to 1 while mouse mode is on\n", toggle);
    g_cursor_visible = toggle;
  }
  return SDL_ShowCursor(config.mouse_enable ? 1 : toggle);
}

typedef void (*glfn_enable)(unsigned cap);
typedef unsigned char (*glfn_isenabled)(unsigned cap);
typedef void (*glfn_scissor)(int x, int y, int w, int h);
typedef void (*glfn_clearcolor)(float r, float g, float b, float a);
typedef void (*glfn_clear)(unsigned mask);
typedef void (*glfn_getfloatv)(unsigned pname, float *out);
typedef void (*glfn_getintv)(unsigned pname, int *out);

static void draw_cursor_overlay(void) {
  static glfn_enable p_glEnable, p_glDisable;
  static glfn_isenabled p_glIsEnabled;
  static glfn_scissor p_glScissor;
  static glfn_clearcolor p_glClearColor;
  static glfn_clear p_glClear;
  static glfn_getfloatv p_glGetFloatv;
  static glfn_getintv p_glGetIntegerv;
  static int gl_ready = -1;

  if (gl_ready < 0) {
    p_glEnable = (glfn_enable)SDL_GL_GetProcAddress("glEnable");
    p_glDisable = (glfn_enable)SDL_GL_GetProcAddress("glDisable");
    p_glIsEnabled = (glfn_isenabled)SDL_GL_GetProcAddress("glIsEnabled");
    p_glScissor = (glfn_scissor)SDL_GL_GetProcAddress("glScissor");
    p_glClearColor = (glfn_clearcolor)SDL_GL_GetProcAddress("glClearColor");
    p_glClear = (glfn_clear)SDL_GL_GetProcAddress("glClear");
    p_glGetFloatv = (glfn_getfloatv)SDL_GL_GetProcAddress("glGetFloatv");
    p_glGetIntegerv = (glfn_getintv)SDL_GL_GetProcAddress("glGetIntegerv");
    gl_ready = (p_glEnable && p_glDisable && p_glIsEnabled && p_glScissor &&
                p_glClearColor && p_glClear && p_glGetFloatv && p_glGetIntegerv);
    if (!gl_ready)
      debugPrintf("cursor overlay: GL entry points missing, using fallback\n");
  }
  if (!gl_ready)
    return;

  float prev_clear[4];
  int prev_scissor[4];
  p_glGetFloatv(0x0C22 , prev_clear);
  p_glGetIntegerv(0x0C10 , prev_scissor);
  const unsigned char was_scissor = p_glIsEnabled(0x0C11 );

  int dw = screen_width, dh = screen_height;
  SDL_GL_GetDrawableSize(g_mouse_window, &dw, &dh);
  if (dw <= 0) dw = screen_width;
  if (dh <= 0) dh = screen_height;
  const int cx = (int)(g_mouse_x * dw / screen_width);
  const int cy = dh - 1 - (int)(g_mouse_y * dh / screen_height);
  p_glEnable(0x0C11);

  p_glScissor(cx - 5, cy - 5, 11, 11);
  p_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  p_glClear(0x4000 );
  p_glScissor(cx - 3, cy - 3, 7, 7);
  p_glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  p_glClear(0x4000);

  if (!was_scissor)
    p_glDisable(0x0C11);
  p_glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
  p_glClearColor(prev_clear[0], prev_clear[1], prev_clear[2], prev_clear[3]);
}

static int SDL_GL_SetSwapInterval_fake(int interval) {
  extern int SDL_GL_SetSwapInterval(int);
  tracePrintf("SDL_GL_SetSwapInterval(%d) -> forced to 0 (vsync off)\n", interval);
  return SDL_GL_SetSwapInterval(0);
}

static volatile uint32_t g_last_swap_ticks;

typedef struct { int tid; uint64_t pc, lr; int ok; } PcSample;

static volatile uint64_t g_sig_pc, g_sig_lr;
static volatile int g_sig_seq;

static void pc_probe_handler(int sig, siginfo_t *si, void *ucv) {
  (void)sig; (void)si;
  ucontext_t *uc = ucv;
  g_sig_pc = uc->uc_mcontext.pc;
  g_sig_lr = uc->uc_mcontext.regs[30];
  __atomic_add_fetch(&g_sig_seq, 1, __ATOMIC_SEQ_CST);
}

static void sample_thread_pc(int tid, PcSample *out) {
  static int handler_ready;
  out->tid = tid; out->ok = 0;
  if (!handler_ready) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pc_probe_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL) != 0)
      return;
    handler_ready = 1;
  }
  const int seq0 = g_sig_seq;
  if (syscall(SYS_tgkill, getpid(), tid, SIGPROF) != 0)
    return;
  for (int i = 0; i < 100 && g_sig_seq == seq0; i++)
    usleep(1000);
  if (g_sig_seq == seq0)
    return;
  out->pc = g_sig_pc;
  out->lr = g_sig_lr;
  out->ok = 1;
}

static void *stall_watchdog(void *arg) {
  (void)arg;
  uint32_t dumped_for = 0;
  for (;;) {
    usleep(200 * 1000);
    const uint32_t last = g_last_swap_ticks;
    if (!last)
      continue;
    const uint32_t now = SDL_GetTicks();
    if (now - last <= 700 || last == dumped_for)
      continue;
    dumped_for = last;
    float up = 0.0f;
    FILE *uf = fopen("/proc/uptime", "r");
    if (uf) { if (fscanf(uf, "%f", &up) != 1) up = 0.0f; fclose(uf); }
    debugPrintf("STALL-DUMP: no frame for %u ms (uptime %.2f), running/blocked threads:\n",
                now - last, up);
    int tids[8];
    int ntid = 0;
    const long self_tid = syscall(SYS_gettid);
    DIR *d = opendir("/proc/self/task");
    if (!d)
      continue;
    struct dirent *de;
    while ((de = readdir(d))) {
      if (de->d_name[0] == '.')
        continue;
      char p[128], comm[64] = "?", wchan[96] = "-";
      char st = '?';
      FILE *f;
      snprintf(p, sizeof(p), "/proc/self/task/%s/stat", de->d_name);
      f = fopen(p, "r");
      if (f) { if (fscanf(f, "%*d %*s %c", &st) != 1) st = '?'; fclose(f); }
      if (st != 'R' && st != 'D')
        continue;
      snprintf(p, sizeof(p), "/proc/self/task/%s/comm", de->d_name);
      f = fopen(p, "r");
      if (f) { if (fgets(comm, sizeof(comm), f)) comm[strcspn(comm, "\n")] = 0; fclose(f); }
      snprintf(p, sizeof(p), "/proc/self/task/%s/wchan", de->d_name);
      f = fopen(p, "r");
      if (f) { if (fgets(wchan, sizeof(wchan), f)) wchan[strcspn(wchan, "\n")] = 0; fclose(f); }
      debugPrintf("  tid=%s [%s] st=%c wchan=%s\n", de->d_name, comm, st, wchan);
      const int tid = atoi(de->d_name);
      if (tid > 0 && tid != self_tid && ntid < 8)
        tids[ntid++] = tid;
    }
    closedir(d);

    for (int round = 0; round < 30 && g_last_swap_ticks == last; round++) {
      PcSample s[8];
      for (int i = 0; i < ntid; i++)
        sample_thread_pc(tids[i], &s[i]);
      for (int i = 0; i < ntid; i++) {
        if (!s[i].ok)
          continue;
        char pcl[96], lrl[96];
        resolve_code_addr((uintptr_t)s[i].pc, pcl, sizeof(pcl));
        resolve_code_addr((uintptr_t)s[i].lr, lrl, sizeof(lrl));
        debugPrintf("  PC[%d] tid=%d pc=%s lr=%s\n", round, s[i].tid, pcl, lrl);
      }
      usleep(300 * 1000);
    }
  }
  return NULL;
}

static void SDL_GL_SwapWindow_fake(void *window) {
  extern void SDL_GL_SwapWindow(void *);

  static uint32_t last_swap;
  static int watchdog_started;
  const uint32_t now = SDL_GetTicks();
  if (!watchdog_started) {
    watchdog_started = 1;
    pthread_t wd;
    if (pthread_create(&wd, NULL, stall_watchdog, NULL) == 0)
      pthread_detach(wd);
  }
  if (last_swap && now - last_swap > 300) {
    float up = 0.0f;
    FILE *f = fopen("/proc/uptime", "r");
    if (f) { if (fscanf(f, "%f", &up) != 1) up = 0.0f; fclose(f); }
    debugPrintf("FRAME STALL %u ms (uptime %.2f)\n", now - last_swap, up);
  }
  last_swap = now;
  g_last_swap_ticks = now;

  if (log_verbose) {
    static uint32_t fps_t0, fps_frames;
    fps_frames++;
    if (!fps_t0) fps_t0 = now;
    else if (now - fps_t0 >= 5000) {
      tracePrintf("FPS avg: %.1f (%u frames / %u ms)\n",
                  fps_frames * 1000.0f / (float)(now - fps_t0), fps_frames, now - fps_t0);
      fps_t0 = now; fps_frames = 0;
    }
  }
  gamma_apply(window);
  osk_draw(window);
  if (config.mouse_enable && g_mouse_window &&
      ((uint32_t)(SDL_GetTicks() - g_mouse_last_move) < 2500 || osk_is_active())) {
    if (!osk_cursor_draw(window, g_mouse_x, g_mouse_y))
      draw_cursor_overlay();
  }
  SDL_GL_SwapWindow(window);
}

XF(SDL_CloseAudioDevice)
XF(SDL_CreateSystemCursor)
XF(SDL_FreeSurface)
XF(SDL_GL_UnloadLibrary)
XF(SDL_GameControllerClose) XF(SDL_GameControllerGetJoystick)
XF(SDL_GameControllerOpen) XF(SDL_GetClipboardText)
XF(SDL_GetCurrentAudioDriver) XF(SDL_GetCurrentVideoDriver)
XF(SDL_GetKeyName)
XF(SDL_GetNumVideoDisplays) XF(SDL_GetRevision) XF(SDL_GetVersion)
XF(SDL_GetWindowGrab) XF(SDL_GetWindowSize)
XF(SDL_HapticClose) XF(SDL_HapticOpenFromJoystick) XF(SDL_HapticRumbleInit)
XF(SDL_HapticRumblePlay) XF(SDL_HapticRumbleStop)
XF(SDL_HasClipboardText) XF(SDL_HideWindow)
XF(SDL_IsGameController) XF(SDL_JoystickGetDeviceGUID)
XF(SDL_JoystickGetGUIDString) XF(SDL_JoystickInstanceID)
XF(SDL_JoystickNameForIndex) XF(SDL_LoadBMP_RW)
XF(SDL_OpenAudioDevice) XF(SDL_OpenURL)
XF(SDL_PauseAudioDevice)
XF(SDL_RWFromFile) XF(SDL_RaiseWindow)
XF(SDL_SetClipboardText) XF(SDL_SetCursor) XF(SDL_SetHint)
XF(SDL_SetWindowBordered)
XF(SDL_SetWindowIcon) XF(SDL_SetWindowPosition) XF(SDL_SetWindowTitle)
XF(SDL_ShowMessageBox) XF(SDL_ShowSimpleMessageBox)
XF(SDL_ShowWindow)
XF(SDL_WasInit) XF(SDL_free) XF(SDL_memset)

XF(adler32) XF(crc32) XF(deflate) XF(deflateEnd) XF(deflateInit2_)
XF(deflateReset) XF(gzclose) XF(gzopen) XF(gzwrite) XF(inflate)
XF(inflateEnd) XF(inflateInit2_) XF(inflateInit_) XF(inflateReset)
XF(inflateReset2) XF(zlibVersion)

DynLibFunction dynlib_functions[] = {

  { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
  { "__cxa_finalize", (uintptr_t)&ret0 },
  { "__cxa_thread_atexit_impl", (uintptr_t)&__cxa_thread_atexit_impl },
  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
  { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
  { "__sF", (uintptr_t)&fake_sF },
  { "__errno", (uintptr_t)&__errno_location },
  { "__assert2", (uintptr_t)&__assert2_fake },
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__android_log_write", (uintptr_t)&__android_log_write },
  { "__android_log_vprint", (uintptr_t)&__android_log_vprint },
  { "android_set_abort_message", (uintptr_t)&android_set_abort_message_fake },
  { "__system_property_get", (uintptr_t)&__system_property_get_fake },
  { "__register_atfork", (uintptr_t)&__register_atfork_fake },
  { "getauxval", (uintptr_t)&getauxval },
  { "gettid", (uintptr_t)&gettid_fake },

  { "__memcpy_chk", (uintptr_t)&__memcpy_chk_fake },
  { "__memmove_chk", (uintptr_t)&__memmove_chk_fake },
  { "__strcat_chk", (uintptr_t)&__strcat_chk_fake },
  { "__strchr_chk", (uintptr_t)&__strchr_chk_fake },
  { "__strcpy_chk", (uintptr_t)&__strcpy_chk_fake },
  { "__strlen_chk", (uintptr_t)&__strlen_chk_fake },
  { "__strncat_chk", (uintptr_t)&__strncat_chk_fake },
  { "__strncpy_chk", (uintptr_t)&__strncpy_chk_fake },
  { "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk_fake },
  { "__FD_SET_chk", (uintptr_t)&__FD_SET_chk_fake },

  { "__FD_ISSET_chk", (uintptr_t)&__FD_ISSET_chk_fake },
  { "__memset_chk", (uintptr_t)&__memset_chk_fake },
  { "__read_chk", (uintptr_t)&__read_chk_fake },
  { "__recvfrom_chk", (uintptr_t)&__recvfrom_chk_fake },
  { "__snprintf_chk", (uintptr_t)&__snprintf_chk_fake },
  { "__sprintf_chk", (uintptr_t)&__sprintf_chk_fake },
  { "__vsprintf_chk", (uintptr_t)&__vsprintf_chk_fake },
  { "__strncpy_chk2", (uintptr_t)&__strncpy_chk2_fake },
  { "__strrchr_chk", (uintptr_t)&__strrchr_chk_fake },
  { "syscall", (uintptr_t)&syscall },
  { "__ctype_get_mb_cur_max", (uintptr_t)&__ctype_get_mb_cur_max_fake },
  { "_ctype_", (uintptr_t)&bionic_ctype },
  { "__google_potentially_blocking_region_begin", (uintptr_t)&__google_potentially_blocking_region_begin_fake },
  { "__google_potentially_blocking_region_end", (uintptr_t)&__google_potentially_blocking_region_end_fake },
  { "isblank", (uintptr_t)&isblank },
  { "ldexp", (uintptr_t)&ldexp },
  { "wcsftime", (uintptr_t)&wcsftime },

  { "dlopen", (uintptr_t)&dlopen_fake },
  { "dlsym", (uintptr_t)&dlsym_fake },
  { "dlclose", (uintptr_t)&dlclose_fake },
  { "dlerror", (uintptr_t)&dlerror_fake },
  { "dladdr", (uintptr_t)&dladdr_fake },
  { "dl_iterate_phdr", (uintptr_t)&so_dl_iterate_phdr },

  { "__fpclassifyd", (uintptr_t)&__fpclassifyd_fake },
  { "__isnanf", (uintptr_t)&__isnanf_fake },
  { "abs", (uintptr_t)&abs },
  { "acos", (uintptr_t)&acos },
  { "acosf", (uintptr_t)&acosf },
  { "asin", (uintptr_t)&asin },
  { "asinf", (uintptr_t)&asinf },
  { "atan", (uintptr_t)&atan },
  { "atan2", (uintptr_t)&atan2 },
  { "atan2f", (uintptr_t)&atan2f },
  { "atanf", (uintptr_t)&atanf },
  { "cbrtf", (uintptr_t)&cbrtf },
  { "cos", (uintptr_t)&cos },
  { "cosf", (uintptr_t)&cosf },
  { "exp", (uintptr_t)&exp },
  { "exp2", (uintptr_t)&exp2 },
  { "exp2f", (uintptr_t)&exp2f },
  { "expf", (uintptr_t)&expf },
  { "floor", (uintptr_t)&floor },
  { "fmod", (uintptr_t)&fmod },
  { "fmodf", (uintptr_t)&fmodf },
  { "fmodl", (uintptr_t)&fmodl },
  { "frexp", (uintptr_t)&frexp },
  { "log", (uintptr_t)&log },
  { "log10", (uintptr_t)&log10 },
  { "log10f", (uintptr_t)&log10f },
  { "logf", (uintptr_t)&logf },
  { "lrintf", (uintptr_t)&lrintf },
  { "modf", (uintptr_t)&modf },
  { "pow", (uintptr_t)&pow },
  { "powf", (uintptr_t)&powf },
  { "scalbn", (uintptr_t)&scalbn },
  { "scalbnl", (uintptr_t)&scalbnl },
  { "sin", (uintptr_t)&sin },
  { "sincos", (uintptr_t)&sincos },
  { "sincosf", (uintptr_t)&sincosf },
  { "sinf", (uintptr_t)&sinf },
  { "sqrt", (uintptr_t)&sqrt },
  { "sqrtf", (uintptr_t)&sqrtf },
  { "tan", (uintptr_t)&tan },
  { "tanf", (uintptr_t)&tanf },

  { "isalnum", (uintptr_t)&isalnum },
  { "iscntrl", (uintptr_t)&iscntrl },
  { "isgraph", (uintptr_t)&isgraph },
  { "isprint", (uintptr_t)&isprint },
  { "ispunct", (uintptr_t)&ispunct },
  { "isspace", (uintptr_t)&isspace },
  { "isupper", (uintptr_t)&isupper },
  { "tolower", (uintptr_t)&tolower },
  { "toupper", (uintptr_t)&toupper },

  { "abort", (uintptr_t)&abort },
  { "exit", (uintptr_t)&exit },
  { "_exit", (uintptr_t)&exit },
  { "atof", (uintptr_t)&atof },
  { "atoi", (uintptr_t)&atoi },
  { "atol", (uintptr_t)&atol },
  { "atoll", (uintptr_t)&atoll },
  { "bsearch", (uintptr_t)&bsearch },
  { "calloc", (uintptr_t)&calloc },
  { "free", (uintptr_t)&free },
  { "malloc", (uintptr_t)&malloc },
  { "realloc", (uintptr_t)&realloc },
  { "posix_memalign", (uintptr_t)&posix_memalign },
  { "malloc_usable_size", (uintptr_t)&malloc_usable_size },
  { "mallinfo", (uintptr_t)&mallinfo_fake },
  { "mmap", (uintptr_t)&mmap },
  { "munmap", (uintptr_t)&munmap },
  { "qsort", (uintptr_t)&qsort },
  { "rand", (uintptr_t)&rand },
  { "srand", (uintptr_t)&srand },
  { "getenv", (uintptr_t)&getenv },
  { "setenv", (uintptr_t)&setenv },
  { "unsetenv", (uintptr_t)&unsetenv },
  { "setlocale", (uintptr_t)&setlocale },
  { "system", (uintptr_t)&system_fake },

  { "basename", (uintptr_t)&basename_fake },
  { "memchr", (uintptr_t)&memchr },
  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy },
  { "memmove", (uintptr_t)&memmove },
  { "memrchr", (uintptr_t)&memrchr },
  { "memset", (uintptr_t)&memset },
  { "stpcpy", (uintptr_t)&stpcpy },
  { "strcasecmp", (uintptr_t)&strcasecmp },
  { "strcat", (uintptr_t)&strcat },
  { "strchr", (uintptr_t)&strchr },
  { "strcmp", (uintptr_t)&strcmp },
  { "strcpy", (uintptr_t)&strcpy },
  { "strcspn", (uintptr_t)&strcspn },
  { "strdup", (uintptr_t)&strdup },
  { "strerror", (uintptr_t)&strerror },
  { "strerror_r", (uintptr_t)&strerror_r_fake },
  { "strlen", (uintptr_t)&strlen },
  { "strncasecmp", (uintptr_t)&strncasecmp },
  { "strncat", (uintptr_t)&strncat },
  { "strncmp", (uintptr_t)&strncmp },
  { "strncpy", (uintptr_t)&strncpy },
  { "strpbrk", (uintptr_t)&strpbrk },
  { "strrchr", (uintptr_t)&strrchr },
  { "strspn", (uintptr_t)&strspn },
  { "strstr", (uintptr_t)&strstr },
  { "strtod", (uintptr_t)&strtod },
  { "strtof", (uintptr_t)&strtof },
  { "strtok", (uintptr_t)&strtok },
  { "strtok_r", (uintptr_t)&strtok_r },
  { "strtol", (uintptr_t)&strtol },
  { "strtoll", (uintptr_t)&strtoll },
  { "strtoul", (uintptr_t)&strtoul },
  { "strtoull", (uintptr_t)&strtoull },
  { "fnmatch", (uintptr_t)&fnmatch },
  { "wcscmp", (uintptr_t)&wcscmp },
  { "wcscpy", (uintptr_t)&wcscpy },

  { "printf", (uintptr_t)&enginePrintf },
  { "putchar", (uintptr_t)&putchar },
  { "puts", (uintptr_t)&enginePuts },
  { "snprintf", (uintptr_t)&snprintf },
  { "sprintf", (uintptr_t)&sprintf },
  { "sscanf", (uintptr_t)&sscanf },
  { "swprintf", (uintptr_t)&swprintf },
  { "swscanf", (uintptr_t)&swscanf },
  { "vsnprintf", (uintptr_t)&vsnprintf },
  { "vsprintf", (uintptr_t)&vsprintf },
  { "vswprintf", (uintptr_t)&vswprintf },

  { "fclose", (uintptr_t)&fclose_fake },
  { "fdopen", (uintptr_t)&fdopen },
  { "feof", (uintptr_t)&feof_fake },
  { "ferror", (uintptr_t)&ferror_fake },
  { "fflush", (uintptr_t)&fflush_fake },
  { "fgetc", (uintptr_t)&fgetc_fake },
  { "fgets", (uintptr_t)&fgets_fake },
  { "fileno", (uintptr_t)&fileno_fake },
  { "fopen", (uintptr_t)&fopen_fake },
  { "fprintf", (uintptr_t)&fprintf_fake },
  { "fputc", (uintptr_t)&fputc_fake },
  { "fputs", (uintptr_t)&fputs_fake },
  { "fread", (uintptr_t)&fread_fake },
  { "freopen", (uintptr_t)&freopen_fake },
  { "fscanf", (uintptr_t)&fscanf_fake },
  { "fseek", (uintptr_t)&fseek_fake },
  { "fseeko", (uintptr_t)&fseeko },
  { "ftell", (uintptr_t)&ftell },
  { "ftello", (uintptr_t)&ftello },
  { "fwrite", (uintptr_t)&fwrite_fake },
  { "rewind", (uintptr_t)&rewind_fake },
  { "setvbuf", (uintptr_t)&setvbuf_fake },
  { "tmpnam", (uintptr_t)&tmpnam },
  { "ungetc", (uintptr_t)&ungetc_fake },
  { "vfprintf", (uintptr_t)&vfprintf_fake },
  { "popen", (uintptr_t)&popen_fake },
  { "pclose", (uintptr_t)&pclose_fake },

  { "access", (uintptr_t)&access },
  { "chdir", (uintptr_t)&chdir },
  { "chmod", (uintptr_t)&ret0 },
  { "close", (uintptr_t)&close },
  { "dup", (uintptr_t)&dup },
  { "fcntl", (uintptr_t)&fcntl },
  { "fstat", (uintptr_t)&fstat },
  { "getcwd", (uintptr_t)&getcwd },
  { "ioctl", (uintptr_t)&ioctl },
  { "isatty", (uintptr_t)&isatty },
  { "lseek", (uintptr_t)&lseek },
  { "lstat", (uintptr_t)&lstat },
  { "mkdir", (uintptr_t)&mkdir },
  { "open", (uintptr_t)&open_fake },
  { "read", (uintptr_t)&read },
  { "realpath", (uintptr_t)&realpath },
  { "remove", (uintptr_t)&remove },
  { "rename", (uintptr_t)&rename },
  { "stat", (uintptr_t)&stat },
  { "statfs64", (uintptr_t)&statfs64 },
  { "unlink", (uintptr_t)&unlink },
  { "utimensat", (uintptr_t)&utimensat },
  { "write", (uintptr_t)&write },

  { "alphasort", (uintptr_t)&alphasort },
  { "closedir", (uintptr_t)&closedir },
  { "opendir", (uintptr_t)&opendir },
  { "readdir", (uintptr_t)&readdir },
  { "readdir64", (uintptr_t)&readdir64 },
  { "scandir", (uintptr_t)&scandir },

  { "asctime", (uintptr_t)&asctime },
  { "clock_gettime", (uintptr_t)&clock_gettime },
  { "ctime", (uintptr_t)&ctime },
  { "ctime_r", (uintptr_t)&ctime_r },
  { "gettimeofday", (uintptr_t)&gettimeofday },
  { "gmtime", (uintptr_t)&gmtime },
  { "gmtime_r", (uintptr_t)&gmtime_r },
  { "localtime", (uintptr_t)&localtime },
  { "localtime_r", (uintptr_t)&localtime_r },
  { "nanosleep", (uintptr_t)&nanosleep },
  { "sleep", (uintptr_t)&sleep },
  { "strftime", (uintptr_t)&strftime },
  { "time", (uintptr_t)&time },
  { "timegm", (uintptr_t)&timegm },
  { "tzset", (uintptr_t)&tzset },
  { "usleep", (uintptr_t)&usleep },

  { "alarm", (uintptr_t)&ret0 },
  { "execlp", (uintptr_t)&retm1 },
  { "fork", (uintptr_t)&retm1 },
  { "geteuid", (uintptr_t)&geteuid },
  { "getpid", (uintptr_t)&getpid },
  { "getpwuid", (uintptr_t)&getpwuid },
  { "getrusage", (uintptr_t)&getrusage },
  { "getuid", (uintptr_t)&getuid },
  { "raise", (uintptr_t)&ret0 },
  { "sigaction", (uintptr_t)&ret0 },
  { "signal", (uintptr_t)&ret0 },
  { "pthread_sigmask", (uintptr_t)&ret0 },
  { "pthread_kill", (uintptr_t)&ret0 },

  { "setjmp", (uintptr_t)&_setjmp },
  { "longjmp", (uintptr_t)&_longjmp },
  { "sigsetjmp", (uintptr_t)&_setjmp },
  { "siglongjmp", (uintptr_t)&_longjmp },

  { "accept", (uintptr_t)&accept },
  { "bind", (uintptr_t)&bind },
  { "connect", (uintptr_t)&connect },
  { "freeaddrinfo", (uintptr_t)&freeaddrinfo_fake },
  { "getaddrinfo", (uintptr_t)&getaddrinfo_fake },
  { "gethostbyname", (uintptr_t)&gethostbyname },
  { "gethostbyaddr", (uintptr_t)&gethostbyaddr },
  { "gethostname", (uintptr_t)&gethostname },
  { "getpeername", (uintptr_t)&getpeername },
  { "getsockname", (uintptr_t)&getsockname },
  { "getsockopt", (uintptr_t)&getsockopt },
  { "if_nametoindex", (uintptr_t)&if_nametoindex },
  { "inet_addr", (uintptr_t)&inet_addr },
  { "inet_ntop", (uintptr_t)&inet_ntop },
  { "inet_pton", (uintptr_t)&inet_pton },
  { "listen", (uintptr_t)&listen },
  { "poll", (uintptr_t)&poll },
  { "recv", (uintptr_t)&recv },
  { "recvfrom", (uintptr_t)&recvfrom },
  { "select", (uintptr_t)&select },
  { "send", (uintptr_t)&send },
  { "sendto", (uintptr_t)&sendto },
  { "setsockopt", (uintptr_t)&setsockopt },
  { "socket", (uintptr_t)&socket },
  { "socketpair", (uintptr_t)&socketpair },

  { "adler32", (uintptr_t)&adler32 },
  { "crc32", (uintptr_t)&crc32 },
  { "deflate", (uintptr_t)&deflate },
  { "deflateEnd", (uintptr_t)&deflateEnd },
  { "deflateInit2_", (uintptr_t)&deflateInit2_ },
  { "deflateReset", (uintptr_t)&deflateReset },
  { "gzclose", (uintptr_t)&gzclose },
  { "gzopen", (uintptr_t)&gzopen },
  { "gzwrite", (uintptr_t)&gzwrite },
  { "inflate", (uintptr_t)&inflate },
  { "inflateEnd", (uintptr_t)&inflateEnd },
  { "inflateInit2_", (uintptr_t)&inflateInit2_ },
  { "inflateInit_", (uintptr_t)&inflateInit_ },
  { "inflateReset", (uintptr_t)&inflateReset },
  { "inflateReset2", (uintptr_t)&inflateReset2 },
  { "zlibVersion", (uintptr_t)&zlibVersion },

  { "pthread_attr_destroy", (uintptr_t)&ret0 },
  { "pthread_attr_init", (uintptr_t)&ret0 },
  { "pthread_attr_setdetachstate", (uintptr_t)&ret0 },
  { "pthread_attr_setstacksize", (uintptr_t)&ret0 },
  { "pthread_attr_getstacksize", (uintptr_t)&b_pthread_attr_getstacksize },
  { "pthread_cond_broadcast", (uintptr_t)&b_cond_broadcast },
  { "pthread_cond_destroy", (uintptr_t)&b_cond_destroy },
  { "pthread_cond_init", (uintptr_t)&b_cond_init },
  { "pthread_cond_signal", (uintptr_t)&b_cond_signal },
  { "pthread_cond_timedwait", (uintptr_t)&b_cond_timedwait },
  { "pthread_cond_wait", (uintptr_t)&b_cond_wait },
  { "pthread_create", (uintptr_t)&b_pthread_create },
  { "pthread_detach", (uintptr_t)&pthread_detach },
  { "pthread_getschedparam", (uintptr_t)&pthread_getschedparam },
  { "pthread_setschedparam", (uintptr_t)&ret0 },
  { "pthread_getspecific", (uintptr_t)&pthread_getspecific },
  { "pthread_join", (uintptr_t)&pthread_join },
  { "pthread_key_create", (uintptr_t)&pthread_key_create },
  { "pthread_key_delete", (uintptr_t)&pthread_key_delete },
  { "pthread_mutex_destroy", (uintptr_t)&b_mutex_destroy },
  { "pthread_mutex_init", (uintptr_t)&b_mutex_init },
  { "pthread_mutex_lock", (uintptr_t)&b_mutex_lock },
  { "pthread_mutex_trylock", (uintptr_t)&b_mutex_trylock },
  { "pthread_mutex_unlock", (uintptr_t)&b_mutex_unlock },
  { "pthread_mutexattr_destroy", (uintptr_t)&ret0 },
  { "pthread_mutexattr_init", (uintptr_t)&b_mutexattr_init },
  { "pthread_mutexattr_settype", (uintptr_t)&b_mutexattr_settype },
  { "pthread_once", (uintptr_t)&pthread_once },
  { "pthread_self", (uintptr_t)&pthread_self },
  { "pthread_setname_np", (uintptr_t)&ret0 },
  { "pthread_setspecific", (uintptr_t)&pthread_setspecific },

  { "pthread_rwlock_rdlock", (uintptr_t)&pthread_rwlock_rdlock },
  { "pthread_rwlock_wrlock", (uintptr_t)&pthread_rwlock_wrlock },
  { "pthread_rwlock_unlock", (uintptr_t)&pthread_rwlock_unlock },
  { "sched_get_priority_max", (uintptr_t)&sched_get_priority_max_fake },
  { "sched_get_priority_min", (uintptr_t)&retm1 },
  { "sched_yield", (uintptr_t)&sched_yield },
  { "sem_destroy", (uintptr_t)&b_sem_destroy },
  { "sem_getvalue", (uintptr_t)&b_sem_getvalue },
  { "sem_init", (uintptr_t)&b_sem_init },
  { "sem_post", (uintptr_t)&b_sem_post },
  { "sem_timedwait", (uintptr_t)&b_sem_timedwait },
  { "sem_trywait", (uintptr_t)&b_sem_trywait },
  { "sem_wait", (uintptr_t)&b_sem_wait },

  { "SDL_AddEventWatch", (uintptr_t)&SDL_AddEventWatch_fake },
  { "SDL_CloseAudioDevice", (uintptr_t)&SDL_CloseAudioDevice },
  { "SDL_CreateSystemCursor", (uintptr_t)&SDL_CreateSystemCursor },
  { "SDL_CreateWindow", (uintptr_t)&SDL_CreateWindow_fake },
  { "SDL_DelEventWatch", (uintptr_t)&SDL_DelEventWatch_fake },
  { "SDL_DestroyWindow", (uintptr_t)&SDL_DestroyWindow },
  { "SDL_FreeSurface", (uintptr_t)&SDL_FreeSurface },
  { "SDL_GL_CreateContext", (uintptr_t)&SDL_GL_CreateContext_fake },
  { "SDL_GL_DeleteContext", (uintptr_t)&SDL_GL_DeleteContext },
  { "SDL_GL_LoadLibrary", (uintptr_t)&SDL_GL_LoadLibrary_fake },
  { "SDL_GL_MakeCurrent", (uintptr_t)&SDL_GL_MakeCurrent },
  { "SDL_GL_SetAttribute", (uintptr_t)&SDL_GL_SetAttribute_fake },
  { "SDL_GL_SetSwapInterval", (uintptr_t)&SDL_GL_SetSwapInterval_fake },
  { "SDL_GL_SwapWindow", (uintptr_t)&SDL_GL_SwapWindow_fake },
  { "SDL_GL_UnloadLibrary", (uintptr_t)&SDL_GL_UnloadLibrary },
  { "SDL_GameControllerClose", (uintptr_t)&SDL_GameControllerClose },
  { "SDL_GameControllerGetJoystick", (uintptr_t)&SDL_GameControllerGetJoystick },
  { "SDL_GameControllerOpen", (uintptr_t)&SDL_GameControllerOpen },
  { "SDL_GetClipboardText", (uintptr_t)&SDL_GetClipboardText },
  { "SDL_GetCurrentAudioDriver", (uintptr_t)&SDL_GetCurrentAudioDriver },
  { "SDL_GetCurrentDisplayMode", (uintptr_t)&SDL_GetCurrentDisplayMode_fake },
  { "SDL_GetCurrentVideoDriver", (uintptr_t)&SDL_GetCurrentVideoDriver },
  { "SDL_GetDesktopDisplayMode", (uintptr_t)&SDL_GetDesktopDisplayMode_fake },
  { "SDL_GetDisplayBounds", (uintptr_t)&SDL_GetDisplayBounds_fake },
  { "SDL_GetError", (uintptr_t)&SDL_GetError },
  { "SDL_GetKeyName", (uintptr_t)&SDL_GetKeyName },
  { "SDL_GetMouseState", (uintptr_t)&SDL_GetMouseState_fake },
  { "SDL_GetNumVideoDisplays", (uintptr_t)&SDL_GetNumVideoDisplays },
  { "SDL_GetRevision", (uintptr_t)&SDL_GetRevision },
  { "SDL_GetVersion", (uintptr_t)&SDL_GetVersion },
  { "SDL_GetWindowGrab", (uintptr_t)&SDL_GetWindowGrab },
  { "SDL_GetWindowID", (uintptr_t)&SDL_GetWindowID },
  { "SDL_GetWindowSize", (uintptr_t)&SDL_GetWindowSize },
  { "SDL_GetWindowWMInfo", (uintptr_t)&SDL_GetWindowWMInfo_fake },
  { "SDL_HapticClose", (uintptr_t)&SDL_HapticClose },
  { "SDL_HapticOpenFromJoystick", (uintptr_t)&SDL_HapticOpenFromJoystick },
  { "SDL_HapticRumbleInit", (uintptr_t)&SDL_HapticRumbleInit },
  { "SDL_HapticRumblePlay", (uintptr_t)&SDL_HapticRumblePlay },
  { "SDL_HapticRumbleStop", (uintptr_t)&SDL_HapticRumbleStop },
  { "SDL_HasClipboardText", (uintptr_t)&SDL_HasClipboardText },
  { "SDL_HideWindow", (uintptr_t)&SDL_HideWindow },
  { "SDL_Init", (uintptr_t)&SDL_Init },
  { "SDL_InitSubSystem", (uintptr_t)&SDL_InitSubSystem },
  { "SDL_IsGameController", (uintptr_t)&SDL_IsGameController },
  { "SDL_JoystickClose", (uintptr_t)&SDL_JoystickClose },
  { "SDL_JoystickGetDeviceGUID", (uintptr_t)&SDL_JoystickGetDeviceGUID },
  { "SDL_JoystickGetGUIDString", (uintptr_t)&SDL_JoystickGetGUIDString },
  { "SDL_JoystickInstanceID", (uintptr_t)&SDL_JoystickInstanceID },
  { "SDL_JoystickNameForIndex", (uintptr_t)&SDL_JoystickNameForIndex },
  { "SDL_JoystickOpen", (uintptr_t)&SDL_JoystickOpen },
  { "SDL_LoadBMP_RW", (uintptr_t)&SDL_LoadBMP_RW },
  { "SDL_NumJoysticks", (uintptr_t)&SDL_NumJoysticks },
  { "SDL_OpenAudioDevice", (uintptr_t)&SDL_OpenAudioDevice },
  { "SDL_OpenURL", (uintptr_t)&SDL_OpenURL },
  { "SDL_PauseAudioDevice", (uintptr_t)&SDL_PauseAudioDevice },
  { "SDL_PollEvent", (uintptr_t)&SDL_PollEvent_fake },
  { "SDL_PushEvent", (uintptr_t)&SDL_PushEvent },
  { "SDL_QuitSubSystem", (uintptr_t)&SDL_QuitSubSystem },
  { "SDL_RWFromFile", (uintptr_t)&SDL_RWFromFile },
  { "SDL_RaiseWindow", (uintptr_t)&SDL_RaiseWindow },
  { "SDL_SetClipboardText", (uintptr_t)&SDL_SetClipboardText },
  { "SDL_SetCursor", (uintptr_t)&SDL_SetCursor },
  { "SDL_SetHint", (uintptr_t)&SDL_SetHint },
  { "SDL_SetRelativeMouseMode", (uintptr_t)&SDL_SetRelativeMouseMode_fake },
  { "SDL_SetWindowBordered", (uintptr_t)&SDL_SetWindowBordered },
  { "SDL_SetWindowDisplayMode", (uintptr_t)&SDL_SetWindowDisplayMode_fake },
  { "SDL_SetWindowFullscreen", (uintptr_t)&SDL_SetWindowFullscreen_fake },
  { "SDL_SetWindowGammaRamp", (uintptr_t)&SDL_SetWindowGammaRamp_fake },
  { "SDL_SetWindowGrab", (uintptr_t)&SDL_SetWindowGrab_fake },
  { "SDL_SetWindowIcon", (uintptr_t)&SDL_SetWindowIcon },
  { "SDL_SetWindowPosition", (uintptr_t)&SDL_SetWindowPosition },
  { "SDL_SetWindowSize", (uintptr_t)&SDL_SetWindowSize_fake },
  { "SDL_SetWindowTitle", (uintptr_t)&SDL_SetWindowTitle },
  { "SDL_ShowCursor", (uintptr_t)&SDL_ShowCursor_fake },
  { "SDL_ShowMessageBox", (uintptr_t)&SDL_ShowMessageBox },
  { "SDL_ShowSimpleMessageBox", (uintptr_t)&SDL_ShowSimpleMessageBox },
  { "SDL_ShowWindow", (uintptr_t)&SDL_ShowWindow },
  { "SDL_StartTextInput", (uintptr_t)&SDL_StartTextInput_fake },
  { "SDL_WaitEventTimeout", (uintptr_t)&SDL_WaitEventTimeout_fake },
  { "SDL_WarpMouseInWindow", (uintptr_t)&SDL_WarpMouseInWindow },
  { "SDL_WasInit", (uintptr_t)&SDL_WasInit },
  { "SDL_free", (uintptr_t)&SDL_free },
  { "SDL_memset", (uintptr_t)&SDL_memset },

  { "eglGetProcAddress", (uintptr_t)&eglGetProcAddress_fake },
};

size_t dynlib_numfunctions = sizeof(dynlib_functions) / sizeof(*dynlib_functions);
