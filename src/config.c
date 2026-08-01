#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "config.h"

#define CONFIG_VARS \
  CONFIG_VAR_INT(screen_width); \
  CONFIG_VAR_INT(screen_height); \
  CONFIG_VAR_STR(args); \
  CONFIG_VAR_STR(lang); \
  CONFIG_VAR_INT(show_fps); \
  CONFIG_VAR_INT(gamepad); \
  CONFIG_VAR_INT(console); \
  CONFIG_VAR_INT(debug); \
  CONFIG_VAR_INT(mouse_enable); \
  CONFIG_VAR_INT(mouse_speed); \
  CONFIG_VAR_INT(mouse_axis_x); \
  CONFIG_VAR_INT(mouse_axis_y); \
  CONFIG_VAR_INT(mouse_click); \
  CONFIG_VAR_INT(mouse_toggle); \
  CONFIG_VAR_INT(esc_button); \
  CONFIG_VAR_INT(aim_btn); \
  CONFIG_VAR_INT(aim_slow); \
  CONFIG_VAR_INT(aim_assist); \
  CONFIG_VAR_INT(aim_fov); \
  CONFIG_VAR_INT(aim_speed); \
  CONFIG_VAR_INT(aim_height); \
  CONFIG_VAR_INT(brightness); \
  CONFIG_VAR_INT(classmenu_preview); \
  CONFIG_VAR_INT(glcache);

Config config;

int screen_width = 640;
int screen_height = 480;

static void strlcpy_(char *dst, const char *src, size_t size) {
  if (!size) return;
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

static int lang_equal(const char *a, const char *b) {
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

static void normalize_lang(void) {
  static const struct {
    const char *from;
    const char *to;
  } aliases[] = {
    { "en_US", "english" }, { "en-US", "english" },
    { "de_DE", "german" }, { "fr_FR", "french" },
    { "it_IT", "italian" }, { "es_ES", "spanish" },
    { "ko_KR", "koreana" }, { "zh_CN", "schinese" },
    { "zh_TW", "tchinese" }, { "ru_RU", "russian" },
    { "th_TH", "thai" }, { "ja_JP", "japanese" },
    { "pt_PT", "portuguese" }, { "pl_PL", "polish" },
    { "da_DK", "danish" }, { "nl_NL", "dutch" },
    { "fi_FI", "finnish" }, { "no_NO", "norwegian" },
    { "nb_NO", "norwegian" }, { "sv_SE", "swedish" },
    { "ro_RO", "romanian" }, { "tr_TR", "turkish" },
    { "hu_HU", "hungarian" }, { "cs_CZ", "czech" },
    { "pt_BR", "brazilian" }, { "bg_BG", "bulgarian" },
    { "el_GR", "greek" }, { "uk_UA", "ukrainian" },
  };

  if (!config.lang[0]) {
    strlcpy_(config.lang, "english", sizeof(config.lang));
    return;
  }

  for (unsigned i = 0; i < sizeof(aliases) / sizeof(*aliases); i++) {
    if (lang_equal(config.lang, aliases[i].from)) {
      strlcpy_(config.lang, aliases[i].to, sizeof(config.lang));
      return;
    }
  }
}

static inline void parse_var(const char *name, const char *value) {
  #define CONFIG_VAR_INT(var) if (!strcmp(name, #var)) { config.var = atoi(value); return; }
  #define CONFIG_VAR_STR(var) if (!strcmp(name, #var)) { strlcpy_(config.var, value, sizeof(config.var)); return; }
  CONFIG_VARS
  #undef CONFIG_VAR_INT
  #undef CONFIG_VAR_STR
}

int read_config(const char *file) {
  char line[1024] = { 0 };

  memset(&config, 0, sizeof(Config));
  config.screen_width = -1;
  config.screen_height = -1;
  strlcpy_(config.install_root, DEFAULT_INSTALL_ROOT, sizeof(config.install_root));
  strlcpy_(config.gamedir, "cstrike", sizeof(config.gamedir));
  strlcpy_(config.args, "", sizeof(config.args));
  strlcpy_(config.lang, "english", sizeof(config.lang));
  config.show_fps = 0;
  config.gamepad = 1;
  config.console = 0;
  config.debug = 0;
  config.mouse_enable = 1;
  config.mouse_speed = 500;
  config.mouse_axis_x = 2;
  config.mouse_axis_y = 3;
  config.mouse_click = 5;
  config.mouse_toggle = 15;
  config.esc_button = 13;
  config.aim_btn = 4;
  config.aim_slow = 35;
  config.aim_assist = 1;
  config.aim_fov = 10;
  config.aim_speed = 10;
  config.aim_height = 52;
  config.brightness = 180;
  config.classmenu_preview = 0;
  config.glcache = 1;

  FILE *f = fopen(file, "r");
  if (f == NULL)
    return -1;

  do {
    char *name = NULL, *value = NULL, *tmp = NULL;
    if (fgets(line, sizeof(line), f) != NULL) {
      name = line;
      while (*name && isspace((int)*name)) ++name;
      if (name[0] == '#') continue;
      for (tmp = name; *tmp && !isspace((int)*tmp); ++tmp);
      if (*tmp != 0) {
        *tmp = 0;
        for (value = tmp + 1; *value && isspace((int)*value); ++value);
        for (tmp = value + strlen(value); tmp > value && isspace((int)*(tmp - 1)); --tmp)
          *(tmp - 1) = 0;
        if (*value)
          parse_var(name, value);
      }
    }
  } while (!feof(f));

  fclose(f);
  normalize_lang();

  return 0;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (f == NULL)
    return -1;

  #define CONFIG_VAR_INT(var) fprintf(f, "%s %d\n", #var, config.var)
  #define CONFIG_VAR_STR(var) if (config.var[0]) fprintf(f, "%s %s\n", #var, config.var)
  CONFIG_VARS
  #undef CONFIG_VAR_INT
  #undef CONFIG_VAR_STR

  fclose(f);

  return 0;
}
