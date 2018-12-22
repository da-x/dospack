#ifndef _DOSPACK_CPU_H__
#define _DOSPACK_CPU_H__

#include "common.h"
#include "memory.h"
#include "io.h"
#include "logging.h"
#include "pic.h"
#include "paging.h"
#include "int_callback.h"
#include "timetrack.h"

/**
 * Segment registers
 */

struct dp_cpu_segment {
	u16 val;
	phys_addr_t phys;
};

enum dp_segment_index {
	dp_seg_es = 0,
	dp_seg_cs,
	dp_seg_ss,
	dp_seg_ds,
	dp_seg_fs,
	dp_seg_gs,
};

struct dp_cpu_segments {
	union {
		struct dp_cpu_segment segs[8];
		struct {
			struct dp_cpu_segment es;
			struct dp_cpu_segment cs;
			struct dp_cpu_segment ss;
			struct dp_cpu_segment ds;
			struct dp_cpu_segment fs;
			struct dp_cpu_segment gs;
		};
	};
};

/**
 * ALU and stack registers
 */

union dp_cpu_reg {
	u32 dword[1];
	u16 word[2];
	u8 byte[4];
};

#ifndef DP_BIG_ENDIAN

/* TODO: Don't use defines, just declare dp_cpu_reg diffrently */

#define DCR_DW_INDEX 0
#define DCR_W_INDEX 0
#define DCR_BH_INDEX 1
#define DCR_BL_INDEX 0

#else

#define DCR_DW_INDEX 0
#define DCR_W_INDEX 1
#define DCR_BH_INDEX 2
#define DCR_BL_INDEX 3

#endif

#define dcr_reg_8l(reg) (reg.byte[DCR_BL_INDEX])
#define dcr_reg_8h(reg) (reg.byte[DCR_BH_INDEX])
#define dcr_reg_16(reg) (reg.word[DCR_W_INDEX])
#define dcr_reg_32(reg) (reg.dword[DCR_DW_INDEX])

struct dp_cpu_regs {
	union {
		union dp_cpu_reg regs[8];
		struct {
			union dp_cpu_reg ax;
			union dp_cpu_reg cx;
			union dp_cpu_reg dx;
			union dp_cpu_reg bx;
			union dp_cpu_reg sp;
			union dp_cpu_reg bp;
			union dp_cpu_reg si;
			union dp_cpu_reg di;
		};
	};
	union dp_cpu_reg ip;
	u32 flags;
};

#define dcr_reg_al	(dcr_reg_8l(cpu->regs.ax))
#define dcr_reg_ah	(dcr_reg_8h(cpu->regs.ax))
#define dcr_reg_bl	(dcr_reg_8l(cpu->regs.bx))
#define dcr_reg_bh	(dcr_reg_8h(cpu->regs.bx))
#define dcr_reg_cl	(dcr_reg_8l(cpu->regs.cx))
#define dcr_reg_ch	(dcr_reg_8h(cpu->regs.cx))
#define dcr_reg_dl	(dcr_reg_8l(cpu->regs.dx))
#define dcr_reg_dh	(dcr_reg_8h(cpu->regs.dx))

#define dcr_reg_ax	(dcr_reg_16(cpu->regs.ax))
#define dcr_reg_bx	(dcr_reg_16(cpu->regs.bx))
#define dcr_reg_cx	(dcr_reg_16(cpu->regs.cx))
#define dcr_reg_dx	(dcr_reg_16(cpu->regs.dx))
#define dcr_reg_si	(dcr_reg_16(cpu->regs.si))
#define dcr_reg_di	(dcr_reg_16(cpu->regs.di))
#define dcr_reg_bp	(dcr_reg_16(cpu->regs.bp))
#define dcr_reg_sp	(dcr_reg_16(cpu->regs.sp))
#define dcr_reg_ip	(dcr_reg_16(cpu->regs.ip))

#define dcr_reg_eax	(dcr_reg_32(cpu->regs.ax))
#define dcr_reg_ebx	(dcr_reg_32(cpu->regs.bx))
#define dcr_reg_ecx	(dcr_reg_32(cpu->regs.cx))
#define dcr_reg_edx	(dcr_reg_32(cpu->regs.dx))
#define dcr_reg_esi	(dcr_reg_32(cpu->regs.si))
#define dcr_reg_edi	(dcr_reg_32(cpu->regs.di))
#define dcr_reg_ebp	(dcr_reg_32(cpu->regs.bp))
#define dcr_reg_esp	(dcr_reg_32(cpu->regs.sp))
#define dcr_reg_eip	(dcr_reg_32(cpu->regs.ip))


