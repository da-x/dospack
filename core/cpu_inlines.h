#ifndef _DOSPACK_CPU_INLINES_H__
#define _DOSPACK_CPU_INLINES_H__

#include "cpu.h"

#define dp_cpu_seg_phys(seg) cpu->segs.segs[seg].phys
#define dp_cpu_seg_value(seg) cpu->segs.segs[seg].val

static inline void dp_seg_set16(struct dp_cpu_segment *seg, u16 val)
{
	seg->val = val;
	seg->phys = ((u32)val) << 4;
}

static inline u32 dp_cpu_desc_get_base(union dp_cpu_descriptor *cd)
{
	return (cd->seg.base_24_31 << 24) | (cd->seg.base_16_23 << 16) | cd->seg.base_0_15;
}

static inline u32 dp_cpu_desc_get_limit(union dp_cpu_descriptor *cd)
{
	u32 limit = (cd->seg.limit_16_19 << 16) | cd->seg.limit_0_15;

	if (cd->seg.g)
		return (limit << 12) | 0xFFF;

	return limit;
}

static inline u32 dp_cpu_desc_get_selector(union dp_cpu_descriptor *cd)
{
	return cd->gate.selector;
}

static inline u32 dp_cpu_desc_type(union dp_cpu_descriptor *cd)
{
	return cd->seg.type;
}

static inline u32 dp_cpu_desc_conforming(union dp_cpu_descriptor *cd)
{
	return cd->seg.type & 8;
}

static inline u32 dp_cpu_desc_dpl(union dp_cpu_descriptor *cd)
{
	return cd->seg.dpl;
}

static inline u32 dp_cpu_desc_big(union dp_cpu_descriptor *cd)
{
	return cd->seg.big;
}

/* TSS descriptors */

static inline u32 dp_cpu_tss_desc_is_busy(union dp_cpu_descriptor *cd)
{
	return cd->seg.type & 2;
}

static inline u32 dp_cpu_tss_desc_is_386(union dp_cpu_descriptor *cd)
{
	return cd->seg.type & 8;
}

static inline void dp_cpu_tss_desc_set_busy(union dp_cpu_descriptor *cd, enum dp_bool busy)
{
	if (busy)
		cd->seg.type |= 2;
	else
		cd->seg.type &= ~2;
}

#endif
