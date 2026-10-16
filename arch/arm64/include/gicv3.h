#ifndef GICV3_H
#define GICV3_H

#include "types.h"

void gic_init();
void gic_inithart();
void gic_enable_spi(uint32 intid, int cpu);

#endif