#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_CPU_DECODE
#define DP_LOGGING           (cpu->logging)

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "cpu_inlines.h"
#include "cpu_disasm.h"
#include "logging.h"
#include "memory.h"

#define LoadMb(off)		dp_memv_readb(memory, off)
#define LoadMw(off)		dp_memv_readw(memory, off)
#define LoadMd(off)		dp_memv_readd(memory, off)
#define SaveMb(off,val)	dp_memv_writeb(memory, off, val)
#define SaveMw(off,val)	dp_memv_writew(memory, off, val)
#define SaveMd(off,val)	dp_memv_writed(memory, off, val)

extern u32 cycle_count;

#if C_FPU
#define CPU_FPU	1		/* Enable FPU escape instructions */
#endif

#define CPU_PIC_CHECK 1
#define CPU_TRAP_CHECK 1

#define OPCODE_NONE			0x000
#define OPCODE_0F			0x100
#define OPCODE_SIZE			0x200

#define PREFIX_ADDR			0x1
#define PREFIX_REP			0x2

#define TEST_PREFIX_ADDR		(cpu->decoder.core.prefixes & PREFIX_ADDR)
#define TEST_PREFIX_REP		(cpu->decoder.core.prefixes & PREFIX_REP)

#define SegBase(c)			dp_cpu_seg_phys(c)
#define SegValue(c)			dp_cpu_seg_value(c)

#define Push_16(args...) dp_cpu_push16(cpu, ##args)
#define Push_32(args...) dp_cpu_push32(cpu, ##args)
#define Pop_16(args...) dp_cpu_pop16(cpu, ##args)
#define Pop_32(args...) dp_cpu_pop32(cpu, ##args)
#define CPU_PopSeg(args...) dp_cpu_popseg(cpu, ##args)
#define CPU_SetSegGeneral(args...) dp_cpu_setseggeneral(cpu, ##args)
#define CPU_Exception(args...) dp_cpu_exception(cpu, ##args)
#define CPU_CALL(args...) dp_cpu_call(cpu, ##args)
#define CPU_RET(args...) dp_cpu_ret(cpu, ##args)
#define CPU_IRET(args...) dp_cpu_iret(cpu, ##args)
#define CPU_JMP(args...) dp_cpu_jmp(cpu, ##args)
#define CPU_HLT(args...) dp_cpu_hlt(cpu, ##args)
#define CPU_POPF(args...) dp_cpu_popf(cpu, ##args)
#define CPU_PUSHF(args...) dp_cpu_pushf(cpu, ##args)
#define CPU_CLI(args...) dp_cpu_cli(cpu, ##args)
#define CPU_STI(args...) dp_cpu_sti(cpu, ##args)
#define CPU_SetFlags(args...) dp_cpu_setflags(cpu, ##args)
#define CPU_ENTER(args...) dp_cpu_enter(cpu, ##args)
#define CPU_SLDT(args...) dp_cpu_sldt(cpu, ##args)
#define CPU_STR(args...) dp_cpu_str(cpu, ##args)
#define CPU_LLDT(args...) dp_cpu_lldt(cpu, ##args)
#define CPU_LTR(args...) dp_cpu_ltr(cpu, ##args)
#define CPU_VERR(args...) dp_cpu_verr(cpu, ##args)
#define CPU_VERW(args...) dp_cpu_verw(cpu, ##args)
#define CPU_LGDT(args...) dp_cpu_lgdt(cpu, ##args)
#define CPU_LIDT(args...) dp_cpu_lidt(cpu, ##args)
#define CPU_SMSW(args...) dp_cpu_smsw(cpu, ##args)
#define CPU_LMSW(args...) dp_cpu_lmsw(cpu, ##args)
#define CPU_LAR(args...) dp_cpu_lar(cpu, ##args)
#define CPU_LSL(args...) dp_cpu_lsl(cpu, ##args)
#define CPU_READ_CRX(args...) dp_cpu_read_crx(cpu, ##args)
#define CPU_READ_DRX(args...) dp_cpu_read_drx(cpu, ##args)
#define CPU_WRITE_CRX(args...) dp_cpu_write_crx(cpu, ##args)
#define CPU_WRITE_DRX(args...) dp_cpu_write_drx(cpu, ##args)
#define CPU_READ_TRX(args...) dp_cpu_read_trx(cpu, ##args)
#define CPU_WRITE_TRX(args...) dp_cpu_write_trx(cpu, ##args)
#define CPU_IO_Exception(args...) dp_cpu_io_exception(cpu, ##args)
#define CPU_HW_Interrupt(args...) dp_cpu_hw_interrupt(cpu, ##args)
#define CPU_CPUID(args...) dp_cpu_cpuid(cpu, ##args)
#define CPU_SGDT_limit(args...) dp_cpu_sgdt_limit(cpu, ##args)
#define CPU_SGDT_base(args...) dp_cpu_sgdt_base(cpu, ##args)
#define CPU_SIDT_limit(args...) dp_cpu_sidt_limit(cpu, ##args)
#define CPU_SIDT_base(args...) dp_cpu_sidt_base(cpu, ##args)
#define CPU_ARPL(args...) dp_cpu_arpl(cpu, ##args)

#define E_Exit(args...) DP_FAT(args)

#define CPU_SW_Interrupt_NoIOPLCheck(args...) dp_cpu_sw_interrupt_noioplcheck(cpu, ##args)
#define CPU_SW_Interrupt(args...) dp_cpu_sw_interrupt(cpu, ##args)

#define EXCEPTION(blah)				\
	{						\
		CPU_Exception(blah, 0);			\
		continue;				\
	}

#define DO_PREFIX_SEG(_SEG)					\
	BaseDS=SegBase(_SEG);					\
	BaseSS=SegBase(_SEG);					\
	cpu->decoder.core.base_val_ds=_SEG;					\
	goto restart_opcode;

#define DO_PREFIX_ADDR()								\
	cpu->decoder.core.prefixes=(cpu->decoder.core.prefixes & ~PREFIX_ADDR) |		\
	(cpu->block.code.big ^ PREFIX_ADDR);						\
	ea_table=&EATable[(cpu->decoder.core.prefixes&1) * 256];		\
	goto restart_opcode;

#define DO_PREFIX_REP(_ZERO)				\
	cpu->decoder.core.prefixes|=PREFIX_REP;				\
	cpu->decoder.core.rep_zero=_ZERO;					\
	goto restart_opcode;

static const u32 AddrMaskTable[2] = { 0x0000ffff, 0xffffffff };

/* Flag Handling */

#define lf_var1b cpu->decoder.lflags.var1.byte[DCR_BL_INDEX]
#define lf_var2b cpu->decoder.lflags.var2.byte[DCR_BL_INDEX]
#define lf_resb cpu->decoder.lflags.res.byte[DCR_BL_INDEX]

#define lf_var1w cpu->decoder.lflags.var1.word[DCR_W_INDEX]
#define lf_var2w cpu->decoder.lflags.var2.word[DCR_W_INDEX]
#define lf_resw cpu->decoder.lflags.res.word[DCR_W_INDEX]

#define lf_var1d cpu->decoder.lflags.var1.dword[DCR_DW_INDEX]
#define lf_var2d cpu->decoder.lflags.var2.dword[DCR_DW_INDEX]
#define lf_resd cpu->decoder.lflags.res.dword[DCR_DW_INDEX]

#define SETFLAGSb(FLAGB)						\
	{								\
		DC_SET_FLAGBIT(OF,get_OF(cpu));			\
		cpu->decoder.lflags.type=t_UNKNOWN;			\
		CPU_SetFlags(FLAGB,DC_FMASK_NORMAL & 0xff);		\
	}

#define SETFLAGSw(FLAGW)						\
	{								\
		cpu->decoder.lflags.type=t_UNKNOWN;			\
		CPU_SetFlagsw(FLAGW);					\
	}

#define SETFLAGSd(FLAGD)						\
	{								\
		cpu->decoder.lflags.type=t_UNKNOWN;			\
		CPU_SetFlagsd(FLAGD);					\
	}

#define LoadCF DC_SET_FLAGBIT(CF,get_CF(cpu));
#define LoadZF DC_SET_FLAGBIT(ZF,get_ZF(cpu));
#define LoadSF DC_SET_FLAGBIT(SF,get_SF(cpu));
#define LoadOF DC_SET_FLAGBIT(OF,get_OF(cpu));
#define LoadAF DC_SET_FLAGBIT(AF,get_AF(cpu));

#define TFLG_O		(get_OF(cpu))
#define TFLG_NO		(!get_OF(cpu))
#define TFLG_B		(get_CF(cpu))
#define TFLG_NB		(!get_CF(cpu))
#define TFLG_Z		(get_ZF(cpu))
#define TFLG_NZ		(!get_ZF(cpu))
#define TFLG_BE		(get_CF(cpu) || get_ZF(cpu))
#define TFLG_NBE	(!get_CF(cpu) && !get_ZF(cpu))
#define TFLG_S		(get_SF(cpu))
#define TFLG_NS		(!get_SF(cpu))
#define TFLG_P		(get_PF(cpu))
#define TFLG_NP		(!get_PF(cpu))
#define TFLG_L		((get_SF(cpu)!=0) != (get_OF(cpu)!=0))
#define TFLG_NL		((get_SF(cpu)!=0) == (get_OF(cpu)!=0))
#define TFLG_LE		(get_ZF(cpu)  || ((get_SF(cpu)!=0) != (get_OF(cpu)!=0)))
#define TFLG_NLE	(!get_ZF(cpu) && ((get_SF(cpu)!=0) == (get_OF(cpu)!=0)))

//Types of Flag changing instructions
enum {
	t_UNKNOWN = 0,
	t_ADDb, t_ADDw, t_ADDd,
	t_ORb, t_ORw, t_ORd,
	t_ADCb, t_ADCw, t_ADCd,
	t_SBBb, t_SBBw, t_SBBd,
	t_ANDb, t_ANDw, t_ANDd,
	t_SUBb, t_SUBw, t_SUBd,
	t_XORb, t_XORw, t_XORd,
	t_CMPb, t_CMPw, t_CMPd,
	t_INCb, t_INCw, t_INCd,
	t_DECb, t_DECw, t_DECd,
	t_TESTb, t_TESTw, t_TESTd,
	t_SHLb, t_SHLw, t_SHLd,
	t_SHRb, t_SHRw, t_SHRd,
	t_SARb, t_SARw, t_SARd,
	t_ROLb, t_ROLw, t_ROLd,
	t_RORb, t_RORw, t_RORd,
	t_RCLb, t_RCLw, t_RCLd,
	t_RCRb, t_RCRw, t_RCRd,
	t_NEGb, t_NEGw, t_NEGd,

	t_DSHLw, t_DSHLd,
	t_DSHRw, t_DSHRd,
	t_MUL, t_DIV,
	t_NOTDONE,
	t_LASTFLAG
};

static u32 get_CF(struct dp_cpu *cpu)
{
	switch (cpu->decoder.lflags.type) {
	case t_UNKNOWN:
	case t_INCb:
	case t_INCw:
	case t_INCd:
	case t_DECb:
	case t_DECw:
	case t_DECd:
	case t_MUL:
		return DC_GET_FLAG(CF);
	case t_ADDb:
		return (lf_resb < lf_var1b);
	case t_ADDw:
		return (lf_resw < lf_var1w);
	case t_ADDd:
		return (lf_resd < lf_var1d);
	case t_ADCb:
		return (lf_resb < lf_var1b) || (cpu->decoder.lflags.oldcf && (lf_resb == lf_var1b));
	case t_ADCw:
		return (lf_resw < lf_var1w) || (cpu->decoder.lflags.oldcf && (lf_resw == lf_var1w));
	case t_ADCd:
		return (lf_resd < lf_var1d) || (cpu->decoder.lflags.oldcf && (lf_resd == lf_var1d));
	case t_SBBb:
		return (lf_var1b < lf_resb) || (cpu->decoder.lflags.oldcf && (lf_var2b == 0xff));
	case t_SBBw:
		return (lf_var1w < lf_resw) || (cpu->decoder.lflags.oldcf && (lf_var2w == 0xffff));
	case t_SBBd:
		return (lf_var1d < lf_resd) || (cpu->decoder.lflags.oldcf && (lf_var2d == 0xffffffff));
	case t_SUBb:
	case t_CMPb:
		return (lf_var1b < lf_var2b);
	case t_SUBw:
	case t_CMPw:
		return (lf_var1w < lf_var2w);
	case t_SUBd:
	case t_CMPd:
		return (lf_var1d < lf_var2d);
	case t_SHLb:
		if (lf_var2b > 8)
			return DP_FALSE;
		else
			return (lf_var1b >> (8 - lf_var2b)) & 1;
	case t_SHLw:
		if (lf_var2b > 16)
			return DP_FALSE;
		else
			return (lf_var1w >> (16 - lf_var2b)) & 1;
	case t_SHLd:
	case t_DSHLw:		/* Hmm this is not correct for shift higher than 16 */
	case t_DSHLd:
		return (lf_var1d >> (32 - lf_var2b)) & 1;
	case t_RCRb:
	case t_SHRb:
		return (lf_var1b >> (lf_var2b - 1)) & 1;
	case t_RCRw:
	case t_SHRw:
		return (lf_var1w >> (lf_var2b - 1)) & 1;
	case t_RCRd:
	case t_SHRd:
	case t_DSHRw:		/* Hmm this is not correct for shift higher than 16 */
	case t_DSHRd:
		return (lf_var1d >> (lf_var2b - 1)) & 1;
	case t_SARb:
		return (((s8) lf_var1b) >> (lf_var2b - 1)) & 1;
	case t_SARw:
		return (((s16) lf_var1w) >> (lf_var2b - 1)) & 1;
	case t_SARd:
		return (((s32) lf_var1d) >> (lf_var2b - 1)) & 1;
	case t_NEGb:
		return lf_var1b;
	case t_NEGw:
		return lf_var1w;
	case t_NEGd:
		return lf_var1d;
	case t_ORb:
	case t_ORw:
	case t_ORd:
	case t_ANDb:
	case t_ANDw:
	case t_ANDd:
	case t_XORb:
	case t_XORw:
	case t_XORd:
	case t_TESTb:
	case t_TESTw:
	case t_TESTd:
		return DP_FALSE;	/* Set to DP_FALSE */
	case t_DIV:
		return DP_FALSE;	/* Unkown */
	default:
		DP_ERR("get_CF Unknown %d", cpu->decoder.lflags.type);
	}
	return 0;
}

/* AF     Adjust flag -- Set on carry from or borrow to the low order
            four bits of   AL; cleared otherwise. Used for decimal
            arithmetic.
*/
static u32 get_AF(struct dp_cpu *cpu)
{
	u32 type = cpu->decoder.lflags.type;
	switch (type) {
	case t_UNKNOWN:
		return DC_GET_FLAG(AF);
	case t_ADDb:
	case t_ADCb:
	case t_SBBb:
	case t_SUBb:
	case t_CMPb:
		return ((lf_var1b ^ lf_var2b) ^ lf_resb) & 0x10;
	case t_ADDw:
	case t_ADCw:
	case t_SBBw:
	case t_SUBw:
	case t_CMPw:
		return ((lf_var1w ^ lf_var2w) ^ lf_resw) & 0x10;
	case t_ADCd:
	case t_ADDd:
	case t_SBBd:
	case t_SUBd:
	case t_CMPd:
		return ((lf_var1d ^ lf_var2d) ^ lf_resd) & 0x10;
	case t_INCb:
		return (lf_resb & 0x0f) == 0;
	case t_INCw:
		return (lf_resw & 0x0f) == 0;
	case t_INCd:
		return (lf_resd & 0x0f) == 0;
	case t_DECb:
		return (lf_resb & 0x0f) == 0x0f;
	case t_DECw:
		return (lf_resw & 0x0f) == 0x0f;
	case t_DECd:
		return (lf_resd & 0x0f) == 0x0f;
	case t_NEGb:
		return lf_var1b & 0x0f;
	case t_NEGw:
		return lf_var1w & 0x0f;
	case t_NEGd:
		return lf_var1d & 0x0f;
	case t_SHLb:
	case t_SHRb:
	case t_SARb:
		return lf_var2b & 0x1f;
	case t_SHLw:
	case t_SHRw:
	case t_SARw:
		return lf_var2w & 0x1f;
	case t_SHLd:
	case t_SHRd:
	case t_SARd:
		return lf_var2d & 0x1f;
	case t_ORb:
	case t_ORw:
	case t_ORd:
	case t_ANDb:
	case t_ANDw:
	case t_ANDd:
	case t_XORb:
	case t_XORw:
	case t_XORd:
	case t_TESTb:
	case t_TESTw:
	case t_TESTd:
	case t_DSHLw:
	case t_DSHLd:
	case t_DSHRw:
	case t_DSHRd:
	case t_DIV:
	case t_MUL:
		return DP_FALSE;	/* Unkown */
	default:
		DP_ERR("get_AF Unknown %d", cpu->decoder.lflags.type);
	}
	return 0;
}

/* ZF     Zero Flag -- Set if result is zero; cleared otherwise.
*/

static u32 get_ZF(struct dp_cpu *cpu)
{
	u32 type = cpu->decoder.lflags.type;
	switch (type) {
	case t_UNKNOWN:
		return DC_GET_FLAG(ZF);
	case t_ADDb:
	case t_ORb:
	case t_ADCb:
	case t_SBBb:
	case t_ANDb:
	case t_XORb:
	case t_SUBb:
	case t_CMPb:
	case t_INCb:
	case t_DECb:
	case t_TESTb:
	case t_SHLb:
	case t_SHRb:
	case t_SARb:
	case t_NEGb:
		return (lf_resb == 0);
	case t_ADDw:
	case t_ORw:
	case t_ADCw:
	case t_SBBw:
	case t_ANDw:
	case t_XORw:
	case t_SUBw:
	case t_CMPw:
	case t_INCw:
	case t_DECw:
	case t_TESTw:
	case t_SHLw:
	case t_SHRw:
	case t_SARw:
	case t_DSHLw:
	case t_DSHRw:
	case t_NEGw:
		return (lf_resw == 0);
	case t_ADDd:
	case t_ORd:
	case t_ADCd:
	case t_SBBd:
	case t_ANDd:
	case t_XORd:
	case t_SUBd:
	case t_CMPd:
	case t_INCd:
	case t_DECd:
	case t_TESTd:
	case t_SHLd:
	case t_SHRd:
	case t_SARd:
	case t_DSHLd:
	case t_DSHRd:
	case t_NEGd:
		return (lf_resd == 0);
	case t_DIV:
	case t_MUL:
		return DP_FALSE;	/* Unkown */
	default:
		DP_ERR("get_ZF Unknown %d", cpu->decoder.lflags.type);
	}
	return DP_FALSE;
}

/* SF     Sign Flag -- Set equal to high-order bit of result (0 is
            positive, 1 if negative).
*/
static u32 get_SF(struct dp_cpu *cpu)
{
	u32 type = cpu->decoder.lflags.type;
	switch (type) {
	case t_UNKNOWN:
		return DC_GET_FLAG(SF);
	case t_ADDb:
	case t_ORb:
	case t_ADCb:
	case t_SBBb:
	case t_ANDb:
	case t_XORb:
	case t_SUBb:
	case t_CMPb:
	case t_INCb:
	case t_DECb:
	case t_TESTb:
	case t_SHLb:
	case t_SHRb:
	case t_SARb:
	case t_NEGb:
		return (lf_resb & 0x80);
	case t_ADDw:
	case t_ORw:
	case t_ADCw:
	case t_SBBw:
	case t_ANDw:
	case t_XORw:
	case t_SUBw:
	case t_CMPw:
	case t_INCw:
	case t_DECw:
	case t_TESTw:
	case t_SHLw:
	case t_SHRw:
	case t_SARw:
	case t_DSHLw:
	case t_DSHRw:
	case t_NEGw:
		return (lf_resw & 0x8000);
	case t_ADDd:
	case t_ORd:
	case t_ADCd:
	case t_SBBd:
	case t_ANDd:
	case t_XORd:
	case t_SUBd:
	case t_CMPd:
	case t_INCd:
	case t_DECd:
	case t_TESTd:
	case t_SHLd:
	case t_SHRd:
	case t_SARd:
	case t_DSHLd:
	case t_DSHRd:
	case t_NEGd:
		return (lf_resd & 0x80000000);
	case t_DIV:
	case t_MUL:
		return DP_FALSE;	/* Unkown */
	default:
		DP_ERR("get_SF Unkown %d", cpu->decoder.lflags.type);
	}
	return DP_FALSE;

}

static u32 get_OF(struct dp_cpu *cpu)
{
	u32 type = cpu->decoder.lflags.type;
	switch (type) {
	case t_UNKNOWN:
	case t_MUL:
		return DC_GET_FLAG(OF);
	case t_ADDb:
	case t_ADCb:
		return ((lf_var1b ^ lf_var2b ^ 0x80) & (lf_resb ^ lf_var2b)) & 0x80;
	case t_ADDw:
	case t_ADCw:
		return ((lf_var1w ^ lf_var2w ^ 0x8000) & (lf_resw ^ lf_var2w)) & 0x8000;
	case t_ADDd:
	case t_ADCd:
		return ((lf_var1d ^ lf_var2d ^ 0x80000000) & (lf_resd ^ lf_var2d)) & 0x80000000;
	case t_SBBb:
	case t_SUBb:
	case t_CMPb:
		return ((lf_var1b ^ lf_var2b) & (lf_var1b ^ lf_resb)) & 0x80;
	case t_SBBw:
	case t_SUBw:
	case t_CMPw:
		return ((lf_var1w ^ lf_var2w) & (lf_var1w ^ lf_resw)) & 0x8000;
	case t_SBBd:
	case t_SUBd:
	case t_CMPd:
		return ((lf_var1d ^ lf_var2d) & (lf_var1d ^ lf_resd)) & 0x80000000;
	case t_INCb:
		return (lf_resb == 0x80);
	case t_INCw:
		return (lf_resw == 0x8000);
	case t_INCd:
		return (lf_resd == 0x80000000);
	case t_DECb:
		return (lf_resb == 0x7f);
	case t_DECw:
		return (lf_resw == 0x7fff);
	case t_DECd:
		return (lf_resd == 0x7fffffff);
	case t_NEGb:
		return (lf_var1b == 0x80);
	case t_NEGw:
		return (lf_var1w == 0x8000);
	case t_NEGd:
		return (lf_var1d == 0x80000000);
	case t_SHLb:
		return (lf_resb ^ lf_var1b) & 0x80;
	case t_SHLw:
	case t_DSHRw:
	case t_DSHLw:
		return (lf_resw ^ lf_var1w) & 0x8000;
	case t_SHLd:
	case t_DSHRd:
	case t_DSHLd:
		return (lf_resd ^ lf_var1d) & 0x80000000;
	case t_SHRb:
		if ((lf_var2b & 0x1f) == 1)
			return (lf_var1b > 0x80);
		else
			return DP_FALSE;
	case t_SHRw:
		if ((lf_var2b & 0x1f) == 1)
			return (lf_var1w > 0x8000);
		else
			return DP_FALSE;
	case t_SHRd:
		if ((lf_var2b & 0x1f) == 1)
			return (lf_var1d > 0x80000000);
		else
			return DP_FALSE;
	case t_ORb:
	case t_ORw:
	case t_ORd:
	case t_ANDb:
	case t_ANDw:
	case t_ANDd:
	case t_XORb:
	case t_XORw:
	case t_XORd:
	case t_TESTb:
	case t_TESTw:
	case t_TESTd:
	case t_SARb:
	case t_SARw:
	case t_SARd:
		return DP_FALSE;	/* Return DP_FALSE */
	case t_DIV:
		return DP_FALSE;	/* Unkown */
	default:
		DP_ERR("get_OF Unkown %d", cpu->decoder.lflags.type);
	}
	return DP_FALSE;
}

static u16 parity_lookup[256] = {
	DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0,
	    DC_FLAG_PF,
	0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF,
	    DC_FLAG_PF, 0,
	0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF,
	    DC_FLAG_PF, 0,
	DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0,
	    DC_FLAG_PF,
	0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF,
	    DC_FLAG_PF, 0,
	DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0,
	    DC_FLAG_PF,
	DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0,
	    DC_FLAG_PF,
	0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF,
	    DC_FLAG_PF, 0,
	0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF,
	    DC_FLAG_PF, 0,
	DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0,
	    DC_FLAG_PF,
	DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0,
	    DC_FLAG_PF,
	0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF,
	    DC_FLAG_PF, 0,
	DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0,
	    DC_FLAG_PF,
	0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF,
	    DC_FLAG_PF, 0,
	0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF,
	    DC_FLAG_PF, 0,
	DC_FLAG_PF, 0, 0, DC_FLAG_PF, 0, DC_FLAG_PF, DC_FLAG_PF, 0, 0, DC_FLAG_PF, DC_FLAG_PF, 0, DC_FLAG_PF, 0, 0,
	    DC_FLAG_PF
};

static u32 get_PF(struct dp_cpu *cpu)
{
	switch (cpu->decoder.lflags.type) {
	case t_UNKNOWN:
		return DC_GET_FLAG(PF);
	default:
		return (parity_lookup[lf_resb]);
	};
	return 0;
}

#define DOFLAG_PF	dcr_reg_flags=(dcr_reg_flags & ~DC_FLAG_PF) | parity_lookup[lf_resb];

#define DOFLAG_AF	dcr_reg_flags=(dcr_reg_flags & ~DC_FLAG_AF) | (((lf_var1b ^ lf_var2b) ^ lf_resb) & 0x10);

#define DOFLAG_ZFb	DC_SET_FLAGBIT(ZF,lf_resb==0);
#define DOFLAG_ZFw	DC_SET_FLAGBIT(ZF,lf_resw==0);
#define DOFLAG_ZFd	DC_SET_FLAGBIT(ZF,lf_resd==0);

#define DOFLAG_SFb	dcr_reg_flags=(dcr_reg_flags & ~DC_FLAG_SF) | ((lf_resb & 0x80) >> 0);
#define DOFLAG_SFw	dcr_reg_flags=(dcr_reg_flags & ~DC_FLAG_SF) | ((lf_resw & 0x8000) >> 8);
#define DOFLAG_SFd	dcr_reg_flags=(dcr_reg_flags & ~DC_FLAG_SF) | ((lf_resd & 0x80000000) >> 24);

#define SETCF(NEWBIT)   dcr_reg_flags=(dcr_reg_flags & ~DC_FLAG_CF)|(NEWBIT);
#define FillFlags       dp_decode_fill_flags

