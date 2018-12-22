#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_INT_CALLBACK
#define DP_LOGGING           (int_callback->logging)

#include <string.h>

#include "int_callback.h"

static u32 dp_cb_alloc_callback(struct dp_int_callback *int_callback)
{
	int i;

	for (i=1; i < DP_CB_MAX; i++) {
		struct dp_int_callback_desc *cb = &int_callback->list[i];
		if (cb->used)
			continue;

		cb->used = DP_TRUE;
		return i;
	}

	DP_FAT("could not allocate callback");
	return -1;
}

static int dp_int_callback_setup(struct dp_memory *memory, u32 cb_index, enum dp_cb_type cb_type, phys_addr_t phy_addr,
				 int use_cb)
{
	int i;

	/* TODO: Clean this up */

	switch (cb_type) {
	case DP_CB_TYPE_NONE:
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
		}
		return (use_cb ? 4 : 0);
	case DP_CB_TYPE_IDLE: {
		for (i = 0; i <= 11; i++) {
			dp_memp_writeb(memory, phy_addr, (u8) 0x90);	// NOP
			phy_addr += 1;
		}
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
		}
		return (use_cb ? 16 : 12);
	}
	case DP_CB_TYPE_RETN:
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xC3);	//A RETN Instruction
		return (use_cb ? 5 : 1);
	case DP_CB_TYPE_RETF:
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xCB);	//A RETF Instruction
		return (use_cb ? 5 : 1);
	case DP_CB_TYPE_RETF8:
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xCA);	//A RETF 8 Instruction
		dp_memp_writew(memory, phy_addr + 0x01, (u16) 0x0008);
		return (use_cb ? 7 : 3);
	case DP_CB_TYPE_IRET:
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xCF);	//An IRET Instruction
		return (use_cb ? 5 : 1);
	case DP_CB_TYPE_IRETD:
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x66);	//An IRETD Instruction
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xCF);
		return (use_cb ? 6 : 2);
	case DP_CB_TYPE_IRET_STI:
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFB);	//STI
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x02, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x03, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xCF);	//An IRET Instruction
		return (use_cb ? 6 : 2);
	case DP_CB_TYPE_IRET_EOI_PIC1:
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x50);	// push ax
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xb0);	// mov al, 0x20
		dp_memp_writeb(memory, phy_addr + 0x02, (u8) 0x20);
		dp_memp_writeb(memory, phy_addr + 0x03, (u8) 0xe6);	// out 0x20, al
		dp_memp_writeb(memory, phy_addr + 0x04, (u8) 0x20);
		dp_memp_writeb(memory, phy_addr + 0x05, (u8) 0x58);	// pop ax
		dp_memp_writeb(memory, phy_addr + 0x06, (u8) 0xcf);	//An IRET Instruction
		return (use_cb ? 0x0b : 0x07);
	case DP_CB_TYPE_IRQ0:		// timer int8
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x50);	// push ax
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x52);	// push dx
		dp_memp_writeb(memory, phy_addr + 0x02, (u8) 0x1e);	// push ds
		dp_memp_writew(memory, phy_addr + 0x03, (u16) 0x1ccd);	// int 1c
		dp_memp_writeb(memory, phy_addr + 0x05, (u8) 0xfa);	// cli
		dp_memp_writeb(memory, phy_addr + 0x06, (u8) 0x1f);	// pop ds
		dp_memp_writeb(memory, phy_addr + 0x07, (u8) 0x5a);	// pop dx
		dp_memp_writew(memory, phy_addr + 0x08, (u16) 0x20b0);	// mov al, 0x20
		dp_memp_writew(memory, phy_addr + 0x0a, (u16) 0x20e6);	// out 0x20, al
		dp_memp_writeb(memory, phy_addr + 0x0c, (u8) 0x58);	// pop ax
		dp_memp_writeb(memory, phy_addr + 0x0d, (u8) 0xcf);	//An IRET Instruction
		return (use_cb ? 0x12 : 0x0e);
	case DP_CB_TYPE_IRQ1:		// keyboard int9
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x50);	// push ax
		dp_memp_writew(memory, phy_addr + 0x01, (u16) 0x60e4);	// in al, 0x60
		dp_memp_writew(memory, phy_addr + 0x03, (u16) 0x4fb4);	// mov ah, 0x4f
		dp_memp_writeb(memory, phy_addr + 0x05, (u8) 0xf9);	// stc
		dp_memp_writew(memory, phy_addr + 0x06, (u16) 0x15cd);	// int 15
		if (use_cb) {
			dp_memp_writew(memory, phy_addr + 0x08, (u16) 0x0473);	// jc skip
			dp_memp_writeb(memory, phy_addr + 0x0a, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x0b, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x0c, (u16) cb_index);	//The immediate word
			// jump here to (skip):
			phy_addr += 6;
		}
		dp_memp_writeb(memory, phy_addr + 0x08, (u8) 0xfa);	// cli
		dp_memp_writew(memory, phy_addr + 0x09, (u16) 0x20b0);	// mov al, 0x20
		dp_memp_writew(memory, phy_addr + 0x0b, (u16) 0x20e6);	// out 0x20, al
		dp_memp_writeb(memory, phy_addr + 0x0d, (u8) 0x58);	// pop ax
		dp_memp_writeb(memory, phy_addr + 0x0e, (u8) 0xcf);	//An IRET Instruction
		return (use_cb ? 0x15 : 0x0f);
	case DP_CB_TYPE_IRQ9:		// pic cascade interrupt
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x50);	// push ax
		dp_memp_writew(memory, phy_addr + 0x01, (u16) 0x61b0);	// mov al, 0x61
		dp_memp_writew(memory, phy_addr + 0x03, (u16) 0xa0e6);	// out 0xa0, al
		dp_memp_writew(memory, phy_addr + 0x05, (u16) 0x0acd);	// int a
		dp_memp_writeb(memory, phy_addr + 0x07, (u8) 0xfa);	// cli
		dp_memp_writeb(memory, phy_addr + 0x08, (u8) 0x58);	// pop ax
		dp_memp_writeb(memory, phy_addr + 0x09, (u8) 0xcf);	//An IRET Instruction
		return (use_cb ? 0x0e : 0x0a);
	case DP_CB_TYPE_IRQ12:		// ps2 mouse int74
		if (!use_cb) {
			return -2;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x1e);	// push ds
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x06);	// push es
		dp_memp_writew(memory, phy_addr + 0x02, (u16) 0x6066);	// pushad
		dp_memp_writeb(memory, phy_addr + 0x04, (u8) 0xfc);	// cld
		dp_memp_writeb(memory, phy_addr + 0x05, (u8) 0xfb);	// sti
		dp_memp_writeb(memory, phy_addr + 0x06, (u8) 0xFE);	//GRP 4
		dp_memp_writeb(memory, phy_addr + 0x07, (u8) 0x38);	//Extra Callback instruction
		dp_memp_writew(memory, phy_addr + 0x08, (u16) cb_index);	//The immediate word
		return 0x0a;
	case DP_CB_TYPE_IRQ12_RET:	// ps2 mouse int74 return
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xfa);	// cli
		dp_memp_writew(memory, phy_addr + 0x01, (u16) 0x20b0);	// mov al, 0x20
		dp_memp_writew(memory, phy_addr + 0x03, (u16) 0xa0e6);	// out 0xa0, al
		dp_memp_writew(memory, phy_addr + 0x05, (u16) 0x20e6);	// out 0x20, al
		dp_memp_writew(memory, phy_addr + 0x07, (u16) 0x6166);	// popad
		dp_memp_writeb(memory, phy_addr + 0x09, (u8) 0x07);	// pop es
		dp_memp_writeb(memory, phy_addr + 0x0a, (u8) 0x1f);	// pop ds
		dp_memp_writeb(memory, phy_addr + 0x0b, (u8) 0xcf);	//An IRET Instruction
		return (use_cb ? 0x10 : 0x0c);
	case DP_CB_TYPE_IRQ6_PCJR:	// pcjr keyboard interrupt
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x50);	// push ax
		dp_memp_writew(memory, phy_addr + 0x01, (u16) 0x60e4);	// in al, 0x60
		dp_memp_writew(memory, phy_addr + 0x03, (u16) 0xe03c);	// cmp al, 0xe0
		if (use_cb) {
			dp_memp_writew(memory, phy_addr + 0x05, (u16) 0x0674);	// je skip
			dp_memp_writeb(memory, phy_addr + 0x07, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x08, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x09, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		} else {
			dp_memp_writew(memory, phy_addr + 0x05, (u16) 0x0274);	// je skip
		}
		dp_memp_writew(memory, phy_addr + 0x07, (u16) 0x09cd);	// int 9
		// jump here to (skip):
		dp_memp_writeb(memory, phy_addr + 0x09, (u8) 0xfa);	// cli
		dp_memp_writew(memory, phy_addr + 0x0a, (u16) 0x20b0);	// mov al, 0x20
		dp_memp_writew(memory, phy_addr + 0x0c, (u16) 0x20e6);	// out 0x20, al
		dp_memp_writeb(memory, phy_addr + 0x0e, (u8) 0x58);	// pop ax
		dp_memp_writeb(memory, phy_addr + 0x0f, (u8) 0xcf);	//An IRET Instruction
		return (use_cb ? 0x14 : 0x10);
	case DP_CB_TYPE_MOUSE:
		dp_memp_writew(memory, phy_addr + 0x00, (u16) 0x07eb);	// jmp i33hd
		phy_addr += 9;
		// jump here to (i33hd):
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xCF);	//An IRET Instruction
		return (use_cb ? 0x0e : 0x0a);
	case DP_CB_TYPE_INT16:
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFB);	//STI
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x02, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x03, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xCF);	//An IRET Instruction
		for (i = 0; i <= 0x0b; i++)
			dp_memp_writeb(memory, phy_addr + 0x02 + i, 0x90);
		dp_memp_writew(memory, phy_addr + 0x0e, (u16) 0xedeb);	//jmp callback
		return (use_cb ? 0x10 : 0x0c);
	case DP_CB_TYPE_INT29:		// fast console output
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x50);	// push ax
		dp_memp_writew(memory, phy_addr + 0x01, (u16) 0x0eb4);	// mov ah, 0x0e
		dp_memp_writew(memory, phy_addr + 0x03, (u16) 0x10cd);	// int 10
		dp_memp_writeb(memory, phy_addr + 0x05, (u8) 0x58);	// pop ax
		dp_memp_writeb(memory, phy_addr + 0x06, (u8) 0xcf);	//An IRET Instruction
		return (use_cb ? 0x0b : 0x07);
	case DP_CB_TYPE_HOOKABLE:
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xEB);	//jump near
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x03);	//offset
		dp_memp_writeb(memory, phy_addr + 0x02, (u8) 0x90);	//NOP
		dp_memp_writeb(memory, phy_addr + 0x03, (u8) 0x90);	//NOP
		dp_memp_writeb(memory, phy_addr + 0x04, (u8) 0x90);	//NOP
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x05, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x06, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x07, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x05, (u8) 0xCB);	//A RETF Instruction
		return (use_cb ? 0x0a : 0x06);
	case DP_CB_TYPE_TDE_IRET:	// TandyDAC end transfer
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x02, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0x50);	// push ax
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xb8);	// mov ax, 0x91fb
		dp_memp_writew(memory, phy_addr + 0x02, (u16) 0x91fb);
		dp_memp_writew(memory, phy_addr + 0x04, (u16) 0x15cd);	// int 15
		dp_memp_writeb(memory, phy_addr + 0x06, (u8) 0xfa);	// cli
		dp_memp_writew(memory, phy_addr + 0x07, (u16) 0x20b0);	// mov al, 0x20
		dp_memp_writew(memory, phy_addr + 0x09, (u16) 0x20e6);	// out 0x20, al
		dp_memp_writeb(memory, phy_addr + 0x0b, (u8) 0x58);	// pop ax
		dp_memp_writeb(memory, phy_addr + 0x0c, (u8) 0xcf);	//An IRET Instruction
		return (use_cb ? 0x11 : 0x0d);
