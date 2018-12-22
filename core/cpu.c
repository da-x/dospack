#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_CPU
#define DP_LOGGING           (cpu->logging)

#include "cpu.h"
#include "cpu_inlines.h"
#include "cpu_decode.h"
#include "marshal.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CPU_CHECK_COND(cond,msg,exc,sel) do {				\
		if (cond) {						\
			DP_FAT("%s: exc=%x, sel=%x", msg, exc, sel);	\
		}							\
	} while (0);

phys_addr_t dp_cpu_get_phyaddr(struct dp_cpu *cpu, u16 seg, u32 offset)
{
	if (seg == dp_cpu_seg_value(dp_seg_cs))
		return dp_cpu_seg_phys(dp_seg_cs) + offset;

	if (cpu->block.pmode  &&  !(dcr_reg_flags & DC_FLAG_VM)) {
		union dp_cpu_descriptor desc;

		if (dp_cpu_get_gdt_descriptor(cpu, seg, &desc))
			return dp_cpu_desc_get_base(&desc) + offset;

		DP_FAT("could not read descriptor");
	}

	return (((u32)seg) << 4) + offset;
}

void dp_cpu_set_flags(struct dp_cpu *cpu, u32 word, u32 mask)
{
	mask |= cpu->flag_id_toggle;
	dcr_reg_flags = (dcr_reg_flags & ~mask) | (word & mask) | 2;
	cpu->block.direction = 1 - ((dcr_reg_flags & DC_FLAG_DF) >> 9);
}

void dp_cpu_descriptor_save(struct dp_cpu *cpu, union dp_cpu_descriptor *desc, phys_addr_t addr)
{
	u32 *data = (u32 *) desc;

	cpu->block.mpl = 0;
	dp_memv_writed(cpu->memory, addr, *data);
	dp_memv_writed(cpu->memory, addr + 4, *(data + 1));
	cpu->block.mpl = 03;
}

void dp_cpu_descriptor_load(struct dp_cpu *cpu, union dp_cpu_descriptor *desc, phys_addr_t addr)
{
	u32 *data = (u32 *) desc;

	cpu->block.mpl = 0;
	*data = dp_memv_readd(cpu->memory, addr);
	*(data + 1) = dp_memv_readd(cpu->memory, addr + 4);
	cpu->block.mpl = 3;
}

enum dp_bool dp_cpu_get_descriptor(struct dp_cpu *cpu, struct dp_cpu_descriptor_table *table, u32 selector,
				   union dp_cpu_descriptor *desc)
{
	selector &= ~7;
	if (selector >= table->limit)
		return DP_FALSE;

	dp_cpu_descriptor_load(cpu, desc, table->base + selector);
	return DP_TRUE;
}

enum dp_bool dp_cpu_get_gdt_descriptor(struct dp_cpu *cpu, u32 selector, union dp_cpu_descriptor *desc)
{
	struct dp_cpu_gdt_descriptor_table *gdt = &cpu->block.gdt;
	u32 address = selector & ~7;

	if (selector & 4) {
		if (address >= gdt->ldt_limit)
			return DP_FALSE;

		dp_cpu_descriptor_load(cpu, desc, gdt->ldt_base + selector);
		return DP_TRUE;
	}

	return dp_cpu_get_descriptor(cpu, &cpu->block.gdt.table, address, desc);
}

enum dp_bool dp_cpu_set_gdt_descriptor(struct dp_cpu *cpu, u32 selector, union dp_cpu_descriptor *desc)
{
	struct dp_cpu_gdt_descriptor_table *gdt = &cpu->block.gdt;
	u32 address = selector & ~7;

	if (selector & 4) {
		if (address >= gdt->ldt_limit)
			return DP_FALSE;

		dp_cpu_descriptor_save(cpu, desc, gdt->ldt_base + selector);
		return DP_TRUE;
	}

	dp_cpu_descriptor_save(cpu, desc, gdt->table.base + address);
	return DP_TRUE;
}

enum dp_bool dp_cpu_lldt(struct dp_cpu *cpu, u32 value)
{
	struct dp_cpu_gdt_descriptor_table *gdt = &cpu->block.gdt;
	union dp_cpu_descriptor desc;