u32 FillFlags(struct dp_cpu *cpu)
{
	switch (cpu->decoder.lflags.type) {
	case t_UNKNOWN:
		break;
	case t_ADDb:
		DC_SET_FLAGBIT(CF, (lf_resb < lf_var1b));
		DOFLAG_AF;
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, ((lf_var1b ^ lf_var2b ^ 0x80) & (lf_resb ^ lf_var1b)) & 0x80);
		DOFLAG_PF;
		break;
	case t_ADDw:
		DC_SET_FLAGBIT(CF, (lf_resw < lf_var1w));
		DOFLAG_AF;
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, ((lf_var1w ^ lf_var2w ^ 0x8000) & (lf_resw ^ lf_var1w)) & 0x8000);
		DOFLAG_PF;
		break;
	case t_ADDd:
		DC_SET_FLAGBIT(CF, (lf_resd < lf_var1d));
		DOFLAG_AF;
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, ((lf_var1d ^ lf_var2d ^ 0x80000000) & (lf_resd ^ lf_var1d)) & 0x80000000);
		DOFLAG_PF;
		break;
	case t_ADCb:
		DC_SET_FLAGBIT(CF, (lf_resb < lf_var1b) || (cpu->decoder.lflags.oldcf && (lf_resb == lf_var1b)));
		DOFLAG_AF;
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, ((lf_var1b ^ lf_var2b ^ 0x80) & (lf_resb ^ lf_var1b)) & 0x80);
		DOFLAG_PF;
		break;
	case t_ADCw:
		DC_SET_FLAGBIT(CF, (lf_resw < lf_var1w) || (cpu->decoder.lflags.oldcf && (lf_resw == lf_var1w)));
		DOFLAG_AF;
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, ((lf_var1w ^ lf_var2w ^ 0x8000) & (lf_resw ^ lf_var1w)) & 0x8000);
		DOFLAG_PF;
		break;
	case t_ADCd:
		DC_SET_FLAGBIT(CF, (lf_resd < lf_var1d) || (cpu->decoder.lflags.oldcf && (lf_resd == lf_var1d)));
		DOFLAG_AF;
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, ((lf_var1d ^ lf_var2d ^ 0x80000000) & (lf_resd ^ lf_var1d)) & 0x80000000);
		DOFLAG_PF;
		break;

	case t_SBBb:
		DC_SET_FLAGBIT(CF, (lf_var1b < lf_resb) || (cpu->decoder.lflags.oldcf && (lf_var2b == 0xff)));
		DOFLAG_AF;
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, (lf_var1b ^ lf_var2b) & (lf_var1b ^ lf_resb) & 0x80);
		DOFLAG_PF;
		break;
	case t_SBBw:
		DC_SET_FLAGBIT(CF, (lf_var1w < lf_resw) || (cpu->decoder.lflags.oldcf && (lf_var2w == 0xffff)));
		DOFLAG_AF;
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, (lf_var1w ^ lf_var2w) & (lf_var1w ^ lf_resw) & 0x8000);
		DOFLAG_PF;
		break;
	case t_SBBd:
		DC_SET_FLAGBIT(CF, (lf_var1d < lf_resd) || (cpu->decoder.lflags.oldcf && (lf_var2d == 0xffffffff)));
		DOFLAG_AF;
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, (lf_var1d ^ lf_var2d) & (lf_var1d ^ lf_resd) & 0x80000000);
		DOFLAG_PF;
		break;

	case t_SUBb:
	case t_CMPb:
		DC_SET_FLAGBIT(CF, (lf_var1b < lf_var2b));
		DOFLAG_AF;
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, (lf_var1b ^ lf_var2b) & (lf_var1b ^ lf_resb) & 0x80);
		DOFLAG_PF;
		break;
	case t_SUBw:
	case t_CMPw:
		DC_SET_FLAGBIT(CF, (lf_var1w < lf_var2w));
		DOFLAG_AF;
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, (lf_var1w ^ lf_var2w) & (lf_var1w ^ lf_resw) & 0x8000);
		DOFLAG_PF;
		break;
	case t_SUBd:
	case t_CMPd:
		DC_SET_FLAGBIT(CF, (lf_var1d < lf_var2d));
		DOFLAG_AF;
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, (lf_var1d ^ lf_var2d) & (lf_var1d ^ lf_resd) & 0x80000000);
		DOFLAG_PF;
		break;

	case t_ORb:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;
	case t_ORw:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;
	case t_ORd:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;

	case t_TESTb:
	case t_ANDb:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;
	case t_TESTw:
	case t_ANDw:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;
	case t_TESTd:
	case t_ANDd:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;

	case t_XORb:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;
	case t_XORw:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;
	case t_XORd:
		DC_SET_FLAGBIT(CF, DP_FALSE);
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		break;

	case t_SHLb:
		if (lf_var2b > 8)
			DC_SET_FLAGBIT(CF, DP_FALSE);
		else
			DC_SET_FLAGBIT(CF, (lf_var1b >> (8 - lf_var2b)) & 1);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, (lf_resb ^ lf_var1b) & 0x80);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2b & 0x1f));
		break;
	case t_SHLw:
		if (lf_var2b > 16)
			DC_SET_FLAGBIT(CF, DP_FALSE);
		else
			DC_SET_FLAGBIT(CF, (lf_var1w >> (16 - lf_var2b)) & 1);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, (lf_resw ^ lf_var1w) & 0x8000);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2w & 0x1f));
		break;
	case t_SHLd:
		DC_SET_FLAGBIT(CF, (lf_var1d >> (32 - lf_var2b)) & 1);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, (lf_resd ^ lf_var1d) & 0x80000000);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2d & 0x1f));
		break;

	case t_DSHLw:
		DC_SET_FLAGBIT(CF, (lf_var1d >> (32 - lf_var2b)) & 1);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, (lf_resw ^ lf_var1w) & 0x8000);
		DOFLAG_PF;
		break;
	case t_DSHLd:
		DC_SET_FLAGBIT(CF, (lf_var1d >> (32 - lf_var2b)) & 1);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, (lf_resd ^ lf_var1d) & 0x80000000);
		DOFLAG_PF;
		break;

	case t_SHRb:
		DC_SET_FLAGBIT(CF, (lf_var1b >> (lf_var2b - 1)) & 1);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		if ((lf_var2b & 0x1f) == 1)
			DC_SET_FLAGBIT(OF, (lf_var1b >= 0x80));
		else
			DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2b & 0x1f));
		break;
	case t_SHRw:
		DC_SET_FLAGBIT(CF, (lf_var1w >> (lf_var2b - 1)) & 1);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		if ((lf_var2w & 0x1f) == 1)
			DC_SET_FLAGBIT(OF, (lf_var1w >= 0x8000));
		else
			DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2w & 0x1f));
		break;
	case t_SHRd:
		DC_SET_FLAGBIT(CF, (lf_var1d >> (lf_var2b - 1)) & 1);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		if ((lf_var2d & 0x1f) == 1)
			DC_SET_FLAGBIT(OF, (lf_var1d >= 0x80000000));
		else
			DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2d & 0x1f));
		break;

	case t_DSHRw:		/* Hmm this is not correct for shift higher than 16 */
		DC_SET_FLAGBIT(CF, (lf_var1d >> (lf_var2b - 1)) & 1);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, (lf_resw ^ lf_var1w) & 0x8000);
		DOFLAG_PF;
		break;
	case t_DSHRd:
		DC_SET_FLAGBIT(CF, (lf_var1d >> (lf_var2b - 1)) & 1);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, (lf_resd ^ lf_var1d) & 0x80000000);
		DOFLAG_PF;
		break;

	case t_SARb:
		DC_SET_FLAGBIT(CF, (((s8) lf_var1b) >> (lf_var2b - 1)) & 1);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2b & 0x1f));
		break;
	case t_SARw:
		DC_SET_FLAGBIT(CF, (((s16) lf_var1w) >> (lf_var2b - 1)) & 1);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2w & 0x1f));
		break;
	case t_SARd:
		DC_SET_FLAGBIT(CF, (((s32) lf_var1d) >> (lf_var2b - 1)) & 1);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, DP_FALSE);
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2d & 0x1f));
		break;

	case t_INCb:
		DC_SET_FLAGBIT(AF, (lf_resb & 0x0f) == 0);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, (lf_resb == 0x80));
		DOFLAG_PF;
		break;
	case t_INCw:
		DC_SET_FLAGBIT(AF, (lf_resw & 0x0f) == 0);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, (lf_resw == 0x8000));
		DOFLAG_PF;
		break;
	case t_INCd:
		DC_SET_FLAGBIT(AF, (lf_resd & 0x0f) == 0);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, (lf_resd == 0x80000000));
		DOFLAG_PF;
		break;

	case t_DECb:
		DC_SET_FLAGBIT(AF, (lf_resb & 0x0f) == 0x0f);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, (lf_resb == 0x7f));
		DOFLAG_PF;
		break;
	case t_DECw:
		DC_SET_FLAGBIT(AF, (lf_resw & 0x0f) == 0x0f);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, (lf_resw == 0x7fff));
		DOFLAG_PF;
		break;
	case t_DECd:
		DC_SET_FLAGBIT(AF, (lf_resd & 0x0f) == 0x0f);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, (lf_resd == 0x7fffffff));
		DOFLAG_PF;
		break;

	case t_NEGb:
		DC_SET_FLAGBIT(CF, (lf_var1b != 0));
		DC_SET_FLAGBIT(AF, (lf_resb & 0x0f) != 0);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DC_SET_FLAGBIT(OF, (lf_var1b == 0x80));
		DOFLAG_PF;
		break;
	case t_NEGw:
		DC_SET_FLAGBIT(CF, (lf_var1w != 0));
		DC_SET_FLAGBIT(AF, (lf_resw & 0x0f) != 0);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DC_SET_FLAGBIT(OF, (lf_var1w == 0x8000));
		DOFLAG_PF;
		break;
	case t_NEGd:
		DC_SET_FLAGBIT(CF, (lf_var1d != 0));
		DC_SET_FLAGBIT(AF, (lf_resd & 0x0f) != 0);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DC_SET_FLAGBIT(OF, (lf_var1d == 0x80000000));
		DOFLAG_PF;
		break;

	case t_DIV:
	case t_MUL:
		break;

	default:
		DP_ERR("Unhandled flag type %d", cpu->decoder.lflags.type);
		return 0;
	}
	cpu->decoder.lflags.type = t_UNKNOWN;
	return dcr_reg_flags;
}

static void FillFlagsNoCFOF(struct dp_cpu *cpu)
{
	switch (cpu->decoder.lflags.type) {
	case t_UNKNOWN:
		return;
	case t_ADDb:
		DOFLAG_AF;
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_ADDw:
		DOFLAG_AF;
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_ADDd:
		DOFLAG_AF;
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;
	case t_ADCb:
		DOFLAG_AF;
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_ADCw:
		DOFLAG_AF;
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_ADCd:
		DOFLAG_AF;
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_SBBb:
		DOFLAG_AF;
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_SBBw:
		DOFLAG_AF;
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_SBBd:
		DOFLAG_AF;
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_SUBb:
	case t_CMPb:
		DOFLAG_AF;
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_SUBw:
	case t_CMPw:
		DOFLAG_AF;
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_SUBd:
	case t_CMPd:
		DOFLAG_AF;
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_ORb:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_ORw:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_ORd:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_TESTb:
	case t_ANDb:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_TESTw:
	case t_ANDw:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_TESTd:
	case t_ANDd:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_XORb:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_XORw:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_XORd:
		DC_SET_FLAGBIT(AF, DP_FALSE);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_SHLb:
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2b & 0x1f));
		break;
	case t_SHLw:
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2w & 0x1f));
		break;
	case t_SHLd:
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2d & 0x1f));
		break;

	case t_DSHLw:
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_DSHLd:
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_SHRb:
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2b & 0x1f));
		break;
	case t_SHRw:
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2w & 0x1f));
		break;
	case t_SHRd:
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2d & 0x1f));
		break;

	case t_DSHRw:		/* Hmm this is not correct for shift higher than 16 */
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_DSHRd:
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_SARb:
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2b & 0x1f));
		break;
	case t_SARw:
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2w & 0x1f));
		break;
	case t_SARd:
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		DC_SET_FLAGBIT(AF, (lf_var2d & 0x1f));
		break;

	case t_INCb:
		DC_SET_FLAGBIT(AF, (lf_resb & 0x0f) == 0);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_INCw:
		DC_SET_FLAGBIT(AF, (lf_resw & 0x0f) == 0);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_INCd:
		DC_SET_FLAGBIT(AF, (lf_resd & 0x0f) == 0);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_DECb:
		DC_SET_FLAGBIT(AF, (lf_resb & 0x0f) == 0x0f);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_DECw:
		DC_SET_FLAGBIT(AF, (lf_resw & 0x0f) == 0x0f);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_DECd:
		DC_SET_FLAGBIT(AF, (lf_resd & 0x0f) == 0x0f);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_NEGb:
		DC_SET_FLAGBIT(AF, (lf_resb & 0x0f) != 0);
		DOFLAG_ZFb;
		DOFLAG_SFb;
		DOFLAG_PF;
		break;
	case t_NEGw:
		DC_SET_FLAGBIT(AF, (lf_resw & 0x0f) != 0);
		DOFLAG_ZFw;
		DOFLAG_SFw;
		DOFLAG_PF;
		break;
	case t_NEGd:
		DC_SET_FLAGBIT(AF, (lf_resd & 0x0f) != 0);
		DOFLAG_ZFd;
		DOFLAG_SFd;
		DOFLAG_PF;
		break;

	case t_DIV:
	case t_MUL:
		break;

	default:
		DP_ERR("Unhandled flag type %d", cpu->decoder.lflags.type);
		break;
	}
	cpu->decoder.lflags.type = t_UNKNOWN;
}

#define DestroyConditionFlags dp_destroy_condition_flags

void DestroyConditionFlags(struct dp_cpu *cpu)
{
	cpu->decoder.lflags.type = t_UNKNOWN;
}

#define GETIP		(cpu->decoder.core.cseip-SegBase(dp_seg_cs))
#define SAVEIP		dcr_reg_eip = GETIP;
#define LOADIP		cpu->decoder.core.cseip=(SegBase(dp_seg_cs)+dcr_reg_eip);

#define BaseDS		cpu->decoder.core.base_ds
#define BaseSS		cpu->decoder.core.base_ss

static inline u8 Fetchb(struct dp_cpu *cpu)
{
	struct dp_memory *memory = cpu->memory;

	u8 temp = LoadMb(cpu->decoder.core.cseip);
	cpu->decoder.core.cseip += 1;
	return temp;
}

static inline u16 Fetchw(struct dp_cpu *cpu)
{
	struct dp_memory *memory = cpu->memory;

	u16 temp = LoadMw(cpu->decoder.core.cseip);
	cpu->decoder.core.cseip += 2;
	return temp;
}

static inline u32 Fetchd(struct dp_cpu *cpu)
{
	struct dp_memory *memory = cpu->memory;

	u32 temp = LoadMd(cpu->decoder.core.cseip);
	cpu->decoder.core.cseip += 4;
	return temp;
}

/* Jumps */

/* All Byte general instructions */
#define ADDB(op1,op2,load,save)								\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b+lf_var2b;					\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_ADDb;

#define ADCB(op1,op2,load,save)								\
	cpu->decoder.lflags.oldcf=get_CF(cpu)!=0;								\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b+lf_var2b+cpu->decoder.lflags.oldcf;		\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_ADCb;

#define SBBB(op1,op2,load,save)								\
	cpu->decoder.lflags.oldcf=get_CF(cpu)!=0;									\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b-(lf_var2b+cpu->decoder.lflags.oldcf);	\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_SBBb;

#define SUBB(op1,op2,load,save)								\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b-lf_var2b;					\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_SUBb;

#define ORB(op1,op2,load,save)								\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b | lf_var2b;				\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_ORb;

#define XORB(op1,op2,load,save)								\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b ^ lf_var2b;				\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_XORb;

#define ANDB(op1,op2,load,save)								\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b & lf_var2b;				\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_ANDb;

#define CMPB(op1,op2,load,save)								\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b-lf_var2b;					\
	cpu->decoder.lflags.type=t_CMPb;

#define TESTB(op1,op2,load,save)							\
	lf_var1b=load(op1);lf_var2b=op2;					\
	lf_resb=lf_var1b & lf_var2b;				\
	cpu->decoder.lflags.type=t_TESTb;

/* All Word General instructions */

#define ADDW(op1,op2,load,save)								\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w+lf_var2w;					\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_ADDw;

#define ADCW(op1,op2,load,save)								\
	cpu->decoder.lflags.oldcf=get_CF(cpu)!=0;									\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w+lf_var2w+cpu->decoder.lflags.oldcf;		\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_ADCw;

#define SBBW(op1,op2,load,save)								\
	cpu->decoder.lflags.oldcf=get_CF(cpu)!=0;									\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w-(lf_var2w+cpu->decoder.lflags.oldcf);	\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_SBBw;

#define SUBW(op1,op2,load,save)								\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w-lf_var2w;					\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_SUBw;

#define ORW(op1,op2,load,save)								\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w | lf_var2w;				\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_ORw;

#define XORW(op1,op2,load,save)								\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w ^ lf_var2w;				\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_XORw;

#define ANDW(op1,op2,load,save)								\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w & lf_var2w;				\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_ANDw;

#define CMPW(op1,op2,load,save)								\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w-lf_var2w;					\
	cpu->decoder.lflags.type=t_CMPw;

#define TESTW(op1,op2,load,save)							\
	lf_var1w=load(op1);lf_var2w=op2;					\
	lf_resw=lf_var1w & lf_var2w;				\
	cpu->decoder.lflags.type=t_TESTw;

/* All DWORD General Instructions */

#define ADDD(op1,op2,load,save)								\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d+lf_var2d;					\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_ADDd;

#define ADCD(op1,op2,load,save)								\
	cpu->decoder.lflags.oldcf=get_CF(cpu)!=0;									\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d+lf_var2d+cpu->decoder.lflags.oldcf;		\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_ADCd;

#define SBBD(op1,op2,load,save)								\
	cpu->decoder.lflags.oldcf=get_CF(cpu)!=0;									\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d-(lf_var2d+cpu->decoder.lflags.oldcf);	\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_SBBd;

#define SUBD(op1,op2,load,save)								\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d-lf_var2d;					\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_SUBd;

#define ORD(op1,op2,load,save)								\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d | lf_var2d;				\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_ORd;

#define XORD(op1,op2,load,save)								\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d ^ lf_var2d;				\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_XORd;

#define ANDD(op1,op2,load,save)								\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d & lf_var2d;				\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_ANDd;

#define CMPD(op1,op2,load,save)								\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d-lf_var2d;					\
	cpu->decoder.lflags.type=t_CMPd;

#define TESTD(op1,op2,load,save)							\
	lf_var1d=load(op1);lf_var2d=op2;					\
	lf_resd=lf_var1d & lf_var2d;				\
	cpu->decoder.lflags.type=t_TESTd;

#define INCB(op1,load,save)									\
	LoadCF;lf_var1b=load(op1);								\
	lf_resb=lf_var1b+1;										\
	save(op1,lf_resb);										\
	cpu->decoder.lflags.type=t_INCb;										\

#define INCW(op1,load,save)									\
	LoadCF;lf_var1w=load(op1);								\
	lf_resw=lf_var1w+1;										\
	save(op1,lf_resw);										\
	cpu->decoder.lflags.type=t_INCw;

#define INCD(op1,load,save)									\
	LoadCF;lf_var1d=load(op1);								\
	lf_resd=lf_var1d+1;										\
	save(op1,lf_resd);										\
	cpu->decoder.lflags.type=t_INCd;

#define DECB(op1,load,save)									\
	LoadCF;lf_var1b=load(op1);								\
	lf_resb=lf_var1b-1;										\
	save(op1,lf_resb);										\
	cpu->decoder.lflags.type=t_DECb;

#define DECW(op1,load,save)									\
	LoadCF;lf_var1w=load(op1);								\
	lf_resw=lf_var1w-1;										\
	save(op1,lf_resw);										\
	cpu->decoder.lflags.type=t_DECw;

#define DECD(op1,load,save)									\
	LoadCF;lf_var1d=load(op1);								\
	lf_resd=lf_var1d-1;										\
	save(op1,lf_resd);										\
	cpu->decoder.lflags.type=t_DECd;

#define ROLB(op1,op2,load,save)						\
	if (!(op2&0x7)) {								\
		if (op2&0x18) {								\
			FillFlagsNoCFOF(cpu);						\
			DC_SET_FLAGBIT(CF,op1 & 1);					\
			DC_SET_FLAGBIT(OF,(op1 & 1) ^ (op1 >> 7));	\
		}											\
		break;										\
	}												\
	FillFlagsNoCFOF(cpu);								\
	lf_var1b=load(op1);								\
	lf_var2b=op2&0x07;								\
	lf_resb=(lf_var1b << lf_var2b) |				\
			(lf_var1b >> (8-lf_var2b));				\
	save(op1,lf_resb);								\
	DC_SET_FLAGBIT(CF,lf_resb & 1);						\
	DC_SET_FLAGBIT(OF,(lf_resb & 1) ^ (lf_resb >> 7));

#define ROLW(op1,op2,load,save)						\
	if (!(op2&0xf)) {								\
		if (op2&0x10) {								\
			FillFlagsNoCFOF(cpu);						\
			DC_SET_FLAGBIT(CF,op1 & 1);					\
			DC_SET_FLAGBIT(OF,(op1 & 1) ^ (op1 >> 15));	\
		}											\
		break;										\
	}												\
	FillFlagsNoCFOF(cpu);								\
	lf_var1w=load(op1);								\
	lf_var2b=op2&0xf;								\
	lf_resw=(lf_var1w << lf_var2b) |				\
			(lf_var1w >> (16-lf_var2b));			\
	save(op1,lf_resw);								\
	DC_SET_FLAGBIT(CF,lf_resw & 1);						\
	DC_SET_FLAGBIT(OF,(lf_resw & 1) ^ (lf_resw >> 15));

#define ROLD(op1,op2,load,save)						\
	if (!op2) break;								\
	FillFlagsNoCFOF(cpu);								\
	lf_var1d=load(op1);								\
	lf_var2b=op2;									\
	lf_resd=(lf_var1d << lf_var2b) |				\
			(lf_var1d >> (32-lf_var2b));			\
	save(op1,lf_resd);								\
	DC_SET_FLAGBIT(CF,lf_resd & 1);						\
	DC_SET_FLAGBIT(OF,(lf_resd & 1) ^ (lf_resd >> 31));

#define RORB(op1,op2,load,save)						\
	if (!(op2&0x7)) {								\
		if (op2&0x18) {								\
			FillFlagsNoCFOF(cpu);						\
			DC_SET_FLAGBIT(CF,op1>>7);					\
			DC_SET_FLAGBIT(OF,(op1>>7) ^ ((op1>>6) & 1));			\
		}											\
		break;										\
	}												\
	FillFlagsNoCFOF(cpu);								\
	lf_var1b=load(op1);								\
	lf_var2b=op2&0x07;								\
	lf_resb=(lf_var1b >> lf_var2b) |				\
			(lf_var1b << (8-lf_var2b));				\
	save(op1,lf_resb);								\
	DC_SET_FLAGBIT(CF,lf_resb & 0x80);					\
	DC_SET_FLAGBIT(OF,(lf_resb ^ (lf_resb<<1)) & 0x80);

#define RORW(op1,op2,load,save)					\
	if (!(op2&0xf)) {							\
		if (op2&0x10) {							\
			FillFlagsNoCFOF(cpu);					\
			DC_SET_FLAGBIT(CF,op1>>15);				\
			DC_SET_FLAGBIT(OF,(op1>>15) ^ ((op1>>14) & 1));			\
		}										\
		break;									\
	}											\
	FillFlagsNoCFOF(cpu);							\
	lf_var1w=load(op1);							\
	lf_var2b=op2&0xf;							\
	lf_resw=(lf_var1w >> lf_var2b) |			\
			(lf_var1w << (16-lf_var2b));		\
	save(op1,lf_resw);							\
	DC_SET_FLAGBIT(CF,lf_resw & 0x8000);			\
	DC_SET_FLAGBIT(OF,(lf_resw ^ (lf_resw<<1)) & 0x8000);

#define RORD(op1,op2,load,save)					\
	if (!op2) break;							\
	FillFlagsNoCFOF(cpu);							\
	lf_var1d=load(op1);							\
	lf_var2b=op2;								\
	lf_resd=(lf_var1d >> lf_var2b) |			\
			(lf_var1d << (32-lf_var2b));		\
	save(op1,lf_resd);							\
	DC_SET_FLAGBIT(CF,lf_resd & 0x80000000);		\
	DC_SET_FLAGBIT(OF,(lf_resd ^ (lf_resd<<1)) & 0x80000000);

#define RCLB(op1,op2,load,save)							\
	if (!(op2%9)) break;								\
{	u8 cf=(u8)FillFlags(cpu)&0x1;					\
	lf_var1b=load(op1);									\
	lf_var2b=op2%9;										\
	lf_resb=(lf_var1b << lf_var2b) |					\
			(cf << (lf_var2b-1)) |						\
			(lf_var1b >> (9-lf_var2b));					\
 	save(op1,lf_resb);									\
	DC_SET_FLAGBIT(CF,((lf_var1b >> (8-lf_var2b)) & 1));	\
	DC_SET_FLAGBIT(OF,(dcr_reg_flags & 1) ^ (lf_resb >> 7));	\
}

#define RCLW(op1,op2,load,save)							\
	if (!(op2%17)) break;								\
{	u16 cf=(u16)FillFlags(cpu)&0x1;					\
	lf_var1w=load(op1);									\
	lf_var2b=op2%17;									\
	lf_resw=(lf_var1w << lf_var2b) |					\
			(cf << (lf_var2b-1)) |						\
			(lf_var1w >> (17-lf_var2b));				\
	save(op1,lf_resw);									\
	DC_SET_FLAGBIT(CF,((lf_var1w >> (16-lf_var2b)) & 1));	\
	DC_SET_FLAGBIT(OF,(dcr_reg_flags & 1) ^ (lf_resw >> 15));	\
}

#define RCLD(op1,op2,load,save)							\
	if (!op2) break;									\
{	u32 cf=(u32)FillFlags(cpu)&0x1;					\
	lf_var1d=load(op1);									\
	lf_var2b=op2;										\
	if (lf_var2b==1)	{								\
		lf_resd=(lf_var1d << 1) | cf;					\
	} else 	{											\
		lf_resd=(lf_var1d << lf_var2b) |				\
		(cf << (lf_var2b-1)) |							\
		(lf_var1d >> (33-lf_var2b));					\
	}													\
	save(op1,lf_resd);									\
	DC_SET_FLAGBIT(CF,((lf_var1d >> (32-lf_var2b)) & 1));	\
	DC_SET_FLAGBIT(OF,(dcr_reg_flags & 1) ^ (lf_resd >> 31));	\
}

#define RCRB(op1,op2,load,save)								\
	if (op2%9) {											\
		u8 cf=(u8)FillFlags(cpu)&0x1;					\
		lf_var1b=load(op1);									\
		lf_var2b=op2%9;										\
	 	lf_resb=(lf_var1b >> lf_var2b) |					\
				(cf << (8-lf_var2b)) |						\
				(lf_var1b << (9-lf_var2b));					\
		save(op1,lf_resb);									\
		DC_SET_FLAGBIT(CF,(lf_var1b >> (lf_var2b - 1)) & 1);	\
		DC_SET_FLAGBIT(OF,(lf_resb ^ (lf_resb<<1)) & 0x80);		\
	}

#define RCRW(op1,op2,load,save)								\
	if (op2%17) {											\
		u16 cf=(u16)FillFlags(cpu)&0x1;					\
		lf_var1w=load(op1);									\
		lf_var2b=op2%17;									\
	 	lf_resw=(lf_var1w >> lf_var2b) |					\
				(cf << (16-lf_var2b)) |						\
				(lf_var1w << (17-lf_var2b));				\
		save(op1,lf_resw);									\
		DC_SET_FLAGBIT(CF,(lf_var1w >> (lf_var2b - 1)) & 1);	\
		DC_SET_FLAGBIT(OF,(lf_resw ^ (lf_resw<<1)) & 0x8000);	\
	}

#define RCRD(op1,op2,load,save)								\
	if (op2) {												\
		u32 cf=(u32)FillFlags(cpu)&0x1;					\
		lf_var1d=load(op1);									\
		lf_var2b=op2;										\
		if (lf_var2b==1) {									\
			lf_resd=lf_var1d >> 1 | cf << 31;				\
		} else {											\
 			lf_resd=(lf_var1d >> lf_var2b) |				\
				(cf << (32-lf_var2b)) |						\
				(lf_var1d << (33-lf_var2b));				\
		}													\
		save(op1,lf_resd);									\
		DC_SET_FLAGBIT(CF,(lf_var1d >> (lf_var2b - 1)) & 1);	\
		DC_SET_FLAGBIT(OF,(lf_resd ^ (lf_resd<<1)) & 0x80000000);	\
	}

#define SHLB(op1,op2,load,save)								\
	if (!op2) break;										\
	lf_var1b=load(op1);lf_var2b=op2;				\
	lf_resb=lf_var1b << lf_var2b;			\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_SHLb;

#define SHLW(op1,op2,load,save)								\
	if (!op2) break;										\
	lf_var1w=load(op1);lf_var2b=op2 ;				\
	lf_resw=lf_var1w << lf_var2b;			\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_SHLw;

#define SHLD(op1,op2,load,save)								\
	if (!op2) break;										\
	lf_var1d=load(op1);lf_var2b=op2;				\
	lf_resd=lf_var1d << lf_var2b;			\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_SHLd;

#define SHRB(op1,op2,load,save)								\
	if (!op2) break;										\
	lf_var1b=load(op1);lf_var2b=op2;				\
	lf_resb=lf_var1b >> lf_var2b;			\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_SHRb;

#define SHRW(op1,op2,load,save)								\
	if (!op2) break;										\
	lf_var1w=load(op1);lf_var2b=op2;				\
	lf_resw=lf_var1w >> lf_var2b;			\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_SHRw;

#define SHRD(op1,op2,load,save)								\
	if (!op2) break;										\
	lf_var1d=load(op1);lf_var2b=op2;				\
	lf_resd=lf_var1d >> lf_var2b;			\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_SHRd;

#define SARB(op1,op2,load,save)								\
	if (!op2) break;										\
	lf_var1b=load(op1);lf_var2b=op2;				\
	if (lf_var2b>8) lf_var2b=8;						\
    if (lf_var1b & 0x80) {								\
		lf_resb=(lf_var1b >> lf_var2b)|		\
		(0xff << (8 - lf_var2b));						\
	} else {												\
		lf_resb=lf_var1b >> lf_var2b;		\
    }														\
	save(op1,lf_resb);								\
	cpu->decoder.lflags.type=t_SARb;

#define SARW(op1,op2,load,save)								\
	if (!op2) break;								\
	lf_var1w=load(op1);lf_var2b=op2;			\
	if (lf_var2b>16) lf_var2b=16;					\
	if (lf_var1w & 0x8000) {							\
		lf_resw=(lf_var1w >> lf_var2b)|		\
		(0xffff << (16 - lf_var2b));					\
	} else {												\
		lf_resw=lf_var1w >> lf_var2b;		\
    }														\
	save(op1,lf_resw);								\
	cpu->decoder.lflags.type=t_SARw;

#define SARD(op1,op2,load,save)								\
	if (!op2) break;								\
	lf_var2b=op2;lf_var1d=load(op1);			\
	if (lf_var1d & 0x80000000) {						\
		lf_resd=(lf_var1d >> lf_var2b)|		\
		(0xffffffff << (32 - lf_var2b));				\
	} else {												\
		lf_resd=lf_var1d >> lf_var2b;		\
    }														\
	save(op1,lf_resd);								\
	cpu->decoder.lflags.type=t_SARd;

#define DAA()												\
	if (((dcr_reg_al & 0x0F)>0x09) || get_AF(cpu)) {				\
		if ((dcr_reg_al > 0x99) || get_CF(cpu)) {					\
			dcr_reg_al+=0x60;									\
			DC_SET_FLAGBIT(CF,DP_TRUE);							\
		} else {											\
			DC_SET_FLAGBIT(CF,DP_FALSE);							\
		}													\
		dcr_reg_al+=0x06;										\
		DC_SET_FLAGBIT(AF,DP_TRUE);								\
	} else {												\
		if ((dcr_reg_al > 0x99) || get_CF(cpu)) {					\
			dcr_reg_al+=0x60;									\
			DC_SET_FLAGBIT(CF,DP_TRUE);							\
		} else {											\
			DC_SET_FLAGBIT(CF,DP_FALSE);							\
		}													\
		DC_SET_FLAGBIT(AF,DP_FALSE);								\
	}														\
	DC_SET_FLAGBIT(SF,(dcr_reg_al&0x80));							\
	DC_SET_FLAGBIT(ZF,(dcr_reg_al==0));								\
	DC_SET_FLAGBIT(PF,parity_lookup[dcr_reg_al]);					\
	cpu->decoder.lflags.type=t_UNKNOWN;

#define DAS()												\
{															\
	u8 osigned=dcr_reg_al & 0x80;							\
	if (((dcr_reg_al & 0x0f) > 9) || get_AF(cpu)) {				\
		if ((dcr_reg_al>0x99) || get_CF(cpu)) {					\
			dcr_reg_al-=0x60;									\
			DC_SET_FLAGBIT(CF,DP_TRUE);							\
		} else {											\
			DC_SET_FLAGBIT(CF,(dcr_reg_al<=0x05));					\
		}													\
		dcr_reg_al-=6;											\
		DC_SET_FLAGBIT(AF,DP_TRUE);								\
	} else {												\
		if ((dcr_reg_al>0x99) || get_CF(cpu)) {					\
			dcr_reg_al-=0x60;									\
			DC_SET_FLAGBIT(CF,DP_TRUE);							\
		} else {											\
			DC_SET_FLAGBIT(CF,DP_FALSE);							\
		}													\
		DC_SET_FLAGBIT(AF,DP_FALSE);								\
	}														\
	DC_SET_FLAGBIT(OF,osigned && ((dcr_reg_al&0x80)==0));			\
	DC_SET_FLAGBIT(SF,(dcr_reg_al&0x80));							\
	DC_SET_FLAGBIT(ZF,(dcr_reg_al==0));								\
	DC_SET_FLAGBIT(PF,parity_lookup[dcr_reg_al]);					\
	cpu->decoder.lflags.type=t_UNKNOWN;									\
}

#define AAA()												\
	DC_SET_FLAGBIT(SF,((dcr_reg_al>=0x7a) && (dcr_reg_al<=0xf9)));		\
	if ((dcr_reg_al & 0xf) > 9) {								\
		DC_SET_FLAGBIT(OF,(dcr_reg_al&0xf0)==0x70);					\
		dcr_reg_ax += 0x106;									\
		DC_SET_FLAGBIT(CF,DP_TRUE);								\
		DC_SET_FLAGBIT(ZF,(dcr_reg_al == 0));						\
		DC_SET_FLAGBIT(AF,DP_TRUE);								\
	} else if (get_AF(cpu)) {									\
		dcr_reg_ax += 0x106;									\
		DC_SET_FLAGBIT(OF,DP_FALSE);								\
		DC_SET_FLAGBIT(CF,DP_TRUE);								\
		DC_SET_FLAGBIT(ZF,DP_FALSE);								\
		DC_SET_FLAGBIT(AF,DP_TRUE);								\
	} else {												\
		DC_SET_FLAGBIT(OF,DP_FALSE);								\
		DC_SET_FLAGBIT(CF,DP_FALSE);								\
		DC_SET_FLAGBIT(ZF,(dcr_reg_al == 0));						\
		DC_SET_FLAGBIT(AF,DP_FALSE);								\
	}														\
	DC_SET_FLAGBIT(PF,parity_lookup[dcr_reg_al]);					\
	dcr_reg_al &= 0x0F;											\
	cpu->decoder.lflags.type=t_UNKNOWN;

