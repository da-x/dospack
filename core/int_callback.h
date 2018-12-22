#ifndef _DOSPACK_INT_CALLBACK_H__
#define _DOSPACK_INT_CALLBACK_H__

#include "common.h"
#include "logging.h"
#include "memory.h"

enum dp_cb_type {
	DP_CB_TYPE_RETN, DP_CB_TYPE_RETF, DP_CB_TYPE_RETF8, DP_CB_TYPE_IRET, DP_CB_TYPE_IRETD, DP_CB_TYPE_IRET_STI, DP_CB_TYPE_IRET_EOI_PIC1,
	DP_CB_TYPE_IRQ0, DP_CB_TYPE_IRQ1, DP_CB_TYPE_IRQ9, DP_CB_TYPE_IRQ12, DP_CB_TYPE_IRQ12_RET, DP_CB_TYPE_IRQ6_PCJR, DP_CB_TYPE_MOUSE,
	DP_CB_TYPE_INT29, DP_CB_TYPE_INT16, DP_CB_TYPE_HOOKABLE, DP_CB_TYPE_TDE_IRET, DP_CB_TYPE_IPXESR, DP_CB_TYPE_IPXESR_RET,
	DP_CB_TYPE_INT21, DP_CB_TYPE_NONE, DP_CB_TYPE_IDLE,
};

enum dp_callback_aal {
	DP_CALLBACK_NONE = 0,
	DP_CALLBACK_STOP = 1,
};

#define DP_CB_MAX	128
#define DP_CB_SIZE	32
#define DP_CB_SEG	0xF000
#define DP_CB_SOFFSET	0x1000

typedef u32 (*dp_cb_func_t)(void *ptr);

struct dp_int_callback_desc {
	struct dp_int_callback *ic;
	enum dp_bool used;
	enum dp_cb_type type;

	dp_cb_func_t func;
	void *ptr;
};

struct dp_int_callback {
	struct dp_int_callback_desc list[DP_CB_MAX];

	char _marshal_sep[0];

	struct dp_memory *memory;
	struct dp_logging *logging;
};

void dp_int_callback_init(struct dp_int_callback *int_callback, struct dp_memory *memory,
			  struct dp_logging *logging, struct dp_marshal *marshal);
void dp_int_callback_marshal(struct dp_int_callback *int_callback, struct dp_marshal *marshal);
void dp_int_callback_unmarshal(struct dp_int_callback *int_callback, struct dp_marshal *marshal);

u32 dp_int_callback_register(struct dp_int_callback *int_callback, dp_cb_func_t func, void *ptr,
			     enum dp_cb_type cb_type, phys_addr_t phy_addr);
u32 dp_int_callback_register_inthandler(struct dp_int_callback *int_callback, u32 idt_entry,
					dp_cb_func_t func, void *ptr, enum dp_cb_type cb_type);
u32 dp_int_callback_register_inthandler_addr(struct dp_int_callback *int_callback, u32 idt_entry,
					     dp_cb_func_t func, void *ptr, enum dp_cb_type cb_type,
					     real_pt_addr_t real_addr);

#define dp_int_callback_base_phy_addr   phys_make(DP_CB_SEG, DP_CB_SOFFSET)

static inline phys_addr_t dp_cb_index_to_phyaddr(u32 cb_index)
{
	return phys_make(DP_CB_SEG, (u16)(DP_CB_SOFFSET + cb_index * DP_CB_SIZE));
}

static inline phys_addr_t dp_cb_index_to_realaddr(u32 cb_index)
{
	return real_make(DP_CB_SEG, (u16)(DP_CB_SOFFSET + cb_index * DP_CB_SIZE));
}

#endif