/*	case DP_CB_TYPE_IPXESR:		// IPX ESR
		if (!use_cb) E_Exit("ipx esr must implement a callback handler!");
		dp_memp_writeb(memory, phy_addr+0x00,(u8)0x1e);		// push ds
		dp_memp_writeb(memory, phy_addr+0x01,(u8)0x06);		// push es
		dp_memp_writew(memory, phy_addr+0x02,(u16)0xa00f);	// push fs
		dp_memp_writew(memory, phy_addr+0x04,(u16)0xa80f);	// push gs
		dp_memp_writeb(memory, phy_addr+0x06,(u8)0x60);		// pusha
		dp_memp_writeb(memory, phy_addr+0x07,(u8)0xFE);		//GRP 4
		dp_memp_writeb(memory, phy_addr+0x08,(u8)0x38);		//Extra Callback instruction
		dp_memp_writew(memory, phy_addr+0x09,(u16)cb_index);	//The immediate word
		dp_memp_writeb(memory, phy_addr+0x0b,(u8)0xCB);		//A RETF Instruction
		return 0x0c;
	case DP_CB_TYPE_IPXESR_RET:		// IPX ESR return
		if (use_cb) E_Exit("ipx esr return must not implement a callback handler!");
		dp_memp_writeb(memory, phy_addr+0x00,(u8)0xfa);		// cli
		dp_memp_writew(memory, phy_addr+0x01,(u16)0x20b0);	// mov al, 0x20
		dp_memp_writew(memory, phy_addr+0x03,(u16)0xa0e6);	// out 0xa0, al
		dp_memp_writew(memory, phy_addr+0x05,(u16)0x20e6);	// out 0x20, al
		dp_memp_writeb(memory, phy_addr+0x07,(u8)0x61);		// popa
		dp_memp_writew(memory, phy_addr+0x08,(u16)0xA90F);	// pop gs
		dp_memp_writew(memory, phy_addr+0x0a,(u16)0xA10F);	// pop fs
		dp_memp_writeb(memory, phy_addr+0x0c,(u8)0x07);		// pop es
		dp_memp_writeb(memory, phy_addr+0x0d,(u8)0x1f);		// pop ds
		dp_memp_writeb(memory, phy_addr+0x0e,(u8)0xcf);		//An IRET Instruction
		return 0x0f; */
	case DP_CB_TYPE_INT21:
		dp_memp_writeb(memory, phy_addr + 0x00, (u8) 0xFB);	//STI
		if (use_cb) {
			dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xFE);	//GRP 4
			dp_memp_writeb(memory, phy_addr + 0x02, (u8) 0x38);	//Extra Callback instruction
			dp_memp_writew(memory, phy_addr + 0x03, (u16) cb_index);	//The immediate word
			phy_addr += 4;
		}
		dp_memp_writeb(memory, phy_addr + 0x01, (u8) 0xCF);	//An IRET Instruction
		dp_memp_writeb(memory, phy_addr + 0x02, (u8) 0xCB);	//A RETF Instruction
		dp_memp_writeb(memory, phy_addr + 0x03, (u8) 0x51);	// push cx
		dp_memp_writeb(memory, phy_addr + 0x04, (u8) 0xB9);	// mov cx,
		dp_memp_writew(memory, phy_addr + 0x05, (u16) 0x0140);	// 0x140
		dp_memp_writew(memory, phy_addr + 0x07, (u16) 0xFEE2);	// loop $-2
		dp_memp_writeb(memory, phy_addr + 0x09, (u8) 0x59);	// pop cx
		dp_memp_writeb(memory, phy_addr + 0x0A, (u8) 0xCF);	//An IRET Instruction
		return (use_cb ? 15 : 11);

	default:
		break;
	}

	return -1;
}