#define AAS()												\
	if ((dcr_reg_al & 0x0f)>9) {								\
		DC_SET_FLAGBIT(SF,(dcr_reg_al>0x85));						\
		dcr_reg_ax -= 0x106;									\
		DC_SET_FLAGBIT(OF,DP_FALSE);								\
		DC_SET_FLAGBIT(CF,DP_TRUE);								\
		DC_SET_FLAGBIT(AF,DP_TRUE);								\
	} else if (get_AF(cpu)) {									\
		DC_SET_FLAGBIT(OF,((dcr_reg_al>=0x80) && (dcr_reg_al<=0x85)));	\
		DC_SET_FLAGBIT(SF,(dcr_reg_al<0x06) || (dcr_reg_al>0x85));		\
		dcr_reg_ax -= 0x106;									\
		DC_SET_FLAGBIT(CF,DP_TRUE);								\
		DC_SET_FLAGBIT(AF,DP_TRUE);								\
	} else {												\
		DC_SET_FLAGBIT(SF,(dcr_reg_al>=0x80));						\
		DC_SET_FLAGBIT(OF,DP_FALSE);								\
		DC_SET_FLAGBIT(CF,DP_FALSE);								\
		DC_SET_FLAGBIT(AF,DP_FALSE);								\
	}														\
	DC_SET_FLAGBIT(ZF,(dcr_reg_al == 0));							\
	DC_SET_FLAGBIT(PF,parity_lookup[dcr_reg_al]);					\
	dcr_reg_al &= 0x0F;											\
	cpu->decoder.lflags.type=t_UNKNOWN;

#define AAM(op1)											\
{															\
	u8 dv=op1;											\
	if (dv!=0) {											\
		dcr_reg_ah=dcr_reg_al / dv;									\
		dcr_reg_al=dcr_reg_al % dv;									\
		DC_SET_FLAGBIT(SF,(dcr_reg_al & 0x80));						\
		DC_SET_FLAGBIT(ZF,(dcr_reg_al == 0));						\
		DC_SET_FLAGBIT(PF,parity_lookup[dcr_reg_al]);				\
		DC_SET_FLAGBIT(CF,DP_FALSE);								\
		DC_SET_FLAGBIT(OF,DP_FALSE);								\
		DC_SET_FLAGBIT(AF,DP_FALSE);								\
		cpu->decoder.lflags.type=t_UNKNOWN;								\
	} else EXCEPTION(0);						\
}

//Took this from bochs, i seriously hate these weird bcd opcodes
#define AAD(op1)											\
	{														\
		u16 ax1 = dcr_reg_ah * op1;							\
		u16 ax2 = ax1 + dcr_reg_al;							\
		dcr_reg_al = (u8) ax2;								\
		dcr_reg_ah = 0;											\
		DC_SET_FLAGBIT(CF,DP_FALSE);								\
		DC_SET_FLAGBIT(OF,DP_FALSE);								\
		DC_SET_FLAGBIT(AF,DP_FALSE);								\
		DC_SET_FLAGBIT(SF,dcr_reg_al >= 0x80);						\
		DC_SET_FLAGBIT(ZF,dcr_reg_al == 0);							\
		DC_SET_FLAGBIT(PF,parity_lookup[dcr_reg_al]);				\
		cpu->decoder.lflags.type=t_UNKNOWN;								\
	}

#define MULB(op1,load,save)									\
	dcr_reg_ax=dcr_reg_al*load(op1);								\
	FillFlagsNoCFOF(cpu);										\
	DC_SET_FLAGBIT(ZF,dcr_reg_al == 0);								\
	if (dcr_reg_ax & 0xff00) {									\
		DC_SET_FLAGBIT(CF,DP_TRUE);DC_SET_FLAGBIT(OF,DP_TRUE);			\
	} else {												\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	}

#define MULW(op1,load,save)									\
{															\
	u32 tempu=(u32)dcr_reg_ax*(u32)(load(op1));				\
	dcr_reg_ax=(u16)(tempu);									\
	dcr_reg_dx=(u16)(tempu >> 16);							\
	FillFlagsNoCFOF(cpu);										\
	DC_SET_FLAGBIT(ZF,dcr_reg_ax == 0);								\
	if (dcr_reg_dx) {											\
		DC_SET_FLAGBIT(CF,DP_TRUE);DC_SET_FLAGBIT(OF,DP_TRUE);			\
	} else {												\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	}														\
}

#define MULD(op1,load,save)									\
{															\
	u64 tempu=(u64)dcr_reg_eax*(u64)(load(op1));		\
	dcr_reg_eax=(u32)(tempu);								\
	dcr_reg_edx=(u32)(tempu >> 32);							\
	FillFlagsNoCFOF(cpu);										\
	DC_SET_FLAGBIT(ZF,dcr_reg_eax == 0);							\
	if (dcr_reg_edx) {											\
		DC_SET_FLAGBIT(CF,DP_TRUE);DC_SET_FLAGBIT(OF,DP_TRUE);			\
	} else {												\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	}														\
}

#define DIVB(op1,load,save)									\
{															\
	slong val=load(op1);										\
	if (val==0)	EXCEPTION(0);								\
	slong quo=dcr_reg_ax / val;									\
	u8 rem=(u8)(dcr_reg_ax % val);						\
	u8 quo8=(u8)(quo&0xff);							\
	if (quo>0xff) EXCEPTION(0);								\
	dcr_reg_ah=rem;												\
	dcr_reg_al=quo8;											\
}

#define DIVW(op1,load,save)									\
{															\
	slong val=load(op1);										\
	if (val==0)	EXCEPTION(0);								\
	slong num=((u32)dcr_reg_dx<<16)|dcr_reg_ax;							\
	slong quo=num/val;										\
	u16 rem=(u16)(num % val);							\
	u16 quo16=(u16)(quo&0xffff);						\
	if (quo!=(u32)quo16) EXCEPTION(0);					\
	dcr_reg_dx=rem;												\
	dcr_reg_ax=quo16;											\
}

#define DIVD(op1,load,save)									\
{															\
	slong val=load(op1);										\
	if (val==0) EXCEPTION(0);									\
	u64 num=(((u64)dcr_reg_edx)<<32)|dcr_reg_eax;				\
	u64 quo=num/val;										\
	slong rem=(u32)(num % val);							\
	slong quo32=(u32)(quo&0xffffffff);					\
	if (quo!=(u64)quo32) EXCEPTION(0);					\
	dcr_reg_edx=rem;											\
	dcr_reg_eax=quo32;											\
}

#define IDIVB(op1,load,save)								\
{															\
	slong val=(s8)(load(op1));							\
	if (val==0)	EXCEPTION(0);								\
	slong quo=((s16)dcr_reg_ax) / val;						\
	s8 rem=(s8)((s16)dcr_reg_ax % val);				\
	s8 quo8s=(s8)(quo&0xff);							\
	if (quo!=(s16)quo8s) EXCEPTION(0);					\
	dcr_reg_ah=rem;												\
	dcr_reg_al=quo8s;											\
}

#define IDIVW(op1,load,save)								\
{															\
	slong val=(s16)(load(op1));							\
	if (val==0) EXCEPTION(0);									\
	slong num=(s32)((dcr_reg_dx<<16)|dcr_reg_ax);					\
	slong quo=num/val;										\
	s16 rem=(s16)(num % val);							\
	s16 quo16s=(s16)quo;								\
	if (quo!=(s32)quo16s) EXCEPTION(0);					\
	dcr_reg_dx=rem;												\
	dcr_reg_ax=quo16s;											\
}

#define IDIVD(op1,load,save)								\
{															\
	slong val=(s32)(load(op1));							\
	if (val==0) EXCEPTION(0);									\
	s64 num=(((u64)dcr_reg_edx)<<32)|dcr_reg_eax;				\
	s64 quo=num/val;										\
	s32 rem=(s32)(num % val);							\
	s32 quo32s=(s32)(quo&0xffffffff);					\
	if (quo!=(s64)quo32s) EXCEPTION(0);				\
	dcr_reg_edx=rem;											\
	dcr_reg_eax=quo32s;							\
}

#define IMULB(op1,load,save)								\
{															\
	dcr_reg_ax=((s8)dcr_reg_al) * ((s8)(load(op1)));			\
	FillFlagsNoCFOF(cpu);										\
	if ((dcr_reg_ax & 0xff80)==0xff80 ||						\
		(dcr_reg_ax & 0xff80)==0x0000) {						\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	} else {												\
		DC_SET_FLAGBIT(CF,DP_TRUE);DC_SET_FLAGBIT(OF,DP_TRUE);			\
	}														\
}

#define IMULW(op1,load,save)								\
{															\
	u32 temps=((s16)dcr_reg_ax)*((s16)(load(op1)));		\
	dcr_reg_ax=(s16)(temps);									\
	dcr_reg_dx=(s16)(temps >> 16);							\
	FillFlagsNoCFOF(cpu);										\
	if (((temps & 0xffff8000)==0xffff8000 ||				\
		(temps & 0xffff8000)==0x0000)) {					\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	} else {												\
		DC_SET_FLAGBIT(CF,DP_TRUE);DC_SET_FLAGBIT(OF,DP_TRUE);			\
	}														\
}

#define IMULD(op1,load,save)								\
{															\
	s64 temps=((s64)((s32)dcr_reg_eax))*				\
				 ((s64)((s32)(load(op1))));			\
	dcr_reg_eax=(u32)(temps);								\
	dcr_reg_edx=(u32)(temps >> 32);							\
	FillFlagsNoCFOF(cpu);										\
	if ((dcr_reg_edx==0xffffffff) &&							\
		(dcr_reg_eax & 0x80000000) ) {							\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	} else if ( (dcr_reg_edx==0x00000000) &&					\
				(dcr_reg_eax< 0x80000000) ) {					\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	} else {												\
		DC_SET_FLAGBIT(CF,DP_TRUE);DC_SET_FLAGBIT(OF,DP_TRUE);			\
	}														\
}

#define DIMULW(op1,op2,op3,load,save)						\
{															\
	u32 res=((s16)op2) * ((s16)op3);					\
	save(op1,res & 0xffff);									\
	FillFlagsNoCFOF(cpu);										\
	if ((res> -32768)  && (res<32767)) {					\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	} else {												\
		DC_SET_FLAGBIT(CF,DP_TRUE);DC_SET_FLAGBIT(OF,DP_TRUE);			\
	}														\
}

#define DIMULD(op1,op2,op3,load,save)						\
{															\
	s64 res=((s64)((s32)op2))*((s64)((s32)op3));	\
	save(op1,(s32)res);									\
	FillFlagsNoCFOF(cpu);										\
	if ((res>-((s64)(2147483647)+1)) &&					\
		(res<(s64)2147483647)) {							\
		DC_SET_FLAGBIT(CF,DP_FALSE);DC_SET_FLAGBIT(OF,DP_FALSE);			\
	} else {												\
		DC_SET_FLAGBIT(CF,DP_TRUE);DC_SET_FLAGBIT(OF,DP_TRUE);			\
	}														\
}

#define GRP2B(blah)											\
{															\
	GetRM;u32 which=(rm>>3)&7;								\
	if (rm >= 0xc0) {										\
		GetEArb;											\
		u8 val=blah & 0x1f;								\
		switch (which)	{									\
		case 0x00:ROLB(*earb,val,LoadRb,SaveRb);break;		\
		case 0x01:RORB(*earb,val,LoadRb,SaveRb);break;		\
		case 0x02:RCLB(*earb,val,LoadRb,SaveRb);break;		\
		case 0x03:RCRB(*earb,val,LoadRb,SaveRb);break;		\
		case 0x04:/* SHL and SAL are the same */			\
		case 0x06:SHLB(*earb,val,LoadRb,SaveRb);break;		\
		case 0x05:SHRB(*earb,val,LoadRb,SaveRb);break;		\
		case 0x07:SARB(*earb,val,LoadRb,SaveRb);break;		\
		}													\
	} else {												\
		GetEAa;												\
		u8 val=blah & 0x1f;								\
		switch (which) {									\
		case 0x00:ROLB(eaa,val,LoadMb,SaveMb);break;		\
		case 0x01:RORB(eaa,val,LoadMb,SaveMb);break;		\
		case 0x02:RCLB(eaa,val,LoadMb,SaveMb);break;		\
		case 0x03:RCRB(eaa,val,LoadMb,SaveMb);break;		\
		case 0x04:/* SHL and SAL are the same */			\
		case 0x06:SHLB(eaa,val,LoadMb,SaveMb);break;		\
		case 0x05:SHRB(eaa,val,LoadMb,SaveMb);break;		\
		case 0x07:SARB(eaa,val,LoadMb,SaveMb);break;		\
		}													\
	}														\
}

#define GRP2W(blah)											\
{															\
	GetRM;u32 which=(rm>>3)&7;								\
	if (rm >= 0xc0) {										\
		GetEArw;											\
		u8 val=blah & 0x1f;								\
		switch (which)	{									\
		case 0x00:ROLW(*earw,val,LoadRw,SaveRw);break;		\
		case 0x01:RORW(*earw,val,LoadRw,SaveRw);break;		\
		case 0x02:RCLW(*earw,val,LoadRw,SaveRw);break;		\
		case 0x03:RCRW(*earw,val,LoadRw,SaveRw);break;		\
		case 0x04:/* SHL and SAL are the same */		\
		case 0x06:SHLW(*earw,val,LoadRw,SaveRw);break;		\
		case 0x05:SHRW(*earw,val,LoadRw,SaveRw);break;		\
		case 0x07:SARW(*earw,val,LoadRw,SaveRw);break;		\
		}							\
	} else {							\
		GetEAa;						\
		u8 val=blah & 0x1f;					\
		switch (which) {					\
		case 0x00:ROLW(eaa,val,LoadMw,SaveMw);break;		\
		case 0x01:RORW(eaa,val,LoadMw,SaveMw);break;		\
		case 0x02:RCLW(eaa,val,LoadMw,SaveMw);break;		\
		case 0x03:RCRW(eaa,val,LoadMw,SaveMw);break;		\
		case 0x04:/* SHL and SAL are the same */		\
		case 0x06:SHLW(eaa,val,LoadMw,SaveMw);break;		\
		case 0x05:SHRW(eaa,val,LoadMw,SaveMw);break;		\
		case 0x07:SARW(eaa,val,LoadMw,SaveMw);break;		\
		}													\
	}														\
}

#define GRP2D(blah)											\
{															\
	GetRM;u32 which=(rm>>3)&7;								\
	if (rm >= 0xc0) {										\
		GetEArd;											\
		u8 val=blah & 0x1f;								\
		switch (which)	{									\
		case 0x00:ROLD(*eard,val,LoadRd,SaveRd);break;		\
		case 0x01:RORD(*eard,val,LoadRd,SaveRd);break;		\
		case 0x02:RCLD(*eard,val,LoadRd,SaveRd);break;		\
		case 0x03:RCRD(*eard,val,LoadRd,SaveRd);break;		\
		case 0x04:/* SHL and SAL are the same */			\
		case 0x06:SHLD(*eard,val,LoadRd,SaveRd);break;		\
		case 0x05:SHRD(*eard,val,LoadRd,SaveRd);break;		\
		case 0x07:SARD(*eard,val,LoadRd,SaveRd);break;		\
		}													\
	} else {												\
		GetEAa;												\
		u8 val=blah & 0x1f;								\
		switch (which) {									\
		case 0x00:ROLD(eaa,val,LoadMd,SaveMd);break;		\
		case 0x01:RORD(eaa,val,LoadMd,SaveMd);break;		\
		case 0x02:RCLD(eaa,val,LoadMd,SaveMd);break;		\
		case 0x03:RCRD(eaa,val,LoadMd,SaveMd);break;		\
		case 0x04:/* SHL and SAL are the same */			\
		case 0x06:SHLD(eaa,val,LoadMd,SaveMd);break;		\
		case 0x05:SHRD(eaa,val,LoadMd,SaveMd);break;		\
		case 0x07:SARD(eaa,val,LoadMd,SaveMd);break;		\
		}													\
	}														\
}

/* let's hope bochs has it correct with the higher than 16 shifts */
/* double-precision shift left has low bits in second argument */
#define DSHLW(op1,op2,op3,load,save)									\
	u8 val=op3 & 0x1F;												\
	if (!val) break;													\
	lf_var2b=val;lf_var1d=(load(op1)<<16)|op2;					\
	u32 tempd=lf_var1d << lf_var2b;							\
  	if (lf_var2b>16) tempd |= (op2 << (lf_var2b - 16));			\
	lf_resw=(u16)(tempd >> 16);								\
	save(op1,lf_resw);											\
	cpu->decoder.lflags.type=t_DSHLw;

#define DSHLD(op1,op2,op3,load,save)									\
	u8 val=op3 & 0x1F;												\
	if (!val) break;													\
	lf_var2b=val;lf_var1d=load(op1);							\
	lf_resd=(lf_var1d << lf_var2b) | (op2 >> (32-lf_var2b));	\
	save(op1,lf_resd);											\
	cpu->decoder.lflags.type=t_DSHLd;

/* double-precision shift right has high bits in second argument */
#define DSHRW(op1,op2,op3,load,save)									\
	u8 val=op3 & 0x1F;												\
	if (!val) break;													\
	lf_var2b=val;lf_var1d=(op2<<16)|load(op1);					\
	u32 tempd=lf_var1d >> lf_var2b;							\
  	if (lf_var2b>16) tempd |= (op2 << (32-lf_var2b ));			\
	lf_resw=(u16)(tempd);										\
	save(op1,lf_resw);											\
	cpu->decoder.lflags.type=t_DSHRw;

#define DSHRD(op1,op2,op3,load,save)									\
	u8 val=op3 & 0x1F;												\
	if (!val) break;													\
	lf_var2b=val;lf_var1d=load(op1);							\
	lf_resd=(lf_var1d >> lf_var2b) | (op2 << (32-lf_var2b));	\
	save(op1,lf_resd);											\
	cpu->decoder.lflags.type=t_DSHRd;

#define BSWAPW(op1)														\
	op1 = 0;

#define BSWAPD(op1)														\
	op1 = (op1>>24)|((op1>>8)&0xFF00)|((op1<<8)&0xFF0000)|((op1<<24)&0xFF000000);

#define LoadMbs(off) (s8)(LoadMb(off))
#define LoadMws(off) (s16)(LoadMw(off))
#define LoadMds(off) (s32)(LoadMd(off))

#define LoadRb(reg) reg
#define LoadRw(reg) reg
#define LoadRd(reg) reg

#define SaveRb(reg,val)	reg=val
#define SaveRw(reg,val)	reg=val
#define SaveRd(reg,val)	reg=val

static inline s8 Fetchbs(struct dp_cpu *cpu)
{
	return Fetchb(cpu);
}

static inline s16 Fetchws(struct dp_cpu *cpu)
{
	return Fetchw(cpu);
}

static inline s32 Fetchds(struct dp_cpu *cpu)
{
	return Fetchd(cpu);
}

#define RUNEXCEPTION() {										\
	CPU_Exception(cpu->block.exception.which, cpu->block.exception.error);		\
	continue;													\
}

//TODO Could probably make all byte operands fast?
#define JumpCond16_b(COND) {						\
	SAVEIP;											\
	if (COND) dcr_reg_ip+=Fetchbs(cpu);					\
	dcr_reg_ip+=1;										\
	continue;										\
}

#define JumpCond16_w(COND) {						\
	SAVEIP;											\
	if (COND) dcr_reg_ip+=Fetchws(cpu);					\
	dcr_reg_ip+=2;										\
	continue;										\
}

#define JumpCond32_b(COND) {						\
	SAVEIP;											\
	if (COND) dcr_reg_eip+=Fetchbs(cpu);					\
	dcr_reg_eip+=1;										\
	continue;										\
}

#define JumpCond32_d(COND) {						\
	SAVEIP;											\
	if (COND) dcr_reg_eip+=Fetchds(cpu);					\
	dcr_reg_eip+=4;										\
	continue;										\
}

#define SETcc(cc)											\
	{														\
		GetRM;												\
		if (rm >= 0xc0 ) {GetEArb;*earb=(cc) ? 1 : 0;}		\
		else {GetEAa;SaveMb(eaa,(cc) ? 1 : 0);}				\
	}

#define GetEAa												\
	phys_addr_t eaa=ea_table[rm](cpu);

#define GetEAa_												\
	ea_table[rm](cpu);

#define GetRMEAa											\
	GetRM;													\
	GetEAa;

#define RMEbGb(inst)														\
	{																		\
		GetRMrb;															\
		if (rm >= 0xc0 ) {GetEArb;inst(*earb,*rmrb,LoadRb,SaveRb);}			\
		else {GetEAa;inst(eaa,*rmrb,LoadMb,SaveMb);}						\
	}

#define RMGbEb(inst)														\
	{																		\
		GetRMrb;															\
		if (rm >= 0xc0 ) {GetEArb;inst(*rmrb,*earb,LoadRb,SaveRb);}			\
		else {GetEAa;inst(*rmrb,LoadMb(eaa),LoadRb,SaveRb);}				\
	}

#define RMEb(inst)															\
	{																		\
		if (rm >= 0xc0 ) {GetEArb;inst(*earb,LoadRb,SaveRb);}				\
		else {GetEAa;inst(eaa,LoadMb,SaveMb);}								\
	}

#define RMEwGw(inst)														\
	{																		\
		GetRMrw;															\
		if (rm >= 0xc0 ) {GetEArw;inst(*earw,*rmrw,LoadRw,SaveRw);}			\
		else {GetEAa;inst(eaa,*rmrw,LoadMw,SaveMw);}						\
	}

#define RMEwGwOp3(inst,op3)													\
	{																		\
		GetRMrw;															\
		if (rm >= 0xc0 ) {GetEArw;inst(*earw,*rmrw,op3,LoadRw,SaveRw);}		\
		else {GetEAa;inst(eaa,*rmrw,op3,LoadMw,SaveMw);}					\
	}

#define RMGwEw(inst)														\
	{																		\
		GetRMrw;															\
		if (rm >= 0xc0 ) {GetEArw;inst(*rmrw,*earw,LoadRw,SaveRw);}			\
		else {GetEAa;inst(*rmrw,LoadMw(eaa),LoadRw,SaveRw);}				\
	}

#define RMGwEwOp3(inst,op3)													\
	{																		\
		GetRMrw;															\
		if (rm >= 0xc0 ) {GetEArw;inst(*rmrw,*earw,op3,LoadRw,SaveRw);}		\
		else {GetEAa;inst(*rmrw,LoadMw(eaa),op3,LoadRw,SaveRw);}			\
	}

#define RMEw(inst)															\
	{																		\
		if (rm >= 0xc0 ) {GetEArw;inst(*earw,LoadRw,SaveRw);}				\
		else {GetEAa;inst(eaa,LoadMw,SaveMw);}								\
	}

#define RMEdGd(inst)														\
	{																		\
		GetRMrd;															\
		if (rm >= 0xc0 ) {GetEArd;inst(*eard,*rmrd,LoadRd,SaveRd);}			\
		else {GetEAa;inst(eaa,*rmrd,LoadMd,SaveMd);}						\
	}

#define RMEdGdOp3(inst,op3)													\
	{																		\
		GetRMrd;															\
		if (rm >= 0xc0 ) {GetEArd;inst(*eard,*rmrd,op3,LoadRd,SaveRd);}		\
		else {GetEAa;inst(eaa,*rmrd,op3,LoadMd,SaveMd);}					\
	}

#define RMGdEd(inst)														\
	{																		\
		GetRMrd;															\
		if (rm >= 0xc0 ) {GetEArd;inst(*rmrd,*eard,LoadRd,SaveRd);}			\
		else {GetEAa;inst(*rmrd,LoadMd(eaa),LoadRd,SaveRd);}				\
	}

#define RMGdEdOp3(inst,op3)													\
	{																		\
		GetRMrd;															\
		if (rm >= 0xc0 ) {GetEArd;inst(*rmrd,*eard,op3,LoadRd,SaveRd);}		\
		else {GetEAa;inst(*rmrd,LoadMd(eaa),op3,LoadRd,SaveRd);}			\
	}

#define RMEw(inst)															\
	{																		\
		if (rm >= 0xc0 ) {GetEArw;inst(*earw,LoadRw,SaveRw);}				\
		else {GetEAa;inst(eaa,LoadMw,SaveMw);}								\
	}

#define RMEd(inst)															\
	{																		\
		if (rm >= 0xc0 ) {GetEArd;inst(*eard,LoadRd,SaveRd);}				\
		else {GetEAa;inst(eaa,LoadMd,SaveMd);}								\
	}

#define ALIb(inst)															\
	{ inst(dcr_reg_al,Fetchb(cpu),LoadRb,SaveRb)}

#define AXIw(inst)															\
	{ inst(dcr_reg_ax,Fetchw(cpu),LoadRw,SaveRw);}

#define EAXId(inst)															\
	{ inst(dcr_reg_eax,Fetchd(cpu),LoadRd,SaveRd);}

#define FPU_ESC(code) {														\
	u8 rm=Fetchb(cpu);														\
	if (rm >= 0xc0) {															\
		FPU_ESC ## code ## _Normal(rm);										\
	} else {																\
		GetEAa;FPU_ESC ## code ## _EA(rm,eaa);								\
	}																		\
}

#define CASE_W(_WHICH)							\
	case (OPCODE_NONE+_WHICH):

#define CASE_D(_WHICH)							\
	case (OPCODE_SIZE+_WHICH):

#define CASE_B(_WHICH)							\
	CASE_W(_WHICH)								\
	CASE_D(_WHICH)

#define CASE_0F_W(_WHICH)						\
	case ((OPCODE_0F|OPCODE_NONE)+_WHICH):

#define CASE_0F_D(_WHICH)						\
	case ((OPCODE_0F|OPCODE_SIZE)+_WHICH):

#define CASE_0F_B(_WHICH)						\
	CASE_0F_W(_WHICH)							\
	CASE_0F_D(_WHICH)

typedef phys_addr_t(*EA_LookupHandler) (void);

/* The MOD/RM Decoder for EA for this decoder's addressing modes */
static phys_addr_t EA_16_00_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx + (s16) dcr_reg_si);
}

static phys_addr_t EA_16_01_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx + (s16) dcr_reg_di);
}

static phys_addr_t EA_16_02_n(struct dp_cpu *cpu)
{
	return BaseSS + (u16) (dcr_reg_bp + (s16) dcr_reg_si);
}

static phys_addr_t EA_16_03_n(struct dp_cpu *cpu)
{
	return BaseSS + (u16) (dcr_reg_bp + (s16) dcr_reg_di);
}

static phys_addr_t EA_16_04_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_si);
}

static phys_addr_t EA_16_05_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_di);
}

static phys_addr_t EA_16_06_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (Fetchw(cpu));
}

static phys_addr_t EA_16_07_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx);
}

static phys_addr_t EA_16_40_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx + (s16) dcr_reg_si + Fetchbs(cpu));
}

static phys_addr_t EA_16_41_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx + (s16) dcr_reg_di + Fetchbs(cpu));
}

static phys_addr_t EA_16_42_n(struct dp_cpu *cpu)
{
	return BaseSS + (u16) (dcr_reg_bp + (s16) dcr_reg_si + Fetchbs(cpu));
}

static phys_addr_t EA_16_43_n(struct dp_cpu *cpu)
{
	return BaseSS + (u16) (dcr_reg_bp + (s16) dcr_reg_di + Fetchbs(cpu));
}

static phys_addr_t EA_16_44_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_si + Fetchbs(cpu));
}

static phys_addr_t EA_16_45_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_di + Fetchbs(cpu));
}

static phys_addr_t EA_16_46_n(struct dp_cpu *cpu)
{
	return BaseSS + (u16) (dcr_reg_bp + Fetchbs(cpu));
}

static phys_addr_t EA_16_47_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx + Fetchbs(cpu));
}

static phys_addr_t EA_16_80_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx + (s16) dcr_reg_si + Fetchws(cpu));
}

static phys_addr_t EA_16_81_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx + (s16) dcr_reg_di + Fetchws(cpu));
}

static phys_addr_t EA_16_82_n(struct dp_cpu *cpu)
{
	return BaseSS + (u16) (dcr_reg_bp + (s16) dcr_reg_si + Fetchws(cpu));
}

static phys_addr_t EA_16_83_n(struct dp_cpu *cpu)
{
	return BaseSS + (u16) (dcr_reg_bp + (s16) dcr_reg_di + Fetchws(cpu));
}

static phys_addr_t EA_16_84_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_si + Fetchws(cpu));
}

static phys_addr_t EA_16_85_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_di + Fetchws(cpu));
}

static phys_addr_t EA_16_86_n(struct dp_cpu *cpu)
{
	return BaseSS + (u16) (dcr_reg_bp + Fetchws(cpu));
}

static phys_addr_t EA_16_87_n(struct dp_cpu *cpu)
{
	return BaseDS + (u16) (dcr_reg_bx + Fetchws(cpu));
}

static inline u32 *GetSIBIndex(struct dp_cpu *cpu, u32 index)
{
	static u32 SIBZero = 0;

	switch (index) {
	case 0:
		return &dcr_reg_eax;
	case 1:
		return &dcr_reg_ecx;
	case 2:
		return &dcr_reg_edx;
	case 3:
		return &dcr_reg_ebx;
	case 4:
		return &SIBZero;
	case 5:
		return &dcr_reg_ebp;
	case 6:
		return &dcr_reg_esi;
	case 7:
		return &dcr_reg_edi;
	}
	return NULL;
}

static inline phys_addr_t Sib(struct dp_cpu *cpu, u32 mode)
{
	u8 sib = Fetchb(cpu);
	phys_addr_t base;
	switch (sib & 7) {
	case 0:		/* EAX Base */
		base = BaseDS + dcr_reg_eax;
		break;
	case 1:		/* ECX Base */
		base = BaseDS + dcr_reg_ecx;
		break;
	case 2:		/* EDX Base */
		base = BaseDS + dcr_reg_edx;
		break;
	case 3:		/* EBX Base */
		base = BaseDS + dcr_reg_ebx;
		break;
	case 4:		/* ESP Base */
		base = BaseSS + dcr_reg_esp;
		break;
	case 5:		/* #1 Base */
		if (!mode) {
			base = BaseDS + Fetchd(cpu);
			break;
		} else {
			base = BaseSS + dcr_reg_ebp;
			break;
		}
	case 6:		/* ESI Base */
		base = BaseDS + dcr_reg_esi;
		break;
	case 7:		/* EDI Base */
		base = BaseDS + dcr_reg_edi;
		break;
	}
	base += *GetSIBIndex(cpu, (sib >> 3) & 7) << (sib >> 6);
	return base;
}

static phys_addr_t EA_32_00_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_eax;
}

static phys_addr_t EA_32_01_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_ecx;
}

static phys_addr_t EA_32_02_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_edx;
}

static phys_addr_t EA_32_03_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_ebx;
}

