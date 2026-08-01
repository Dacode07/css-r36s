#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include "config.h"
#include "util.h"
#include "error.h"
#include "so_util.h"
#include "imports.h"
#include "libc_shim.h"
#include "jni_fake.h"
#include "gamma.h"
#include "setup.h"

static const char *const module_names[] = {
  "libtier0.so",
  "libvstdlib.so",
  "libsteam_api.so",
  "libtogl.so",
  "libfilesystem_stdio.so",
  "libinputsystem.so",
  "libdatacache.so",
  "libscenefilecache.so",
  "libsoundemittersystem.so",
  "libvideo_services.so",
  "libvaudio_minimp3.so",
  "libvaudio_opus.so",
  "libvphysics.so",
  "libstudiorender.so",
  "libmaterialsystem.so",
  "libshaderapidx9.so",
  "libstdshader_dx9.so",
  "libvgui2.so",
  "libvguimatsurface.so",
  "libengine.so",
  "libGameUI.so",
  "libServerBrowser.so",
  "libclient.so",
  "libserver.so",
  "liblauncher.so",
};
#define NUM_MODULES (sizeof(module_names) / sizeof(*module_names))

static so_module modules[NUM_MODULES];

static void crash_handler(int sig, siginfo_t *si, void *uctx) {
  ucontext_t *uc = uctx;
  const uintptr_t pc = uc ? (uintptr_t)uc->uc_mcontext.pc : 0;
  const uintptr_t lr = uc ? (uintptr_t)uc->uc_mcontext.regs[30] : 0;

  debugPrintf("\n*** CRASH: signal %d, fault addr %p, pc %p, lr %p\n",
              sig, si ? si->si_addr : NULL, (void *)pc, (void *)lr);

  const uintptr_t addrs[2] = { pc, lr };
  const char *what[2] = { "pc", "lr" };
  for (int i = 0; i < 2; i++) {
    int found = 0;
    for (so_module *m = so_first(); m; m = m->next) {
      const uintptr_t base = (uintptr_t)m->load_virtbase;
      if (addrs[i] >= base && addrs[i] < base + m->load_size) {
        debugPrintf("    %s = %s + 0x%lx\n", what[i], m->name,
                    (unsigned long)(addrs[i] - base));
        found = 1;
        break;
      }
    }

    if (!found) {
      Dl_info info;
      if (dladdr((void *)addrs[i], &info) && info.dli_fname)
        debugPrintf("    %s = %s (%s+0x%lx)\n", what[i], info.dli_fname,
                    info.dli_sname ? info.dli_sname : "?",
                    info.dli_saddr ? (unsigned long)(addrs[i] - (uintptr_t)info.dli_saddr) : 0ul);
    }
  }

  if (log_verbose) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (maps) {
      char line[256];
      while (fgets(line, sizeof(line), maps))
        debugPrintf("map: %s", line);
      fclose(maps);
    }
  }

  signal(sig, SIG_DFL);
  raise(sig);
}

static void install_crash_handlers(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = crash_handler;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
}

static void set_screen_size(void) {
  if (config.screen_width > 0 && config.screen_height > 0) {
    screen_width = config.screen_width;
    screen_height = config.screen_height;
  } else {
    screen_width = 640;
    screen_height = 480;
  }
}

static void check_data(void) {
  const char *files[] = {
    "lib/liblauncher.so",
    "lib/libengine.so",
    "lib/libclient.so",
    "lib/libserver.so",
    "lib/libtogl.so",
    "assets/extras_dir.vpk",
    "files/dejavusans.ttf",
    "cstrike/gameinfo.txt",
    "cstrike/steam.inf",
    "platform/platform_misc_dir.vpk",
  };
  struct stat st;
  const unsigned int numfiles = sizeof(files) / sizeof(*files);
  for (unsigned int i = 0; i < numfiles; ++i) {
    if (stat(files[i], &st) >= 0)
      continue;

    char missing[160];
    snprintf(missing, sizeof(missing), "  %s", files[i]);
    const int is_gamedata = strncmp(files[i], "lib/", 4) != 0 &&
                            strncmp(files[i], "assets/", 7) != 0 &&
                            strncmp(files[i], "files/", 6) != 0;
    const char *lines[8];
    int n = 0;
    lines[n++] = "This file is missing from the css folder:";
    lines[n++] = "";
    lines[n++] = missing;
    lines[n++] = "";
    if (is_gamedata) {
      lines[n++] = "Copy your own game data into css:";
      lines[n++] = "  cstrike/   hl2/   platform/";
    } else {
      lines[n++] = "Delete the lib folder and start again to";
      lines[n++] = "re-run the APK setup.";
    }
    setup_show_error("MISSING GAME FILES", lines, n);
    fatal_error("missing %s/%s", config.install_root, files[i]);
  }
}

