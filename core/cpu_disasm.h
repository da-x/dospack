#ifndef _DOSPACK_CPU_DISASM_H__
#define _DOSPACK_CPU_DISASM_H__

#include "memory.h"

u32 dp_debug_i386dis(struct dp_memory *memory, char *buffer, phys_addr_t pc, u32 cur_ip, enum dp_bool bit32);


#endif