static phys_addr_t EA_32_04_n(struct dp_cpu *cpu)
{
	return Sib(cpu, 0);
}

static phys_addr_t EA_32_05_n(struct dp_cpu *cpu)
{
	return BaseDS + Fetchd(cpu);
}

static phys_addr_t EA_32_06_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_esi;
}

static phys_addr_t EA_32_07_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_edi;
}

static phys_addr_t EA_32_40_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_eax + Fetchbs(cpu);
}

static phys_addr_t EA_32_41_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_ecx + Fetchbs(cpu);
}

static phys_addr_t EA_32_42_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_edx + Fetchbs(cpu);
}

static phys_addr_t EA_32_43_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_ebx + Fetchbs(cpu);
}

static phys_addr_t EA_32_44_n(struct dp_cpu *cpu)
{
	phys_addr_t temp = Sib(cpu, 1);
	return temp + Fetchbs(cpu);
}

//static phys_addr_t EA_32_44_n(struct dp_cpu *cpu) { return Sib(1)+Fetchbs(cpu);}
static phys_addr_t EA_32_45_n(struct dp_cpu *cpu)
{
	return BaseSS + dcr_reg_ebp + Fetchbs(cpu);
}

static phys_addr_t EA_32_46_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_esi + Fetchbs(cpu);
}

static phys_addr_t EA_32_47_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_edi + Fetchbs(cpu);
}

static phys_addr_t EA_32_80_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_eax + Fetchds(cpu);
}

static phys_addr_t EA_32_81_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_ecx + Fetchds(cpu);
}

static phys_addr_t EA_32_82_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_edx + Fetchds(cpu);
}

static phys_addr_t EA_32_83_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_ebx + Fetchds(cpu);
}

static phys_addr_t EA_32_84_n(struct dp_cpu *cpu)
{
	phys_addr_t temp = Sib(cpu, 2);
	return temp + Fetchds(cpu);
}

//static phys_addr_t EA_32_84_n(struct dp_cpu *cpu) { return Sib(2)+Fetchds(cpu);}
static phys_addr_t EA_32_85_n(struct dp_cpu *cpu)
{
	return BaseSS + dcr_reg_ebp + Fetchds(cpu);
}

static phys_addr_t EA_32_86_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_esi + Fetchds(cpu);
}

static phys_addr_t EA_32_87_n(struct dp_cpu *cpu)
{
	return BaseDS + dcr_reg_edi + Fetchds(cpu);
}

typedef phys_addr_t(*GetEAHandler) (struct dp_cpu * cpu);
static GetEAHandler EATable[512] = {
/* 00 */
	EA_16_00_n, EA_16_01_n, EA_16_02_n, EA_16_03_n, EA_16_04_n, EA_16_05_n, EA_16_06_n, EA_16_07_n,
	EA_16_00_n, EA_16_01_n, EA_16_02_n, EA_16_03_n, EA_16_04_n, EA_16_05_n, EA_16_06_n, EA_16_07_n,
	EA_16_00_n, EA_16_01_n, EA_16_02_n, EA_16_03_n, EA_16_04_n, EA_16_05_n, EA_16_06_n, EA_16_07_n,
	EA_16_00_n, EA_16_01_n, EA_16_02_n, EA_16_03_n, EA_16_04_n, EA_16_05_n, EA_16_06_n, EA_16_07_n,
	EA_16_00_n, EA_16_01_n, EA_16_02_n, EA_16_03_n, EA_16_04_n, EA_16_05_n, EA_16_06_n, EA_16_07_n,
	EA_16_00_n, EA_16_01_n, EA_16_02_n, EA_16_03_n, EA_16_04_n, EA_16_05_n, EA_16_06_n, EA_16_07_n,
	EA_16_00_n, EA_16_01_n, EA_16_02_n, EA_16_03_n, EA_16_04_n, EA_16_05_n, EA_16_06_n, EA_16_07_n,
	EA_16_00_n, EA_16_01_n, EA_16_02_n, EA_16_03_n, EA_16_04_n, EA_16_05_n, EA_16_06_n, EA_16_07_n,
/* 01 */
	EA_16_40_n, EA_16_41_n, EA_16_42_n, EA_16_43_n, EA_16_44_n, EA_16_45_n,
	EA_16_46_n, EA_16_47_n,
	EA_16_40_n, EA_16_41_n, EA_16_42_n, EA_16_43_n, EA_16_44_n, EA_16_45_n,
	EA_16_46_n, EA_16_47_n,
	EA_16_40_n, EA_16_41_n, EA_16_42_n, EA_16_43_n, EA_16_44_n, EA_16_45_n,
	EA_16_46_n, EA_16_47_n,
	EA_16_40_n, EA_16_41_n, EA_16_42_n, EA_16_43_n, EA_16_44_n, EA_16_45_n,
	EA_16_46_n, EA_16_47_n,
	EA_16_40_n, EA_16_41_n, EA_16_42_n, EA_16_43_n, EA_16_44_n, EA_16_45_n,
	EA_16_46_n, EA_16_47_n,
	EA_16_40_n, EA_16_41_n, EA_16_42_n, EA_16_43_n, EA_16_44_n, EA_16_45_n,
	EA_16_46_n, EA_16_47_n,
	EA_16_40_n, EA_16_41_n, EA_16_42_n, EA_16_43_n, EA_16_44_n, EA_16_45_n,
	EA_16_46_n, EA_16_47_n,
	EA_16_40_n, EA_16_41_n, EA_16_42_n, EA_16_43_n, EA_16_44_n, EA_16_45_n,
	EA_16_46_n, EA_16_47_n,
/* 10 */
	EA_16_80_n, EA_16_81_n, EA_16_82_n, EA_16_83_n, EA_16_84_n, EA_16_85_n,
	EA_16_86_n, EA_16_87_n,
	EA_16_80_n, EA_16_81_n, EA_16_82_n, EA_16_83_n, EA_16_84_n, EA_16_85_n,
	EA_16_86_n, EA_16_87_n,
	EA_16_80_n, EA_16_81_n, EA_16_82_n, EA_16_83_n, EA_16_84_n, EA_16_85_n,
	EA_16_86_n, EA_16_87_n,
	EA_16_80_n, EA_16_81_n, EA_16_82_n, EA_16_83_n, EA_16_84_n, EA_16_85_n,
	EA_16_86_n, EA_16_87_n,
	EA_16_80_n, EA_16_81_n, EA_16_82_n, EA_16_83_n, EA_16_84_n, EA_16_85_n,
	EA_16_86_n, EA_16_87_n,
	EA_16_80_n, EA_16_81_n, EA_16_82_n, EA_16_83_n, EA_16_84_n, EA_16_85_n,
	EA_16_86_n, EA_16_87_n,
	EA_16_80_n, EA_16_81_n, EA_16_82_n, EA_16_83_n, EA_16_84_n, EA_16_85_n,
	EA_16_86_n, EA_16_87_n,
	EA_16_80_n, EA_16_81_n, EA_16_82_n, EA_16_83_n, EA_16_84_n, EA_16_85_n,
	EA_16_86_n, EA_16_87_n,
/* 11 These are illegal so make em 0 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 00 */
	EA_32_00_n, EA_32_01_n, EA_32_02_n, EA_32_03_n, EA_32_04_n, EA_32_05_n,
	EA_32_06_n, EA_32_07_n,
	EA_32_00_n, EA_32_01_n, EA_32_02_n, EA_32_03_n, EA_32_04_n, EA_32_05_n,
	EA_32_06_n, EA_32_07_n,
	EA_32_00_n, EA_32_01_n, EA_32_02_n, EA_32_03_n, EA_32_04_n, EA_32_05_n,
	EA_32_06_n, EA_32_07_n,
	EA_32_00_n, EA_32_01_n, EA_32_02_n, EA_32_03_n, EA_32_04_n, EA_32_05_n,
	EA_32_06_n, EA_32_07_n,
	EA_32_00_n, EA_32_01_n, EA_32_02_n, EA_32_03_n, EA_32_04_n, EA_32_05_n,
	EA_32_06_n, EA_32_07_n,
	EA_32_00_n, EA_32_01_n, EA_32_02_n, EA_32_03_n, EA_32_04_n, EA_32_05_n,
	EA_32_06_n, EA_32_07_n,
	EA_32_00_n, EA_32_01_n, EA_32_02_n, EA_32_03_n, EA_32_04_n, EA_32_05_n,
	EA_32_06_n, EA_32_07_n,
	EA_32_00_n, EA_32_01_n, EA_32_02_n, EA_32_03_n, EA_32_04_n, EA_32_05_n,
	EA_32_06_n, EA_32_07_n,
/* 01 */
	EA_32_40_n, EA_32_41_n, EA_32_42_n, EA_32_43_n, EA_32_44_n, EA_32_45_n,
	EA_32_46_n, EA_32_47_n,
	EA_32_40_n, EA_32_41_n, EA_32_42_n, EA_32_43_n, EA_32_44_n, EA_32_45_n,
	EA_32_46_n, EA_32_47_n,
	EA_32_40_n, EA_32_41_n, EA_32_42_n, EA_32_43_n, EA_32_44_n, EA_32_45_n,
	EA_32_46_n, EA_32_47_n,
	EA_32_40_n, EA_32_41_n, EA_32_42_n, EA_32_43_n, EA_32_44_n, EA_32_45_n,
	EA_32_46_n, EA_32_47_n,
	EA_32_40_n, EA_32_41_n, EA_32_42_n, EA_32_43_n, EA_32_44_n, EA_32_45_n,
	EA_32_46_n, EA_32_47_n,
	EA_32_40_n, EA_32_41_n, EA_32_42_n, EA_32_43_n, EA_32_44_n, EA_32_45_n,
	EA_32_46_n, EA_32_47_n,
	EA_32_40_n, EA_32_41_n, EA_32_42_n, EA_32_43_n, EA_32_44_n, EA_32_45_n,
	EA_32_46_n, EA_32_47_n,
	EA_32_40_n, EA_32_41_n, EA_32_42_n, EA_32_43_n, EA_32_44_n, EA_32_45_n,
	EA_32_46_n, EA_32_47_n,
/* 10 */
	EA_32_80_n, EA_32_81_n, EA_32_82_n, EA_32_83_n, EA_32_84_n, EA_32_85_n,
	EA_32_86_n, EA_32_87_n,
	EA_32_80_n, EA_32_81_n, EA_32_82_n, EA_32_83_n, EA_32_84_n, EA_32_85_n,
	EA_32_86_n, EA_32_87_n,
	EA_32_80_n, EA_32_81_n, EA_32_82_n, EA_32_83_n, EA_32_84_n, EA_32_85_n,
	EA_32_86_n, EA_32_87_n,
	EA_32_80_n, EA_32_81_n, EA_32_82_n, EA_32_83_n, EA_32_84_n, EA_32_85_n,
	EA_32_86_n, EA_32_87_n,
	EA_32_80_n, EA_32_81_n, EA_32_82_n, EA_32_83_n, EA_32_84_n, EA_32_85_n,
	EA_32_86_n, EA_32_87_n,
	EA_32_80_n, EA_32_81_n, EA_32_82_n, EA_32_83_n, EA_32_84_n, EA_32_85_n,
	EA_32_86_n, EA_32_87_n,
	EA_32_80_n, EA_32_81_n, EA_32_82_n, EA_32_83_n, EA_32_84_n, EA_32_85_n,
	EA_32_86_n, EA_32_87_n,
	EA_32_80_n, EA_32_81_n, EA_32_82_n, EA_32_83_n, EA_32_84_n, EA_32_85_n,
	EA_32_86_n, EA_32_87_n,
/* 11 These are illegal so make em 0 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define GetEADirect							\
	phys_addr_t eaa;								\
	if (TEST_PREFIX_ADDR) {					\
		eaa=BaseDS+Fetchd(cpu);				\
	} else {								\
		eaa=BaseDS+Fetchw(cpu);				\
	}										\

static u8 *get_reg_al(struct dp_cpu *cpu)
{
	return &(dcr_reg_8l(cpu->regs.ax));
}

static u8 *get_reg_ah(struct dp_cpu *cpu)
{
	return &(dcr_reg_8h(cpu->regs.ax));
}

static u8 *get_reg_bl(struct dp_cpu *cpu)
{
	return &(dcr_reg_8l(cpu->regs.bx));
}

static u8 *get_reg_bh(struct dp_cpu *cpu)
{
	return &(dcr_reg_8h(cpu->regs.bx));
}

static u8 *get_reg_cl(struct dp_cpu *cpu)
{
	return &(dcr_reg_8l(cpu->regs.cx));
}

static u8 *get_reg_ch(struct dp_cpu *cpu)
{
	return &(dcr_reg_8h(cpu->regs.cx));
}

static u8 *get_reg_dl(struct dp_cpu *cpu)
{
	return &(dcr_reg_8l(cpu->regs.dx));
}

static u8 *get_reg_dh(struct dp_cpu *cpu)
{
	return &(dcr_reg_8h(cpu->regs.dx));
}

static u16 *get_reg_ax(struct dp_cpu *cpu)
{
	return &(dcr_reg_16(cpu->regs.ax));
}

static u16 *get_reg_bx(struct dp_cpu *cpu)
{
	return &(dcr_reg_16(cpu->regs.bx));
}

static u16 *get_reg_cx(struct dp_cpu *cpu)
{
	return &(dcr_reg_16(cpu->regs.cx));
}

static u16 *get_reg_dx(struct dp_cpu *cpu)
{
	return &(dcr_reg_16(cpu->regs.dx));
}

static u16 *get_reg_si(struct dp_cpu *cpu)
{
	return &(dcr_reg_16(cpu->regs.si));
}

static u16 *get_reg_di(struct dp_cpu *cpu)
{
	return &(dcr_reg_16(cpu->regs.di));
}

static u16 *get_reg_bp(struct dp_cpu *cpu)
{
	return &(dcr_reg_16(cpu->regs.bp));
}

static u16 *get_reg_sp(struct dp_cpu *cpu)
{
	return &(dcr_reg_16(cpu->regs.sp));
}

static u32 *get_reg_eax(struct dp_cpu *cpu)
{
	return &(dcr_reg_32(cpu->regs.ax));
}

static u32 *get_reg_ebx(struct dp_cpu *cpu)
{
	return &(dcr_reg_32(cpu->regs.bx));
}

static u32 *get_reg_ecx(struct dp_cpu *cpu)
{
	return &(dcr_reg_32(cpu->regs.cx));
}

static u32 *get_reg_edx(struct dp_cpu *cpu)
{
	return &(dcr_reg_32(cpu->regs.dx));
}

static u32 *get_reg_esi(struct dp_cpu *cpu)
{
	return &(dcr_reg_32(cpu->regs.si));
}

static u32 *get_reg_edi(struct dp_cpu *cpu)
{
	return &(dcr_reg_32(cpu->regs.di));
}

static u32 *get_reg_ebp(struct dp_cpu *cpu)
{
	return &(dcr_reg_32(cpu->regs.bp));
}

static u32 *get_reg_esp(struct dp_cpu *cpu)
{
	return &(dcr_reg_32(cpu->regs.sp));
}

typedef u8 *(*get_u8_reg_f) (struct dp_cpu * cpu);
static get_u8_reg_f lookupRMregb[] = {
	&get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al,
	&get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl,
	&get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl,
	&get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl,
	&get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah,
	&get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch,
	&get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh,
	&get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh,

	&get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al,
	&get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl,
	&get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl,
	&get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl,
	&get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah,
	&get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch,
	&get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh,
	&get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh,

	&get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al,
	&get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl,
	&get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl,
	&get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl,
	&get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah,
	&get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch,
	&get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh,
	&get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh,

	&get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al, &get_reg_al,
	&get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl, &get_reg_cl,
	&get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl, &get_reg_dl,
	&get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl, &get_reg_bl,
	&get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah, &get_reg_ah,
	&get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch, &get_reg_ch,
	&get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh, &get_reg_dh,
	&get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh, &get_reg_bh
};

typedef u16 *(*get_u16_reg_f) (struct dp_cpu * cpu);
static get_u16_reg_f lookupRMregw[] = {
	&get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax,
	&get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx,
	&get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx,
	&get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx,
	&get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp,
	&get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp,
	&get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si,
	&get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di,

	&get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax,
	&get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx,
	&get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx,
	&get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx,
	&get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp,
	&get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp,
	&get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si,
	&get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di,

	&get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax,
	&get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx,
	&get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx,
	&get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx,
	&get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp,
	&get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp,
	&get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si,
	&get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di,

	&get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax, &get_reg_ax,
	&get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx, &get_reg_cx,
	&get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx, &get_reg_dx,
	&get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx, &get_reg_bx,
	&get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp, &get_reg_sp,
	&get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp, &get_reg_bp,
	&get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si, &get_reg_si,
	&get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di, &get_reg_di
};

typedef u32 *(*get_u32_reg_f) (struct dp_cpu * cpu);
static get_u32_reg_f lookupRMregd[256] = {
	&get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax,
	&get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx,
	&get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx,
	&get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx,
	&get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp,
	&get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp,
	&get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi,
	&get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi,

	&get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax,
	&get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx,
	&get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx,
	&get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx,
	&get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp,
	&get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp,
	&get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi,
	&get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi,

	&get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax,
	&get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx,
	&get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx,
	&get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx,
	&get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp,
	&get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp,
	&get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi,
	&get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi,

	&get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax, &get_reg_eax,
	&get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx, &get_reg_ecx,
	&get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx, &get_reg_edx,
	&get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx, &get_reg_ebx,
	&get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp, &get_reg_esp,
	&get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp, &get_reg_ebp,
	&get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi, &get_reg_esi,
	&get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi, &get_reg_edi
};

static get_u8_reg_f lookupRMEAregb[256] = {
	/* 12 lines of 16*0 should give nice errors when used */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	&get_reg_al, &get_reg_cl, &get_reg_dl, &get_reg_bl, &get_reg_ah, &get_reg_ch, &get_reg_dh, &get_reg_bh,
	&get_reg_al, &get_reg_cl, &get_reg_dl, &get_reg_bl, &get_reg_ah, &get_reg_ch, &get_reg_dh, &get_reg_bh,
	&get_reg_al, &get_reg_cl, &get_reg_dl, &get_reg_bl, &get_reg_ah, &get_reg_ch, &get_reg_dh, &get_reg_bh,
	&get_reg_al, &get_reg_cl, &get_reg_dl, &get_reg_bl, &get_reg_ah, &get_reg_ch, &get_reg_dh, &get_reg_bh,
	&get_reg_al, &get_reg_cl, &get_reg_dl, &get_reg_bl, &get_reg_ah, &get_reg_ch, &get_reg_dh, &get_reg_bh,
	&get_reg_al, &get_reg_cl, &get_reg_dl, &get_reg_bl, &get_reg_ah, &get_reg_ch, &get_reg_dh, &get_reg_bh,
	&get_reg_al, &get_reg_cl, &get_reg_dl, &get_reg_bl, &get_reg_ah, &get_reg_ch, &get_reg_dh, &get_reg_bh,
	&get_reg_al, &get_reg_cl, &get_reg_dl, &get_reg_bl, &get_reg_ah, &get_reg_ch, &get_reg_dh, &get_reg_bh
};

static get_u16_reg_f lookupRMEAregw[256] = {
	/* 12 lines of 16*0 should give nice errors when used */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	&get_reg_ax, &get_reg_cx, &get_reg_dx, &get_reg_bx, &get_reg_sp, &get_reg_bp, &get_reg_si, &get_reg_di,
	&get_reg_ax, &get_reg_cx, &get_reg_dx, &get_reg_bx, &get_reg_sp, &get_reg_bp, &get_reg_si, &get_reg_di,
	&get_reg_ax, &get_reg_cx, &get_reg_dx, &get_reg_bx, &get_reg_sp, &get_reg_bp, &get_reg_si, &get_reg_di,
	&get_reg_ax, &get_reg_cx, &get_reg_dx, &get_reg_bx, &get_reg_sp, &get_reg_bp, &get_reg_si, &get_reg_di,
	&get_reg_ax, &get_reg_cx, &get_reg_dx, &get_reg_bx, &get_reg_sp, &get_reg_bp, &get_reg_si, &get_reg_di,
	&get_reg_ax, &get_reg_cx, &get_reg_dx, &get_reg_bx, &get_reg_sp, &get_reg_bp, &get_reg_si, &get_reg_di,
	&get_reg_ax, &get_reg_cx, &get_reg_dx, &get_reg_bx, &get_reg_sp, &get_reg_bp, &get_reg_si, &get_reg_di,
	&get_reg_ax, &get_reg_cx, &get_reg_dx, &get_reg_bx, &get_reg_sp, &get_reg_bp, &get_reg_si, &get_reg_di
};

static get_u32_reg_f lookupRMEAregd[256] = {
	/* 12 lines of 16*0 should give nice errors when used */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	&get_reg_eax, &get_reg_ecx, &get_reg_edx, &get_reg_ebx, &get_reg_esp, &get_reg_ebp, &get_reg_esi, &get_reg_edi,
	&get_reg_eax, &get_reg_ecx, &get_reg_edx, &get_reg_ebx, &get_reg_esp, &get_reg_ebp, &get_reg_esi, &get_reg_edi,
	&get_reg_eax, &get_reg_ecx, &get_reg_edx, &get_reg_ebx, &get_reg_esp, &get_reg_ebp, &get_reg_esi, &get_reg_edi,
	&get_reg_eax, &get_reg_ecx, &get_reg_edx, &get_reg_ebx, &get_reg_esp, &get_reg_ebp, &get_reg_esi, &get_reg_edi,
	&get_reg_eax, &get_reg_ecx, &get_reg_edx, &get_reg_ebx, &get_reg_esp, &get_reg_ebp, &get_reg_esi, &get_reg_edi,
	&get_reg_eax, &get_reg_ecx, &get_reg_edx, &get_reg_ebx, &get_reg_esp, &get_reg_ebp, &get_reg_esi, &get_reg_edi,
	&get_reg_eax, &get_reg_ecx, &get_reg_edx, &get_reg_ebx, &get_reg_esp, &get_reg_ebp, &get_reg_esi, &get_reg_edi,
	&get_reg_eax, &get_reg_ecx, &get_reg_edx, &get_reg_ebx, &get_reg_esp, &get_reg_ebp, &get_reg_esi, &get_reg_edi
};

#define GetRM												\
	u8 rm=Fetchb(cpu);

#define Getrb												\
	u8 * rmrb;											\
	rmrb=lookupRMregb[rm](cpu);

#define Getrw												\
	u16 * rmrw;											\
	rmrw=lookupRMregw[rm](cpu);

#define Getrd												\
	u32 * rmrd;											\
	rmrd=lookupRMregd[rm](cpu);

#define GetRMrb												\
	GetRM;													\
	Getrb;

#define GetRMrw												\
	GetRM;													\
	Getrw;

#define GetRMrd												\
	GetRM;													\
	Getrd;

#define GetEArb												\
	u8 * earb=lookupRMEAregb[rm](cpu);

#define GetEArw												\
	u16 * earw=lookupRMEAregw[rm](cpu);

#define GetEArd												\
	u32 * eard=lookupRMEAregd[rm](cpu);

enum STRING_OP {
	R_OUTSB, R_OUTSW, R_OUTSD,
	R_INSB, R_INSW, R_INSD,
	R_MOVSB, R_MOVSW, R_MOVSD,
	R_LODSB, R_LODSW, R_LODSD,
	R_STOSB, R_STOSW, R_STOSD,
	R_SCASB, R_SCASW, R_SCASD,
	R_CMPSB, R_CMPSW, R_CMPSD
};

#define LoadD(_BLAH) _BLAH