	if ((value & 0xfffc) == 0) {
		gdt->ldt_value = 0;
		gdt->ldt_base = 0;
		gdt->ldt_limit = 0;
		return DP_TRUE;
	}

	if (!dp_cpu_get_gdt_descriptor(cpu, value, &desc))
		return !dp_cpu_prepareexception(cpu, DC_EXCEPTION_GP, value);

	if (desc.seg.type != DC_DESC_LDT)
		return !dp_cpu_prepareexception(cpu, DC_EXCEPTION_GP, value);

	if (!desc.seg.p)
		return !dp_cpu_prepareexception(cpu, DC_EXCEPTION_NP, value);

	gdt->ldt_base = dp_cpu_desc_get_base(&desc);
	gdt->ldt_limit = dp_cpu_desc_get_limit(&desc);
	gdt->ldt_value = value;
	return DP_TRUE;
}

void dp_cpu_set_crx(struct dp_cpu *cpu, u32 cr, u32 value)
{
	switch (cr) {
	case 0:{
			u32 changed = cpu->block.cr0 ^ value;
			if (!changed)
				return;

			cpu->block.cr0 = value;

			if (value & DC_CR0_PROTECTION) {
				DP_FAT("paing not supported");
			} else {
				cpu->block.pmode = DP_FALSE;

				if (value & DC_CR0_PAGING) {
					DP_ERR("Paging requested without PE=1");
				}

				/* FIXME: Disable paging if we enabled it earlier */
				DP_INF("Entering real mode");
			}
			break;
		}
	case 2:{
			DP_FAT("paging not supported");
			break;
		}
	case 3:
		DP_FAT("paging not supported");
		break;
	default:
		DP_FAT("unhandled mov cr%d: value=%x", cr, value);
		break;
	}
}

enum TSwitchType {
	TSwitch_JMP, TSwitch_CALL_INT, TSwitch_IRET
};

