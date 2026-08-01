#ifndef __OSK_H__
#define __OSK_H__

int osk_is_active(void);
void osk_open(void *window);
void osk_close(void);
void osk_toggle(void *window);

int osk_handle_click(int x, int y, int down);

void osk_draw(void *window);

int osk_cursor_draw(void *window, float x, float y);

#endif