static void patch_class_preview(so_module *client) {
  const uintptr_t vaddr = 0xadf7ec;
  const uint32_t orig = 0xa9ba7bfd;
  if (config.classmenu_preview || !client)
    return;
  uint32_t *insn = (uint32_t *)((uintptr_t)client->load_virtbase + vaddr);
  if (*insn != orig) {
    debugPrintf("class menu patch skipped: code at %p is %08x, expected %08x\n",
                (void *)insn, *insn, orig);
    return;
  }
  *insn = 0xd65f03c0;
  tracePrintf("class menu 3D preview disabled\n");
}

static void load_modules(void) {
  char path[512];

  for (unsigned int i = 0; i < NUM_MODULES; i++) {
    snprintf(path, sizeof(path), "lib/%s", module_names[i]);
    const int res = so_load(&modules[i], path);
    if (res < 0)
      fatal_error("Could not load\n%s\n(so_load: %d)", path, res);
  }

  for (unsigned int i = 0; i < NUM_MODULES; i++) {
    so_relocate(&modules[i]);
    so_resolve(&modules[i], dynlib_functions, dynlib_numfunctions, 1);
  }

  patch_class_preview(so_find_module("libclient.so"));

  for (unsigned int i = 0; i < NUM_MODULES; i++) {
    so_finalize(&modules[i]);
    so_flush_caches(&modules[i]);
  }
  for (unsigned int i = 0; i < NUM_MODULES; i++) {
    so_execute_init_array(&modules[i]);
    so_free_temp(&modules[i]);
  }
}

static void append_arg(char *dst, size_t dst_size, const char *arg) {
  size_t len;
  size_t arg_len;

  if (!dst_size || !arg || !arg[0])
    return;

  len = strlen(dst);
  if (len >= dst_size - 1)
    return;

  if (len > 0) {
    dst[len++] = ' ';
    dst[len] = '\0';
  }

  arg_len = strlen(arg);
  if (arg_len >= dst_size - len)
    arg_len = dst_size - len - 1;
  memcpy(dst + len, arg, arg_len);
  dst[len + arg_len] = '\0';
}

static void append_int_arg(char *dst, size_t dst_size, int value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", value);
  append_arg(dst, dst_size, buf);
}

static int option_arg_takes_value(const char *arg) {
  return !strcmp(arg, "+joystick") ||
         !strcmp(arg, "joystick") ||
         !strcmp(arg, "+touch_enable") ||
         !strcmp(arg, "touch_enable") ||
         !strcmp(arg, "+cl_showfps") ||
         !strcmp(arg, "cl_showfps") ||
         !strcmp(arg, "-language") ||
         !strcmp(arg, "-audiolanguage");
}

static int option_arg_is_managed(const char *arg) {
  return !strcmp(arg, "-console") || option_arg_takes_value(arg);
}

static void append_user_args(char *dst, size_t dst_size, const char *args) {
  char buf[256];
  char *tok;
  int skip_next = 0;

  if (!args || !args[0])
    return;

  snprintf(buf, sizeof(buf), "%s", args);
  tok = strtok(buf, " \t\r\n");
  while (tok) {
    if (skip_next) {
      skip_next = 0;
    } else if (option_arg_is_managed(tok)) {
      skip_next = option_arg_takes_value(tok);
    } else {
      append_arg(dst, dst_size, tok);
    }
    tok = strtok(NULL, " \t\r\n");
  }
}