u32 dp_int_callback_register(struct dp_int_callback *int_callback, dp_cb_func_t func, void *ptr,
			     enum dp_cb_type cb_type, phys_addr_t phy_addr)
{
	struct dp_int_callback_desc *cb;
	u32 cb_index;
	int ret;

	cb_index = dp_cb_alloc_callback(int_callback);
	cb = &int_callback->list[cb_index];

	DP_DBG("Registering callback %d: %p", cb_index, func);

	cb->func = func;
	cb->ptr = ptr;
	cb->type = cb_type;

	if (phy_addr == 0)
		phy_addr = dp_cb_index_to_phyaddr(cb_index);

	ret = dp_int_callback_setup(int_callback->memory, cb_index, cb_type, phy_addr, func != NULL);

	if (ret < 0) {
		DP_FAT("dp_int_callback_setup failed on callback %d, returned %d", cb_index, ret);
	}

	return cb_index;
}

u32 dp_int_callback_register_inthandler(struct dp_int_callback *int_callback, u32 idt_entry,
					dp_cb_func_t func, void *ptr, enum dp_cb_type cb_type)
{
	u32 cb_index;

	cb_index = dp_int_callback_register(int_callback, func, ptr, cb_type, 0);
	dp_memp_set_realvec(int_callback->memory, idt_entry, dp_cb_index_to_realaddr(cb_index));

	return cb_index;
}