enum dp_bool dp_cpu_switch_task(struct dp_cpu *cpu, u32 new_tss_selector, enum TSwitchType tstype, u32 old_eip)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
#if (0)
	FillFlags();
	TaskStateSegment new_tss;
	if (!new_tss.SetSelector(new_tss_selector))
		E_Exit("Illegal TSS for switch, selector=%x, switchtype=%x",new_tss_selector,tstype);
	if (tstype==TSwitch_IRET) {
		if (!new_tss.desc.IsBusy())
			E_Exit("TSS not busy for IRET");
	} else {
		if (new_tss.desc.IsBusy())
			E_Exit("TSS busy for JMP/CALL/INT");
	}
	Bitu new_cr3=0;
	Bitu new_eax,new_ebx,new_ecx,new_edx,new_esp,new_ebp,new_esi,new_edi;
	Bitu new_es,new_cs,new_ss,new_ds,new_fs,new_gs;
	Bitu new_ldt,new_eip,new_eflags;
	/* Read new context from new TSS */
	if (new_tss.is386) {
		new_cr3=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,cr3));
		new_eip=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,eip));
		new_eflags=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,eflags));
		new_eax=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,eax));
		new_ecx=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,ecx));
		new_edx=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,edx));
		new_ebx=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,ebx));
		new_esp=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,esp));
		new_ebp=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,ebp));
		new_edi=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,edi));
		new_esi=dp_memv_readd(cpu->memory, new_tss.base+offsetof(TSS_32,esi));

		new_es=dp_memv_readw(cpu->memory, new_tss.base+offsetof(TSS_32,dp_seg_es));
		new_cs=dp_memv_readw(cpu->memory, new_tss.base+offsetof(TSS_32,dp_seg_cs));
		new_ss=dp_memv_readw(cpu->memory, new_tss.base+offsetof(TSS_32,dp_seg_ss));
		new_ds=dp_memv_readw(cpu->memory, new_tss.base+offsetof(TSS_32,dp_seg_ds));
		new_fs=dp_memv_readw(cpu->memory, new_tss.base+offsetof(TSS_32,dp_seg_fs));
		new_gs=dp_memv_readw(cpu->memory, new_tss.base+offsetof(TSS_32,dp_seg_gs));
		new_ldt=dp_memv_readw(cpu->memory, new_tss.base+offsetof(TSS_32,ldt));
	} else {
		E_Exit("286 task switch");
		new_cr3=0;
		new_eip=0;
		new_eflags=0;
		new_eax=0;	new_ecx=0;	new_edx=0;	new_ebx=0;
		new_esp=0;	new_ebp=0;	new_edi=0;	new_esi=0;

		new_es=0;	new_cs=0;	new_ss=0;	new_ds=0;	new_fs=0;	new_gs=0;
		new_ldt=0;
	}

	/* Check if we need to clear busy bit of old TASK */
	if (tstype==TSwitch_JMP || tstype==TSwitch_IRET) {
		cpu_tss.desc.SetBusy(false);
		cpu_tss.SaveSelector();
	}
	u32 old_flags = reg_flags;
	if (tstype==TSwitch_IRET) old_flags &= (~FLAG_NT);

	/* Save current context in current TSS */
	if (cpu_tss.is386) {
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,eflags),old_flags);
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,eip),old_eip);

		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,eax),dcr_reg_eax);
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,ecx),dcr_reg_ecx);
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,edx),dcr_reg_edx);
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,ebx),dcr_reg_ebc);
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,esp),dcr_reg_esp);
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,ebp),dcr_reg_ebp);
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,esi),dcr_reg_esi);
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,edi),dcr_reg_edi);

		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,dp_seg_es),SegValue(dp_seg_es));
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,dp_seg_cs),SegValue(dp_seg_cs));
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,dp_seg_ss),SegValue(dp_seg_ss));
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,dp_seg_ds),SegValue(dp_seg_ds));
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,dp_seg_fs),SegValue(dp_seg_fs));
		dp_memv_writed(cpu->memory, (cpu_tss.base+offsetof(TSS_32,dp_seg_gs),SegValue(dp_seg_gs));
	} else {
		E_Exit("286 task switch");
	}

	/* Setup a back link to the old TSS in new TSS */
	if (tstype==TSwitch_CALL_INT) {
		if (new_tss.is386) {
			dp_memv_writed(cpu->memory, (new_tss.base+offsetof(TSS_32,back),cpu_tss.selector);
		} else {
			dp_memv_writew(cpu->memory, (new_tss.base+offsetof(TSS_16,back),cpu_tss.selector);
		}
		/* And make the new task's eflag have the nested task bit */
		new_eflags|=FLAG_NT;
	}
	/* Set the busy bit in the new task */
	if (tstype==TSwitch_JMP || tstype==TSwitch_CALL_INT) {
		new_tss.desc.SetBusy(true);
		new_tss.SaveSelector();
	}

//	cpu.cr0|=CR0_TASKSWITCHED;
	if (new_tss_selector == cpu_tss.selector) {
		dcr_reg_eip = old_eip;
		new_cs = SegValue(dp_seg_cs);
		new_ss = SegValue(dp_seg_ss);
		new_ds = SegValue(dp_seg_ds);
		new_es = SegValue(dp_seg_es);
		new_fs = SegValue(dp_seg_fs);
		new_gs = SegValue(dp_seg_gs);
	} else {
	
		/* Setup the new cr3 */
		PAGING_SetDirBase(new_cr3);

		/* Load new context */
		if (new_tss.is386) {
			dcr_reg_eip=new_eip;
			CPU_SetFlags(new_eflags,FMASK_ALL | FLAG_VM);
			DP_FAT("FLAG_VM not supported by IO layer (need to implement hooks)");
			dcr_reg_eax=new_eax;
			dcr_reg_ecx=new_ecx;
			dcr_reg_edx=new_edx;
			dcr_reg_ebc=new_ebx;
			dcr_reg_esp=new_esp;
			dcr_reg_ebp=new_ebp;
			dcr_reg_edi=new_edi;
			dcr_reg_esi=new_esi;

//			new_cs=dp_memv_readw(cpu->memory, new_tss.base+offsetof(TSS_32,dp_seg_cs));
		} else {
			E_Exit("286 task switch");
		}
	}
	/* Load the new selectors */
	if (reg_flags & FLAG_VM) {
		SegSet16(dp_seg_cs,new_cs);
		cpu.code.big=false;
		cpu.cpl=3;			//We don't have segment caches so this will do
	} else {
		/* Protected mode task */
		if (new_ldt!=0) CPU_LLDT(new_ldt);
		/* Load the new CS*/
		Descriptor cs_desc;
		cpu.cpl=new_cs & 3;
		if (!cpu.gdt.GetDescriptor(new_cs,cs_desc))
			E_Exit("Task switch with CS beyond limits");
		if (!cs_desc.saved.seg.p)
			E_Exit("Task switch with non present code-segment");
		switch (cs_desc.Type()) {
		case DESC_CODE_N_NC_A:		case DESC_CODE_N_NC_NA:
		case DESC_CODE_R_NC_A:		case DESC_CODE_R_NC_NA:
			if (cpu.cpl != cs_desc.DPL()) E_Exit("Task CS RPL != DPL");
			goto doconforming;
		case DESC_CODE_N_C_A:		case DESC_CODE_N_C_NA:
		case DESC_CODE_R_C_A:		case DESC_CODE_R_C_NA:
			if (cpu.cpl < cs_desc.DPL()) E_Exit("Task CS RPL < DPL");
doconforming:
			Segs.phys[dp_seg_cs]=cs_desc.GetBase();
			cpu.code.big=cs_desc.Big()>0;
			Segs.val[dp_seg_cs]=new_cs;
			break;
		default:
			E_Exit("Task switch CS Type %d",cs_desc.Type());
		}
	}
	CPU_SetSegGeneral(dp_seg_es,new_es);
	CPU_SetSegGeneral(dp_seg_ss,new_ss);
	CPU_SetSegGeneral(dp_seg_ds,new_ds);
	CPU_SetSegGeneral(dp_seg_fs,new_fs);
	CPU_SetSegGeneral(dp_seg_gs,new_gs);
	if (!cpu_tss.SetSelector(new_tss_selector)) {
		LOG(LOG_CPU,LOG_NORMAL)("TaskSwitch: set tss selector %X failed",new_tss_selector);
	}
//	cpu_tss.desc.SetBusy(true);
//	cpu_tss.SaveSelector();
//	LOG_MSG("Task CPL %X CS:%X IP:%X SS:%X SP:%X eflags %x",cpu.cpl,SegValue(dp_seg_cs),dcr_reg_eip,SegValue(dp_seg_ss),dcr_reg_esp,reg_flags);
	return true;
#endif
}

void dp_cpu_jmp(struct dp_cpu *cpu, enum dp_bool use32, u32 selector, u32 offset, u32 oldeip)
{
	if (!cpu->block.pmode || (dcr_reg_flags & DC_FLAG_VM)) {
		if (!use32) {
			dcr_reg_eip = offset & 0xffff;
		} else {
			dcr_reg_eip = offset;
		}

		dp_seg_set16(&cpu->segs.cs, selector);
		cpu->block.code.big = DP_FALSE;
		return;
	} else {
		u32 rpl = selector & 3;
		union dp_cpu_descriptor desc;

		CPU_CHECK_COND((selector & 0xfffc) == 0, "JMP:CS selector zero", DC_EXCEPTION_GP, 0);
		CPU_CHECK_COND(!dp_cpu_get_gdt_descriptor(cpu, selector, &desc),
			       "JMP:CS beyond limits", DC_EXCEPTION_GP, selector & 0xfffc);

		switch (desc.seg.type) {
		case DC_DESC_CODE_N_NC_A:
		case DC_DESC_CODE_N_NC_NA:
		case DC_DESC_CODE_R_NC_A:
		case DC_DESC_CODE_R_NC_NA:
			CPU_CHECK_COND(rpl > cpu->block.cpl, "JMP:NC:RPL>CPL", DC_EXCEPTION_GP, selector & 0xfffc);
			CPU_CHECK_COND(cpu->block.cpl != desc.seg.dpl,
				       "JMP:NC:RPL != DPL", DC_EXCEPTION_GP, selector & 0xfffc);
			DP_DBG("JMP:Code:NC to %X:%X big %d", selector, offset, desc.seg.big);
			goto CODE_jmp;
		case DC_DESC_CODE_N_C_A:
		case DC_DESC_CODE_N_C_NA:
		case DC_DESC_CODE_R_C_A:
		case DC_DESC_CODE_R_C_NA:
			DP_DBG("JMP:Code:C to %X:%X big %d", selector, offset, desc.seg.big);
			CPU_CHECK_COND(cpu->block.cpl < desc.seg.dpl, "JMP:C:CPL < DPL", DC_EXCEPTION_GP, selector & 0xfffc);
		CODE_jmp:
			if (!desc.seg.p) {
				// win
				dp_cpu_exception(cpu, DC_EXCEPTION_NP, selector & 0xfffc);
				return;
			}

			/* Normal jump to another selector:offset */
			cpu->segs.cs.phys = dp_cpu_desc_get_base(&desc);
			cpu->segs.cs.val = (selector & 0xfffc) | cpu->block.cpl;
			cpu->block.code.big = desc.seg.big > 0;
			dcr_reg_eip = offset;
			return;

		case DC_DESC_386_TSS_A:
			CPU_CHECK_COND(desc.seg.dpl < cpu->block.cpl, "JMP:TSS:dpl<cpl", DC_EXCEPTION_GP, selector & 0xfffc);
			CPU_CHECK_COND(desc.seg.dpl < rpl, "JMP:TSS:dpl<rpl", DC_EXCEPTION_GP, selector & 0xfffc);
			DP_DBG("JMP:TSS to %X", selector);;
			dp_cpu_switch_task(cpu, selector, TSwitch_JMP, oldeip);
			break;
		default:
			DP_FAT("JMP Illegal descriptor type %X", desc.seg.type);
		}
	}
}

enum dp_bool dp_cpu_ltr(struct dp_cpu *cpu, u32 selector)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

void dp_cpu_lidt(struct dp_cpu *cpu, u32 limit, u32 base)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_lgdt(struct dp_cpu *cpu, u32 limit, u32 base)
{
	DP_UNIMPLEMENTED_FAT;
}

u32 dp_cpu_str(struct dp_cpu *cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

u32 dp_cpu_sldt(struct dp_cpu * cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

u32 dp_cpu_sidt_base(struct dp_cpu * cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

u32 dp_cpu_sidt_limit(struct dp_cpu * cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

u32 dp_cpu_sgdt_base(struct dp_cpu * cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

u32 dp_cpu_sgdt_limit(struct dp_cpu * cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

void dp_cpu_arpl(struct dp_cpu *cpu, u32 * dest_sel, u32 src_sel)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_lar(struct dp_cpu *cpu, u32 selector, u32 * ar)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_lsl(struct dp_cpu *cpu, u32 selector, u32 * limit)
{
	DP_UNIMPLEMENTED_FAT;
}

enum dp_bool dp_cpu_write_crx(struct dp_cpu *cpu, u32 cr, u32 value)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

u32 dp_cpu_get_crx(struct dp_cpu * cpu, u32 cr)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

enum dp_bool dp_cpu_read_crx(struct dp_cpu *cpu, u32 cr, u32 * retvalue)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

enum dp_bool dp_cpu_write_drx(struct dp_cpu *cpu, u32 dr, u32 value)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

enum dp_bool dp_cpu_read_drx(struct dp_cpu *cpu, u32 dr, u32 * retvalue)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

enum dp_bool dp_cpu_write_trx(struct dp_cpu *cpu, u32 dr, u32 value)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

enum dp_bool dp_cpu_read_trx(struct dp_cpu *cpu, u32 dr, u32 * retvalue)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

u32 dp_cpu_smsw(struct dp_cpu * cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

enum dp_bool dp_cpu_lmsw(struct dp_cpu *cpu, u32 word)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

void dp_cpu_verr(struct dp_cpu *cpu, u32 selector)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_verw(struct dp_cpu *cpu, u32 selector)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_call(struct dp_cpu *cpu, enum dp_bool use32, u32 selector, u32 offset, u32 oldeip)
{
	if (!cpu->block.pmode || (dcr_reg_flags & DC_FLAG_VM)) {
		if (!use32) {
			dp_cpu_push16(cpu, dcr_reg_cs.val);
			dp_cpu_push16(cpu, oldeip);
			dcr_reg_eip = offset & 0xffff;
		} else {
			dp_cpu_push32(cpu, dcr_reg_cs.val);
			dp_cpu_push32(cpu, oldeip);
			dcr_reg_eip = offset;
		}

		cpu->block.code.big = DP_FALSE;
		dp_seg_set16(&cpu->segs.cs, selector);
		return;
	} else {
		DP_FAT("dp_cpu_call: pmode not implemented");
	}
}

void dp_cpu_ret(struct dp_cpu *cpu, enum dp_bool use32, u32 bytes, u32 oldeip)
{
	if (!cpu->block.pmode || (dcr_reg_flags & DC_FLAG_VM)) {
		u32 new_ip, new_cs;

		if (!use32) {
			new_ip = dp_cpu_pop16(cpu);
			new_cs = dp_cpu_pop16(cpu);
		} else {
			new_ip = dp_cpu_pop32(cpu);
			new_cs = dp_cpu_pop32(cpu) & 0xffff;
		}

		dcr_reg_esp += bytes;
		dp_seg_set16(&cpu->segs.cs, new_cs);
		dcr_reg_eip = new_ip;
		cpu->block.code.big = DP_FALSE;
		return;
	} else {
		DP_FAT("dp_cpu_ret: pmode not implemented");
	}
}

void dp_cpu_iret(struct dp_cpu *cpu, enum dp_bool use32, u32 oldeip)
{
	u32 new_cs;

	if (cpu->block.pmode) {
		DP_FAT("iret real mode not implemented");
		return;
	}

	if (use32) {
		dcr_reg_eip = dp_cpu_pop32(cpu);
		new_cs = dp_cpu_pop32(cpu);
		dp_seg_set16(&cpu->segs.cs, new_cs);
		dp_cpu_set_flags(cpu, dp_cpu_pop32(cpu), DC_FMASK_ALL);
	} else {
		dcr_reg_eip = dp_cpu_pop16(cpu);
		new_cs = dp_cpu_pop16(cpu);
		dp_seg_set16(&cpu->segs.cs, new_cs);
		dp_cpu_set_flags(cpu, dp_cpu_pop16(cpu), DC_FMASK_ALL & 0xffff);
	}

	cpu->block.code.big = DP_FALSE;
	dp_destroy_condition_flags(cpu);
}

void dp_cpu_hlt(struct dp_cpu *cpu, u32 oldeip)
{
	DP_UNIMPLEMENTED_FAT;
}

enum dp_bool dp_cpu_popf(struct dp_cpu *cpu, u32 use32)
{
	u32 mask = DC_FMASK_ALL;

	if (cpu->block.pmode) {
		DP_FAT("not implemented popf for protected mode");
		return DP_FALSE;
	}

	if (use32)
		dp_cpu_set_flags(cpu, dp_cpu_pop32(cpu), mask);
	else
		dp_cpu_set_flags(cpu, dp_cpu_pop16(cpu), mask & 0xffff);

	dp_destroy_condition_flags(cpu);
	return DP_FALSE;
}

enum dp_bool dp_cpu_pushf(struct dp_cpu *cpu, u32 use32)
{
	if (cpu->block.pmode) {
		DP_FAT("not implemented popf for protected mode");
		return DP_FALSE;
	}

	if (use32)
		dp_cpu_push32(cpu, dcr_reg_flags & 0xfcffff);
	else
		dp_cpu_push16(cpu, dcr_reg_flags);

	dp_decode_fill_flags(cpu);
	return DP_FALSE;
}

enum dp_bool dp_cpu_cli(struct dp_cpu *cpu)
{
	if (cpu->block.pmode) {
		DP_FAT("not implemented cli for protected mode");

		// && ((!GETFLAG(VM) && (GETFLAG_IOPL < cpu.cpl)) || (GETFLAG(VM) && (GETFLAG_IOPL < 3)))
		return dp_cpu_prepareexception(cpu, DC_EXCEPTION_GP, 0);
	}

	DC_SET_FLAGBIT(IF, DP_FALSE);
	return DP_FALSE;
}

enum dp_bool dp_cpu_sti(struct dp_cpu *cpu)
{
	if (cpu->block.pmode) {
		DP_FAT("not implemented sti for protected mode");

		// && ((!GETFLAG(VM) && (GETFLAG_IOPL < cpu.cpl)) || (GETFLAG(VM) && (GETFLAG_IOPL < 3)))
		return dp_cpu_prepareexception(cpu, DC_EXCEPTION_GP, 0);
	}

	DC_SET_FLAGBIT(IF, DP_TRUE);
	return DP_FALSE;
}

enum dp_bool dp_cpu_io_exception(struct dp_cpu *cpu, u32 port, u32 size)
{
	if (cpu->block.pmode) {
		DP_FAT("not implemented io exceptionsfor protected mode");
	}

	return DP_FALSE;
}

void dp_cpu_runexception(struct dp_cpu *cpu)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_enter(struct dp_cpu *cpu, enum dp_bool use32, u32 bytes, u32 level)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_interrupt(struct dp_cpu *cpu, u32 num, u32 type, u32 oldeip)
{
	real_pt_addr_t csip;

	dp_decode_fill_flags(cpu);

	DP_DBG("interrupt %d, type %d, eip %d", num, type, oldeip);

	/* TODO: handle debug interrupts */

	if (cpu->block.pmode) {
		DP_FAT("not implemented interrupt for protected mode");
		return;
	}

	/* Save everything on a 16-bit stack */
	dp_cpu_push16(cpu, dcr_reg_flags & 0xffff);
	dp_cpu_push16(cpu, dcr_reg_cs.val);
	dp_cpu_push16(cpu, oldeip);

	DC_SET_FLAGBIT(IF, DP_FALSE);
	DC_SET_FLAGBIT(TF, DP_TRUE);

	/* Get the new CS:IP from vector table */
	csip = dp_memv_readd(cpu->memory, cpu->block.idt.base + num * 4);
	dcr_reg_eip = real_to_offset(csip);
	dp_seg_set16(&dcr_reg_cs, real_to_seg(csip));
	cpu->block.code.big = DP_FALSE;
}

enum dp_bool dp_cpu_prepareexception(struct dp_cpu *cpu, u32 which, u32 error)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

void dp_cpu_exception(struct dp_cpu *cpu, u32 which, u32 error)
{
	DP_UNIMPLEMENTED_FAT;
}

enum dp_bool dp_cpu_setseggeneral(struct dp_cpu *cpu, enum dp_segment_index seg, u32 value)
{
	value &= 0xffff;

	if (!cpu->block.pmode || (dcr_reg_flags & DC_FLAG_VM)) {
		dp_seg_set16(&cpu->segs.segs[seg], value);
		if (seg == dp_seg_ss) {
			cpu->block.stack.big = DP_FALSE;
			cpu->block.stack.mask = 0xffff;
			cpu->block.stack.notmask = 0xffff0000;
		}
		return DP_FALSE;
	} else {
		DP_FAT("dp_cpu_setseggeneral: pmode not implemented");
	}

	return DP_FALSE;
}

enum dp_bool dp_cpu_popseg(struct dp_cpu *cpu, enum dp_segment_index seg, enum dp_bool use32)
{
	u32 val = dp_memv_readw(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + (dcr_reg_esp & cpu->block.stack.mask));
	u32 addsp = use32 ? 0x04 : 0x02;

	if (dp_cpu_setseggeneral(cpu, seg, val))
		return DP_TRUE;

	dcr_reg_esp = (dcr_reg_esp & cpu->block.stack.notmask) | ((dcr_reg_esp + addsp) & cpu->block.stack.mask);
	return DP_FALSE;
}

enum dp_bool dp_cpu_cpuid(struct dp_cpu *cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return DP_FALSE;
}

u32 dp_cpu_pop16(struct dp_cpu * cpu)
{
	u32 val = dp_memv_readw(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + (dcr_reg_esp & cpu->block.stack.mask));
	dcr_reg_esp = (dcr_reg_esp & cpu->block.stack.notmask) | ((dcr_reg_esp + 2) & cpu->block.stack.mask);
	return val;
}

u32 dp_cpu_pop32(struct dp_cpu * cpu)
{
	DP_UNIMPLEMENTED_FAT;
	return 0;
}

void dp_cpu_push16(struct dp_cpu *cpu, u32 value)
{
	u32 new_esp = (dcr_reg_esp & cpu->block.stack.notmask) | ((dcr_reg_esp - 2) & cpu->block.stack.mask);
	dp_memv_writew(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + (new_esp & cpu->block.stack.mask), value);
	dcr_reg_esp = new_esp;
}

void dp_cpu_push32(struct dp_cpu *cpu, u32 value)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_setflags(struct dp_cpu *cpu, u32 word, u32 mask)
{
	DP_UNIMPLEMENTED_FAT;
}

void dp_cpu_callback_szf(struct dp_cpu *cpu, enum dp_bool val)
{
	u16 tempf = dp_memv_readw(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4);
	if (val)
		tempf |= DC_FLAG_ZF;
	else
		tempf &= ~DC_FLAG_ZF;
	dp_memv_writew(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4, tempf);
}

void dp_cpu_callback_scf(struct dp_cpu *cpu, enum dp_bool val)
{
	u16 tempf = dp_memv_readw(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4);
	if (val)
		tempf |= DC_FLAG_CF;
	else
		tempf &= ~DC_FLAG_CF;
	dp_memv_writew(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4, tempf);
}

void dp_cpu_callback_sif(struct dp_cpu *cpu, enum dp_bool val)
{
	u16 tempf = dp_memv_readw(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4);
	if (val)
		tempf |= DC_FLAG_IF;
	else
		tempf &= ~DC_FLAG_IF;
	dp_memv_writew(cpu->memory, dp_cpu_seg_phys(dp_seg_ss) + dcr_reg_sp + 4, tempf);
}

static void dp_cpu_pic_hw_interrupt_callback(void *user_ptr, u32 vector)
{
	struct dp_cpu *cpu = user_ptr;

	dp_cpu_hw_interrupt(cpu, vector);
}

void dp_cpu_init(struct dp_cpu *cpu, struct dp_logging *logging,
		 struct dp_memory *memory, struct dp_io *io, struct dp_paging *paging, struct dp_pic *pic,
		 struct dp_int_callback *int_callback, struct dp_timetrack *timetrack, struct dp_marshal *marshal)
{
	dp_marshal_register_pointee(marshal, cpu, "cpu");
	dp_marshal_register_pointee(marshal, dp_cpu_decode_normal_run, "cpudec_nor");

	memset(cpu, 0, sizeof(*cpu));
	cpu->logging = logging;
	cpu->memory = memory;
	cpu->io = io;
	cpu->pic = pic;
	cpu->paging = paging;
	cpu->int_callback = int_callback;
	cpu->timetrack = timetrack;

	DP_INF("initializing CPU");

	cpu->arch = DP_CPU_ARCHTYPE_MIXED;
	dp_cpu_set_flags(cpu, DC_FLAG_IF, DC_FMASK_ALL);
	cpu->block.cr0 = 0xffffffff;
	dp_cpu_set_crx(cpu, 0, 0);
	cpu->block.stack.mask = 0xffff;
	cpu->block.stack.notmask = 0xffff0000;
	cpu->block.idt.base = 0;
	cpu->block.idt.limit = 1023;
	cpu->block.drx[6] = 0xffff1ff0;	/* FIXME: On Pentium Slow it is different */
	cpu->block.drx[7] = 0x00000400;
	cpu->decoder.func = dp_cpu_decode_normal_run;

	dp_cpu_jmp(cpu, DP_FALSE, 0, 0, 0);

	dp_marshal_register_pointee(marshal, dp_cpu_pic_hw_interrupt_callback, "cpu_hw_intrcb");
	dp_pic_set_hw_intr_callback(pic, dp_cpu_pic_hw_interrupt_callback, cpu);
}

void dp_cpu_marshal(struct dp_cpu *cpu, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);
	dp_marshal_write(marshal, cpu, offsetof(struct dp_cpu, _marshal_sep));
}

void dp_cpu_unmarshal(struct dp_cpu *cpu, struct dp_marshal *marshal)
{
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);
	dp_marshal_read(marshal, cpu, offsetof(struct dp_cpu, _marshal_sep));
	dp_marshal_read_ptr_fix(marshal, (void **)&cpu->decoder.func);
	dp_marshal_read_ptr_fix(marshal, (void **)&cpu->decoder.inst_hook_func);
	dp_marshal_read_ptr_fix(marshal, (void **)&cpu->decoder.inst_hook_data);
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