#define dcr_reg_es	(cpu->segs.es)
#define dcr_reg_cs	(cpu->segs.cs)
#define dcr_reg_ss	(cpu->segs.ss)
#define dcr_reg_ds	(cpu->segs.ds)
#define dcr_reg_fs	(cpu->segs.fs)
#define dcr_reg_gs	(cpu->segs.gs)

#define dcr_reg_flags	(cpu->regs.flags)

/**
 * Flags register
 */

#define DC_FLAG_CF		0x00000001
#define DC_FLAG_PF		0x00000004
#define DC_FLAG_AF		0x00000010
#define DC_FLAG_ZF		0x00000040
#define DC_FLAG_SF		0x00000080
#define DC_FLAG_OF		0x00000800

#define DC_FLAG_TF		0x00000100
#define DC_FLAG_IF		0x00000200
#define DC_FLAG_DF		0x00000400

#define DC_FLAG_IOPL		0x00003000
#define DC_FLAG_NT		0x00004000
#define DC_FLAG_VM		0x00020000
#define DC_FLAG_AC		0x00040000
#define DC_FLAG_ID		0x00200000

#define DC_FMASK_TEST		(DC_FLAG_CF | DC_FLAG_PF | DC_FLAG_AF | DC_FLAG_ZF | DC_FLAG_SF | DC_FLAG_OF)
#define DC_FMASK_NORMAL	(DC_FMASK_TEST | DC_FLAG_DF | DC_FLAG_TF | DC_FLAG_IF | DC_FLAG_AC)
#define DC_FMASK_ALL		(DC_FMASK_NORMAL | DC_FLAG_IOPL | DC_FLAG_NT)

#define DC_SET_FLAGBIT(TYPE, TEST) do {			\
		if (TEST)					\
			cpu->regs.flags |= DC_FLAG_##TYPE;	\
		else						\
			cpu->regs.flags &= ~DC_FLAG_##TYPE;	\
	} while (0)						\

#define DC_GET_FLAG(TYPE)	(cpu->regs.flags & DC_FLAG_ ## TYPE)
#define DC_GET_FLAG_IOPL	((cpu->regs.flags & DC_FLAG_IOPL) >> 12)

/**
 * Control registers
 */

#define DC_CR0_PROTECTION		0x00000001
#define DC_CR0_MONITORPROCESSOR	0x00000002
#define DC_CR0_FPUEMULATION		0x00000004
#define DC_CR0_TASKSWITCH		0x00000008
#define DC_CR0_FPUPRESENT		0x00000010
#define DC_CR0_PAGING			0x80000000

/**
 * Descriptors
 */

#define DC_DESC_INVALID			0x00
#define DC_DESC_286_TSS_A			0x01
#define DC_DESC_LDT				0x02
#define DC_DESC_286_TSS_B			0x03
#define DC_DESC_286_CALL_GATE			0x04
#define DC_DESC_TASK_GATE			0x05
#define DC_DESC_286_INT_GATE			0x06
#define DC_DESC_286_TRAP_GATE			0x07

#define DC_DESC_386_TSS_A			0x09
#define DC_DESC_386_TSS_B			0x0b
#define DC_DESC_386_CALL_GATE			0x0c
#define DC_DESC_386_INT_GATE			0x0e
#define DC_DESC_386_TRAP_GATE			0x0f

/* EU/ED Expand UP/DOWN RO/RW Read Only/Read Write NA/A Accessed */
#define DC_DESC_DATA_EU_RO_NA			0x10
#define DC_DESC_DATA_EU_RO_A			0x11
#define DC_DESC_DATA_EU_RW_NA			0x12
#define DC_DESC_DATA_EU_RW_A			0x13
#define DC_DESC_DATA_ED_RO_NA			0x14
#define DC_DESC_DATA_ED_RO_A			0x15
#define DC_DESC_DATA_ED_RW_NA			0x16
#define DC_DESC_DATA_ED_RW_A			0x17