u32 dp_int_callback_register_inthandler_addr(struct dp_int_callback *int_callback, u32 idt_entry,
					     dp_cb_func_t func, void *ptr, enum dp_cb_type cb_type,
					     real_pt_addr_t real_addr)
{
	u32 cb_index;

	cb_index = dp_int_callback_register(int_callback, func, ptr, cb_type, real_to_phys(real_addr));
	dp_memp_set_realvec(int_callback->memory, idt_entry, real_addr);

	return cb_index;
}


static u32 dp_cb_invalid_callback(void *ptr)
{
	struct dp_int_callback_desc *desc = ptr;
	struct dp_int_callback *int_callback = desc->ic;
	unsigned int cb_index = desc - int_callback->list;

	DP_FAT("invalid callback %d called", cb_index);

	return DP_CALLBACK_STOP;
}


static u32 dp_cb_stop_callback(void *ptr)
{
	struct dp_int_callback *int_callback = ptr;

	DP_DBG("stop callback called");

	return DP_CALLBACK_STOP;
}

static u32 dp_cb_idle_callback(void *ptr)
{
	struct dp_int_callback *int_callback = ptr;

	DP_DBG("idle callback called");

	return DP_CALLBACK_STOP;
}

static u32 dp_cb_default_callback(void *ptr)
{
	struct dp_int_callback *int_callback = ptr;

	DP_WRN("default callback called");

	return DP_CALLBACK_NONE;
}


