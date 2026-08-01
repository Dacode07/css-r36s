#ifndef __SDL2_MIN_H__
#define __SDL2_MIN_H__

#include <stdint.h>

typedef struct {
  uint32_t format;
  int w;
  int h;
  int refresh_rate;
  void *driverdata;
} SDL_DisplayMode_min;

typedef struct {
  int x, y, w, h;
} SDL_Rect_min;

extern int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode_min *mode);
extern int SDL_GetCurrentDisplayMode(int displayIndex, SDL_DisplayMode_min *mode);
extern int SDL_GetDisplayBounds(int displayIndex, SDL_Rect_min *rect);
extern void SDL_SetWindowSize(void *window, int w, int h);
extern int SDL_SetWindowDisplayMode(void *window, const SDL_DisplayMode_min *mode);

extern void *SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags);
extern void *SDL_GL_CreateContext(void *window);
extern int SDL_GL_LoadLibrary(const char *path);
extern int SDL_GL_SetAttribute(int attr, int value);
extern const char *SDL_GetError(void);
extern void *SDL_GL_GetProcAddress(const char *proc);

typedef union {
  uint32_t type;
  struct { uint32_t type, timestamp; int32_t which; uint8_t axis, p1, p2, p3; int16_t value; } jaxis;
  struct { uint32_t type, timestamp; int32_t which; uint8_t button, state, p1, p2; } jbutton;
  struct { uint32_t type, timestamp, windowID, which; uint8_t button, state, clicks, p1; int32_t x, y; } button;
  struct { uint32_t type, timestamp, windowID, which, state; int32_t x, y, xrel, yrel; } motion;
  struct { uint32_t type, timestamp, windowID; uint8_t state, repeat, p1, p2; int32_t scancode, sym; uint16_t mod; uint32_t unused; } key;
  struct { uint32_t type, timestamp, windowID, which; int32_t x, y; uint32_t direction; } wheel;
  uint8_t padding[64];
} SDL_Event_min;

#define SDLM_MOUSEWHEEL      0x403
#define SDLM_KEYDOWN         0x300
#define SDLM_KEYUP           0x301
#define SDLM_MOUSEMOTION     0x400
#define SDLM_MOUSEBUTTONDOWN 0x401
#define SDLM_MOUSEBUTTONUP   0x402
#define SDLM_JOYAXISMOTION   0x600
#define SDLM_JOYBUTTONDOWN   0x603
#define SDLM_JOYBUTTONUP     0x604
#define SDLM_CTRLAXISMOTION  0x650
#define SDLM_CTRLBUTTONDOWN  0x651
#define SDLM_CTRLBUTTONUP    0x652

extern int SDL_PollEvent(SDL_Event_min *event);
extern int SDL_WaitEventTimeout(SDL_Event_min *event, int timeout);
extern int SDL_PushEvent(SDL_Event_min *event);
extern void SDL_WarpMouseInWindow(void *window, int x, int y);
extern uint32_t SDL_GetWindowID(void *window);
extern int SDL_SetRelativeMouseMode(int enabled);
extern int SDL_ShowCursor(int toggle);
extern void SDL_GL_GetDrawableSize(void *window, int *w, int *h);
extern uint32_t SDL_GetTicks(void);
extern uint32_t SDL_GetMouseState(int *x, int *y);

#define SDLM_INIT_VIDEO 0x00000020
#define SDLM_INIT_JOYSTICK 0x00000200
#define SDLM_WINDOW_OPENGL 0x00000002
#define SDLM_WINDOW_FULLSCREEN 0x00000001
#define SDLM_WINDOWPOS_CENTERED 0x2FFF0000
#define SDLM_GL_CONTEXT_MAJOR_VERSION 17
#define SDLM_GL_CONTEXT_MINOR_VERSION 18
#define SDLM_GL_CONTEXT_PROFILE_MASK 21
#define SDLM_GL_CONTEXT_PROFILE_ES 4
#define SDLM_GL_DOUBLEBUFFER 5
#define SDLM_GL_DEPTH_SIZE 6
#define SDLM_QUIT 0x100

extern int SDL_Init(uint32_t flags);
extern int SDL_InitSubSystem(uint32_t flags);
extern void SDL_QuitSubSystem(uint32_t flags);
extern void SDL_DestroyWindow(void *window);
extern void SDL_GL_DeleteContext(void *context);
extern void SDL_GL_SwapWindow(void *window);
extern int SDL_GL_MakeCurrent(void *window, void *context);
extern int SDL_GL_SetSwapInterval(int interval);
extern void SDL_Delay(uint32_t ms);
extern int SDL_NumJoysticks(void);
extern void *SDL_JoystickOpen(int index);
extern void SDL_JoystickClose(void *joystick);

#endif