static const char *language_env_name(const char *lang) {
  static const struct {
    const char *name;
    const char *env;
  } langs[] = {
    { "english", "en_US" },
    { "german", "de_DE" },
    { "french", "fr_FR" },
    { "italian", "it_IT" },
    { "spanish", "es_ES" },
    { "koreana", "ko_KR" },
    { "schinese", "zh_CN" },
    { "tchinese", "zh_TW" },
    { "russian", "ru_RU" },
    { "thai", "th_TH" },
    { "japanese", "ja_JP" },
    { "portuguese", "pt_PT" },
    { "polish", "pl_PL" },
    { "danish", "da_DK" },
    { "dutch", "nl_NL" },
    { "finnish", "fi_FI" },
    { "norwegian", "no_NO" },
    { "swedish", "sv_SE" },
    { "romanian", "ro_RO" },
    { "turkish", "tr_TR" },
    { "hungarian", "hu_HU" },
    { "czech", "cs_CZ" },
    { "brazilian", "pt_BR" },
    { "bulgarian", "bg_BG" },
    { "greek", "el_GR" },
    { "ukrainian", "uk_UA" },
  };

  for (unsigned i = 0; i < sizeof(langs) / sizeof(*langs); i++) {
    if (!strcmp(lang, langs[i].name))
      return langs[i].env;
  }
  return "en_US";
}

static const char *const baked_cfg_lines[] = {
  "sv_cheats 1",
  "// --- master server (nillerusr topluluk sunucu listesi) ---",
  "addmaster ms.workbench.network:27010",
  "sk_autoaim_mode 2",
  "autoaim_max_dist 8192",
  "autoaim_max_deflect 0.90",
  "joy_autoaimdampen 0.4",
  "joy_autoaimdampenrange 12",
  "mat_picmip 2",
  "r_rootlod 2",
  "r_lod 2",
  "r_staticprop_lod 3",
  "mat_reducefillrate 1",
  "mat_bumpmap 0",
  "mat_specular 0",
  "mat_hdr_level 0",

  "mat_queue_mode 2",
  "mat_antialias 0",
  "r_3dsky 0",
  "r_shadows 0",
  "r_shadowrendertotexture 0",
  "r_dynamic 0",
  "r_occlusion 0",
  "r_pixelfog 0",
  "r_worldlights 0",
  "r_lightaverage 0",
  "r_dopixelvisibility 0",
  "r_PhysPropStaticLighting 0",
  "r_drawbatchdecals 0",
  "r_drawdetailprops 0",
  "r_drawflecks 0",
  "r_maxdlights 0",
  "r_renderoverlayfragment 0",
  "r_threaded_particles 1",
  "r_threaded_renderables 1",
  "r_queued_ropes 1",
  "rope_smooth 0",
  "rope_subdiv 0",
  "r_waterdrawreflection 0",
  "r_waterdrawrefraction 0",
  "fog_enable 0",
  "fog_enable_water_fog 0",
  "muzzleflash_light 0",
  "mat_disable_bloom 1",
  "r_decals 32",
  "mp_decals 32",
  "cl_detaildist 0",
  "cl_phys_props_enable 0",
  "cl_phys_props_max 30",
  "props_break_max_pieces 0",
  "func_break_max_pieces 0",
  "cl_ejectbrass 0",
  "cl_show_splashes 0",
  "cl_smooth 0",
  "cl_ragdoll_fade_time 3",
  "cl_detail_avoid_force 0",
  "cl_detail_avoid_radius 0",
  "cl_detail_max_sway 0",
  "cl_show_achievement_popups 0",
  "mod_load_anims_async 1",
  "mod_load_vcollide_async 1",
  "mod_load_mesh_async 1",
  "cl_threaded_bone_setup 1",
  "// --- cok cekirdek: SADECE ses ayri thread. host_thread_mode 1 bu",
  "// derlemede girdiyi/hareketi kilitliyor (2026-07-15 kullanici raporu),",
  "// cl_threaded_client_leaf_system de ayni supheyle acikca 0 ---",
  "host_thread_mode 0",
  "cl_threaded_client_leaf_system 0",
  "snd_mix_async 1",
  "r_threaded_client_shadow_manager 1",
  "sv_unlag 0",
  "hud_fastswitch 1",
  "// --- CPU optimizasyonlari (2026-07-16): bot animasyon/fizik yuku azaltma ---",
  "ai_expression_optimization 1",
  "g_ragdoll_maxcount 0",
  "r_eyes 0",
  "r_eyemove 0",
  "r_flex 0",
  "r_teeth 0",
  "anim_3wayblend 0",
  "r_drawmodeldecals 0",
  "violence_ablood 0",
  "violence_hblood 0",
  "sv_voiceenable 0",
  "// --- 2. optimizasyon dalgasi (2026-07-16, binary'de dogrulandi) ---",
  "sv_parallel_packentities 1",
  "sv_parallel_sendsnapshot 1",
  "r_flashlightdepthtexture 0",
  "r_flashlightrendermodels 0",
  "r_waterforceexpensive 0",
  "r_fastzreject 1",
  "r_lightcache_numambientsamples 6",
  "snd_pitchquality 0",
  "nb_update_frequency 0.15",
  "// --- L1+X / L1+Y kombolari (loader F3/F4 gonderir) ---",
  "bind F3 autobuy",
  "bind F4 rebuy",
  "snd_mixahead 0.3",
  "snd_async_fullyasync 1",
  "snd_async_spew_blocking 1",
  "// forcepreload GERI ALINDI 2026-07-15: donmalari 3-4sn'den 8.7sn'ye uzatti",
  "// (gec yuklenen kaynak bundle'ini tek seferde cekiyor)",
  "sv_forcepreload 0",
  "cl_forcepreload 0",
  "cl_minmodels 1",
  "cl_min_ct 1",
  "cl_min_t 1",
  "mat_monitorgamma 1.6",
  "mat_postprocess_enable 0",
  "// --- 3D view %70 boyutta cizilir, dev/upscale ile tam ekrana buyutulur ---",
  "// (HUD/menu tam cozunurlukte kalir; kapatmak icin mat_viewportscale 1)",
  "mat_viewportscale 0.63",
  "mat_viewportupscale 1",
  "cl_crosshaircolor 1",
  "cl_crosshairthickness 1.5",
  "cl_crosshairusealpha 1",
  "cl_crosshairalpha 255",
  "dsp_slow_cpu 1",
  "// --- master listesindeki sunucular (m1..): konsola m1 yaz ---",
  "alias mservers \"echo ==MASTER SUNUCULARI==; echo m1=188.220.168.50:27020; echo m2=188.220.168.50:27022; echo m3=85.90.216.158:27015; echo m4=82.157.12.183:27015; echo m5=82.157.12.183:27016; echo m6=82.157.12.183:27014; echo m7=15.204.223.3:27018\"",
  "alias m1 \"connect 188.220.168.50:27020\"",
  "alias m2 \"connect 188.220.168.50:27022\"",
  "alias m3 \"connect 85.90.216.158:27015\"",
  "alias m4 \"connect 82.157.12.183:27015\"",
  "alias m5 \"connect 82.157.12.183:27016\"",
  "alias m6 \"connect 82.157.12.183:27014\"",
  "alias m7 \"connect 15.204.223.3:27018\"",

  "fps_max 40",
};