void dp_int_callback_init(struct dp_int_callback *int_callback, struct dp_memory *memory, struct dp_logging *logging, struct dp_marshal *marshal)
{
	u32 call_default, call_default2, call_stop;
	phys_addr_t rint_base;
	int i, ct;

	dp_marshal_register_pointee(marshal, int_callback, "callb");
	dp_marshal_register_pointee(marshal, dp_cb_stop_callback, "callb_1");
	dp_marshal_register_pointee(marshal, dp_cb_default_callback, "callb_2");
	dp_marshal_register_pointee(marshal, dp_cb_invalid_callback, "callb_3");
	dp_marshal_register_pointee(marshal, dp_cb_idle_callback, "callb_4");

	memset(int_callback, 0, sizeof(*int_callback));
	int_callback->memory = memory;
	int_callback->logging = logging;

	DP_INF("initializing interrupt callbacks");

	for (i = 0; i < DP_CB_MAX; i++) {
		struct dp_int_callback_desc *cb = &int_callback->list[i];

		cb->ic = int_callback;
		cb->func = dp_cb_invalid_callback;
		cb->ptr = cb;

		dp_marshal_register_pointee(marshal, cb, "callb%d", i);
	}

	call_stop = dp_int_callback_register(int_callback, dp_cb_stop_callback, int_callback, DP_CB_TYPE_NONE, 0);
	dp_int_callback_register(int_callback, dp_cb_idle_callback, int_callback, DP_CB_TYPE_IDLE, 0);
	call_default = dp_int_callback_register(int_callback, dp_cb_default_callback, int_callback, DP_CB_TYPE_IRET, 0);
	call_default2 = dp_int_callback_register(int_callback, dp_cb_default_callback, int_callback, DP_CB_TYPE_IRET, 0);

	for (ct = 0; ct < 0x60; ct++)
		dp_memp_set_realvec(int_callback->memory, ct, dp_cb_index_to_realaddr(call_default));

	for (ct = 0x68; ct < 0x70; ct++)
		dp_memp_set_realvec(int_callback->memory, ct, dp_cb_index_to_realaddr(call_default));

	rint_base = dp_int_callback_base_phy_addr + DP_CB_MAX * DP_CB_SIZE;
	for (i = 0; i <= 0xff; i++) {
		dp_memp_writeb(memory, rint_base, 0xCD);
		dp_memp_writeb(memory, rint_base + 1, (u8) i);
		dp_memp_writeb(memory, rint_base + 2, 0xFE);
		dp_memp_writeb(memory, rint_base + 3, 0x38);
		dp_memp_writeb(memory, rint_base + 4, (u16) call_stop);
		rint_base += 6;
	}

	dp_memp_set_realvec(memory, 0x0e, dp_cb_index_to_realaddr(call_default2));	//design your own railroad
	dp_memp_set_realvec(memory, 0x66, dp_cb_index_to_realaddr(call_default));	//war2d
	dp_memp_set_realvec(memory, 0x67, dp_cb_index_to_realaddr(call_default));
	dp_memp_set_realvec(memory, 0x68, dp_cb_index_to_realaddr(call_default));
	dp_memp_set_realvec(memory, 0x5c, dp_cb_index_to_realaddr(call_default));	//Network stuff

	/* TODO: Add virtualiztion of I/O i.e. call_priv_io */
}

void dp_int_callback_marshal(struct dp_int_callback *int_callback, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);

	dp_marshal_write(marshal, int_callback->list, sizeof(int_callback->list));
}

void dp_int_callback_unmarshal(struct dp_int_callback *int_callback, struct dp_marshal *marshal)
{
	int i;

	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);
	dp_marshal_read(marshal, int_callback->list, sizeof(int_callback->list));

	for (i = 0; i < DP_CB_MAX; i++) {
		struct dp_int_callback_desc *cb = &int_callback->list[i];

		dp_marshal_read_ptr_fix(marshal, (void **)&cb->ic);
		dp_marshal_read_ptr_fix(marshal, (void **)&cb->func);
		dp_marshal_read_ptr_fix(marshal, (void **)&cb->ptr);
	}
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