/* N/R Readable  NC/C Confirming A/NA Accessed */
#define DC_DESC_CODE_N_NC_A			0x18
#define DC_DESC_CODE_N_NC_NA			0x19
#define DC_DESC_CODE_R_NC_A			0x1a
#define DC_DESC_CODE_R_NC_NA			0x1b
#define DC_DESC_CODE_N_C_A			0x1c
#define DC_DESC_CODE_N_C_NA			0x1d
#define DC_DESC_CODE_R_C_A			0x1e
#define DC_DESC_CODE_R_C_NA			0x1f

struct dp_cpu_seg_descriptor {
#if (ARCH_BIT_ORDER == ARCH_BIT_ORDER_BIG)
	u32 base_0_15	:16;
	u32 limit_0_15	:16;
	u32 base_24_31	:8;
	u32 g		:1;
	u32 big	:1;
	u32 r		:1;
	u32 avl	:1;
	u32 limit_16_19 :4;
	u32 p		:1;
	u32 dpl	:2;
	u32 type	:5;
	u32 base_16_23	:8;
#else
	u32 limit_0_15	:16;
	u32 base_0_15	:16;
	u32 base_16_23	:8;
	u32 type	:5;
	u32 dpl	:2;
	u32 p		:1;
	u32 limit_16_19 :4;
	u32 avl	:1;
	u32 r		:1;
	u32 big	:1;
	u32 g		:1;
	u32 base_24_31	:8;
#endif
} __attribute__((packed));

struct dp_cpu_gate_descriptor {
#if (ARCH_BIT_ORDER == ARCH_BIT_ORDER_BIG)
	u32 selector	:16;
	u32 offset_0_15 :16;
	u32 offset_16_31:16;
	u32 p		:1;
	u32 dpl	:2;
	u32 type	:5;
	u32 reserved	:3;
	u32 paramcount	:5;
#else
	u32 offset_0_15 :16;
	u32 selector	:16;
	u32 paramcount	:5;
	u32 reserved	:3;
	u32 type	:5;
	u32 dpl	:2;
	u32 p		:1;
	u32 offset_16_31:16;
#endif
} __attribute__((packed));

union dp_cpu_descriptor {
	struct dp_cpu_seg_descriptor seg;
	struct dp_cpu_gate_descriptor gate;
	u32 values[2];
};

struct dp_cpu_descriptor_table {
	phys_addr_t base;
	u32 limit;
};

struct dp_cpu_gdt_descriptor_table {
	struct dp_cpu_descriptor_table table;
	phys_addr_t ldt_base;
	u32 ldt_limit;
	u32 ldt_value;
};

/**
 * Exception values
 */
#define DC_EXCEPTION_UD			6
#define DC_EXCEPTION_TS			10
#define DC_EXCEPTION_NP			11
#define DC_EXCEPTION_SS			12
#define DC_EXCEPTION_GP			13
#define DC_EXCEPTION_PF			14

/**
 * CPU block
 */
struct dp_cpu_block {
	u32 cpl;			/* Current Privilege */
	u32 mpl;
	u32 cr0;
	enum dp_bool pmode;			/* Is Protected mode enabled */
	struct dp_cpu_gdt_descriptor_table gdt;
	struct dp_cpu_descriptor_table idt;
	struct {
		u32 mask,notmask;
		enum dp_bool big;
	} stack;
	struct {
		enum dp_bool big;
	} code;
	struct {
		u32 cs, eip;
	} hlt;
	struct {
		u32 which, error;
	} exception;
	s32 direction;
	enum dp_bool trap_skip;
	u32 drx[8];
	u32 trx[8];
};

struct dp_cpu;

typedef u32 (*cpu_decoder_f)(struct dp_cpu *cpu);
typedef void (*cpu_instruction_hook_f)(void *hook_data);

struct dp_cpu_decoder {
	struct {
		u32 opcode_index;
		phys_addr_t cseip;
		phys_addr_t base_ds, base_ss;
		u8 base_val_ds;
		enum dp_bool rep_zero;
		u32 prefixes;
	} core;

	struct LazyFlags {
		union dp_cpu_reg var1, var2, res;
		u32 type;
		u32 prev_type;
		u32 oldcf;
	} lflags;

	cpu_decoder_f func;

