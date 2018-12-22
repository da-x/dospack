#ifndef _DOSPACK_MEMORY_H__
#define _DOSPACK_MEMORY_H__

#include "common.h"
#include "logging.h"
#include "marshal.h"

typedef u32 phys_addr_t;
typedef u32 real_pt_addr_t;

#define DP_MEM_PAGE_SIZE 4096

struct dp_memory {
	u32 size;

	char _marshal_sep[0];

	u8 *data;
	struct dp_logging *logging;
};

static inline u16 real_to_seg(real_pt_addr_t pt)
{
	return (u16)(pt >> 16);
}

static inline u16 real_to_offset(real_pt_addr_t pt)
{
	return (u16)(pt & 0xffff);
}

static inline phys_addr_t real_to_phys(real_pt_addr_t pt)
{
	return (real_to_seg(pt) << 4) + real_to_offset(pt);
}

static inline phys_addr_t phys_make(u16 seg, u16 off)
{
	return ((u32)seg << 4) + off;
}

static inline real_pt_addr_t real_make(u16 seg, u16 off)
{
	return ((u32)seg << 16) + off;
}

static inline void *dp_memp_get_real_seg(struct dp_memory *memory, u16 seg)
{
	return memory->data + phys_make(seg, 0);
}

static inline void *dp_memp_get_phys(struct dp_memory *memory, phys_addr_t phy)
{
	return memory->data + phy;
}


static inline u32 dp_mem_size_in_pages(struct dp_memory *memory)
{
	return memory->size / DP_MEM_PAGE_SIZE;
}

void dp_mem_init(struct dp_memory *memory, struct dp_logging *logging, u32 size, void *data, struct dp_marshal *marshal);
void dp_mem_marshal(struct dp_memory *memory, struct dp_marshal *marshal);
void dp_mem_unmarshal(struct dp_memory *memory, struct dp_marshal *marshal);
void dp_mem_dump(struct dp_memory *memory);
real_pt_addr_t dp_memp_get_realvec(struct dp_memory *memory, u32 vec);
void dp_memp_set_realvec(struct dp_memory *memory, u32 vec, real_pt_addr_t real_addr);


u8 dp_memv_readb(struct dp_memory *memory, phys_addr_t off);
u16 dp_memv_readw(struct dp_memory *memory, phys_addr_t off);
u32 dp_memv_readd(struct dp_memory *memory, phys_addr_t off);
void dp_memv_writeb(struct dp_memory *memory, phys_addr_t off, u8 val);
void dp_memv_writew(struct dp_memory *memory, phys_addr_t off, u16 val);
void dp_memv_writed(struct dp_memory *memory, phys_addr_t off, u32 val);

void dp_memv_block_write(struct dp_memory *memory, phys_addr_t off, const void *p, u32 size);
void dp_memv_block_read(struct dp_memory *memory, phys_addr_t off, void *p, u32 size);

static inline u8 dp_realv_readb(struct dp_memory *memory, u16 seg, u16 off)
{
	return dp_memv_readb(memory, phys_make(seg, off));
}

static inline u16 dp_realv_readw(struct dp_memory *memory, u16 seg, u16 off)
{
	return dp_memv_readw(memory, phys_make(seg, off));
}

static inline u32 dp_realv_readd(struct dp_memory *memory, u16 seg, u16 off)
{
	return dp_memv_readd(memory, phys_make(seg, off));
}

static inline void dp_realv_writeb(struct dp_memory *memory, u16 seg, u16 off, u8 val)
{
	dp_memv_writeb(memory, phys_make(seg, off), val);
}

static inline void dp_realv_writew(struct dp_memory *memory, u16 seg, u16 off, u16 val)
{
	dp_memv_writew(memory, phys_make(seg, off), val);
}

static inline void dp_realv_writed(struct dp_memory *memory, u16 seg, u16 off, u32 val)
{
	dp_memv_writed(memory, phys_make(seg, off), val);
}

u8 dp_memp_readb(struct dp_memory *memory, phys_addr_t off);
u16 dp_memp_readw(struct dp_memory *memory, phys_addr_t off);
u32 dp_memp_readd(struct dp_memory *memory, phys_addr_t off);
void dp_memp_writeb(struct dp_memory *memory, phys_addr_t off, u8 val);
void dp_memp_writew(struct dp_memory *memory, phys_addr_t off, u16 val);
void dp_memp_writed(struct dp_memory *memory, phys_addr_t off, u32 val);

void dp_memv_readstr(struct dp_memory *memory, phys_addr_t off, char *c, u32 max_size);
void dp_memp_writestr(struct dp_memory *memory, phys_addr_t off, const char *str);

#endif
