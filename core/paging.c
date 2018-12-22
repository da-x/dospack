#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_PAGING
#define DP_LOGGING           (paging->logging)

#include <string.h>

#include "paging.h"

void dp_paging_init(struct dp_paging *paging, struct dp_memory *memory, struct dp_logging *logging, struct dp_marshal *marshal)
{
	memset(paging, 0, sizeof(*paging));
	paging->memory = memory;
	paging->logging = logging;

	DP_INF("initializing paging");
}

void dp_paging_marshal(struct dp_paging *paging, struct dp_marshal *marshal)
{
}

void dp_paging_unmarshal(struct dp_paging *paging, struct dp_marshal *marshal)
{
}

void dp_paging_clear_tlb(struct dp_paging *paging)
{
	DP_FAT("not implemented - clear_tlb");
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
