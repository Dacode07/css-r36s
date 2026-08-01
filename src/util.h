#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>

extern int log_verbose;

int debugPrintf(const char *text, ...);

int tracePrintf(const char *text, ...);

void engineLine(const char *text);
void engineStream(const char *text);
int enginePrintf(const char *text, ...);
int enginePuts(const char *text);

void resolve_code_addr(uintptr_t pc, char *out, size_t n);

int ret0(void);
int retm1(void);

static inline uint64_t umin(uint64_t a, uint64_t b) {
  return (a < b) ? a : b;
}

#endif
