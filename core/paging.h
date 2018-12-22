#ifndef _DOSPACK_PAGING_H__
#define _DOSPACK_PAGING_H__

#include "logging.h"
#include "memory.h"

struct dp_paging {
	int dummy;

	char _marshal_sep[0];

	struct dp_memory *memory;
	struct dp_logging *logging;
};

void dp_paging_init(struct dp_paging *paging,
		    struct dp_memory *memory,
		    struct dp_logging *logging, struct dp_marshal *marshal);
void dp_paging_marshal(struct dp_paging *paging, struct dp_marshal *marshal);
void dp_paging_unmarshal(struct dp_paging *paging, struct dp_marshal *marshal);
void dp_paging_clear_tlb(struct dp_paging *paging);

#endif