static void DoString(struct dp_cpu *cpu, enum STRING_OP type)
{
	struct dp_memory *memory = cpu->memory;
	phys_addr_t si_base, di_base;
	u32 si_index, di_index;
	u32 add_mask;
	u32 count, count_left = 0;
	u32 add_index;

	si_base = BaseDS;
	di_base = SegBase(dp_seg_es);
	add_mask = AddrMaskTable[cpu->decoder.core.prefixes & PREFIX_ADDR];
	si_index = dcr_reg_esi & add_mask;
	di_index = dcr_reg_edi & add_mask;
	count = dcr_reg_ecx & add_mask;
	if (!TEST_PREFIX_REP) {
		count = 1;
	}
	add_index = cpu->block.direction;
	if (count)
		switch (type) {
		case R_OUTSB:
			for (; count > 0; count--) {
				dp_io_writeb(cpu->io, dcr_reg_dx, LoadMb(si_base + si_index));
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_OUTSW:
			add_index <<= 1;
			for (; count > 0; count--) {
				dp_io_writew(cpu->io, dcr_reg_dx, LoadMw(si_base + si_index));
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_OUTSD:
			add_index <<= 2;
			for (; count > 0; count--) {
				dp_io_writed(cpu->io, dcr_reg_dx, LoadMd(si_base + si_index));
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_INSB:
			for (; count > 0; count--) {
				SaveMb(di_base + di_index, dp_io_readb(cpu->io, dcr_reg_dx));
				di_index = (di_index + add_index) & add_mask;
			}
			break;
		case R_INSW:
			add_index <<= 1;
			for (; count > 0; count--) {
				SaveMw(di_base + di_index, dp_io_readw(cpu->io, dcr_reg_dx));
				di_index = (di_index + add_index) & add_mask;
			}
			break;
		case R_STOSB:
			for (; count > 0; count--) {
				SaveMb(di_base + di_index, dcr_reg_al);
				di_index = (di_index + add_index) & add_mask;
			}
			break;
		case R_STOSW:
			add_index <<= 1;
			for (; count > 0; count--) {
				SaveMw(di_base + di_index, dcr_reg_ax);
				di_index = (di_index + add_index) & add_mask;
			}
			break;
		case R_STOSD:
			add_index <<= 2;
			for (; count > 0; count--) {
				SaveMd(di_base + di_index, dcr_reg_eax);
				di_index = (di_index + add_index) & add_mask;
			}
			break;
		case R_MOVSB:
			for (; count > 0; count--) {
				SaveMb(di_base + di_index, LoadMb(si_base + si_index));
				di_index = (di_index + add_index) & add_mask;
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_MOVSW:
			add_index <<= 1;
			for (; count > 0; count--) {
				SaveMw(di_base + di_index, LoadMw(si_base + si_index));
				di_index = (di_index + add_index) & add_mask;
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_MOVSD:
			add_index <<= 2;
			for (; count > 0; count--) {
				SaveMd(di_base + di_index, LoadMd(si_base + si_index));
				di_index = (di_index + add_index) & add_mask;
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_LODSB:
			for (; count > 0; count--) {
				dcr_reg_al = LoadMb(si_base + si_index);
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_LODSW:
			add_index <<= 1;
			for (; count > 0; count--) {
				dcr_reg_ax = LoadMw(si_base + si_index);
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_LODSD:
			add_index <<= 2;
			for (; count > 0; count--) {
				dcr_reg_eax = LoadMd(si_base + si_index);
				si_index = (si_index + add_index) & add_mask;
			}
			break;
		case R_SCASB:
			{
				u8 val2;
				for (; count > 0;) {
					count--;
					val2 = LoadMb(di_base + di_index);
					di_index = (di_index + add_index) & add_mask;
					if ((dcr_reg_al == val2) != cpu->decoder.core.rep_zero)
						break;
				}
				CMPB(dcr_reg_al, val2, LoadD, 0);
			}
			break;
		case R_SCASW:
			{
				add_index <<= 1;
				u16 val2;
				for (; count > 0;) {
					count--;
					val2 = LoadMw(di_base + di_index);
					di_index = (di_index + add_index) & add_mask;
					if ((dcr_reg_ax == val2) != cpu->decoder.core.rep_zero)
						break;
				}
				CMPW(dcr_reg_ax, val2, LoadD, 0);
			}
			break;
		case R_SCASD:
			{
				add_index <<= 2;
				u32 val2;
				for (; count > 0;) {
					count--;
					val2 = LoadMd(di_base + di_index);
					di_index = (di_index + add_index) & add_mask;
					if ((dcr_reg_eax == val2) != cpu->decoder.core.rep_zero)
						break;
				}
				CMPD(dcr_reg_eax, val2, LoadD, 0);
			}
			break;
		case R_CMPSB:
			{
				u8 val1, val2;
				for (; count > 0;) {
					count--;
					val1 = LoadMb(si_base + si_index);
					val2 = LoadMb(di_base + di_index);
					si_index = (si_index + add_index) & add_mask;
					di_index = (di_index + add_index) & add_mask;
					if ((val1 == val2) != cpu->decoder.core.rep_zero)
						break;
				}
				CMPB(val1, val2, LoadD, 0);
			}
			break;
		case R_CMPSW:
			{
				add_index <<= 1;
				u16 val1, val2;
				for (; count > 0;) {
					count--;
					val1 = LoadMw(si_base + si_index);
					val2 = LoadMw(di_base + di_index);
					si_index = (si_index + add_index) & add_mask;
					di_index = (di_index + add_index) & add_mask;
					if ((val1 == val2) != cpu->decoder.core.rep_zero)
						break;
				}
				CMPW(val1, val2, LoadD, 0);
			}
			break;
		case R_CMPSD:
			{
				add_index <<= 2;
				u32 val1, val2;
				for (; count > 0;) {
					count--;
					val1 = LoadMd(si_base + si_index);
					val2 = LoadMd(di_base + di_index);
					si_index = (si_index + add_index) & add_mask;
					di_index = (di_index + add_index) & add_mask;
					if ((val1 == val2) != cpu->decoder.core.rep_zero)
						break;
				}
				CMPD(val1, val2, LoadD, 0);
			}
			break;
		default:
			DP_ERR("unhandled string op %d", type);
		}
	/* Clean up after certain amount of instructions */
	dcr_reg_esi &= (~add_mask);
	dcr_reg_esi |= (si_index & add_mask);
	dcr_reg_edi &= (~add_mask);
	dcr_reg_edi |= (di_index & add_mask);
	if (TEST_PREFIX_REP) {
		count += count_left;
		dcr_reg_ecx &= (~add_mask);
		dcr_reg_ecx |= (count & add_mask);
	}
}

void dp_cpu_disasm_before_exc(struct dp_cpu *cpu)
{
	char buffer[0x100] = {0, };
	char hex[10*3+1] = {0, };
	char regs[0x100];
	phys_addr_t start, scan_start;
	u32 size, i;

	start = dp_cpu_get_phyaddr(cpu, dp_cpu_seg_value(dp_seg_cs), dcr_reg_eip);
	size = dp_debug_i386dis(cpu->memory, buffer, start,
			       dcr_reg_eip, cpu->block.code.big);

	i = 0;
	scan_start = start;
	while (i < size  &&  i < 10) {
		u8 byte = dp_memv_readb(cpu->memory, scan_start);
		snprintf(&hex[i*3], 4, "%02x ", byte);
		scan_start++;
		i++;
	}

	snprintf(regs, sizeof(regs),
		 "[%08x:%08x:%08x:%08x] [%08x:%08x:%08x:%08x]",
		 dcr_reg_eax, dcr_reg_ebx, dcr_reg_ecx, dcr_reg_edx,
		 dcr_reg_ebp, dcr_reg_esp, dcr_reg_esi, dcr_reg_edi);

	DP_LOGI(DEBUG, CPU_DISASM, "%08x: %-25s %-15s: %s", start, buffer, hex, regs);
}

static u32 CPU_Core_Normal_Trap_Run(struct dp_cpu *cpu);

u32 dp_cpu_decode_normal_run(struct dp_cpu *cpu)
{
	GetEAHandler *ea_table;
	struct dp_memory *memory = cpu->memory;

	cpu->decode_count = 128;
	while (cpu->decode_count-- > 0) {
		if (DC_GET_FLAG(IF)  &&  cpu->pic->irq_check != 0) {
			dp_pic_run_irqs(cpu->pic);
		}

		dp_timetrack_run_events_check(cpu->timetrack);

		LOADIP;
		cpu->decoder.core.opcode_index = cpu->block.code.big * 0x200;
		cpu->decoder.core.prefixes = cpu->block.code.big;
		ea_table = &EATable[cpu->block.code.big * 256];
		BaseDS = SegBase(dp_seg_ds);
		BaseSS = SegBase(dp_seg_ss);
		cpu->decoder.core.base_val_ds = dp_seg_ds;
		dp_timetrack_charge_ticks(cpu->timetrack, 1);

		DP_FILTERED_OP(DP_LOG_LEVEL_DEBUG,
			       DP_LOG_FACILITY_CPU_DISASM, dp_cpu_disasm_before_exc(cpu));

		if (cpu->decoder.inst_hook_func)
			cpu->decoder.inst_hook_func(cpu->decoder.inst_hook_data);

restart_opcode:
		switch (cpu->decoder.core.opcode_index + Fetchb(cpu)) {
			CASE_B(0x00)	/* ADD Eb,Gb */
			    RMEbGb(ADDB);
			break;
			CASE_W(0x01)	/* ADD Ew,Gw */
			    RMEwGw(ADDW);
			break;
			CASE_B(0x02)	/* ADD Gb,Eb */
			    RMGbEb(ADDB);
			break;
			CASE_W(0x03)	/* ADD Gw,Ew */
			    RMGwEw(ADDW);
			break;
			CASE_B(0x04)	/* ADD AL,Ib */
			    ALIb(ADDB);
			break;
			CASE_W(0x05)	/* ADD AX,Iw */
			    AXIw(ADDW);
			break;
			CASE_W(0x06)	/* PUSH ES */
			    Push_16(SegValue(dp_seg_es));
			break;
			CASE_W(0x07)	/* POP ES */
			    if (CPU_PopSeg(dp_seg_es, DP_FALSE))
				RUNEXCEPTION();
			break;
			CASE_B(0x08)	/* OR Eb,Gb */
			    RMEbGb(ORB);
			break;
			CASE_W(0x09)	/* OR Ew,Gw */
			    RMEwGw(ORW);
			break;
			CASE_B(0x0a)	/* OR Gb,Eb */
			    RMGbEb(ORB);
			break;
			CASE_W(0x0b)	/* OR Gw,Ew */
			    RMGwEw(ORW);
			break;
			CASE_B(0x0c)	/* OR AL,Ib */
			    ALIb(ORB);
			break;
			CASE_W(0x0d)	/* OR AX,Iw */
			    AXIw(ORW);
			break;
			CASE_W(0x0e)	/* PUSH CS */
			    Push_16(SegValue(dp_seg_cs));
			break;
			CASE_B(0x0f)	/* 2 byte opcodes */
			    cpu->decoder.core.opcode_index |= OPCODE_0F;
			goto restart_opcode;
			break;
			CASE_B(0x10)	/* ADC Eb,Gb */
			    RMEbGb(ADCB);
			break;
			CASE_W(0x11)	/* ADC Ew,Gw */
			    RMEwGw(ADCW);
			break;
			CASE_B(0x12)	/* ADC Gb,Eb */
			    RMGbEb(ADCB);
			break;
			CASE_W(0x13)	/* ADC Gw,Ew */
			    RMGwEw(ADCW);
			break;
			CASE_B(0x14)	/* ADC AL,Ib */
			    ALIb(ADCB);
			break;
			CASE_W(0x15)	/* ADC AX,Iw */
			    AXIw(ADCW);
			break;
			CASE_W(0x16)	/* PUSH SS */
			    Push_16(SegValue(dp_seg_ss));
			break;
			CASE_W(0x17)	/* POP SS */
			    if (CPU_PopSeg(dp_seg_ss, DP_FALSE))
				RUNEXCEPTION();
			cpu->decode_count++;	//Always do another instruction
			break;
			CASE_B(0x18)	/* SBB Eb,Gb */
			    RMEbGb(SBBB);
			break;
			CASE_W(0x19)	/* SBB Ew,Gw */
			    RMEwGw(SBBW);
			break;
			CASE_B(0x1a)	/* SBB Gb,Eb */
			    RMGbEb(SBBB);
			break;
			CASE_W(0x1b)	/* SBB Gw,Ew */
			    RMGwEw(SBBW);
			break;
			CASE_B(0x1c)	/* SBB AL,Ib */
			    ALIb(SBBB);
			break;
			CASE_W(0x1d)	/* SBB AX,Iw */
			    AXIw(SBBW);
			break;
			CASE_W(0x1e)	/* PUSH DS */
			    Push_16(SegValue(dp_seg_ds));
			break;
			CASE_W(0x1f)	/* POP DS */
			    if (CPU_PopSeg(dp_seg_ds, DP_FALSE))
				RUNEXCEPTION();
			break;
			CASE_B(0x20)	/* AND Eb,Gb */
			    RMEbGb(ANDB);
			break;
			CASE_W(0x21)	/* AND Ew,Gw */
			    RMEwGw(ANDW);
			break;
			CASE_B(0x22)	/* AND Gb,Eb */
			    RMGbEb(ANDB);
			break;
			CASE_W(0x23)	/* AND Gw,Ew */
			    RMGwEw(ANDW);
			break;
			CASE_B(0x24)	/* AND AL,Ib */
			    ALIb(ANDB);
			break;
			CASE_W(0x25)	/* AND AX,Iw */
			    AXIw(ANDW);
			break;
			CASE_B(0x26)	/* SEG ES: */
			    DO_PREFIX_SEG(dp_seg_es);
			break;
			CASE_B(0x27)	/* DAA */
			    DAA();
			break;
			CASE_B(0x28)	/* SUB Eb,Gb */
			    RMEbGb(SUBB);
			break;
			CASE_W(0x29)	/* SUB Ew,Gw */
			    RMEwGw(SUBW);
			break;
			CASE_B(0x2a)	/* SUB Gb,Eb */
			    RMGbEb(SUBB);
			break;
			CASE_W(0x2b)	/* SUB Gw,Ew */
			    RMGwEw(SUBW);
			break;
			CASE_B(0x2c)	/* SUB AL,Ib */
			    ALIb(SUBB);
			break;
			CASE_W(0x2d)	/* SUB AX,Iw */
			    AXIw(SUBW);
			break;
			CASE_B(0x2e)	/* SEG CS: */
			    DO_PREFIX_SEG(dp_seg_cs);
			break;
			CASE_B(0x2f)	/* DAS */
			    DAS();
			break;
			CASE_B(0x30)	/* XOR Eb,Gb */
			    RMEbGb(XORB);
			break;
			CASE_W(0x31)	/* XOR Ew,Gw */
			    RMEwGw(XORW);
			break;
			CASE_B(0x32)	/* XOR Gb,Eb */
			    RMGbEb(XORB);
			break;
			CASE_W(0x33)	/* XOR Gw,Ew */
			    RMGwEw(XORW);
			break;
			CASE_B(0x34)	/* XOR AL,Ib */
			    ALIb(XORB);
			break;
			CASE_W(0x35)	/* XOR AX,Iw */
			    AXIw(XORW);
			break;
			CASE_B(0x36)	/* SEG SS: */
			    DO_PREFIX_SEG(dp_seg_ss);
			break;
			CASE_B(0x37)	/* AAA */
			    AAA();
			break;
			CASE_B(0x38)	/* CMP Eb,Gb */
			    RMEbGb(CMPB);
			break;
			CASE_W(0x39)	/* CMP Ew,Gw */
			    RMEwGw(CMPW);
			break;
			CASE_B(0x3a)	/* CMP Gb,Eb */
			    RMGbEb(CMPB);
			break;
			CASE_W(0x3b)	/* CMP Gw,Ew */
			    RMGwEw(CMPW);
			break;
			CASE_B(0x3c)	/* CMP AL,Ib */
			    ALIb(CMPB);
			break;
			CASE_W(0x3d)	/* CMP AX,Iw */
			    AXIw(CMPW);
			break;
			CASE_B(0x3e)	/* SEG DS: */
			    DO_PREFIX_SEG(dp_seg_ds);
			break;
			CASE_B(0x3f)	/* AAS */
			    AAS();
			break;
			CASE_W(0x40)	/* INC AX */
			    INCW(dcr_reg_ax, LoadRw, SaveRw);
			break;
			CASE_W(0x41)	/* INC CX */
			    INCW(dcr_reg_cx, LoadRw, SaveRw);
			break;
			CASE_W(0x42)	/* INC DX */
			    INCW(dcr_reg_dx, LoadRw, SaveRw);
			break;
			CASE_W(0x43)	/* INC BX */
			    INCW(dcr_reg_bx, LoadRw, SaveRw);
			break;
			CASE_W(0x44)	/* INC SP */
			    INCW(dcr_reg_sp, LoadRw, SaveRw);
			break;
			CASE_W(0x45)	/* INC BP */
			    INCW(dcr_reg_bp, LoadRw, SaveRw);
			break;
			CASE_W(0x46)	/* INC SI */
			    INCW(dcr_reg_si, LoadRw, SaveRw);
			break;
			CASE_W(0x47)	/* INC DI */
			    INCW(dcr_reg_di, LoadRw, SaveRw);
			break;
			CASE_W(0x48)	/* DEC AX */
			    DECW(dcr_reg_ax, LoadRw, SaveRw);
			break;
			CASE_W(0x49)	/* DEC CX */
			    DECW(dcr_reg_cx, LoadRw, SaveRw);
			break;
			CASE_W(0x4a)	/* DEC DX */
			    DECW(dcr_reg_dx, LoadRw, SaveRw);
			break;
			CASE_W(0x4b)	/* DEC BX */
			    DECW(dcr_reg_bx, LoadRw, SaveRw);
			break;
			CASE_W(0x4c)	/* DEC SP */
			    DECW(dcr_reg_sp, LoadRw, SaveRw);
			break;
			CASE_W(0x4d)	/* DEC BP */
			    DECW(dcr_reg_bp, LoadRw, SaveRw);
			break;
			CASE_W(0x4e)	/* DEC SI */
			    DECW(dcr_reg_si, LoadRw, SaveRw);
			break;
			CASE_W(0x4f)	/* DEC DI */
			    DECW(dcr_reg_di, LoadRw, SaveRw);
			break;
			CASE_W(0x50)	/* PUSH AX */
			    Push_16(dcr_reg_ax);
			break;
			CASE_W(0x51)	/* PUSH CX */
			    Push_16(dcr_reg_cx);
			break;
			CASE_W(0x52)	/* PUSH DX */
			    Push_16(dcr_reg_dx);
			break;
			CASE_W(0x53)	/* PUSH BX */
			    Push_16(dcr_reg_bx);
			break;
			CASE_W(0x54)	/* PUSH SP */
			    Push_16(dcr_reg_sp);
			break;
			CASE_W(0x55)	/* PUSH BP */
			    Push_16(dcr_reg_bp);
			break;
			CASE_W(0x56)	/* PUSH SI */
			    Push_16(dcr_reg_si);
			break;
			CASE_W(0x57)	/* PUSH DI */
			    Push_16(dcr_reg_di);
			break;
			CASE_W(0x58)	/* POP AX */
			    dcr_reg_ax = Pop_16();
			break;
			CASE_W(0x59)	/* POP CX */
			    dcr_reg_cx = Pop_16();
			break;
			CASE_W(0x5a)	/* POP DX */
			    dcr_reg_dx = Pop_16();
			break;
			CASE_W(0x5b)	/* POP BX */
			    dcr_reg_bx = Pop_16();
			break;
			CASE_W(0x5c)	/* POP SP */
			    dcr_reg_sp = Pop_16();
			break;
			CASE_W(0x5d)	/* POP BP */
			    dcr_reg_bp = Pop_16();
			break;
			CASE_W(0x5e)	/* POP SI */
			    dcr_reg_si = Pop_16();
			break;
			CASE_W(0x5f)	/* POP DI */
			    dcr_reg_di = Pop_16();
			break;
			CASE_W(0x60) {	/* PUSHA */
				u16 old_sp = dcr_reg_sp;
				Push_16(dcr_reg_ax);
				Push_16(dcr_reg_cx);
				Push_16(dcr_reg_dx);
				Push_16(dcr_reg_bx);
				Push_16(old_sp);
				Push_16(dcr_reg_bp);
				Push_16(dcr_reg_si);
				Push_16(dcr_reg_di);
			}
			break;
			CASE_W(0x61)	/* POPA */
			    dcr_reg_di = Pop_16();
			dcr_reg_si = Pop_16();
			dcr_reg_bp = Pop_16();
			Pop_16();	//Don't save SP
			dcr_reg_bx = Pop_16();
			dcr_reg_dx = Pop_16();
			dcr_reg_cx = Pop_16();
			dcr_reg_ax = Pop_16();
			break;
			CASE_W(0x62) {	/* BOUND */
				s16 bound_min, bound_max;
				GetRMrw;
				GetEAa;
				bound_min = LoadMw(eaa);
				bound_max = LoadMw(eaa + 2);
				if ((((s16) * rmrw) < bound_min)
				    || (((s16) * rmrw) > bound_max)) {
					EXCEPTION(5);
				}
			}
			break;
			CASE_W(0x63) {	/* ARPL Ew,Rw */
				if ((dcr_reg_flags & DC_FLAG_VM) || (!cpu->block.pmode))
					goto illegal_opcode;
				GetRMrw;
				if (rm >= 0xc0) {
					GetEArw;
					u32 new_sel = *earw;
					CPU_ARPL(&new_sel, *rmrw);
					*earw = (u16) new_sel;
				} else {
					GetEAa;
					u32 new_sel = LoadMw(eaa);
					CPU_ARPL(&new_sel, *rmrw);
					SaveMw(eaa, (u16) new_sel);
				}
			}
			break;
			CASE_B(0x64)	/* SEG FS: */
			    DO_PREFIX_SEG(dp_seg_fs);
			break;
			CASE_B(0x65)	/* SEG GS: */
			    DO_PREFIX_SEG(dp_seg_gs);
			break;
			CASE_B(0x66)	/* Operand Size Prefix */
			    cpu->decoder.core.opcode_index = (cpu->block.code.big ^ 0x1) * 0x200;
			goto restart_opcode;
			CASE_B(0x67)	/* Address Size Prefix */
			    DO_PREFIX_ADDR();
			CASE_W(0x68)	/* PUSH Iw */
			    Push_16(Fetchw(cpu));
			break;
			CASE_W(0x69)	/* IMUL Gw,Ew,Iw */
			    RMGwEwOp3(DIMULW, Fetchws(cpu));
			break;
			CASE_W(0x6a)	/* PUSH Ib */
			    Push_16(Fetchbs(cpu));
			break;
			CASE_W(0x6b)	/* IMUL Gw,Ew,Ib */
			    RMGwEwOp3(DIMULW, Fetchbs(cpu));
			break;
			CASE_B(0x6c)	/* INSB */
			    if (CPU_IO_Exception(dcr_reg_dx, 1))
				RUNEXCEPTION();
			DoString(cpu, R_INSB);
			break;
			CASE_W(0x6d)	/* INSW */
			    if (CPU_IO_Exception(dcr_reg_dx, 2))
				RUNEXCEPTION();
			DoString(cpu, R_INSW);
			break;
			CASE_B(0x6e)	/* OUTSB */
			    if (CPU_IO_Exception(dcr_reg_dx, 1))
				RUNEXCEPTION();
			DoString(cpu, R_OUTSB);
			break;
			CASE_W(0x6f)	/* OUTSW */
			    if (CPU_IO_Exception(dcr_reg_dx, 2))
				RUNEXCEPTION();
			DoString(cpu, R_OUTSW);
			break;
			CASE_W(0x70)	/* JO */
			    JumpCond16_b(TFLG_O);
			break;
			CASE_W(0x71)	/* JNO */
			    JumpCond16_b(TFLG_NO);
			break;
			CASE_W(0x72)	/* JB */
			    JumpCond16_b(TFLG_B);
			break;
			CASE_W(0x73)	/* JNB */
			    JumpCond16_b(TFLG_NB);
			break;
			CASE_W(0x74)	/* JZ */
			    JumpCond16_b(TFLG_Z);
			break;
			CASE_W(0x75)	/* JNZ */
			    JumpCond16_b(TFLG_NZ);
			break;
			CASE_W(0x76)	/* JBE */
			    JumpCond16_b(TFLG_BE);
			break;
			CASE_W(0x77)	/* JNBE */
			    JumpCond16_b(TFLG_NBE);
			break;
			CASE_W(0x78)	/* JS */
			    JumpCond16_b(TFLG_S);
			break;
			CASE_W(0x79)	/* JNS */
			    JumpCond16_b(TFLG_NS);
			break;
			CASE_W(0x7a)	/* JP */
			    JumpCond16_b(TFLG_P);
			break;
			CASE_W(0x7b)	/* JNP */
			    JumpCond16_b(TFLG_NP);
			break;
			CASE_W(0x7c)	/* JL */
			    JumpCond16_b(TFLG_L);
			break;
			CASE_W(0x7d)	/* JNL */
			    JumpCond16_b(TFLG_NL);
			break;
			CASE_W(0x7e)	/* JLE */
			    JumpCond16_b(TFLG_LE);
			break;
			CASE_W(0x7f)	/* JNLE */
			    JumpCond16_b(TFLG_NLE);
			break;
			CASE_B(0x80)	/* Grpl Eb,Ib */
			    CASE_B(0x82) {	/* Grpl Eb,Ib Mirror instruction */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm >= 0xc0) {
					GetEArb;
					u8 ib = Fetchb(cpu);
					switch (which) {
					case 0x00:
						ADDB(*earb, ib, LoadRb, SaveRb);
						break;
					case 0x01:
						ORB(*earb, ib, LoadRb, SaveRb);
						break;
					case 0x02:
						ADCB(*earb, ib, LoadRb, SaveRb);
						break;
					case 0x03:
						SBBB(*earb, ib, LoadRb, SaveRb);
						break;
					case 0x04:
						ANDB(*earb, ib, LoadRb, SaveRb);
						break;
					case 0x05:
						SUBB(*earb, ib, LoadRb, SaveRb);
						break;
					case 0x06:
						XORB(*earb, ib, LoadRb, SaveRb);
						break;
					case 0x07:
						CMPB(*earb, ib, LoadRb, SaveRb);
						break;
					}
				} else {
					GetEAa;
					u8 ib = Fetchb(cpu);
					switch (which) {
					case 0x00:
						ADDB(eaa, ib, LoadMb, SaveMb);
						break;
					case 0x01:
						ORB(eaa, ib, LoadMb, SaveMb);
						break;
					case 0x02:
						ADCB(eaa, ib, LoadMb, SaveMb);
						break;
					case 0x03:
						SBBB(eaa, ib, LoadMb, SaveMb);
						break;
					case 0x04:
						ANDB(eaa, ib, LoadMb, SaveMb);
						break;
					case 0x05:
						SUBB(eaa, ib, LoadMb, SaveMb);
						break;
					case 0x06:
						XORB(eaa, ib, LoadMb, SaveMb);
						break;
					case 0x07:
						CMPB(eaa, ib, LoadMb, SaveMb);
						break;
					}
				}
				break;
			}
			CASE_W(0x81) {	/* Grpl Ew,Iw */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm >= 0xc0) {
					GetEArw;
					u16 iw = Fetchw(cpu);
					switch (which) {
					case 0x00:
						ADDW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x01:
						ORW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x02:
						ADCW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x03:
						SBBW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x04:
						ANDW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x05:
						SUBW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x06:
						XORW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x07:
						CMPW(*earw, iw, LoadRw, SaveRw);
						break;
					}
				} else {
					GetEAa;
					u16 iw = Fetchw(cpu);
					switch (which) {
					case 0x00:
						ADDW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x01:
						ORW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x02:
						ADCW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x03:
						SBBW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x04:
						ANDW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x05:
						SUBW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x06:
						XORW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x07:
						CMPW(eaa, iw, LoadMw, SaveMw);
						break;
					}
				}
				break;
			}
			CASE_W(0x83) {	/* Grpl Ew,Ix */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm >= 0xc0) {
					GetEArw;
					u16 iw = (s16) Fetchbs(cpu);
					switch (which) {
					case 0x00:
						ADDW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x01:
						ORW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x02:
						ADCW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x03:
						SBBW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x04:
						ANDW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x05:
						SUBW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x06:
						XORW(*earw, iw, LoadRw, SaveRw);
						break;
					case 0x07:
						CMPW(*earw, iw, LoadRw, SaveRw);
						break;
					}
				} else {
					GetEAa;
					u16 iw = (s16) Fetchbs(cpu);
					switch (which) {
					case 0x00:
						ADDW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x01:
						ORW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x02:
						ADCW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x03:
						SBBW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x04:
						ANDW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x05:
						SUBW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x06:
						XORW(eaa, iw, LoadMw, SaveMw);
						break;
					case 0x07:
						CMPW(eaa, iw, LoadMw, SaveMw);
						break;
					}
				}
				break;
			}
			CASE_B(0x84)	/* TEST Eb,Gb */
			    RMEbGb(TESTB);
			break;
			CASE_W(0x85)	/* TEST Ew,Gw */
			    RMEwGw(TESTW);
			break;
			CASE_B(0x86) {	/* XCHG Eb,Gb */
				GetRMrb;
				u8 oldrmrb = *rmrb;
				if (rm >= 0xc0) {
					GetEArb;
					*rmrb = *earb;
					*earb = oldrmrb;
				} else {
					GetEAa;
					*rmrb = LoadMb(eaa);
					SaveMb(eaa, oldrmrb);
				}
				break;
			}
			CASE_W(0x87) {	/* XCHG Ew,Gw */
				GetRMrw;
				u16 oldrmrw = *rmrw;
				if (rm >= 0xc0) {
					GetEArw;
					*rmrw = *earw;
					*earw = oldrmrw;
				} else {
					GetEAa;
					*rmrw = LoadMw(eaa);
					SaveMw(eaa, oldrmrw);
				}
				break;
			}
			CASE_B(0x88) {	/* MOV Eb,Gb */
				GetRMrb;
				if (rm >= 0xc0) {
					GetEArb;
					*earb = *rmrb;
				} else {
					if (cpu->block.pmode) {
						if ((rm == 0x05)
						    && (!cpu->block.code.big)) {
							union dp_cpu_descriptor desc;
							memset(&desc, 0, sizeof(desc));
							dp_cpu_get_gdt_descriptor(cpu,
										  (SegValue
										   (cpu->decoder.core.base_val_ds)),
										  &desc);

							if ((desc.seg.type == DC_DESC_CODE_R_NC_A)
							    || (desc.seg.type == DC_DESC_CODE_R_NC_NA)) {
								CPU_Exception
								    (DC_EXCEPTION_GP,
								     SegValue(cpu->decoder.core.base_val_ds)
								     & 0xfffc);
								continue;
							}
						}
					}
					GetEAa;
					SaveMb(eaa, *rmrb);
				}
				break;
			}
			CASE_W(0x89) {	/* MOV Ew,Gw */
				GetRMrw;
				if (rm >= 0xc0) {
					GetEArw;
					*earw = *rmrw;
				} else {
					GetEAa;
					SaveMw(eaa, *rmrw);
				}
				break;
			}
			CASE_B(0x8a) {	/* MOV Gb,Eb */
				GetRMrb;
				if (rm >= 0xc0) {
					GetEArb;
					*rmrb = *earb;
				} else {
					GetEAa;
					*rmrb = LoadMb(eaa);
				}
				break;
			}
			CASE_W(0x8b) {	/* MOV Gw,Ew */
				GetRMrw;
				if (rm >= 0xc0) {
					GetEArw;
					*rmrw = *earw;
				} else {
					GetEAa;
					*rmrw = LoadMw(eaa);
				}
				break;
			}
			CASE_W(0x8c) {	/* Mov Ew,Sw */
				GetRM;
				u16 val;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* MOV Ew,ES */
					val = SegValue(dp_seg_es);
					break;
				case 0x01:	/* MOV Ew,CS */
					val = SegValue(dp_seg_cs);
					break;
				case 0x02:	/* MOV Ew,SS */
					val = SegValue(dp_seg_ss);
					break;
				case 0x03:	/* MOV Ew,DS */
					val = SegValue(dp_seg_ds);
					break;
				case 0x04:	/* MOV Ew,FS */
					val = SegValue(dp_seg_fs);
					break;
				case 0x05:	/* MOV Ew,GS */
					val = SegValue(dp_seg_gs);
					break;
				default:
					DP_ERR("CPU:8c:Illegal RM Byte");
					goto illegal_opcode;
				}
				if (rm >= 0xc0) {
					GetEArw;
					*earw = val;
				} else {
					GetEAa;
					SaveMw(eaa, val);
				}
				break;
			}
			CASE_W(0x8d) {	/* LEA Gw */
				//Little hack to always use segprefixed version
				BaseDS = BaseSS = 0;
				GetRMrw;
				if (TEST_PREFIX_ADDR) {
					*rmrw = (u16) (*EATable[256 + rm]) (cpu);
				} else {
					*rmrw = (u16) (*EATable[rm]) (cpu);
				}
				break;
			}
			CASE_B(0x8e) {	/* MOV Sw,Ew */
				GetRM;
				u16 val;
				u32 which = (rm >> 3) & 7;
				if (rm >= 0xc0) {
					GetEArw;
					val = *earw;
				} else {
					GetEAa;
					val = LoadMw(eaa);
				}
				switch (which) {
				case 0x02:	/* MOV SS,Ew */
					cpu->decode_count++;	//Always do another instruction
				case 0x00:	/* MOV ES,Ew */
				case 0x03:	/* MOV DS,Ew */
				case 0x05:	/* MOV GS,Ew */
				case 0x04:	/* MOV FS,Ew */
					if (CPU_SetSegGeneral(which, val))
						RUNEXCEPTION();
					break;
				default:
					goto illegal_opcode;
				}
				break;
			}
			CASE_W(0x8f) {	/* POP Ew */
				u16 val = Pop_16();
				GetRM;
				if (rm >= 0xc0) {
					GetEArw;
					*earw = val;
				} else {
					GetEAa;
					SaveMw(eaa, val);
				}
				break;
			}
			CASE_B(0x90)	/* NOP */
			    break;
			CASE_W(0x91) {	/* XCHG CX,AX */
				u16 temp = dcr_reg_ax;
				dcr_reg_ax = dcr_reg_cx;
				dcr_reg_cx = temp;
			}
			break;
			CASE_W(0x92) {	/* XCHG DX,AX */
				u16 temp = dcr_reg_ax;
				dcr_reg_ax = dcr_reg_dx;
				dcr_reg_dx = temp;
			}
			break;
			CASE_W(0x93) {	/* XCHG BX,AX */
				u16 temp = dcr_reg_ax;
				dcr_reg_ax = dcr_reg_bx;
				dcr_reg_bx = temp;
			}
			break;
			CASE_W(0x94) {	/* XCHG SP,AX */
				u16 temp = dcr_reg_ax;
				dcr_reg_ax = dcr_reg_sp;
				dcr_reg_sp = temp;
			}
			break;
			CASE_W(0x95) {	/* XCHG BP,AX */
				u16 temp = dcr_reg_ax;
				dcr_reg_ax = dcr_reg_bp;
				dcr_reg_bp = temp;
			}
			break;
			CASE_W(0x96) {	/* XCHG SI,AX */
				u16 temp = dcr_reg_ax;
				dcr_reg_ax = dcr_reg_si;
				dcr_reg_si = temp;
			}
			break;
			CASE_W(0x97) {	/* XCHG DI,AX */
				u16 temp = dcr_reg_ax;
				dcr_reg_ax = dcr_reg_di;
				dcr_reg_di = temp;
			}
			break;
			CASE_W(0x98)	/* CBW */
			    dcr_reg_ax = (s8) dcr_reg_al;
			break;
			CASE_W(0x99)	/* CWD */
			    if (dcr_reg_ax & 0x8000)
				dcr_reg_dx = 0xffff;
			else
				dcr_reg_dx = 0;
			break;
			CASE_W(0x9a) {	/* CALL Ap */
				FillFlags(cpu);
				u16 newip = Fetchw(cpu);
				u16 newcs = Fetchw(cpu);
				CPU_CALL(DP_FALSE, newcs, newip, GETIP);
#if CPU_TRAP_CHECK
				if (DC_GET_FLAG(TF)) {
					cpu->decoder.func = CPU_Core_Normal_Trap_Run;
					return DP_CALLBACK_NONE;
				}
#endif
				continue;
			}
			CASE_B(0x9b)	/* WAIT */
			    break;	/* No waiting here */
			CASE_W(0x9c)	/* PUSHF */
			    if (CPU_PUSHF(DP_FALSE))
				RUNEXCEPTION();
			break;
			CASE_W(0x9d)	/* POPF */
			    if (CPU_POPF(DP_FALSE))
				RUNEXCEPTION();
#if CPU_TRAP_CHECK
			if (DC_GET_FLAG(TF)) {
				cpu->decoder.func = CPU_Core_Normal_Trap_Run;
				goto decode_end;
			}
#endif
#if	CPU_PIC_CHECK
			if (DC_GET_FLAG(IF) && cpu->pic->irq_check)
				goto decode_end;
#endif
			break;
			CASE_B(0x9e)	/* SAHF */
			    SETFLAGSb(dcr_reg_ah);
			break;
			CASE_B(0x9f)	/* LAHF */
			    FillFlags(cpu);
			dcr_reg_ah = dcr_reg_flags & 0xff;
			break;
			CASE_B(0xa0) {	/* MOV AL,Ob */
				GetEADirect;
				dcr_reg_al = LoadMb(eaa);
			}
			break;
			CASE_W(0xa1) {	/* MOV AX,Ow */
				GetEADirect;
				dcr_reg_ax = LoadMw(eaa);
			}
			break;
			CASE_B(0xa2) {	/* MOV Ob,AL */
				GetEADirect;
				SaveMb(eaa, dcr_reg_al);
			}
			break;
			CASE_W(0xa3) {	/* MOV Ow,AX */
				GetEADirect;
				SaveMw(eaa, dcr_reg_ax);
			}
			break;
			CASE_B(0xa4)	/* MOVSB */
			    DoString(cpu, R_MOVSB);
			break;
			CASE_W(0xa5)	/* MOVSW */
			    DoString(cpu, R_MOVSW);
			break;
			CASE_B(0xa6)	/* CMPSB */
			    DoString(cpu, R_CMPSB);
			break;
			CASE_W(0xa7)	/* CMPSW */
			    DoString(cpu, R_CMPSW);
			break;
			CASE_B(0xa8)	/* TEST AL,Ib */
			    ALIb(TESTB);
			break;
			CASE_W(0xa9)	/* TEST AX,Iw */
			    AXIw(TESTW);
			break;
			CASE_B(0xaa)	/* STOSB */
			    DoString(cpu, R_STOSB);
			break;
			CASE_W(0xab)	/* STOSW */
			    DoString(cpu, R_STOSW);
			break;
			CASE_B(0xac)	/* LODSB */
			    DoString(cpu, R_LODSB);
			break;
			CASE_W(0xad)	/* LODSW */
			    DoString(cpu, R_LODSW);
			break;
			CASE_B(0xae)	/* SCASB */
			    DoString(cpu, R_SCASB);
			break;
			CASE_W(0xaf)	/* SCASW */
			    DoString(cpu, R_SCASW);
			break;
			CASE_B(0xb0)	/* MOV AL,Ib */
			    dcr_reg_al = Fetchb(cpu);
			break;
			CASE_B(0xb1)	/* MOV CL,Ib */
			    dcr_reg_cl = Fetchb(cpu);
			break;
			CASE_B(0xb2)	/* MOV DL,Ib */
			    dcr_reg_dl = Fetchb(cpu);
			break;
			CASE_B(0xb3)	/* MOV BL,Ib */
			    dcr_reg_bl = Fetchb(cpu);
			break;
			CASE_B(0xb4)	/* MOV AH,Ib */
			    dcr_reg_ah = Fetchb(cpu);
			break;
			CASE_B(0xb5)	/* MOV CH,Ib */
			    dcr_reg_ch = Fetchb(cpu);
			break;
			CASE_B(0xb6)	/* MOV DH,Ib */
			    dcr_reg_dh = Fetchb(cpu);
			break;
			CASE_B(0xb7)	/* MOV BH,Ib */
			    dcr_reg_bh = Fetchb(cpu);
			break;
			CASE_W(0xb8)	/* MOV AX,Iw */
			    dcr_reg_ax = Fetchw(cpu);
			break;
			CASE_W(0xb9)	/* MOV CX,Iw */
			    dcr_reg_cx = Fetchw(cpu);
			break;
			CASE_W(0xba)	/* MOV DX,Iw */
			    dcr_reg_dx = Fetchw(cpu);
			break;
			CASE_W(0xbb)	/* MOV BX,Iw */
			    dcr_reg_bx = Fetchw(cpu);
			break;
			CASE_W(0xbc)	/* MOV SP,Iw */
			    dcr_reg_sp = Fetchw(cpu);
			break;
			CASE_W(0xbd)	/* MOV BP.Iw */
			    dcr_reg_bp = Fetchw(cpu);
			break;
			CASE_W(0xbe)	/* MOV SI,Iw */
			    dcr_reg_si = Fetchw(cpu);
			break;
			CASE_W(0xbf)	/* MOV DI,Iw */
			    dcr_reg_di = Fetchw(cpu);
			break;
			CASE_B(0xc0)	/* GRP2 Eb,Ib */
			    GRP2B(Fetchb(cpu));
			break;
			CASE_W(0xc1)	/* GRP2 Ew,Ib */
			    GRP2W(Fetchb(cpu));
			break;
			CASE_W(0xc2)	/* RETN Iw */
			    dcr_reg_eip = Pop_16();
			dcr_reg_esp += Fetchw(cpu);
			continue;
			CASE_W(0xc3)	/* RETN */
			    dcr_reg_eip = Pop_16();
			continue;
			CASE_W(0xc4) {	/* LES */
				GetRMrw;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_es, LoadMw(eaa + 2)))
					RUNEXCEPTION();
				*rmrw = LoadMw(eaa);
				break;
			}
			CASE_W(0xc5) {	/* LDS */
				GetRMrw;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_ds, LoadMw(eaa + 2)))
					RUNEXCEPTION();
				*rmrw = LoadMw(eaa);
				break;
			}
			CASE_B(0xc6) {	/* MOV Eb,Ib */
				GetRM;
				if (rm >= 0xc0) {
					GetEArb;
					*earb = Fetchb(cpu);
				} else {
					GetEAa;
					SaveMb(eaa, Fetchb(cpu));
				}
				break;
			}
			CASE_W(0xc7) {	/* MOV EW,Iw */
				GetRM;
				if (rm >= 0xc0) {
					GetEArw;
					*earw = Fetchw(cpu);
				} else {
					GetEAa;
					SaveMw(eaa, Fetchw(cpu));
				}
				break;
			}
			CASE_W(0xc8) {	/* ENTER Iw,Ib */
				u32 bytes = Fetchw(cpu);
				u32 level = Fetchb(cpu);
				CPU_ENTER(DP_FALSE, bytes, level);
			}
			break;
			CASE_W(0xc9)	/* LEAVE */
			    dcr_reg_esp &= cpu->block.stack.notmask;
			dcr_reg_esp |= (dcr_reg_ebp & cpu->block.stack.mask);
			dcr_reg_bp = Pop_16();
			break;
			CASE_W(0xca) {	/* RETF Iw */
				u32 words = Fetchw(cpu);
				FillFlags(cpu);
				CPU_RET(DP_FALSE, words, GETIP);
				continue;
			}
			CASE_W(0xcb)	/* RETF */
			    FillFlags(cpu);
			CPU_RET(DP_FALSE, 0, GETIP);
			continue;
			CASE_B(0xcc)	/* INT3 */
#if C_DEBUG
			    FillFlags(cpu);
			if (DEBUG_Breakpoint())
				return debugCallback;
#endif
			CPU_SW_Interrupt_NoIOPLCheck(3, GETIP);
#if CPU_TRAP_CHECK
			cpu->block.trap_skip = DP_TRUE;
#endif
			continue;
			CASE_B(0xcd) {	/* INT Ib */
				u8 num = Fetchb(cpu);
#if C_DEBUG
				FillFlags(cpu);
				if (DEBUG_IntBreakpoint(num)) {
					return debugCallback;
				}
#endif
				CPU_SW_Interrupt(num, GETIP);
#if CPU_TRAP_CHECK
				cpu->block.trap_skip = DP_TRUE;
#endif
				continue;
			}
			CASE_B(0xce)	/* INTO */
			    if (get_OF(cpu)) {
				CPU_SW_Interrupt(4, GETIP);
#if CPU_TRAP_CHECK
				cpu->block.trap_skip = DP_TRUE;
#endif
				continue;
			}
			break;
			CASE_W(0xcf) {	/* IRET */
				CPU_IRET(DP_FALSE, GETIP);
#if CPU_TRAP_CHECK
				if (DC_GET_FLAG(TF)) {
					cpu->decoder.func = CPU_Core_Normal_Trap_Run;
					return DP_CALLBACK_NONE;
				}
#endif
#if CPU_PIC_CHECK
				if (DC_GET_FLAG(IF) && cpu->pic->irq_check)
					return DP_CALLBACK_NONE;
#endif
				continue;
			}
			CASE_B(0xd0)	/* GRP2 Eb,1 */
			    GRP2B(1);
			break;
			CASE_W(0xd1)	/* GRP2 Ew,1 */
			    GRP2W(1);
			break;
			CASE_B(0xd2)	/* GRP2 Eb,CL */
			    GRP2B(dcr_reg_cl);
			break;
			CASE_W(0xd3)	/* GRP2 Ew,CL */
			    GRP2W(dcr_reg_cl);
			break;
			CASE_B(0xd4)	/* AAM Ib */
			    AAM(Fetchb(cpu));
			break;
			CASE_B(0xd5)	/* AAD Ib */
			    AAD(Fetchb(cpu));
			break;
			CASE_B(0xd6)	/* SALC */
			    dcr_reg_al = get_CF(cpu) ? 0xFF : 0;
			break;
			CASE_B(0xd7)	/* XLAT */
			    if (TEST_PREFIX_ADDR) {
				dcr_reg_al = LoadMb(BaseDS + (u32) (dcr_reg_ebx + dcr_reg_al));
			} else {
				dcr_reg_al = LoadMb(BaseDS + (u16) (dcr_reg_bx + dcr_reg_al));
			}
			break;
#ifdef CPU_FPU
			CASE_B(0xd8)	/* FPU ESC 0 */
			    FPU_ESC(0);
			break;
			CASE_B(0xd9)	/* FPU ESC 1 */
			    FPU_ESC(1);
			break;
			CASE_B(0xda)	/* FPU ESC 2 */
			    FPU_ESC(2);
			break;
			CASE_B(0xdb)	/* FPU ESC 3 */
			    FPU_ESC(3);
			break;
			CASE_B(0xdc)	/* FPU ESC 4 */
			    FPU_ESC(4);
			break;
			CASE_B(0xdd)	/* FPU ESC 5 */
			    FPU_ESC(5);
			break;
			CASE_B(0xde)	/* FPU ESC 6 */
			    FPU_ESC(6);
			break;
			CASE_B(0xdf)	/* FPU ESC 7 */
			    FPU_ESC(7);
			break;
#else
			CASE_B(0xd8)	/* FPU ESC 0 */
			    CASE_B(0xd9)	/* FPU ESC 1 */
			    CASE_B(0xda)	/* FPU ESC 2 */
			    CASE_B(0xdb)	/* FPU ESC 3 */
			    CASE_B(0xdc)	/* FPU ESC 4 */
			    CASE_B(0xdd)	/* FPU ESC 5 */
			    CASE_B(0xde)	/* FPU ESC 6 */
			    CASE_B(0xdf) {	/* FPU ESC 7 */
				DP_DBG("FPU used");
				u8 rm = Fetchb(cpu);
				if (rm < 0xc0) {
					GetEAa_;
				}
			}
			break;
#endif
			CASE_W(0xe0)	/* LOOPNZ */
			    if (TEST_PREFIX_ADDR) {
				JumpCond16_b(--dcr_reg_ecx && !get_ZF(cpu));
			} else {
				JumpCond16_b(--dcr_reg_cx && !get_ZF(cpu));
			}
			break;
			CASE_W(0xe1)	/* LOOPZ */
			    if (TEST_PREFIX_ADDR) {
				JumpCond16_b(--dcr_reg_ecx && get_ZF(cpu));
			} else {
				JumpCond16_b(--dcr_reg_cx && get_ZF(cpu));
			}
			break;
			CASE_W(0xe2)	/* LOOP */
			    if (TEST_PREFIX_ADDR) {
				JumpCond16_b(--dcr_reg_ecx);
			} else {
				JumpCond16_b(--dcr_reg_cx);
			}
			break;
			CASE_W(0xe3)	/* JCXZ */
			    JumpCond16_b(!(dcr_reg_ecx & AddrMaskTable[cpu->decoder.core.prefixes & PREFIX_ADDR]));
			break;
			CASE_B(0xe4) {	/* IN AL,Ib */
				u32 port = Fetchb(cpu);
				if (CPU_IO_Exception(port, 1))
					RUNEXCEPTION();
				dcr_reg_al = dp_io_readb(cpu->io, port);
				break;
			}
			CASE_W(0xe5) {	/* IN AX,Ib */
				u32 port = Fetchb(cpu);
				if (CPU_IO_Exception(port, 2))
					RUNEXCEPTION();
				dcr_reg_al = dp_io_readw(cpu->io, port);
				break;
			}
			CASE_B(0xe6) {	/* OUT Ib,AL */
				u32 port = Fetchb(cpu);
				if (CPU_IO_Exception(port, 1))
					RUNEXCEPTION();
				dp_io_writeb(cpu->io, port, dcr_reg_al);
				break;
			}
			CASE_W(0xe7) {	/* OUT Ib,AX */
				u32 port = Fetchb(cpu);
				if (CPU_IO_Exception(port, 2))
					RUNEXCEPTION();
				dp_io_writew(cpu->io, port, dcr_reg_ax);
				break;
			}
			CASE_W(0xe8) {	/* CALL Jw */
				u16 addip = Fetchws(cpu);
				SAVEIP;
				Push_16(dcr_reg_eip);
				dcr_reg_eip = (u16) (dcr_reg_eip + addip);
				continue;
			}
			CASE_W(0xe9) {	/* JMP Jw */
				u16 addip = Fetchws(cpu);
				SAVEIP;
				dcr_reg_eip = (u16) (dcr_reg_eip + addip);
				continue;
			}
			CASE_W(0xea) {	/* JMP Ap */
				u16 newip = Fetchw(cpu);
				u16 newcs = Fetchw(cpu);
				FillFlags(cpu);
				CPU_JMP(DP_FALSE, newcs, newip, GETIP);
#if CPU_TRAP_CHECK
				if (DC_GET_FLAG(TF)) {
					cpu->decoder.func = CPU_Core_Normal_Trap_Run;
					return DP_CALLBACK_NONE;
				}
#endif
				continue;
			}
			CASE_W(0xeb) {	/* JMP Jb */
				s16 addip = Fetchbs(cpu);
				SAVEIP;
				dcr_reg_eip = (u16) (dcr_reg_eip + addip);
				continue;
			}
			CASE_B(0xec)	/* IN AL,DX */
			    if (CPU_IO_Exception(dcr_reg_dx, 1))
				RUNEXCEPTION();
			dcr_reg_al = dp_io_readb(cpu->io, dcr_reg_dx);
			break;
			CASE_W(0xed)	/* IN AX,DX */
			    if (CPU_IO_Exception(dcr_reg_dx, 2))
				RUNEXCEPTION();
			dcr_reg_ax = dp_io_readw(cpu->io, dcr_reg_dx);
			break;
			CASE_B(0xee)	/* OUT DX,AL */
			    if (CPU_IO_Exception(dcr_reg_dx, 1))
				RUNEXCEPTION();
			dp_io_writeb(cpu->io, dcr_reg_dx, dcr_reg_al);
			break;
			CASE_W(0xef)	/* OUT DX,AX */
			    if (CPU_IO_Exception(dcr_reg_dx, 2))
				RUNEXCEPTION();
			dp_io_writew(cpu->io, dcr_reg_dx, dcr_reg_ax);
			break;
			CASE_B(0xf0)	/* LOCK */
			    DP_DBG("CPU:LOCK");	/* FIXME: see case D_LOCK in core_full/load.h */
			break;
			CASE_B(0xf1)	/* ICEBP */
			    CPU_SW_Interrupt_NoIOPLCheck(1, GETIP);
#if CPU_TRAP_CHECK
			cpu->block.trap_skip = DP_TRUE;
#endif
			continue;
			CASE_B(0xf2)	/* REPNZ */
			    DO_PREFIX_REP(DP_FALSE);
			break;
			CASE_B(0xf3)	/* REPZ */
			    DO_PREFIX_REP(DP_TRUE);
			break;
			CASE_B(0xf4)	/* HLT */
			    if (cpu->block.pmode && cpu->block.cpl)
				EXCEPTION(DC_EXCEPTION_GP);
			FillFlags(cpu);
			CPU_HLT(GETIP);
			return DP_CALLBACK_NONE;	//Needs to return for hlt cpu core
			CASE_B(0xf5)	/* CMC */
			    FillFlags(cpu);
			DC_SET_FLAGBIT(CF, !(dcr_reg_flags & DC_FLAG_CF));
			break;
			CASE_B(0xf6) {	/* GRP3 Eb(,Ib) */
				GetRM;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* TEST Eb,Ib */
				case 0x01:	/* TEST Eb,Ib Undocumented */
					{
						if (rm >= 0xc0) {
							GetEArb;
						TESTB(*earb, Fetchb(cpu), LoadRb, 0)} else {
							GetEAa;
							TESTB(eaa, Fetchb(cpu), LoadMb, 0);
						}
						break;
					}
				case 0x02:	/* NOT Eb */
					{
						if (rm >= 0xc0) {
							GetEArb;
							*earb = ~*earb;
						} else {
							GetEAa;
							SaveMb(eaa, ~LoadMb(eaa));
						}
						break;
					}
				case 0x03:	/* NEG Eb */
					{
						cpu->decoder.lflags.type = t_NEGb;
						if (rm >= 0xc0) {
							GetEArb;
							lf_var1b = *earb;
							lf_resb = 0 - lf_var1b;
							*earb = lf_resb;
						} else {
							GetEAa;
							lf_var1b = LoadMb(eaa);
							lf_resb = 0 - lf_var1b;
							SaveMb(eaa, lf_resb);
						}
						break;
					}
				case 0x04:	/* MUL AL,Eb */
					RMEb(MULB);
					break;
				case 0x05:	/* IMUL AL,Eb */
					RMEb(IMULB);
					break;
				case 0x06:	/* DIV Eb */
					RMEb(DIVB);
					break;
				case 0x07:	/* IDIV Eb */
					RMEb(IDIVB);
					break;
				}
				break;
			}
			CASE_W(0xf7) {	/* GRP3 Ew(,Iw) */
				GetRM;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* TEST Ew,Iw */
				case 0x01:	/* TEST Ew,Iw Undocumented */
					{
						if (rm >= 0xc0) {
							GetEArw;
							TESTW(*earw, Fetchw(cpu), LoadRw, SaveRw);
						} else {
							GetEAa;
							TESTW(eaa, Fetchw(cpu), LoadMw, SaveMw);
						}
						break;
					}
				case 0x02:	/* NOT Ew */
					{
						if (rm >= 0xc0) {
							GetEArw;
							*earw = ~*earw;
						} else {
							GetEAa;
							SaveMw(eaa, ~LoadMw(eaa));
						}
						break;
					}
				case 0x03:	/* NEG Ew */
					{
						cpu->decoder.lflags.type = t_NEGw;
						if (rm >= 0xc0) {
							GetEArw;
							lf_var1w = *earw;
							lf_resw = 0 - lf_var1w;
							*earw = lf_resw;
						} else {
							GetEAa;
							lf_var1w = LoadMw(eaa);
							lf_resw = 0 - lf_var1w;
							SaveMw(eaa, lf_resw);
						}
						break;
					}
				case 0x04:	/* MUL AX,Ew */
					RMEw(MULW);
					break;
				case 0x05:	/* IMUL AX,Ew */
					RMEw(IMULW)
					    break;
				case 0x06:	/* DIV Ew */
					RMEw(DIVW)
					    break;
				case 0x07:	/* IDIV Ew */
					RMEw(IDIVW)
					    break;
				}
				break;
			}
			CASE_B(0xf8)	/* CLC */
			    FillFlags(cpu);
			DC_SET_FLAGBIT(CF, DP_FALSE);
			break;
			CASE_B(0xf9)	/* STC */
			    FillFlags(cpu);
			DC_SET_FLAGBIT(CF, DP_TRUE);
			break;
			CASE_B(0xfa)	/* CLI */
			    if (CPU_CLI())
				RUNEXCEPTION();
			break;
			CASE_B(0xfb)	/* STI */
			    if (CPU_STI())
				RUNEXCEPTION();
#if CPU_PIC_CHECK
			if (DC_GET_FLAG(IF) && cpu->pic->irq_check)
				goto decode_end;
#endif
			break;
			CASE_B(0xfc)	/* CLD */
			    DC_SET_FLAGBIT(DF, DP_FALSE);
			cpu->block.direction = 1;
			break;
			CASE_B(0xfd)	/* STD */
			    DC_SET_FLAGBIT(DF, DP_TRUE);
			cpu->block.direction = -1;
			break;
			CASE_B(0xfe) {	/* GRP4 Eb */
				GetRM;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* INC Eb */
					RMEb(INCB);
					break;
				case 0x01:	/* DEC Eb */
					RMEb(DECB);
					break;
				case 0x07:	/* CallBack */
					{
						u32 cb = Fetchw(cpu);
						FillFlags(cpu);
						SAVEIP;
						return cb;
					}
				default:
					E_Exit("Illegal GRP4 Call %d", (rm >> 3) & 7);
					break;
				}
				break;
			}
			CASE_W(0xff) {	/* GRP5 Ew */
				GetRM;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* INC Ew */
					RMEw(INCW);
					break;
				case 0x01:	/* DEC Ew */
					RMEw(DECW);
					break;
				case 0x02:	/* CALL Ev */
					if (rm >= 0xc0) {
						GetEArw;
						dcr_reg_eip = *earw;
					} else {
						GetEAa;
						dcr_reg_eip = LoadMw(eaa);
					}
					Push_16(GETIP);
					continue;
				case 0x03:	/* CALL Ep */
					{
						if (rm >= 0xc0)
							goto illegal_opcode;
						GetEAa;
						u16 newip = LoadMw(eaa);
						u16 newcs = LoadMw(eaa + 2);
						FillFlags(cpu);
						CPU_CALL(DP_FALSE, newcs, newip, GETIP);
#if CPU_TRAP_CHECK
						if (DC_GET_FLAG(TF)) {
							cpu->decoder.func = CPU_Core_Normal_Trap_Run;
							return DP_CALLBACK_NONE;
						}
#endif
						continue;
					}
					break;
				case 0x04:	/* JMP Ev */
					if (rm >= 0xc0) {
						GetEArw;
						dcr_reg_eip = *earw;
					} else {
						GetEAa;
						dcr_reg_eip = LoadMw(eaa);
					}
					continue;
				case 0x05:	/* JMP Ep */
					{
						if (rm >= 0xc0)
							goto illegal_opcode;
						GetEAa;
						u16 newip = LoadMw(eaa);
						u16 newcs = LoadMw(eaa + 2);
						FillFlags(cpu);
						CPU_JMP(DP_FALSE, newcs, newip, GETIP);
#if CPU_TRAP_CHECK
						if (DC_GET_FLAG(TF)) {
							cpu->decoder.func = CPU_Core_Normal_Trap_Run;
							return DP_CALLBACK_NONE;
						}
#endif
						continue;
					}
					break;
				case 0x06:	/* PUSH Ev */
					if (rm >= 0xc0) {
						GetEArw;
						Push_16(*earw);
					} else {
						GetEAa;
						Push_16(LoadMw(eaa));
					}
					break;
				default:
					DP_ERR("CPU:GRP5:Illegal Call %2X", which);
					goto illegal_opcode;
				}
				break;
			}

			CASE_0F_W(0x00) {	/* GRP 6 Exxx */
				if ((dcr_reg_flags & DC_FLAG_VM) || (!cpu->block.pmode))
					goto illegal_opcode;
				GetRM;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* SLDT */
				case 0x01:	/* STR */
					{
						u32 saveval;
						if (!which)
							saveval = CPU_SLDT();
						else
							saveval = CPU_STR();
						if (rm >= 0xc0) {
							GetEArw;
							*earw = saveval;
						} else {
							GetEAa;
							SaveMw(eaa, saveval);
						}
					}
					break;
				case 0x02:
				case 0x03:
				case 0x04:
				case 0x05:
					{
						u32 loadval;
						if (rm >= 0xc0) {
							GetEArw;
							loadval = *earw;
						} else {
							GetEAa;
							loadval = LoadMw(eaa);
						}
						switch (which) {
						case 0x02:
							if (cpu->block.cpl)
								EXCEPTION(DC_EXCEPTION_GP);
							if (CPU_LLDT(loadval))
								RUNEXCEPTION();
							break;
						case 0x03:
							if (cpu->block.cpl)
								EXCEPTION(DC_EXCEPTION_GP);
							if (CPU_LTR(loadval))
								RUNEXCEPTION();
							break;
						case 0x04:
							CPU_VERR(loadval);
							break;
						case 0x05:
							CPU_VERW(loadval);
							break;
						}
					}
					break;
				default:
					goto illegal_opcode;
				}
			}
			break;
			CASE_0F_W(0x01) {	/* Group 7 Ew */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm < 0xc0) {	//First ones all use EA
					GetEAa;
					u32 limit;
					switch (which) {
					case 0x00:	/* SGDT */
						SaveMw(eaa, CPU_SGDT_limit());
						SaveMd(eaa + 2, CPU_SGDT_base());
						break;
					case 0x01:	/* SIDT */
						SaveMw(eaa, CPU_SIDT_limit());
						SaveMd(eaa + 2, CPU_SIDT_base());
						break;
					case 0x02:	/* LGDT */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						CPU_LGDT(LoadMw(eaa), LoadMd(eaa + 2) & 0xFFFFFF);
						break;
					case 0x03:	/* LIDT */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						CPU_LIDT(LoadMw(eaa), LoadMd(eaa + 2) & 0xFFFFFF);
						break;
					case 0x04:	/* SMSW */
						SaveMw(eaa, CPU_SMSW());
						break;
					case 0x06:	/* LMSW */
						limit = LoadMw(eaa);
						if (CPU_LMSW(limit))
							RUNEXCEPTION();
						break;
					case 0x07:	/* INVLPG */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						dp_paging_clear_tlb(cpu->paging);
						break;
					}
				} else {
					GetEArw;
					switch (which) {
					case 0x02:	/* LGDT */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						goto illegal_opcode;
					case 0x03:	/* LIDT */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						goto illegal_opcode;
					case 0x04:	/* SMSW */
						*earw = CPU_SMSW();
						break;
					case 0x06:	/* LMSW */
						if (CPU_LMSW(*earw))
							RUNEXCEPTION();
						break;
					default:
						goto illegal_opcode;
					}
				}
			}
			break;
			CASE_0F_W(0x02) {	/* LAR Gw,Ew */
				if ((dcr_reg_flags & DC_FLAG_VM) || (!cpu->block.pmode))
					goto illegal_opcode;
				GetRMrw;
				u32 ar = *rmrw;
				if (rm >= 0xc0) {
					GetEArw;
					CPU_LAR(*earw, &ar);
				} else {
					GetEAa;
					CPU_LAR(LoadMw(eaa), &ar);
				}
				*rmrw = (u16) ar;
			}
			break;
			CASE_0F_W(0x03) {	/* LSL Gw,Ew */
				if ((dcr_reg_flags & DC_FLAG_VM) || (!cpu->block.pmode))
					goto illegal_opcode;
				GetRMrw;
				u32 limit = *rmrw;
				if (rm >= 0xc0) {
					GetEArw;
					CPU_LSL(*earw, &limit);
				} else {
					GetEAa;
					CPU_LSL(LoadMw(eaa), &limit);
				}
				*rmrw = (u16) limit;
			}
			break;
			CASE_0F_B(0x06)	/* CLTS */
			    if (cpu->block.pmode && cpu->block.cpl)
				EXCEPTION(DC_EXCEPTION_GP);
			cpu->block.cr0 &= (~DC_CR0_TASKSWITCH);
			break;
			CASE_0F_B(0x08)	/* INVD */
			    CASE_0F_B(0x09)	/* WBINVD */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			if (cpu->block.pmode && cpu->block.cpl)
				EXCEPTION(DC_EXCEPTION_GP);
			break;
			CASE_0F_B(0x20) {	/* MOV Rd.CRx */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm < 0xc0) {
					rm |= 0xc0;
					DP_ERR("MOV XXX,CR%u with non-register", which);
				}
				GetEArd;
				u32 crx_value;
				if (CPU_READ_CRX(which, &crx_value))
					RUNEXCEPTION();
				*eard = crx_value;
			}
			break;
			CASE_0F_B(0x21) {	/* MOV Rd,DRx */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm < 0xc0) {
					rm |= 0xc0;
					DP_ERR("MOV XXX,DR%u with non-register", which);
				}
				GetEArd;
				u32 drx_value;
				if (CPU_READ_DRX(which, &drx_value))
					RUNEXCEPTION();
				*eard = drx_value;
			}
			break;
			CASE_0F_B(0x22) {	/* MOV CRx,Rd */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm < 0xc0) {
					rm |= 0xc0;
					DP_ERR("MOV XXX,CR%u with non-register", which);
				}
				GetEArd;
				if (CPU_WRITE_CRX(which, *eard))
					RUNEXCEPTION();
			}
			break;
			CASE_0F_B(0x23) {	/* MOV DRx,Rd */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm < 0xc0) {
					rm |= 0xc0;
					DP_ERR("MOV DR%u,XXX with non-register", which);
				}
				GetEArd;
				if (CPU_WRITE_DRX(which, *eard))
					RUNEXCEPTION();
			}
			break;
			CASE_0F_B(0x24) {	/* MOV Rd,TRx */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm < 0xc0) {
					rm |= 0xc0;
					DP_ERR("MOV XXX,TR%u with non-register", which);
				}
				GetEArd;
				u32 trx_value;
				if (CPU_READ_TRX(which, &trx_value))
					RUNEXCEPTION();
				*eard = trx_value;
			}
			break;
			CASE_0F_B(0x26) {	/* MOV TRx,Rd */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm < 0xc0) {
					rm |= 0xc0;
					DP_ERR("MOV TR%u,XXX with non-register", which);
				}
				GetEArd;
				if (CPU_WRITE_TRX(which, *eard))
					RUNEXCEPTION();
			}
			break;
			CASE_0F_B(0x31) {	/* RDTSC */
				if (cpu->arch < DP_CPU_ARCHTYPE_PENTIUMSLOW)
					goto illegal_opcode;
				s64 tsc = (s64) (cpu->timetrack->ticks);
				dcr_reg_edx = (u32) (tsc >> 32);
				dcr_reg_eax = (u32) (tsc & 0xffffffff);
			}
			break;
			CASE_0F_W(0x80)	/* JO */
			    JumpCond16_w(TFLG_O);
			break;
			CASE_0F_W(0x81)	/* JNO */
			    JumpCond16_w(TFLG_NO);
			break;
			CASE_0F_W(0x82)	/* JB */
			    JumpCond16_w(TFLG_B);
			break;
			CASE_0F_W(0x83)	/* JNB */
			    JumpCond16_w(TFLG_NB);
			break;
			CASE_0F_W(0x84)	/* JZ */
			    JumpCond16_w(TFLG_Z);
			break;
			CASE_0F_W(0x85)	/* JNZ */
			    JumpCond16_w(TFLG_NZ);
			break;
			CASE_0F_W(0x86)	/* JBE */
			    JumpCond16_w(TFLG_BE);
			break;
			CASE_0F_W(0x87)	/* JNBE */
			    JumpCond16_w(TFLG_NBE);
			break;
			CASE_0F_W(0x88)	/* JS */
			    JumpCond16_w(TFLG_S);
			break;
			CASE_0F_W(0x89)	/* JNS */
			    JumpCond16_w(TFLG_NS);
			break;
			CASE_0F_W(0x8a)	/* JP */
			    JumpCond16_w(TFLG_P);
			break;
			CASE_0F_W(0x8b)	/* JNP */
			    JumpCond16_w(TFLG_NP);
			break;
			CASE_0F_W(0x8c)	/* JL */
			    JumpCond16_w(TFLG_L);
			break;
			CASE_0F_W(0x8d)	/* JNL */
			    JumpCond16_w(TFLG_NL);
			break;
			CASE_0F_W(0x8e)	/* JLE */
			    JumpCond16_w(TFLG_LE);
			break;
			CASE_0F_W(0x8f)	/* JNLE */
			    JumpCond16_w(TFLG_NLE);
			break;
			CASE_0F_B(0x90)	/* SETO */
			    SETcc(TFLG_O);
			break;
			CASE_0F_B(0x91)	/* SETNO */
			    SETcc(TFLG_NO);
			break;
			CASE_0F_B(0x92)	/* SETB */
			    SETcc(TFLG_B);
			break;
			CASE_0F_B(0x93)	/* SETNB */
			    SETcc(TFLG_NB);
			break;
			CASE_0F_B(0x94)	/* SETZ */
			    SETcc(TFLG_Z);
			break;
			CASE_0F_B(0x95)	/* SETNZ */
			    SETcc(TFLG_NZ);
			break;
			CASE_0F_B(0x96)	/* SETBE */
			    SETcc(TFLG_BE);
			break;
			CASE_0F_B(0x97)	/* SETNBE */
			    SETcc(TFLG_NBE);
			break;
			CASE_0F_B(0x98)	/* SETS */
			    SETcc(TFLG_S);
			break;
			CASE_0F_B(0x99)	/* SETNS */
			    SETcc(TFLG_NS);
			break;
			CASE_0F_B(0x9a)	/* SETP */
			    SETcc(TFLG_P);
			break;
			CASE_0F_B(0x9b)	/* SETNP */
			    SETcc(TFLG_NP);
			break;
			CASE_0F_B(0x9c)	/* SETL */
			    SETcc(TFLG_L);
			break;
			CASE_0F_B(0x9d)	/* SETNL */
			    SETcc(TFLG_NL);
			break;
			CASE_0F_B(0x9e)	/* SETLE */
			    SETcc(TFLG_LE);
			break;
			CASE_0F_B(0x9f)	/* SETNLE */
			    SETcc(TFLG_NLE);
			break;

			CASE_0F_W(0xa0)	/* PUSH FS */
			    Push_16(SegValue(dp_seg_fs));
			break;
			CASE_0F_W(0xa1)	/* POP FS */
			    if (CPU_PopSeg(dp_seg_fs, DP_FALSE))
				RUNEXCEPTION();
			break;
			CASE_0F_B(0xa2)	/* CPUID */
			    if (!CPU_CPUID())
				goto illegal_opcode;
			break;
			CASE_0F_W(0xa3) {	/* BT Ew,Gw */
				FillFlags(cpu);
				GetRMrw;
				u16 mask = 1 << (*rmrw & 15);
				if (rm >= 0xc0) {
					GetEArw;
					DC_SET_FLAGBIT(CF, (*earw & mask));
				} else {
					GetEAa;
					eaa += (((s16) * rmrw) >> 4) * 2;
					u16 old = LoadMw(eaa);
					DC_SET_FLAGBIT(CF, (old & mask));
				}
				break;
			}
			CASE_0F_W(0xa4)	/* SHLD Ew,Gw,Ib */
			    RMEwGwOp3(DSHLW, Fetchb(cpu));
			break;
			CASE_0F_W(0xa5)	/* SHLD Ew,Gw,CL */
			    RMEwGwOp3(DSHLW, dcr_reg_cl);
			break;
			CASE_0F_W(0xa8)	/* PUSH GS */
			    Push_16(SegValue(dp_seg_gs));
			break;
			CASE_0F_W(0xa9)	/* POP GS */
			    if (CPU_PopSeg(dp_seg_gs, DP_FALSE))
				RUNEXCEPTION();
			break;
			CASE_0F_W(0xab) {	/* BTS Ew,Gw */
				FillFlags(cpu);
				GetRMrw;
				u16 mask = 1 << (*rmrw & 15);
				if (rm >= 0xc0) {
					GetEArw;
					DC_SET_FLAGBIT(CF, (*earw & mask));
					*earw |= mask;
				} else {
					GetEAa;
					eaa += (((s16) * rmrw) >> 4) * 2;
					u16 old = LoadMw(eaa);
					DC_SET_FLAGBIT(CF, (old & mask));
					SaveMw(eaa, old | mask);
				}
				break;
			}
			CASE_0F_W(0xac)	/* SHRD Ew,Gw,Ib */
			    RMEwGwOp3(DSHRW, Fetchb(cpu));
			break;
			CASE_0F_W(0xad)	/* SHRD Ew,Gw,CL */
			    RMEwGwOp3(DSHRW, dcr_reg_cl);
			break;
			CASE_0F_W(0xaf)	/* IMUL Gw,Ew */
			    RMGwEwOp3(DIMULW, *rmrw);
			break;
			CASE_0F_B(0xb0) {	/* cmpxchg Eb,Gb */
				if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
					goto illegal_opcode;
				FillFlags(cpu);
				GetRMrb;
				if (rm >= 0xc0) {
					GetEArb;
					if (dcr_reg_al == *earb) {
						*earb = *rmrb;
						DC_SET_FLAGBIT(ZF, 1);
					} else {
						dcr_reg_al = *earb;
						DC_SET_FLAGBIT(ZF, 0);
					}
				} else {
					GetEAa;
					u8 val = LoadMb(eaa);
					if (dcr_reg_al == val) {
						SaveMb(eaa, *rmrb);
						DC_SET_FLAGBIT(ZF, 1);
					} else {
						SaveMb(eaa, val);	// cmpxchg always issues a write
						dcr_reg_al = val;
						DC_SET_FLAGBIT(ZF, 0);
					}
				}
				break;
			}
			CASE_0F_W(0xb1) {	/* cmpxchg Ew,Gw */
				if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
					goto illegal_opcode;
				FillFlags(cpu);
				GetRMrw;
				if (rm >= 0xc0) {
					GetEArw;
					if (dcr_reg_ax == *earw) {
						*earw = *rmrw;
						DC_SET_FLAGBIT(ZF, 1);
					} else {
						dcr_reg_ax = *earw;
						DC_SET_FLAGBIT(ZF, 0);
					}
				} else {
					GetEAa;
					u16 val = LoadMw(eaa);
					if (dcr_reg_ax == val) {
						SaveMw(eaa, *rmrw);
						DC_SET_FLAGBIT(ZF, 1);
					} else {
						SaveMw(eaa, val);	// cmpxchg always issues a write
						dcr_reg_ax = val;
						DC_SET_FLAGBIT(ZF, 0);
					}
				}
				break;
			}

			CASE_0F_W(0xb2) {	/* LSS Ew */
				GetRMrw;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_ss, LoadMw(eaa + 2)))
					RUNEXCEPTION();
				*rmrw = LoadMw(eaa);
				break;
			}
			CASE_0F_W(0xb3) {	/* BTR Ew,Gw */
				FillFlags(cpu);
				GetRMrw;
				u16 mask = 1 << (*rmrw & 15);
				if (rm >= 0xc0) {
					GetEArw;
					DC_SET_FLAGBIT(CF, (*earw & mask));
					*earw &= ~mask;
				} else {
					GetEAa;
					eaa += (((s16) * rmrw) >> 4) * 2;
					u16 old = LoadMw(eaa);
					DC_SET_FLAGBIT(CF, (old & mask));
					SaveMw(eaa, old & ~mask);
				}
				break;
			}
			CASE_0F_W(0xb4) {	/* LFS Ew */
				GetRMrw;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_fs, LoadMw(eaa + 2)))
					RUNEXCEPTION();
				*rmrw = LoadMw(eaa);
				break;
			}
			CASE_0F_W(0xb5) {	/* LGS Ew */
				GetRMrw;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_gs, LoadMw(eaa + 2)))
					RUNEXCEPTION();
				*rmrw = LoadMw(eaa);
				break;
			}
			CASE_0F_W(0xb6) {	/* MOVZX Gw,Eb */
				GetRMrw;
				if (rm >= 0xc0) {
					GetEArb;
					*rmrw = *earb;
				} else {
					GetEAa;
					*rmrw = LoadMb(eaa);
				}
				break;
			}
			CASE_0F_W(0xb7)	/* MOVZX Gw,Ew */
			    CASE_0F_W(0xbf) {	/* MOVSX Gw,Ew */
				GetRMrw;
				if (rm >= 0xc0) {
					GetEArw;
					*rmrw = *earw;
				} else {
					GetEAa;
					*rmrw = LoadMw(eaa);
				}
				break;
			}
			CASE_0F_W(0xba) {	/* GRP8 Ew,Ib */
				FillFlags(cpu);
				GetRM;
				if (rm >= 0xc0) {
					GetEArw;
					u16 mask = 1 << (Fetchb(cpu) & 15);
					DC_SET_FLAGBIT(CF, (*earw & mask));
					switch (rm & 0x38) {
					case 0x20:	/* BT */
						break;
					case 0x28:	/* BTS */
						*earw |= mask;
						break;
					case 0x30:	/* BTR */
						*earw &= ~mask;
						break;
					case 0x38:	/* BTC */
						*earw ^= mask;
						break;
					default:
						E_Exit("CPU:0F:BA:Illegal subfunction %X", rm & 0x38);
					}
				} else {
					GetEAa;
					u16 old = LoadMw(eaa);
					u16 mask = 1 << (Fetchb(cpu) & 15);
					DC_SET_FLAGBIT(CF, (old & mask));
					switch (rm & 0x38) {
					case 0x20:	/* BT */
						break;
					case 0x28:	/* BTS */
						SaveMw(eaa, old | mask);
						break;
					case 0x30:	/* BTR */
						SaveMw(eaa, old & ~mask);
						break;
					case 0x38:	/* BTC */
						SaveMw(eaa, old ^ mask);
						break;
					default:
						E_Exit("CPU:0F:BA:Illegal subfunction %X", rm & 0x38);
					}
				}
				break;
			}
			CASE_0F_W(0xbb) {	/* BTC Ew,Gw */
				FillFlags(cpu);
				GetRMrw;
				u16 mask = 1 << (*rmrw & 15);
				if (rm >= 0xc0) {
					GetEArw;
					DC_SET_FLAGBIT(CF, (*earw & mask));
					*earw ^= mask;
				} else {
					GetEAa;
					eaa += (((s16) * rmrw) >> 4) * 2;
					u16 old = LoadMw(eaa);
					DC_SET_FLAGBIT(CF, (old & mask));
					SaveMw(eaa, old ^ mask);
				}
				break;
			}
			CASE_0F_W(0xbc) {	/* BSF Gw,Ew */
				GetRMrw;
				u16 result, value;
				if (rm >= 0xc0) {
					GetEArw;
					value = *earw;
				} else {
					GetEAa;
					value = LoadMw(eaa);
				}
				if (value == 0) {
					DC_SET_FLAGBIT(ZF, DP_TRUE);
				} else {
					result = 0;
					while ((value & 0x01) == 0) {
						result++;
						value >>= 1;
					}
					DC_SET_FLAGBIT(ZF, DP_FALSE);
					*rmrw = result;
				}
				cpu->decoder.lflags.type = t_UNKNOWN;
				break;
			}
			CASE_0F_W(0xbd) {	/* BSR Gw,Ew */
				GetRMrw;
				u16 result, value;
				if (rm >= 0xc0) {
					GetEArw;
					value = *earw;
				} else {
					GetEAa;
					value = LoadMw(eaa);
				}
				if (value == 0) {
					DC_SET_FLAGBIT(ZF, DP_TRUE);
				} else {
					result = 15;	// Operandsize-1
					while ((value & 0x8000) == 0) {
						result--;
						value <<= 1;
					}
					DC_SET_FLAGBIT(ZF, DP_FALSE);
					*rmrw = result;
				}
				cpu->decoder.lflags.type = t_UNKNOWN;
				break;
			}
			CASE_0F_W(0xbe) {	/* MOVSX Gw,Eb */
				GetRMrw;
				if (rm >= 0xc0) {
					GetEArb;
					*rmrw = *(s8 *) earb;
				} else {
					GetEAa;
					*rmrw = LoadMbs(eaa);
				}
				break;
			}
			CASE_0F_B(0xc0) {	/* XADD Gb,Eb */
				if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
					goto illegal_opcode;
				GetRMrb;
				u8 oldrmrb = *rmrb;
				if (rm >= 0xc0) {
					GetEArb;
					*rmrb = *earb;
					*earb += oldrmrb;
				} else {
					GetEAa;
					*rmrb = LoadMb(eaa);
					SaveMb(eaa, LoadMb(eaa) + oldrmrb);
				}
				break;
			}
			CASE_0F_W(0xc1) {	/* XADD Gw,Ew */
				if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
					goto illegal_opcode;
				GetRMrw;
				u16 oldrmrw = *rmrw;
				if (rm >= 0xc0) {
					GetEArw;
					*rmrw = *earw;
					*earw += oldrmrw;
				} else {
					GetEAa;
					*rmrw = LoadMw(eaa);
					SaveMw(eaa, LoadMw(eaa) + oldrmrw);
				}
				break;
			}
			CASE_0F_W(0xc8)	/* BSWAP AX */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPW(dcr_reg_ax);
			break;
			CASE_0F_W(0xc9)	/* BSWAP CX */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPW(dcr_reg_cx);
			break;
			CASE_0F_W(0xca)	/* BSWAP DX */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPW(dcr_reg_dx);
			break;
			CASE_0F_W(0xcb)	/* BSWAP BX */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPW(dcr_reg_bx);
			break;
			CASE_0F_W(0xcc)	/* BSWAP SP */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPW(dcr_reg_sp);
			break;
			CASE_0F_W(0xcd)	/* BSWAP BP */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPW(dcr_reg_bp);
			break;
			CASE_0F_W(0xce)	/* BSWAP SI */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPW(dcr_reg_si);
			break;
			CASE_0F_W(0xcf)	/* BSWAP DI */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPW(dcr_reg_di);
			break;

			CASE_D(0x01)	/* ADD Ed,Gd */
			    RMEdGd(ADDD);
			break;
			CASE_D(0x03)	/* ADD Gd,Ed */
			    RMGdEd(ADDD);
			break;
			CASE_D(0x05)	/* ADD EAX,Id */
			    EAXId(ADDD);
			break;
			CASE_D(0x06)	/* PUSH ES */
			    Push_32(SegValue(dp_seg_es));
			break;
			CASE_D(0x07)	/* POP ES */
			    if (CPU_PopSeg(dp_seg_es, DP_TRUE))
				RUNEXCEPTION();
			break;
			CASE_D(0x09)	/* OR Ed,Gd */
			    RMEdGd(ORD);
			break;
			CASE_D(0x0b)	/* OR Gd,Ed */
			    RMGdEd(ORD);
			break;
			CASE_D(0x0d)	/* OR EAX,Id */
			    EAXId(ORD);
			break;
			CASE_D(0x0e)	/* PUSH CS */
			    Push_32(SegValue(dp_seg_cs));
			break;
			CASE_D(0x11)	/* ADC Ed,Gd */
			    RMEdGd(ADCD);
			break;
			CASE_D(0x13)	/* ADC Gd,Ed */
			    RMGdEd(ADCD);
			break;
			CASE_D(0x15)	/* ADC EAX,Id */
			    EAXId(ADCD);
			break;
			CASE_D(0x16)	/* PUSH SS */
			    Push_32(SegValue(dp_seg_ss));
			break;
			CASE_D(0x17)	/* POP SS */
			    if (CPU_PopSeg(dp_seg_ss, DP_TRUE))
				RUNEXCEPTION();
			cpu->decode_count++;
			break;
			CASE_D(0x19)	/* SBB Ed,Gd */
			    RMEdGd(SBBD);
			break;
			CASE_D(0x1b)	/* SBB Gd,Ed */
			    RMGdEd(SBBD);
			break;
			CASE_D(0x1d)	/* SBB EAX,Id */
			    EAXId(SBBD);
			break;
			CASE_D(0x1e)	/* PUSH DS */
			    Push_32(SegValue(dp_seg_ds));
			break;
			CASE_D(0x1f)	/* POP DS */
			    if (CPU_PopSeg(dp_seg_ds, DP_TRUE))
				RUNEXCEPTION();
			break;
			CASE_D(0x21)	/* AND Ed,Gd */
			    RMEdGd(ANDD);
			break;
			CASE_D(0x23)	/* AND Gd,Ed */
			    RMGdEd(ANDD);
			break;
			CASE_D(0x25)	/* AND EAX,Id */
			    EAXId(ANDD);
			break;
			CASE_D(0x29)	/* SUB Ed,Gd */
			    RMEdGd(SUBD);
			break;
			CASE_D(0x2b)	/* SUB Gd,Ed */
			    RMGdEd(SUBD);
			break;
			CASE_D(0x2d)	/* SUB EAX,Id */
			    EAXId(SUBD);
			break;
			CASE_D(0x31)	/* XOR Ed,Gd */
			    RMEdGd(XORD);
			break;
			CASE_D(0x33)	/* XOR Gd,Ed */
			    RMGdEd(XORD);
			break;
			CASE_D(0x35)	/* XOR EAX,Id */
			    EAXId(XORD);
			break;
			CASE_D(0x39)	/* CMP Ed,Gd */
			    RMEdGd(CMPD);
			break;
			CASE_D(0x3b)	/* CMP Gd,Ed */
			    RMGdEd(CMPD);
			break;
			CASE_D(0x3d)	/* CMP EAX,Id */
			    EAXId(CMPD);
			break;
			CASE_D(0x40)	/* INC EAX */
			    INCD(dcr_reg_eax, LoadRd, SaveRd);
			break;
			CASE_D(0x41)	/* INC ECX */
			    INCD(dcr_reg_ecx, LoadRd, SaveRd);
			break;
			CASE_D(0x42)	/* INC EDX */
			    INCD(dcr_reg_edx, LoadRd, SaveRd);
			break;
			CASE_D(0x43)	/* INC EBX */
			    INCD(dcr_reg_ebx, LoadRd, SaveRd);
			break;
			CASE_D(0x44)	/* INC ESP */
			    INCD(dcr_reg_esp, LoadRd, SaveRd);
			break;
			CASE_D(0x45)	/* INC EBP */
			    INCD(dcr_reg_ebp, LoadRd, SaveRd);
			break;
			CASE_D(0x46)	/* INC ESI */
			    INCD(dcr_reg_esi, LoadRd, SaveRd);
			break;
			CASE_D(0x47)	/* INC EDI */
			    INCD(dcr_reg_edi, LoadRd, SaveRd);
			break;
			CASE_D(0x48)	/* DEC EAX */
			    DECD(dcr_reg_eax, LoadRd, SaveRd);
			break;
			CASE_D(0x49)	/* DEC ECX */
			    DECD(dcr_reg_ecx, LoadRd, SaveRd);
			break;
			CASE_D(0x4a)	/* DEC EDX */
			    DECD(dcr_reg_edx, LoadRd, SaveRd);
			break;
			CASE_D(0x4b)	/* DEC EBX */
			    DECD(dcr_reg_ebx, LoadRd, SaveRd);
			break;
			CASE_D(0x4c)	/* DEC ESP */
			    DECD(dcr_reg_esp, LoadRd, SaveRd);
			break;
			CASE_D(0x4d)	/* DEC EBP */
			    DECD(dcr_reg_ebp, LoadRd, SaveRd);
			break;
			CASE_D(0x4e)	/* DEC ESI */
			    DECD(dcr_reg_esi, LoadRd, SaveRd);
			break;
			CASE_D(0x4f)	/* DEC EDI */
			    DECD(dcr_reg_edi, LoadRd, SaveRd);
			break;
			CASE_D(0x50)	/* PUSH EAX */
			    Push_32(dcr_reg_eax);
			break;
			CASE_D(0x51)	/* PUSH ECX */
			    Push_32(dcr_reg_ecx);
			break;
			CASE_D(0x52)	/* PUSH EDX */
			    Push_32(dcr_reg_edx);
			break;
			CASE_D(0x53)	/* PUSH EBX */
			    Push_32(dcr_reg_ebx);
			break;
			CASE_D(0x54)	/* PUSH ESP */
			    Push_32(dcr_reg_esp);
			break;
			CASE_D(0x55)	/* PUSH EBP */
			    Push_32(dcr_reg_ebp);
			break;
			CASE_D(0x56)	/* PUSH ESI */
			    Push_32(dcr_reg_esi);
			break;
			CASE_D(0x57)	/* PUSH EDI */
			    Push_32(dcr_reg_edi);
			break;
			CASE_D(0x58)	/* POP EAX */
			    dcr_reg_eax = Pop_32();
			break;
			CASE_D(0x59)	/* POP ECX */
			    dcr_reg_ecx = Pop_32();
			break;
			CASE_D(0x5a)	/* POP EDX */
			    dcr_reg_edx = Pop_32();
			break;
			CASE_D(0x5b)	/* POP EBX */
			    dcr_reg_ebx = Pop_32();
			break;
			CASE_D(0x5c)	/* POP ESP */
			    dcr_reg_esp = Pop_32();
			break;
			CASE_D(0x5d)	/* POP EBP */
			    dcr_reg_ebp = Pop_32();
			break;
			CASE_D(0x5e)	/* POP ESI */
			    dcr_reg_esi = Pop_32();
			break;
			CASE_D(0x5f)	/* POP EDI */
			    dcr_reg_edi = Pop_32();
			break;
			CASE_D(0x60) {	/* PUSHAD */
				u32 tmpesp = dcr_reg_esp;
				Push_32(dcr_reg_eax);
				Push_32(dcr_reg_ecx);
				Push_32(dcr_reg_edx);
				Push_32(dcr_reg_ebx);
				Push_32(tmpesp);
				Push_32(dcr_reg_ebp);
				Push_32(dcr_reg_esi);
				Push_32(dcr_reg_edi);
			};
			break;
			CASE_D(0x61)	/* POPAD */
			    dcr_reg_edi = Pop_32();
			dcr_reg_esi = Pop_32();
			dcr_reg_ebp = Pop_32();
			Pop_32();	//Don't save ESP
			dcr_reg_ebx = Pop_32();
			dcr_reg_edx = Pop_32();
			dcr_reg_ecx = Pop_32();
			dcr_reg_eax = Pop_32();
			break;
			CASE_D(0x62) {	/* BOUND Ed */
				s32 bound_min, bound_max;
				GetRMrd;
				GetEAa;
				bound_min = LoadMd(eaa);
				bound_max = LoadMd(eaa + 4);
				if ((((s32) * rmrd) < bound_min)
				    || (((s32) * rmrd) > bound_max)) {
					EXCEPTION(5);
				}
			}
			break;
			CASE_D(0x63) {	/* ARPL Ed,Rd */
				if (((cpu->block.pmode) && (dcr_reg_flags & DC_FLAG_VM))
				    || (!cpu->block.pmode))
					goto illegal_opcode;
				GetRMrw;
				if (rm >= 0xc0) {
					GetEArd;
					u32 new_sel = (u16) * eard;
					CPU_ARPL(&new_sel, *rmrw);
					*eard = (u32) new_sel;
				} else {
					GetEAa;
					u32 new_sel = LoadMw(eaa);
					CPU_ARPL(&new_sel, *rmrw);
					SaveMd(eaa, (u32) new_sel);
				}
			}
			break;
			CASE_D(0x68)	/* PUSH Id */
			    Push_32(Fetchd(cpu));
			break;
			CASE_D(0x69)	/* IMUL Gd,Ed,Id */
			    RMGdEdOp3(DIMULD, Fetchds(cpu));
			break;
			CASE_D(0x6a)	/* PUSH Ib */
			    Push_32(Fetchbs(cpu));
			break;
			CASE_D(0x6b)	/* IMUL Gd,Ed,Ib */
			    RMGdEdOp3(DIMULD, Fetchbs(cpu));
			break;
			CASE_D(0x6d)	/* INSD */
			    if (CPU_IO_Exception(dcr_reg_dx, 4))
				RUNEXCEPTION();
			DoString(cpu, R_INSD);
			break;
			CASE_D(0x6f)	/* OUTSD */
			    if (CPU_IO_Exception(dcr_reg_dx, 4))
				RUNEXCEPTION();
			DoString(cpu, R_OUTSD);
			break;
			CASE_D(0x70)	/* JO */
			    JumpCond32_b(TFLG_O);
			break;
			CASE_D(0x71)	/* JNO */
			    JumpCond32_b(TFLG_NO);
			break;
			CASE_D(0x72)	/* JB */
			    JumpCond32_b(TFLG_B);
			break;
			CASE_D(0x73)	/* JNB */
			    JumpCond32_b(TFLG_NB);
			break;
			CASE_D(0x74)	/* JZ */
			    JumpCond32_b(TFLG_Z);
			break;
			CASE_D(0x75)	/* JNZ */
			    JumpCond32_b(TFLG_NZ);
			break;
			CASE_D(0x76)	/* JBE */
			    JumpCond32_b(TFLG_BE);
			break;
			CASE_D(0x77)	/* JNBE */
			    JumpCond32_b(TFLG_NBE);
			break;
			CASE_D(0x78)	/* JS */
			    JumpCond32_b(TFLG_S);
			break;
			CASE_D(0x79)	/* JNS */
			    JumpCond32_b(TFLG_NS);
			break;
			CASE_D(0x7a)	/* JP */
			    JumpCond32_b(TFLG_P);
			break;
			CASE_D(0x7b)	/* JNP */
			    JumpCond32_b(TFLG_NP);
			break;
			CASE_D(0x7c)	/* JL */
			    JumpCond32_b(TFLG_L);
			break;
			CASE_D(0x7d)	/* JNL */
			    JumpCond32_b(TFLG_NL);
			break;
			CASE_D(0x7e)	/* JLE */
			    JumpCond32_b(TFLG_LE);
			break;
			CASE_D(0x7f)	/* JNLE */
			    JumpCond32_b(TFLG_NLE);
			break;
			CASE_D(0x81) {	/* Grpl Ed,Id */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm >= 0xc0) {
					GetEArd;
					u32 id = Fetchd(cpu);
					switch (which) {
					case 0x00:
						ADDD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x01:
						ORD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x02:
						ADCD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x03:
						SBBD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x04:
						ANDD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x05:
						SUBD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x06:
						XORD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x07:
						CMPD(*eard, id, LoadRd, SaveRd);
						break;
					}
				} else {
					GetEAa;
					u32 id = Fetchd(cpu);
					switch (which) {
					case 0x00:
						ADDD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x01:
						ORD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x02:
						ADCD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x03:
						SBBD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x04:
						ANDD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x05:
						SUBD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x06:
						XORD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x07:
						CMPD(eaa, id, LoadMd, SaveMd);
						break;
					}
				}
			}
			break;
			CASE_D(0x83) {	/* Grpl Ed,Ix */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm >= 0xc0) {
					GetEArd;
					u32 id = (s32) Fetchbs(cpu);
					switch (which) {
					case 0x00:
						ADDD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x01:
						ORD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x02:
						ADCD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x03:
						SBBD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x04:
						ANDD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x05:
						SUBD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x06:
						XORD(*eard, id, LoadRd, SaveRd);
						break;
					case 0x07:
						CMPD(*eard, id, LoadRd, SaveRd);
						break;
					}
				} else {
					GetEAa;
					u32 id = (s32) Fetchbs(cpu);
					switch (which) {
					case 0x00:
						ADDD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x01:
						ORD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x02:
						ADCD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x03:
						SBBD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x04:
						ANDD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x05:
						SUBD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x06:
						XORD(eaa, id, LoadMd, SaveMd);
						break;
					case 0x07:
						CMPD(eaa, id, LoadMd, SaveMd);
						break;
					}
				}
			}
			break;
			CASE_D(0x85)	/* TEST Ed,Gd */
			    RMEdGd(TESTD);
			break;
			CASE_D(0x87) {	/* XCHG Ed,Gd */
				GetRMrd;
				u32 oldrmrd = *rmrd;
				if (rm >= 0xc0) {
					GetEArd;
					*rmrd = *eard;
					*eard = oldrmrd;
				} else {
					GetEAa;
					*rmrd = LoadMd(eaa);
					SaveMd(eaa, oldrmrd);
				}
				break;
			}
			CASE_D(0x89) {	/* MOV Ed,Gd */
				GetRMrd;
				if (rm >= 0xc0) {
					GetEArd;
					*eard = *rmrd;
				} else {
					GetEAa;
					SaveMd(eaa, *rmrd);
				}
				break;
			}
			CASE_D(0x8b) {	/* MOV Gd,Ed */
				GetRMrd;
				if (rm >= 0xc0) {
					GetEArd;
					*rmrd = *eard;
				} else {
					GetEAa;
					*rmrd = LoadMd(eaa);
				}
				break;
			}
			CASE_D(0x8c) {	/* Mov Ew,Sw */
				GetRM;
				u16 val;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* MOV Ew,ES */
					val = SegValue(dp_seg_es);
					break;
				case 0x01:	/* MOV Ew,CS */
					val = SegValue(dp_seg_cs);
					break;
				case 0x02:	/* MOV Ew,SS */
					val = SegValue(dp_seg_ss);
					break;
				case 0x03:	/* MOV Ew,DS */
					val = SegValue(dp_seg_ds);
					break;
				case 0x04:	/* MOV Ew,FS */
					val = SegValue(dp_seg_fs);
					break;
				case 0x05:	/* MOV Ew,GS */
					val = SegValue(dp_seg_gs);
					break;
				default:
					DP_ERR("CPU:8c:Illegal RM Byte");
					goto illegal_opcode;
				}
				if (rm >= 0xc0) {
					GetEArd;
					*eard = val;
				} else {
					GetEAa;
					SaveMw(eaa, val);
				}
				break;
			}
			CASE_D(0x8d) {	/* LEA Gd */
				//Little hack to always use segprefixed version
				GetRMrd;
				BaseDS = BaseSS = 0;
				if (TEST_PREFIX_ADDR) {
					*rmrd = (u32) (*EATable[256 + rm]) (cpu);
				} else {
					*rmrd = (u32) (*EATable[rm]) (cpu);
				}
				break;
			}
			CASE_D(0x8f) {	/* POP Ed */
				u32 val = Pop_32();
				GetRM;
				if (rm >= 0xc0) {
					GetEArd;
					*eard = val;
				} else {
					GetEAa;
					SaveMd(eaa, val);
				}
				break;
			}
			CASE_D(0x91) {	/* XCHG ECX,EAX */
				u32 temp = dcr_reg_eax;
				dcr_reg_eax = dcr_reg_ecx;
				dcr_reg_ecx = temp;
				break;
			}
			CASE_D(0x92) {	/* XCHG EDX,EAX */
				u32 temp = dcr_reg_eax;
				dcr_reg_eax = dcr_reg_edx;
				dcr_reg_edx = temp;
				break;
			}
			break;
			CASE_D(0x93) {	/* XCHG EBX,EAX */
				u32 temp = dcr_reg_eax;
				dcr_reg_eax = dcr_reg_ebx;
				dcr_reg_ebx = temp;
				break;
			}
			break;
			CASE_D(0x94) {	/* XCHG ESP,EAX */
				u32 temp = dcr_reg_eax;
				dcr_reg_eax = dcr_reg_esp;
				dcr_reg_esp = temp;
				break;
			}
			break;
			CASE_D(0x95) {	/* XCHG EBP,EAX */
				u32 temp = dcr_reg_eax;
				dcr_reg_eax = dcr_reg_ebp;
				dcr_reg_ebp = temp;
				break;
			}
			break;
			CASE_D(0x96) {	/* XCHG ESI,EAX */
				u32 temp = dcr_reg_eax;
				dcr_reg_eax = dcr_reg_esi;
				dcr_reg_esi = temp;
				break;
			}
			break;
			CASE_D(0x97) {	/* XCHG EDI,EAX */
				u32 temp = dcr_reg_eax;
				dcr_reg_eax = dcr_reg_edi;
				dcr_reg_edi = temp;
				break;
			}
			break;
			CASE_D(0x98)	/* CWDE */
			    dcr_reg_eax = (s16) dcr_reg_ax;
			break;
			CASE_D(0x99)	/* CDQ */
			    if (dcr_reg_eax & 0x80000000)
				dcr_reg_edx = 0xffffffff;
			else
				dcr_reg_edx = 0;
			break;
			CASE_D(0x9a) {	/* CALL FAR Ad */
				u32 newip = Fetchd(cpu);
				u16 newcs = Fetchw(cpu);
				FillFlags(cpu);
				CPU_CALL(DP_TRUE, newcs, newip, GETIP);
#if CPU_TRAP_CHECK
				if (DC_GET_FLAG(TF)) {
					cpu->decoder.func = CPU_Core_Normal_Trap_Run;
					return DP_CALLBACK_NONE;
				}
#endif
				continue;
			}
			CASE_D(0x9c)	/* PUSHFD */
			    if (CPU_PUSHF(DP_TRUE))
				RUNEXCEPTION();
			break;
			CASE_D(0x9d)	/* POPFD */
			    if (CPU_POPF(DP_TRUE))
				RUNEXCEPTION();
