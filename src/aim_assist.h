#ifndef __AIM_ASSIST_H__
#define __AIM_ASSIST_H__

void aim_assist_tick(int engaged);

int engine_run_command(const char *cmd);

int engine_gameui_visible(void);

#endif
