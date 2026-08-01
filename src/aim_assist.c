#include <string.h>
#include <stdint.h>
#include <math.h>

#include "config.h"
#include "util.h"
#include "so_util.h"
#include "aim_assist.h"

extern uint32_t SDL_GetTicks(void);

typedef struct { float x, y, z; } Vector;
typedef struct { float pitch, yaw, roll; } QAngle;

typedef struct RecvTable_s RecvTable;
typedef struct RecvProp_s RecvProp;

struct RecvProp_s {
  const char *m_pVarName;
  int m_RecvType;
  int m_Flags;
  int m_StringBufferSize;
  unsigned char m_bInsideArray;
  const void *m_pExtraData;
  RecvProp *m_pArrayProp;
  void *m_ArrayLengthProxy;
  void *m_ProxyFn;
  void *m_DataTableProxyFn;
  RecvTable *m_pDataTable;
  int m_Offset;
  int m_ElementStride;
  int m_nElements;
  const char *m_pParentArrayPropName;
};

struct RecvTable_s {
  RecvProp *m_pProps;
  int m_nProps;
  void *m_pDecoder;
  const char *m_pNetTableName;
  unsigned char m_bInitialized;
  unsigned char m_bInMainList;
};

typedef struct ClientClass_s ClientClass;
struct ClientClass_s {
  void *m_pCreateFn;
  void *m_pCreateEventFn;
  const char *m_pNetworkName;
  RecvTable *m_pRecvTable;
  ClientClass *m_pNext;
  int m_ClassID;
};

typedef struct {
  char name[32];
  int userID;
  char guid[33];
  unsigned int friendsID;
  char friendsName[32];
  unsigned char fakeplayer;
  unsigned char ishltv;
  unsigned int customFiles[4];
  unsigned char filesDownloaded;
  unsigned char _spare[64];
} player_info_min;

enum {
  VE_GetScreenSize   = 5,
  VE_GetPlayerInfo   = 8,
  VE_GetLocalPlayer  = 12,
  VE_GetViewAngles   = 19,
  VE_SetViewAngles   = 20,
  VE_GetMaxClients   = 21,
  VE_IsInGame        = 26,
  VE_GetGameDirectory = 35,
};
enum {
  EL_GetClientNetworkable = 0,
  EL_GetClientEntity      = 3,
};
enum {
  NW_IsDormant = 8,
};
enum {
  CD_GetAllClasses = 8,
};

static inline void *vfn(void *obj, int idx) {
  return (*(void ***)obj)[idx];
}

typedef void *(*create_interface_fn)(const char *name, int *ret);

static void *g_engine;
static void *g_entlist;
static int g_state;

enum { VE_ExecuteClientCmd = 102 };
#define EXECCLIENTCMD_OFFSET 0x64a810
static void *g_cmd_engine;
static void (*g_exec_cmd)(void *, const char *);
static int g_cmd_state;

static void bind_engine_cmd(void) {
  if (g_cmd_state != 0)
    return;
  so_module *eng_mod = so_find_module("libengine.so");
  if (!eng_mod)
    return;
  create_interface_fn eng_ci =
      (create_interface_fn)so_lookup_export(eng_mod, "CreateInterface");
  void *engine = eng_ci ? eng_ci("VEngineClient014", NULL) : NULL;
  if (!engine)
    return;
  void *fn = vfn(engine, VE_ExecuteClientCmd);
  const uintptr_t base = (uintptr_t)eng_mod->load_virtbase;
  if ((uintptr_t)fn == base + EXECCLIENTCMD_OFFSET) {
    g_cmd_engine = engine;
    g_exec_cmd = (void (*)(void *, const char *))fn;
    g_cmd_state = 1;
    tracePrintf("engine cmd: ExecuteClientCmd @ vtable[102] verified\n");
  } else {
    g_cmd_state = -1;
    debugPrintf("engine cmd: vtable[102]=%p does not match expected %p, disabled\n",
                fn, (void *)(base + EXECCLIENTCMD_OFFSET));
  }
}

int engine_run_command(const char *cmd) {
  if (g_cmd_state == 0)
    bind_engine_cmd();
  if (g_cmd_state != 1 || !g_exec_cmd || !g_cmd_engine)
    return 0;
  g_exec_cmd(g_cmd_engine, cmd);
  return 1;
}

enum { VGUI_IsGameUIVisible = 3 };
#define ISGAMEUIVISIBLE_OFFSET 0x767c94
static void *g_vgui;
static int (*g_is_gameui_visible)(void *);
static int g_vgui_state;