#if CPU_TRAP_CHECK
			if (DC_GET_FLAG(TF)) {
				cpu->decoder.func = CPU_Core_Normal_Trap_Run;
				goto decode_end;
			}
#endif
#if CPU_PIC_CHECK
			if (DC_GET_FLAG(IF) && cpu->pic->irq_check)
				goto decode_end;
#endif
			break;
			CASE_D(0xa1) {	/* MOV EAX,Od */
				GetEADirect;
				dcr_reg_eax = LoadMd(eaa);
			}
			break;
			CASE_D(0xa3) {	/* MOV Od,EAX */
				GetEADirect;
				SaveMd(eaa, dcr_reg_eax);
			}
			break;
			CASE_D(0xa5)	/* MOVSD */
			    DoString(cpu, R_MOVSD);
			break;
			CASE_D(0xa7)	/* CMPSD */
			    DoString(cpu, R_CMPSD);
			break;
			CASE_D(0xa9)	/* TEST EAX,Id */
			    EAXId(TESTD);
			break;
			CASE_D(0xab)	/* STOSD */
			    DoString(cpu, R_STOSD);
			break;
			CASE_D(0xad)	/* LODSD */
			    DoString(cpu, R_LODSD);
			break;
			CASE_D(0xaf)	/* SCASD */
			    DoString(cpu, R_SCASD);
			break;
			CASE_D(0xb8)	/* MOV EAX,Id */
			    dcr_reg_eax = Fetchd(cpu);
			break;
			CASE_D(0xb9)	/* MOV ECX,Id */
			    dcr_reg_ecx = Fetchd(cpu);
			break;
			CASE_D(0xba)	/* MOV EDX,Iw */
			    dcr_reg_edx = Fetchd(cpu);
			break;
			CASE_D(0xbb)	/* MOV EBX,Id */
			    dcr_reg_ebx = Fetchd(cpu);
			break;
			CASE_D(0xbc)	/* MOV ESP,Id */
			    dcr_reg_esp = Fetchd(cpu);
			break;
			CASE_D(0xbd)	/* MOV EBP.Id */
			    dcr_reg_ebp = Fetchd(cpu);
			break;
			CASE_D(0xbe)	/* MOV ESI,Id */
			    dcr_reg_esi = Fetchd(cpu);
			break;
			CASE_D(0xbf)	/* MOV EDI,Id */
			    dcr_reg_edi = Fetchd(cpu);
			break;
			CASE_D(0xc1)	/* GRP2 Ed,Ib */
			    GRP2D(Fetchb(cpu));
			break;
			CASE_D(0xc2)	/* RETN Iw */
			    dcr_reg_eip = Pop_32();
			dcr_reg_esp += Fetchw(cpu);
			continue;
			CASE_D(0xc3)	/* RETN */
			    dcr_reg_eip = Pop_32();
			continue;
			CASE_D(0xc4) {	/* LES */
				GetRMrd;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_es, LoadMw(eaa + 4)))
					RUNEXCEPTION();
				*rmrd = LoadMd(eaa);
				break;
			}
			CASE_D(0xc5) {	/* LDS */
				GetRMrd;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_ds, LoadMw(eaa + 4)))
					RUNEXCEPTION();
				*rmrd = LoadMd(eaa);
				break;
			}
			CASE_D(0xc7) {	/* MOV Ed,Id */
				GetRM;
				if (rm >= 0xc0) {
					GetEArd;
					*eard = Fetchd(cpu);
				} else {
					GetEAa;
					SaveMd(eaa, Fetchd(cpu));
				}
				break;
			}
			CASE_D(0xc8) {	/* ENTER Iw,Ib */
				u32 bytes = Fetchw(cpu);
				u32 level = Fetchb(cpu);
				CPU_ENTER(DP_TRUE, bytes, level);
			}
			break;
			CASE_D(0xc9)	/* LEAVE */
			    dcr_reg_esp &= cpu->block.stack.notmask;
			dcr_reg_esp |= (dcr_reg_ebp & cpu->block.stack.mask);
			dcr_reg_ebp = Pop_32();
			break;
			CASE_D(0xca) {	/* RETF Iw */
				u32 words = Fetchw(cpu);
				FillFlags(cpu);
				CPU_RET(DP_TRUE, words, GETIP);
				continue;
			}
			CASE_D(0xcb) {	/* RETF */
				FillFlags(cpu);
				CPU_RET(DP_TRUE, 0, GETIP);
				continue;
			}
			CASE_D(0xcf) {	/* IRET */
				CPU_IRET(DP_TRUE, GETIP);
#if CPU_TRAP_CHECK
				if (DC_GET_FLAG(TF)) {
					cpu->decoder.func = CPU_Core_Normal_Trap_Run;
					return DP_CALLBACK_NONE;
				}
#endif
#if CPU_PIC_CHECK
				if (DC_GET_FLAG(IF) && cpu->pic->irq_check)
					return DP_CALLBACK_NONE;
#endif
				continue;
			}
			CASE_D(0xd1)	/* GRP2 Ed,1 */
			    GRP2D(1);
			break;
			CASE_D(0xd3)	/* GRP2 Ed,CL */
			    GRP2D(dcr_reg_cl);
			break;
			CASE_D(0xe0)	/* LOOPNZ */
			    if (TEST_PREFIX_ADDR) {
				JumpCond32_b(--dcr_reg_ecx && !get_ZF(cpu));
			} else {
				JumpCond32_b(--dcr_reg_cx && !get_ZF(cpu));
			}
			break;
			CASE_D(0xe1)	/* LOOPZ */
			    if (TEST_PREFIX_ADDR) {
				JumpCond32_b(--dcr_reg_ecx && get_ZF(cpu));
			} else {
				JumpCond32_b(--dcr_reg_cx && get_ZF(cpu));
			}
			break;
			CASE_D(0xe2)	/* LOOP */
			    if (TEST_PREFIX_ADDR) {
				JumpCond32_b(--dcr_reg_ecx);
			} else {
				JumpCond32_b(--dcr_reg_cx);
			}
			break;
			CASE_D(0xe3)	/* JCXZ */
			    JumpCond32_b(!(dcr_reg_ecx & AddrMaskTable[cpu->decoder.core.prefixes & PREFIX_ADDR]));
			break;
			CASE_D(0xe5) {	/* IN EAX,Ib */
				u32 port = Fetchb(cpu);
				if (CPU_IO_Exception(port, 4))
					RUNEXCEPTION();
				dcr_reg_eax = dp_io_readd(cpu->io, port);
				break;
			}
			CASE_D(0xe7) {	/* OUT Ib,EAX */
				u32 port = Fetchb(cpu);
				if (CPU_IO_Exception(port, 4))
					RUNEXCEPTION();
				dp_io_writed(cpu->io, port, dcr_reg_eax);
				break;
			}
			CASE_D(0xe8) {	/* CALL Jd */
				s32 addip = Fetchds(cpu);
				SAVEIP;
				Push_32(dcr_reg_eip);
				dcr_reg_eip += addip;
				continue;
			}
			CASE_D(0xe9) {	/* JMP Jd */
				s32 addip = Fetchds(cpu);
				SAVEIP;
				dcr_reg_eip += addip;
				continue;
			}
			CASE_D(0xea) {	/* JMP Ad */
				u32 newip = Fetchd(cpu);
				u16 newcs = Fetchw(cpu);
				FillFlags(cpu);
				CPU_JMP(DP_TRUE, newcs, newip, GETIP);
#if CPU_TRAP_CHECK
				if (DC_GET_FLAG(TF)) {
					cpu->decoder.func = CPU_Core_Normal_Trap_Run;
					return DP_CALLBACK_NONE;
				}
#endif
				continue;
			}
			CASE_D(0xeb) {	/* JMP Jb */
				s32 addip = Fetchbs(cpu);
				SAVEIP;
				dcr_reg_eip += addip;
				continue;
			}
			CASE_D(0xed)	/* IN EAX,DX */
			    dcr_reg_eax = dp_io_readd(cpu->io, dcr_reg_dx);
			break;
			CASE_D(0xef)	/* OUT DX,EAX */
			    dp_io_writed(cpu->io, dcr_reg_dx, dcr_reg_eax);
			break;
			CASE_D(0xf7) {	/* GRP3 Ed(,Id) */
				GetRM;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* TEST Ed,Id */
				case 0x01:	/* TEST Ed,Id Undocumented */
					{
						if (rm >= 0xc0) {
							GetEArd;
							TESTD(*eard, Fetchd(cpu), LoadRd, SaveRd);
						} else {
							GetEAa;
							TESTD(eaa, Fetchd(cpu), LoadMd, SaveMd);
						}
						break;
					}
				case 0x02:	/* NOT Ed */
					{
						if (rm >= 0xc0) {
							GetEArd;
							*eard = ~*eard;
						} else {
							GetEAa;
							SaveMd(eaa, ~LoadMd(eaa));
						}
						break;
					}
				case 0x03:	/* NEG Ed */
					{
						cpu->decoder.lflags.type = t_NEGd;
						if (rm >= 0xc0) {
							GetEArd;
							lf_var1d = *eard;
							lf_resd = 0 - lf_var1d;
							*eard = lf_resd;
						} else {
							GetEAa;
							lf_var1d = LoadMd(eaa);
							lf_resd = 0 - lf_var1d;
							SaveMd(eaa, lf_resd);
						}
						break;
					}
				case 0x04:	/* MUL EAX,Ed */
					RMEd(MULD);
					break;
				case 0x05:	/* IMUL EAX,Ed */
					RMEd(IMULD);
					break;
				case 0x06:	/* DIV Ed */
					RMEd(DIVD);
					break;
				case 0x07:	/* IDIV Ed */
					RMEd(IDIVD);
					break;
				}
				break;
			}
			CASE_D(0xff) {	/* GRP 5 Ed */
				GetRM;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* INC Ed */
					RMEd(INCD);
					break;
				case 0x01:	/* DEC Ed */
					RMEd(DECD);
					break;
				case 0x02:	/* CALL NEAR Ed */
					if (rm >= 0xc0) {
						GetEArd;
						dcr_reg_eip = *eard;
					} else {
						GetEAa;
						dcr_reg_eip = LoadMd(eaa);
					}
					Push_32(GETIP);
					continue;
				case 0x03:	/* CALL FAR Ed */
					{
						if (rm >= 0xc0)
							goto illegal_opcode;
						GetEAa;
						u32 newip = LoadMd(eaa);
						u16 newcs = LoadMw(eaa + 4);
						FillFlags(cpu);
						CPU_CALL(DP_TRUE, newcs, newip, GETIP);
#if CPU_TRAP_CHECK
						if (DC_GET_FLAG(TF)) {
							cpu->decoder.func = CPU_Core_Normal_Trap_Run;
							return DP_CALLBACK_NONE;
						}
#endif
						continue;
					}
				case 0x04:	/* JMP NEAR Ed */
					if (rm >= 0xc0) {
						GetEArd;
						dcr_reg_eip = *eard;
					} else {
						GetEAa;
						dcr_reg_eip = LoadMd(eaa);
					}
					continue;
				case 0x05:	/* JMP FAR Ed */
					{
						if (rm >= 0xc0)
							goto illegal_opcode;
						GetEAa;
						u32 newip = LoadMd(eaa);
						u16 newcs = LoadMw(eaa + 4);
						FillFlags(cpu);
						CPU_JMP(DP_TRUE, newcs, newip, GETIP);
#if CPU_TRAP_CHECK
						if (DC_GET_FLAG(TF)) {
							cpu->decoder.func = CPU_Core_Normal_Trap_Run;
							return DP_CALLBACK_NONE;
						}
#endif
						continue;
					}
					break;
				case 0x06:	/* Push Ed */
					if (rm >= 0xc0) {
						GetEArd;
						Push_32(*eard);
					} else {
						GetEAa;
						Push_32(LoadMd(eaa));
					}
					break;
				default:
					DP_ERR("CPU:66:GRP5:Illegal call %2X", which);
					goto illegal_opcode;
				}
				break;
			}

			CASE_0F_D(0x00) {	/* GRP 6 Exxx */
				if ((dcr_reg_flags & DC_FLAG_VM) || (!cpu->block.pmode))
					goto illegal_opcode;
				GetRM;
				u32 which = (rm >> 3) & 7;
				switch (which) {
				case 0x00:	/* SLDT */
				case 0x01:	/* STR */
					{
						u32 saveval;
						if (!which)
							saveval = CPU_SLDT();
						else
							saveval = CPU_STR();
						if (rm >= 0xc0) {
							GetEArw;
							*earw = (u16) saveval;
						} else {
							GetEAa;
							SaveMw(eaa, saveval);
						}
					}
					break;
				case 0x02:
				case 0x03:
				case 0x04:
				case 0x05:
					{
						/* Just use 16-bit loads since were only using selectors */
						u32 loadval;
						if (rm >= 0xc0) {
							GetEArw;
							loadval = *earw;
						} else {
							GetEAa;
							loadval = LoadMw(eaa);
						}
						switch (which) {
						case 0x02:
							if (cpu->block.cpl)
								EXCEPTION(DC_EXCEPTION_GP);
							if (CPU_LLDT(loadval))
								RUNEXCEPTION();
							break;
						case 0x03:
							if (cpu->block.cpl)
								EXCEPTION(DC_EXCEPTION_GP);
							if (CPU_LTR(loadval))
								RUNEXCEPTION();
							break;
						case 0x04:
							CPU_VERR(loadval);
							break;
						case 0x05:
							CPU_VERW(loadval);
							break;
						}
					}
					break;
				default:
					DP_ERR("GRP6:Illegal call %2X", which);
					goto illegal_opcode;
				}
			}
			break;
			CASE_0F_D(0x01) {	/* Group 7 Ed */
				GetRM;
				u32 which = (rm >> 3) & 7;
				if (rm < 0xc0) {	//First ones all use EA
					GetEAa;
					u32 limit;
					switch (which) {
					case 0x00:	/* SGDT */
						SaveMw(eaa, (u16)
						       CPU_SGDT_limit());
						SaveMd(eaa + 2, (u32)
						       CPU_SGDT_base());
						break;
					case 0x01:	/* SIDT */
						SaveMw(eaa, (u16)
						       CPU_SIDT_limit());
						SaveMd(eaa + 2, (u32)
						       CPU_SIDT_base());
						break;
					case 0x02:	/* LGDT */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						CPU_LGDT(LoadMw(eaa), LoadMd(eaa + 2));
						break;
					case 0x03:	/* LIDT */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						CPU_LIDT(LoadMw(eaa), LoadMd(eaa + 2));
						break;
					case 0x04:	/* SMSW */
						SaveMw(eaa, (u16) CPU_SMSW());
						break;
					case 0x06:	/* LMSW */
						limit = LoadMw(eaa);
						if (CPU_LMSW((u16) limit))
							RUNEXCEPTION();
						break;
					case 0x07:	/* INVLPG */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						dp_paging_clear_tlb(cpu->paging);
						break;
					}
				} else {
					GetEArd;
					switch (which) {
					case 0x02:	/* LGDT */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						goto illegal_opcode;
					case 0x03:	/* LIDT */
						if (cpu->block.pmode && cpu->block.cpl)
							EXCEPTION(DC_EXCEPTION_GP);
						goto illegal_opcode;
					case 0x04:	/* SMSW */
						*eard = (u32) CPU_SMSW();
						break;
					case 0x06:	/* LMSW */
						if (CPU_LMSW(*eard))
							RUNEXCEPTION();
						break;
					default:
						DP_ERR("Illegal group 7 RM subfunction %d", which);
						goto illegal_opcode;
						break;
					}

				}
			}
			break;
			CASE_0F_D(0x02) {	/* LAR Gd,Ed */
				if ((dcr_reg_flags & DC_FLAG_VM) || (!cpu->block.pmode))
					goto illegal_opcode;
				GetRMrd;
				u32 ar = *rmrd;
				if (rm >= 0xc0) {
					GetEArw;
					CPU_LAR(*earw, &ar);
				} else {
					GetEAa;
					CPU_LAR(LoadMw(eaa), &ar);
				}
				*rmrd = (u32) ar;
			}
			break;
			CASE_0F_D(0x03) {	/* LSL Gd,Ew */
				if ((dcr_reg_flags & DC_FLAG_VM) || (!cpu->block.pmode))
					goto illegal_opcode;
				GetRMrd;
				u32 limit = *rmrd;
				/* Just load 16-bit values for selectors */
				if (rm >= 0xc0) {
					GetEArw;
					CPU_LSL(*earw, &limit);
				} else {
					GetEAa;
					CPU_LSL(LoadMw(eaa), &limit);
				}
				*rmrd = (u32) limit;
			}
			break;
			CASE_0F_D(0x80)	/* JO */
			    JumpCond32_d(TFLG_O);
			break;
			CASE_0F_D(0x81)	/* JNO */
			    JumpCond32_d(TFLG_NO);
			break;
			CASE_0F_D(0x82)	/* JB */
			    JumpCond32_d(TFLG_B);
			break;
			CASE_0F_D(0x83)	/* JNB */
			    JumpCond32_d(TFLG_NB);
			break;
			CASE_0F_D(0x84)	/* JZ */
			    JumpCond32_d(TFLG_Z);
			break;
			CASE_0F_D(0x85)	/* JNZ */
			    JumpCond32_d(TFLG_NZ);
			break;
			CASE_0F_D(0x86)	/* JBE */
			    JumpCond32_d(TFLG_BE);
			break;
			CASE_0F_D(0x87)	/* JNBE */
			    JumpCond32_d(TFLG_NBE);
			break;
			CASE_0F_D(0x88)	/* JS */
			    JumpCond32_d(TFLG_S);
			break;
			CASE_0F_D(0x89)	/* JNS */
			    JumpCond32_d(TFLG_NS);
			break;
			CASE_0F_D(0x8a)	/* JP */
			    JumpCond32_d(TFLG_P);
			break;
			CASE_0F_D(0x8b)	/* JNP */
			    JumpCond32_d(TFLG_NP);
			break;
			CASE_0F_D(0x8c)	/* JL */
			    JumpCond32_d(TFLG_L);
			break;
			CASE_0F_D(0x8d)	/* JNL */
			    JumpCond32_d(TFLG_NL);
			break;
			CASE_0F_D(0x8e)	/* JLE */
			    JumpCond32_d(TFLG_LE);
			break;
			CASE_0F_D(0x8f)	/* JNLE */
			    JumpCond32_d(TFLG_NLE);
			break;

			CASE_0F_D(0xa0)	/* PUSH FS */
			    Push_32(SegValue(dp_seg_fs));
			break;
			CASE_0F_D(0xa1)	/* POP FS */
			    if (CPU_PopSeg(dp_seg_fs, DP_TRUE))
				RUNEXCEPTION();
			break;
			CASE_0F_D(0xa3) {	/* BT Ed,Gd */
				FillFlags(cpu);
				GetRMrd;
				u32 mask = 1 << (*rmrd & 31);
				if (rm >= 0xc0) {
					GetEArd;
					DC_SET_FLAGBIT(CF, (*eard & mask));
				} else {
					GetEAa;
					eaa += (((s32) * rmrd) >> 5) * 4;
					u32 old = LoadMd(eaa);
					DC_SET_FLAGBIT(CF, (old & mask));
				}
				break;
			}
			CASE_0F_D(0xa4)	/* SHLD Ed,Gd,Ib */
			    RMEdGdOp3(DSHLD, Fetchb(cpu));
			break;
			CASE_0F_D(0xa5)	/* SHLD Ed,Gd,CL */
			    RMEdGdOp3(DSHLD, dcr_reg_cl);
			break;
			CASE_0F_D(0xa8)	/* PUSH GS */
			    Push_32(SegValue(dp_seg_gs));
			break;
			CASE_0F_D(0xa9)	/* POP GS */
			    if (CPU_PopSeg(dp_seg_gs, DP_TRUE))
				RUNEXCEPTION();
			break;
			CASE_0F_D(0xab) {	/* BTS Ed,Gd */
				FillFlags(cpu);
				GetRMrd;
				u32 mask = 1 << (*rmrd & 31);
				if (rm >= 0xc0) {
					GetEArd;
					DC_SET_FLAGBIT(CF, (*eard & mask));
					*eard |= mask;
				} else {
					GetEAa;
					eaa += (((s32) * rmrd) >> 5) * 4;
					u32 old = LoadMd(eaa);
					DC_SET_FLAGBIT(CF, (old & mask));
					SaveMd(eaa, old | mask);
				}
				break;
			}

			CASE_0F_D(0xac)	/* SHRD Ed,Gd,Ib */
			    RMEdGdOp3(DSHRD, Fetchb(cpu));
			break;
			CASE_0F_D(0xad)	/* SHRD Ed,Gd,CL */
			    RMEdGdOp3(DSHRD, dcr_reg_cl);
			break;
			CASE_0F_D(0xaf) {	/* IMUL Gd,Ed */
				RMGdEdOp3(DIMULD, *rmrd);
				break;
			}
			CASE_0F_D(0xb1) {	/* CMPXCHG Ed,Gd */
				if (cpu->arch < DP_CPU_ARCHTYPE_486NEWSLOW)
					goto illegal_opcode;
				FillFlags(cpu);
				GetRMrd;
				if (rm >= 0xc0) {
					GetEArd;
					if (*eard == dcr_reg_eax) {
						*eard = *rmrd;
						DC_SET_FLAGBIT(ZF, 1);
					} else {
						dcr_reg_eax = *eard;
						DC_SET_FLAGBIT(ZF, 0);
					}
				} else {
					GetEAa;
					u32 val = LoadMd(eaa);
					if (val == dcr_reg_eax) {
						SaveMd(eaa, *rmrd);
						DC_SET_FLAGBIT(ZF, 1);
					} else {
						SaveMd(eaa, val);	// cmpxchg always issues a write
						dcr_reg_eax = val;
						DC_SET_FLAGBIT(ZF, 0);
					}
				}
				break;
			}
			CASE_0F_D(0xb2) {	/* LSS Ed */
				GetRMrd;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_ss, LoadMw(eaa + 4)))
					RUNEXCEPTION();
				*rmrd = LoadMd(eaa);
				break;
			}
			CASE_0F_D(0xb3) {	/* BTR Ed,Gd */
				FillFlags(cpu);
				GetRMrd;
				u32 mask = 1 << (*rmrd & 31);
				if (rm >= 0xc0) {
					GetEArd;
					DC_SET_FLAGBIT(CF, (*eard & mask));
					*eard &= ~mask;
				} else {
					GetEAa;
					eaa += (((s32) * rmrd) >> 5) * 4;
					u32 old = LoadMd(eaa);
					DC_SET_FLAGBIT(CF, (old & mask));
					SaveMd(eaa, old & ~mask);
				}
				break;
			}
			CASE_0F_D(0xb4) {	/* LFS Ed */
				GetRMrd;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_fs, LoadMw(eaa + 4)))
					RUNEXCEPTION();
				*rmrd = LoadMd(eaa);
				break;
			}
			CASE_0F_D(0xb5) {	/* LGS Ed */
				GetRMrd;
				if (rm >= 0xc0)
					goto illegal_opcode;
				GetEAa;
				if (CPU_SetSegGeneral(dp_seg_gs, LoadMw(eaa + 4)))
					RUNEXCEPTION();
				*rmrd = LoadMd(eaa);
				break;
			}
			CASE_0F_D(0xb6) {	/* MOVZX Gd,Eb */
				GetRMrd;
				if (rm >= 0xc0) {
					GetEArb;
					*rmrd = *earb;
				} else {
					GetEAa;
					*rmrd = LoadMb(eaa);
				}
				break;
			}
			CASE_0F_D(0xb7) {	/* MOVXZ Gd,Ew */
				GetRMrd;
				if (rm >= 0xc0) {
					GetEArw;
					*rmrd = *earw;
				} else {
					GetEAa;
					*rmrd = LoadMw(eaa);
				}
				break;
			}
			CASE_0F_D(0xba) {	/* GRP8 Ed,Ib */
				FillFlags(cpu);
				GetRM;
				if (rm >= 0xc0) {
					GetEArd;
					u32 mask = 1 << (Fetchb(cpu) & 31);
					DC_SET_FLAGBIT(CF, (*eard & mask));
					switch (rm & 0x38) {
					case 0x20:	/* BT */
						break;
					case 0x28:	/* BTS */
						*eard |= mask;
						break;
					case 0x30:	/* BTR */
						*eard &= ~mask;
						break;
					case 0x38:	/* BTC */
						if (DC_GET_FLAG(CF))
							*eard &= ~mask;
						else
							*eard |= mask;
						break;
					default:
						E_Exit("CPU:66:0F:BA:Illegal subfunction %X", rm & 0x38);
					}
				} else {
					GetEAa;
					u32 old = LoadMd(eaa);
					u32 mask = 1 << (Fetchb(cpu) & 31);
					DC_SET_FLAGBIT(CF, (old & mask));
					switch (rm & 0x38) {
					case 0x20:	/* BT */
						break;
					case 0x28:	/* BTS */
						SaveMd(eaa, old | mask);
						break;
					case 0x30:	/* BTR */
						SaveMd(eaa, old & ~mask);
						break;
					case 0x38:	/* BTC */
						if (DC_GET_FLAG(CF))
							old &= ~mask;
						else
							old |= mask;
						SaveMd(eaa, old);
						break;
					default:
						E_Exit("CPU:66:0F:BA:Illegal subfunction %X", rm & 0x38);
					}
				}
				break;
			}
			CASE_0F_D(0xbb) {	/* BTC Ed,Gd */
				FillFlags(cpu);
				GetRMrd;
				u32 mask = 1 << (*rmrd & 31);
				if (rm >= 0xc0) {
					GetEArd;
					DC_SET_FLAGBIT(CF, (*eard & mask));
					*eard ^= mask;
				} else {
					GetEAa;
					eaa += (((s32) * rmrd) >> 5) * 4;
					u32 old = LoadMd(eaa);
					DC_SET_FLAGBIT(CF, (old & mask));
					SaveMd(eaa, old ^ mask);
				}
				break;
			}
			CASE_0F_D(0xbc) {	/* BSF Gd,Ed */
				GetRMrd;
				u32 result, value;
				if (rm >= 0xc0) {
					GetEArd;
					value = *eard;
				} else {
					GetEAa;
					value = LoadMd(eaa);
				}
				if (value == 0) {
					DC_SET_FLAGBIT(ZF, DP_TRUE);
				} else {
					result = 0;
					while ((value & 0x01) == 0) {
						result++;
						value >>= 1;
					}
					DC_SET_FLAGBIT(ZF, DP_FALSE);
					*rmrd = result;
				}
				cpu->decoder.lflags.type = t_UNKNOWN;
				break;
			}
			CASE_0F_D(0xbd) {	/*  BSR Gd,Ed */
				GetRMrd;
				u32 result, value;
				if (rm >= 0xc0) {
					GetEArd;
					value = *eard;
				} else {
					GetEAa;
					value = LoadMd(eaa);
				}
				if (value == 0) {
					DC_SET_FLAGBIT(ZF, DP_TRUE);
				} else {
					result = 31;	// Operandsize-1
					while ((value & 0x80000000) == 0) {
						result--;
						value <<= 1;
					}
					DC_SET_FLAGBIT(ZF, DP_FALSE);
					*rmrd = result;
				}
				cpu->decoder.lflags.type = t_UNKNOWN;
				break;
			}
			CASE_0F_D(0xbe) {	/* MOVSX Gd,Eb */
				GetRMrd;
				if (rm >= 0xc0) {
					GetEArb;
					*rmrd = *(s8 *) earb;
				} else {
					GetEAa;
					*rmrd = LoadMbs(eaa);
				}
				break;
			}
			CASE_0F_D(0xbf) {	/* MOVSX Gd,Ew */
				GetRMrd;
				if (rm >= 0xc0) {
					GetEArw;
					*rmrd = *(s16 *) earw;
				} else {
					GetEAa;
					*rmrd = LoadMws(eaa);
				}
				break;
			}
			CASE_0F_D(0xc1) {	/* XADD Gd,Ed */
				if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
					goto illegal_opcode;
				GetRMrd;
				u32 oldrmrd = *rmrd;
				if (rm >= 0xc0) {
					GetEArd;
					*rmrd = *eard;
					*eard += oldrmrd;
				} else {
					GetEAa;
					*rmrd = LoadMd(eaa);
					SaveMd(eaa, LoadMd(eaa) + oldrmrd);
				}
				break;
			}
			CASE_0F_D(0xc8)	/* BSWAP EAX */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPD(dcr_reg_eax);
			break;
			CASE_0F_D(0xc9)	/* BSWAP ECX */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPD(dcr_reg_ecx);
			break;
			CASE_0F_D(0xca)	/* BSWAP EDX */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPD(dcr_reg_edx);
			break;
			CASE_0F_D(0xcb)	/* BSWAP EBX */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPD(dcr_reg_ebx);
			break;
			CASE_0F_D(0xcc)	/* BSWAP ESP */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPD(dcr_reg_esp);
			break;
			CASE_0F_D(0xcd)	/* BSWAP EBP */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPD(dcr_reg_ebp);
			break;
			CASE_0F_D(0xce)	/* BSWAP ESI */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPD(dcr_reg_esi);
			break;
			CASE_0F_D(0xcf)	/* BSWAP EDI */
			    if (cpu->arch < DP_CPU_ARCHTYPE_486OLDSLOW)
				goto illegal_opcode;
			BSWAPD(dcr_reg_edi);
			break;
		default:
illegal_opcode:
			CPU_Exception(6, 0);
			continue;
		}
		SAVEIP;
	}
	FillFlags(cpu);
	return DP_CALLBACK_NONE;

decode_end:
	SAVEIP;
	FillFlags(cpu);
	return DP_CALLBACK_NONE;
}

static u32 CPU_Core_Normal_Trap_Run(struct dp_cpu *cpu)
{
	cpu->block.trap_skip = DP_FALSE;

	u32 ret = dp_cpu_decode_normal_run(cpu);

	if (!cpu->block.trap_skip)
		CPU_HW_Interrupt(1);

	cpu->decoder.func = &dp_cpu_decode_normal_run;

	return ret;
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
