#ifndef __IMPORTS_H__
#define __IMPORTS_H__

#include <stddef.h>
#include "so_util.h"

extern DynLibFunction dynlib_functions[];
extern size_t dynlib_numfunctions;

void *eglGetProcAddress_fake(const char *name);

#endif