static void write_baked_cfg(void) {
  char dir[512], path[600];
  snprintf(dir, sizeof(dir), "%s/cstrike/cfg", config.install_root);
  mkdir(dir, 0777);
  snprintf(path, sizeof(path), "%s/css_r36s.cfg", dir);
  FILE *f = fopen(path, "w");
  if (!f) { debugPrintf("WARNING: could not write %s\n", path); return; }
  fputs("// css_r36s.cfg — css_arkos tarafindan otomatik uretildi (her acilista\n", f);
  fputs("// uzerine yazilir; kalici degisiklik icin autoexec.cfg kullan).\n", f);
  for (unsigned i = 0; i < sizeof(baked_cfg_lines)/sizeof(*baked_cfg_lines); i++) {
    fputs(baked_cfg_lines[i], f);
    fputc('\n', f);
  }
  fclose(f);
  tracePrintf("baked config written: %s\n", path);
}

static void setup_game_environment(so_module *launcher) {
  static char extras_path[384];
  static char lib_path[384];
  static char cmdline[1536];

  write_baked_cfg();

  snprintf(extras_path, sizeof(extras_path), "%s/assets/extras_dir.vpk", config.install_root);
  snprintf(lib_path, sizeof(lib_path), "%s/lib", config.install_root);

  setenv("APP_DATA_PATH", config.install_root, 1);
  setenv("APP_LIB_PATH", lib_path, 1);
  unsetenv("APP_MOD_LIB");
  setenv("VALVE_GAME_PATH", config.install_root, 1);
  setenv("EXTRAS_VPK_PATH", extras_path, 1);
  setenv("LANG", language_env_name(config.lang), 1);
  setenv("HOME", config.install_root, 1);
  setenv("SDL_TOUCH_MOUSE_EVENTS", "0", 1);

  cmdline[0] = '\0';
  append_arg(cmdline, sizeof(cmdline), "-game");
  append_arg(cmdline, sizeof(cmdline), config.gamedir);
  append_arg(cmdline, sizeof(cmdline), "-w");
  append_int_arg(cmdline, sizeof(cmdline), screen_width);
  append_arg(cmdline, sizeof(cmdline), "-h");
  append_int_arg(cmdline, sizeof(cmdline), screen_height);
  append_user_args(cmdline, sizeof(cmdline), config.args);
  if (config.console)
    append_arg(cmdline, sizeof(cmdline), "-console");
  append_arg(cmdline, sizeof(cmdline), "+cl_showfps");
  append_arg(cmdline, sizeof(cmdline), config.show_fps ? "1" : "0");
  append_arg(cmdline, sizeof(cmdline), "+joystick");
  append_arg(cmdline, sizeof(cmdline), config.gamepad ? "1" : "0");
  append_arg(cmdline, sizeof(cmdline), "+touch_enable");
  append_arg(cmdline, sizeof(cmdline), "0");
  append_arg(cmdline, sizeof(cmdline), "-language");
  append_arg(cmdline, sizeof(cmdline), config.lang);
  append_arg(cmdline, sizeof(cmdline), "-audiolanguage");
  append_arg(cmdline, sizeof(cmdline), config.lang);

  append_arg(cmdline, sizeof(cmdline),
             "-tickrate 30 +mat_queue_mode 2 +mat_picmip 2 +r_rootlod 2 "

             "+datacachesize 128 +mat_bumpmap 0 +mat_specular 0 "
             "+mat_reducefillrate 1 +sk_autoaim_mode 2 +bot_difficulty 0 "
             "-threads 3 "
             "+exec css_r36s.cfg");

  if (config.debug)
    append_arg(cmdline, sizeof(cmdline), "-dev 2");

  tracePrintf("command line: %s\n", cmdline);

  void (* setArgs)(void *env, void *cls, const char *jstr) =
      (void *)so_find_addr_rx(launcher, "Java_com_valvesoftware_ValveActivity2_setArgs");
  setArgs(fake_env, NULL, cmdline);
}