	cpu_instruction_hook_f inst_hook_func;
	void *inst_hook_data;
};

#define DP_CPU_INT_SOFTWARE		0x1
#define DP_CPU_INT_EXCEPTION		0x2
#define DP_CPU_INT_HAS_ERROR		0x4
#define DP_CPU_INT_NOIOPLCHECK		0x8

enum dp_cpu_arch {
	DP_CPU_ARCHTYPE_MIXED		= 0xff,
	DP_CPU_ARCHTYPE_386SLOW	= 0x30,
	DP_CPU_ARCHTYPE_386FAST	= 0x35,
	DP_CPU_ARCHTYPE_486OLDSLOW	= 0x40,
	DP_CPU_ARCHTYPE_486NEWSLOW	= 0x45,
	DP_CPU_ARCHTYPE_PENTIUMSLOW	= 0x50,
};

/**
 * CPU state
 */
struct dp_cpu {
	struct dp_cpu_regs regs;
	struct dp_cpu_segments segs;
	struct dp_cpu_block block;
	struct dp_cpu_decoder decoder;
	u32 flag_id_toggle;
	s32 decode_count;
	enum dp_cpu_arch arch;

	char _marshal_sep[0];

	struct dp_int_callback *int_callback;
	struct dp_memory *memory;
	struct dp_io *io;
	struct dp_logging *logging;
	struct dp_pic *pic;
	struct dp_paging *paging;
	struct dp_timetrack *timetrack;
};

void dp_cpu_init(struct dp_cpu *cpu, struct dp_logging *logging,
		 struct dp_memory *memory, struct dp_io *io, struct dp_paging *paging, struct dp_pic *pic,
		 struct dp_int_callback *int_callback, struct dp_timetrack *timetrack, struct dp_marshal *marshal);
void dp_cpu_marshal(struct dp_cpu *cpu, struct dp_marshal *marshal);
void dp_cpu_unmarshal(struct dp_cpu *cpu, struct dp_marshal *marshal);

phys_addr_t dp_cpu_get_phyaddr(struct dp_cpu *cpu, u16 seg, u32 offset);

void dp_cpu_descriptor_save(struct dp_cpu *cpu, union dp_cpu_descriptor *desc, phys_addr_t addr);
void dp_cpu_descriptor_load(struct dp_cpu *cpu, union dp_cpu_descriptor *desc, phys_addr_t addr);
enum dp_bool dp_cpu_get_descriptor(struct dp_cpu *cpu, struct dp_cpu_descriptor_table *table, u32 selector,
				   union dp_cpu_descriptor *desc);
enum dp_bool dp_cpu_get_gdt_descriptor(struct dp_cpu *cpu, u32 selector, union dp_cpu_descriptor *desc);
enum dp_bool dp_cpu_set_gdt_descriptor(struct dp_cpu *cpu, u32 selector, union dp_cpu_descriptor *desc);

enum dp_bool dp_cpu_lldt(struct dp_cpu *cpu, u32 selector);
enum dp_bool dp_cpu_ltr(struct dp_cpu *cpu, u32 selector);
void dp_cpu_lidt(struct dp_cpu *cpu, u32 limit, u32 base);
void dp_cpu_lgdt(struct dp_cpu *cpu, u32 limit, u32 base);

u32 dp_cpu_str(struct dp_cpu *cpu);
u32 dp_cpu_sldt(struct dp_cpu *cpu);
u32 dp_cpu_sidt_base(struct dp_cpu *cpu);
u32 dp_cpu_sidt_limit(struct dp_cpu *cpu);
u32 dp_cpu_sgdt_base(struct dp_cpu *cpu);
u32 dp_cpu_sgdt_limit(struct dp_cpu *cpu);

void dp_cpu_arpl(struct dp_cpu *cpu, u32 *dest_sel, u32 src_sel);
void dp_cpu_lar(struct dp_cpu *cpu, u32 selector, u32 *ar);
void dp_cpu_lsl(struct dp_cpu *cpu, u32 selector, u32 *limit);

void dp_cpu_set_crx(struct dp_cpu *cpu, u32 cr, u32 value);
enum dp_bool dp_cpu_write_crx(struct dp_cpu *cpu, u32 cr, u32 value);
u32 dp_cpu_get_crx(struct dp_cpu *cpu, u32 cr);
enum dp_bool dp_cpu_read_crx(struct dp_cpu *cpu, u32 cr, u32 *retvalue);

