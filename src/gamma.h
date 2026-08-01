#ifndef __GAMMA_H__
#define __GAMMA_H__

#include <stdint.h>

void gamma_init_from_config(int bright100);
void gamma_set_ramp(const uint16_t *r, const uint16_t *g, const uint16_t *b);

void gamma_apply(void *window);

#endif
