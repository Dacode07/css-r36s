#ifndef __SETUP_H__
#define __SETUP_H__

int setup_needed(const char *root);

int setup_run(const char *root);

void setup_show_error(const char *title, const char *const *lines, int nlines);

#endif