enum dp_bool dp_cpu_write_drx(struct dp_cpu *cpu, u32 dr, u32 value);
enum dp_bool dp_cpu_read_drx(struct dp_cpu *cpu, u32 dr, u32 *retvalue);

enum dp_bool dp_cpu_write_trx(struct dp_cpu *cpu, u32 dr, u32 value);
enum dp_bool dp_cpu_read_trx(struct dp_cpu *cpu, u32 dr, u32 *retvalue);

u32 dp_cpu_smsw(struct dp_cpu *cpu);
enum dp_bool dp_cpu_lmsw(struct dp_cpu *cpu, u32 word);

void dp_cpu_verr(struct dp_cpu *cpu, u32 selector);
void dp_cpu_verw(struct dp_cpu *cpu, u32 selector);

void dp_cpu_jmp(struct dp_cpu *cpu, enum dp_bool use32, u32 selector, u32 offset, u32 oldeip);
void dp_cpu_call(struct dp_cpu *cpu, enum dp_bool use32, u32 selector, u32 offset, u32 oldeip);
void dp_cpu_ret(struct dp_cpu *cpu, enum dp_bool use32, u32 bytes, u32 oldeip);
void dp_cpu_iret(struct dp_cpu *cpu, enum dp_bool use32, u32 oldeip);
void dp_cpu_hlt(struct dp_cpu *cpu, u32 oldeip);

enum dp_bool dp_cpu_popf(struct dp_cpu *cpu, u32 use32);
enum dp_bool dp_cpu_pushf(struct dp_cpu *cpu, u32 use32);
enum dp_bool dp_cpu_cli(struct dp_cpu *cpu);
enum dp_bool dp_cpu_sti(struct dp_cpu *cpu);

enum dp_bool dp_cpu_io_exception(struct dp_cpu *cpu, u32 port, u32 size);
void dp_cpu_runexception(struct dp_cpu *cpu);

void dp_cpu_enter(struct dp_cpu *cpu, enum dp_bool use32, u32 bytes, u32 level);

void dp_cpu_interrupt(struct dp_cpu *cpu, u32 num, u32 type, u32 oldeip);

static inline void dp_cpu_hw_interrupt(struct dp_cpu *cpu, u32 num)
{
	dp_cpu_interrupt(cpu, num, 0, dcr_reg_eip);
}

static inline void dp_cpu_sw_interrupt(struct dp_cpu *cpu, u32 num, u32 oldeip)
{
	dp_cpu_interrupt(cpu, num, DP_CPU_INT_SOFTWARE, oldeip);
}

static inline void dp_cpu_sw_interrupt_noioplcheck(struct dp_cpu *cpu, u32 num, u32 oldeip)
{
	dp_cpu_interrupt(cpu, num, DP_CPU_INT_SOFTWARE | DP_CPU_INT_NOIOPLCHECK, oldeip);
}

enum dp_bool dp_cpu_prepareexception(struct dp_cpu *cpu, u32 which, u32 error);
void dp_cpu_exception(struct dp_cpu *cpu, u32 which, u32 error);

enum dp_bool dp_cpu_setseggeneral(struct dp_cpu *cpu, enum dp_segment_index seg, u32 value);
enum dp_bool dp_cpu_popseg(struct dp_cpu *cpu, enum dp_segment_index seg, enum dp_bool use32);

enum dp_bool dp_cpu_cpuid(struct dp_cpu *cpu);
u32 dp_cpu_pop16(struct dp_cpu *cpu);
u32 dp_cpu_pop32(struct dp_cpu *cpu);
void dp_cpu_push16(struct dp_cpu *cpu, u32 value);
void dp_cpu_push32(struct dp_cpu *cpu, u32 value);

void dp_cpu_setflags(struct dp_cpu *cpu, u32 word, u32 mask);

void dp_cpu_callback_szf(struct dp_cpu *cpu, enum dp_bool val);
void dp_cpu_callback_scf(struct dp_cpu *cpu, enum dp_bool val);
void dp_cpu_callback_sif(struct dp_cpu *cpu, enum dp_bool val);

#endif
