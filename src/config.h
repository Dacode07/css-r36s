#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DEFAULT_INSTALL_ROOT "/roms/ports/css"

#define CONFIG_NAME "config.txt"

extern int screen_width;
extern int screen_height;

typedef struct {
  int screen_width;
  int screen_height;
  char install_root[256];
  char gamedir[64];
  char args[256];
  char lang[32];
  int show_fps;
  int gamepad;
  int console;
  int debug;

  int mouse_enable;
  int mouse_speed;
  int mouse_axis_x;
  int mouse_axis_y;
  int mouse_click;
  int mouse_toggle;
  int esc_button;
  int aim_btn;
  int aim_slow;
  int aim_assist;
  int aim_fov;
  int aim_speed;
  int aim_height;
  int brightness;
  int classmenu_preview;
  int glcache;
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
