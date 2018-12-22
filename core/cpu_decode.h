#ifndef _DOSPACK_CPU_DECODE_H__
#define _DOSPACK_CPU_DECODE_H__

#include "cpu.h"

u32 dp_decode_fill_flags(struct dp_cpu *cpu);
void dp_destroy_condition_flags(struct dp_cpu *cpu);
u32 dp_cpu_decode_normal_run(struct dp_cpu *cpu);

#endif