static void bind_engine_vgui(void) {
  if (g_vgui_state != 0)
    return;
  so_module *eng_mod = so_find_module("libengine.so");
  if (!eng_mod)
    return;
  create_interface_fn eng_ci =
      (create_interface_fn)so_lookup_export(eng_mod, "CreateInterface");
  void *vgui = eng_ci ? eng_ci("VEngineVGui001", NULL) : NULL;
  if (!vgui)
    return;
  void *fn = vfn(vgui, VGUI_IsGameUIVisible);
  const uintptr_t base = (uintptr_t)eng_mod->load_virtbase;
  if ((uintptr_t)fn == base + ISGAMEUIVISIBLE_OFFSET) {
    g_vgui = vgui;
    g_is_gameui_visible = (int (*)(void *))fn;
    g_vgui_state = 1;
    tracePrintf("engine vgui: IsGameUIVisible @ vtable[3] verified\n");
  } else {
    g_vgui_state = -1;
    debugPrintf("engine vgui: vtable[3]=%p does not match expected %p, "
                "Start will only open the menu\n",
                fn, (void *)(base + ISGAMEUIVISIBLE_OFFSET));
  }
}

int engine_gameui_visible(void) {
  if (g_vgui_state == 0)
    bind_engine_vgui();
  if (g_vgui_state != 1 || !g_is_gameui_visible || !g_vgui)
    return -1;
  return g_is_gameui_visible(g_vgui) ? 1 : 0;
}

static int off_origin = -1;
static int off_team = -1;
static int off_lifestate = -1;
static int off_viewoffset_z = -1;

static int g_lock_idx;

static int recv_find(RecvTable *table, const char *name, int base, int depth) {
  if (!table || depth > 12)
    return -1;
  for (int i = 0; i < table->m_nProps; i++) {
    RecvProp *p = &table->m_pProps[i];
    if (p->m_pVarName && !strcmp(p->m_pVarName, name))
      return base + p->m_Offset;
    if (p->m_pDataTable) {
      const int r = recv_find(p->m_pDataTable, name, base + p->m_Offset, depth + 1);
      if (r >= 0)
        return r;
    }
  }
  return -1;
}

static int scan_netvars(void *clientdll) {
  ClientClass *(*get_all)(void *) = vfn(clientdll, CD_GetAllClasses);
  for (ClientClass *cc = get_all(clientdll); cc; cc = cc->m_pNext) {
    if (!cc->m_pNetworkName || strcmp(cc->m_pNetworkName, "CCSPlayer"))
      continue;
    off_origin = recv_find(cc->m_pRecvTable, "m_vecOrigin", 0, 0);
    off_team = recv_find(cc->m_pRecvTable, "m_iTeamNum", 0, 0);
    off_lifestate = recv_find(cc->m_pRecvTable, "m_lifeState", 0, 0);
    off_viewoffset_z = recv_find(cc->m_pRecvTable, "m_vecViewOffset[2]", 0, 0);
    tracePrintf("aim assist: netvar offsets origin=%d team=%d life=%d viewz=%d\n",
                off_origin, off_team, off_lifestate, off_viewoffset_z);
    return off_origin >= 0 && off_team >= 0 && off_lifestate >= 0;
  }
  return 0;
}

static int aim_init(void) {
  so_module *eng_mod = so_find_module("libengine.so");
  so_module *cl_mod = so_find_module("libclient.so");
  if (!eng_mod || !cl_mod)
    return 0;

  create_interface_fn eng_ci = (create_interface_fn)so_lookup_export(eng_mod, "CreateInterface");
  create_interface_fn cl_ci = (create_interface_fn)so_lookup_export(cl_mod, "CreateInterface");
  if (!eng_ci || !cl_ci)
    return 0;

  void *engine = eng_ci("VEngineClient014", NULL);
  void *entlist = cl_ci("VClientEntityList003", NULL);
  void *clientdll = cl_ci("VClient017", NULL);
  if (!engine || !entlist || !clientdll)
    return 0;

  int w = 0, h = 0;
  ((void (*)(void *, int *, int *))vfn(engine, VE_GetScreenSize))(engine, &w, &h);
  const char *gamedir =
      ((const char *(*)(void *))vfn(engine, VE_GetGameDirectory))(engine);
  if (w != screen_width || h != screen_height ||
      !gamedir || !strstr(gamedir, "cstrike")) {
    debugPrintf("aim assist: validation FAILED (%dx%d, dir=%s), disabled\n",
                w, h, gamedir ? gamedir : "(null)");
    return -1;
  }

  if (!scan_netvars(clientdll)) {
    debugPrintf("aim assist: CCSPlayer netvar scan failed, disabled\n");
    return -1;
  }

  g_engine = engine;
  g_entlist = entlist;
  debugPrintf("aim assist: ready (%dx%d, %s)\n", w, h, gamedir);
  return 1;
}