int main(int argc, char **argv) {

  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  if (argc > 0 && argv[0] && strchr(argv[0], '/')) {
    char *dir = strdup(argv[0]);
    chdir(dirname(dir));
    free(dir);
  }

  if (read_config(CONFIG_NAME) < 0) {
    chdir(DEFAULT_INSTALL_ROOT);
    if (read_config(CONFIG_NAME) < 0)
      write_config(CONFIG_NAME);
  }

  if (!getcwd(config.install_root, sizeof(config.install_root)))
    fatal_error("Could not read the working directory.");

  log_verbose = config.debug;
  install_crash_handlers();
  set_screen_size();

  debugPrintf("css_arkos: %s (%dx%d, %s%s)\n", config.install_root,
              screen_width, screen_height, config.lang,
              log_verbose ? ", verbose log" : "");

  if (setup_needed(config.install_root) && setup_run(config.install_root) != 0)
    _exit(1);

  check_data();

  fake_tls_install();
  jni_init();
  gamma_init_from_config(config.brightness);
  proc_files_init(config.install_root);

  load_modules();

  so_module *launcher = so_find_module("liblauncher.so");
  if (!launcher)
    fatal_error("liblauncher.so did not load");

  setup_game_environment(launcher);

  int (* LauncherMainAndroid)(int argc, char **argv) =
      (void *)so_find_addr_rx(launcher, "LauncherMainAndroid");

  static char *fake_argv[] = { "cstrike_linux", NULL };
  const int ret = LauncherMainAndroid(1, fake_argv);
  debugPrintf("LauncherMainAndroid returned %d\n", ret);

  _exit(0);
  return 0;
}