static float ang_norm(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}

void aim_assist_tick(int engaged) {
  static uint32_t last_run, last_engaged_ms;

  if (!config.aim_assist || g_state < 0)
    return;

  if (!engaged) {
    g_lock_idx = 0;
    last_engaged_ms = 0;
    return;
  }

  const uint32_t now = SDL_GetTicks();
  if (now == last_run)
    return;
  last_run = now;

  if (g_state == 0) {

    const int r = aim_init();
    if (r == 0)
      return;
    g_state = r;
    if (r < 0)
      return;
  }

  float dt = last_engaged_ms ? (now - last_engaged_ms) / 1000.0f : 0.0f;
  last_engaged_ms = now;
  if (dt > 0.1f) dt = 0.1f;

  void *engine = g_engine, *entlist = g_entlist;
  if (!((int (*)(void *))vfn(engine, VE_IsInGame))(engine)) {
    g_lock_idx = 0;
    return;
  }

  const int local_idx = ((int (*)(void *))vfn(engine, VE_GetLocalPlayer))(engine);
  const int max_clients = ((int (*)(void *))vfn(engine, VE_GetMaxClients))(engine);
  char *local = ((char *(*)(void *, int))vfn(entlist, EL_GetClientEntity))(entlist, local_idx);
  if (!local || max_clients <= 0 || max_clients > 128)
    return;

  const int local_team = *(int *)(local + off_team);
  if (local_team != 2 && local_team != 3)
    return;
  if (*(unsigned char *)(local + off_lifestate) != 0)
    return;

  Vector eye = *(Vector *)(local + off_origin);
  eye.z += (off_viewoffset_z >= 0) ? *(float *)(local + off_viewoffset_z) : 64.0f;

  QAngle va;
  ((void (*)(void *, QAngle *))vfn(engine, VE_GetViewAngles))(engine, &va);

  const float fov = (float)config.aim_fov;
  float best_err = 1e9f, best_dyaw = 0, best_dpitch = 0;
  int best_idx = 0;

  for (int i = 1; i <= max_clients; i++) {
    if (i == local_idx)
      continue;

    void *nw = ((void *(*)(void *, int))vfn(entlist, EL_GetClientNetworkable))(entlist, i);
    if (!nw || ((int (*)(void *))vfn(nw, NW_IsDormant))(nw))
      continue;

    char *ent = ((char *(*)(void *, int))vfn(entlist, EL_GetClientEntity))(entlist, i);
    if (!ent)
      continue;
    const int team = *(int *)(ent + off_team);
    if (team == local_team || (team != 2 && team != 3))
      continue;
    if (*(unsigned char *)(ent + off_lifestate) != 0)
      continue;

    Vector tp = *(Vector *)(ent + off_origin);
    tp.z += (float)config.aim_height;

    const float dx = tp.x - eye.x, dy = tp.y - eye.y, dz = tp.z - eye.z;
    const float dist2d = sqrtf(dx * dx + dy * dy);
    if (dist2d + fabsf(dz) < 8.0f)
      continue;

    const float want_yaw = atan2f(dy, dx) * (180.0f / (float)M_PI);
    const float want_pitch = -atan2f(dz, dist2d) * (180.0f / (float)M_PI);
    const float dyaw = ang_norm(want_yaw - va.yaw);
    const float dpitch = ang_norm(want_pitch - va.pitch);
    const float err = sqrtf(dyaw * dyaw + dpitch * dpitch);

    const float limit = (i == g_lock_idx) ? fov * 1.5f : fov;
    const float score = (i == g_lock_idx) ? err * 0.6f : err;
    if (err < limit && score < best_err) {
      best_err = score;
      best_idx = i;
      best_dyaw = dyaw;
      best_dpitch = dpitch;
    }
  }

  g_lock_idx = best_idx;
  if (!best_idx || dt <= 0.0f)
    return;

  const float t = 1.0f - expf(-(float)config.aim_speed * dt);
  va.yaw = ang_norm(va.yaw + best_dyaw * t);
  va.pitch += best_dpitch * t;
  if (va.pitch > 89.0f) va.pitch = 89.0f;
  if (va.pitch < -89.0f) va.pitch = -89.0f;
  ((void (*)(void *, QAngle *))vfn(engine, VE_SetViewAngles))(engine, &va);
}
