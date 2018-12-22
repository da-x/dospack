#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_PIC
#define DP_LOGGING           (pic->logging)

#include <string.h>

#include "pic.h"

static u32 IRQ_priority_order[DP_PIC_IRQS] = {0, 1, 2, 8, 9, 10, 11, 12, 13, 14, 15, 3, 4, 5, 6, 7};
static u16 IRQ_priority_lookup[DP_PIC_IRQS + 1] = {0, 1, 2, 11, 12, 13, 14, 15, 3, 4, 5, 6, 7, 8, 9, 10, 16};

static void write_command(void *user_ptr, u32 port, u8 val)
{
	struct dp_pic *pic = user_ptr;
	struct dp_pic_controller *picx = &pic->pics[port == 0x20 ? 0 : 1];
	u32 irq_base = port == 0x20 ? 0 : DP_PIC_IRQS_PER_CTR;
	u32 i;
	if ((val & 0x10)) {	// ICW1 issued
		if (val & 0x04)
			DP_FAT("PIC: 4 byte interval not handled");
		if (val & 0x08)
			DP_FAT("PIC: level triggered mode not handled");
		if (val & 0xe0)
			DP_FAT("PIC: 8080/8085 mode not handled");
		picx->single = (val & 0x02) == 0x02;
		picx->icw_index = 1;	// next is ICW2
		picx->icw_words = 2 + (val & 0x01);	// =3 if ICW4 needed
	} else if ((val & 0x08)) {	// OCW3 issued
		if (val & 0x04)
			DP_FAT("PIC: poll command not handled");
		if (val & 0x02) {	// function select
			if (val & 0x01)
				picx->request_issr = DP_TRUE;	/* select read interrupt in-service register */
			else
				picx->request_issr = DP_FALSE;	/* select read interrupt request register */
		}
		if (val & 0x40) {	// special mask select
			if (val & 0x20)
				picx->special = DP_TRUE;
			else
				picx->special = DP_FALSE;
			if (pic->pics[0].special || pic->pics[1].special)
				pic->special_mode = DP_TRUE;
			else
				pic->special_mode = DP_FALSE;

			DP_DBG("port %X : special mask %s", port, (picx->special) ? "ON" : "OFF");
		}
	} else {		// OCW2 issued
		if (val & 0x20) {	// EOI commands
			if ((val & 0x80))
				DP_FAT("rotate mode not supported");
			if (val & 0x40) {	// specific EOI
				if (pic->active == (irq_base + val - 0x60U)) {
					pic->irqs[pic->active].inservice = DP_FALSE;
					pic->active = DP_PIC_NO_IRQ_MASK;
					for (i = 0; i < DP_PIC_IRQS; i++) {
						if (pic->irqs[IRQ_priority_order[i]].inservice) {
							pic->active = IRQ_priority_order[i];
							break;
						}
					}
				}
//                              if (val&0x80);  // perform rotation
			} else {	// nonspecific EOI
				if (pic->active < (irq_base + DP_PIC_IRQS_PER_CTR)) {
					pic->irqs[pic->active].inservice = DP_FALSE;
					pic->active = DP_PIC_NO_IRQ_MASK;
					for (i = 0; i < DP_PIC_IRQS; i++) {
						if ((pic->irqs[IRQ_priority_order[i]].inservice)) {
							pic->active = IRQ_priority_order[i];
							break;
						}
					}
				}
//                              if (val&0x80);  // perform rotation
			}
		} else {
			if ((val & 0x40) == 0) {	// rotate in auto EOI mode
				if (val & 0x80)
					picx->rotate_on_auto_eoi = DP_TRUE;
				else
					picx->rotate_on_auto_eoi = DP_FALSE;
			} else if (val & 0x80) {
				DP_DBG("set priority command not handled");
			}	// else NOP command
		}
	}			// end OCW2
}

static void write_data(void *user_ptr, u32 port, u8 val)
{
	struct dp_pic *pic = user_ptr;
	struct dp_pic_controller *picx = &pic->pics[port == 0x21 ? 0 : 1];
	u32 irq_base = (port == 0x21) ? 0 : DP_PIC_IRQS_PER_CTR;
	u32 i;
	enum dp_bool old_irq2_mask = pic->irqs[2].masked;
	switch (picx->icw_index) {
	case 0:		/* mask register */
		DP_DBG("%d mask %X", port == 0x21 ? 0 : 1, val);
		for (i = 0; i <= 7; i++) {
			pic->irqs[i + irq_base].masked = (val & (1 << i)) > 0;
			if (port == 0x21) {
				if (pic->irqs[i + irq_base].active && !pic->irqs[i + irq_base].masked)
					pic->irq_check |= (1 << (i + irq_base));
				else
					pic->irq_check &= ~(1 << (i + irq_base));
			} else {
				if (pic->irqs[i + irq_base].active && !pic->irqs[i + irq_base].masked && !pic->irqs[2].masked)
					pic->irq_check |= (1 << (i + irq_base));
				else
					pic->irq_check &= ~(1 << (i + irq_base));
			}
		}
		if (pic->irqs[2].masked != old_irq2_mask) {
			/* Irq 2 mask has changed recheck second pic */
			for (i = DP_PIC_IRQS_PER_CTR; i < DP_PIC_IRQS; i++) {
				if (pic->irqs[i].active && !pic->irqs[i].masked && !pic->irqs[2].masked)
					pic->irq_check |= (1 << (i));
				else
					pic->irq_check &= ~(1 << (i));
			}
		}
		break;
	case 1:		/* icw2          */
		DP_DBG("%d:Base vector %X", port == 0x21 ? 0 : 1, val);
		for (i = 0; i <= 7; i++) {
			pic->irqs[i + irq_base].vector = (val & 0xf8) + i;
		};
		if (picx->icw_index++ >= picx->icw_words)
			picx->icw_index = 0;
		else if (picx->single)
			picx->icw_index = 3;	/* skip ICW3 in single mode */
		break;
	case 2:		/* icw 3 */
		DP_DBG("%d:ICW 3 %X", port == 0x21 ? 0 : 1, val);
		if (picx->icw_index++ >= picx->icw_words)
			picx->icw_index = 0;
		break;
	case 3:		/* icw 4 */
		/*
		   0        1 8086/8080  0 mcs-8085 mode
		   1        1 Auto EOI   0 Normal EOI
		   2-3     0x Non buffer Mode
		   10 Buffer Mode Slave
		   11 Buffer mode Master
		   4            Special/Not Special nested mode
		 */
		picx->auto_eoi = (val & 0x2) > 0;

		DP_DBG("%d:ICW 4 %X", port == 0x21 ? 0 : 1, val);

		if ((val & 0x01) == 0)
			DP_FAT("PIC:ICW4: %x, 8085 mode not handled", val);
		if ((val & 0x10) != 0)
			DP_INF("PIC:ICW4: %x, special fully-nested mode not handled", val);

		if (picx->icw_index++ >= picx->icw_words)
			picx->icw_index = 0;
		break;
	default:
		DP_DBG("ICW HUH? %X", val);
		break;
	}
}

static u8 read_command(void *user_ptr, u32 port)
{
	struct dp_pic *pic = user_ptr;
	struct dp_pic_controller *picx = &pic->pics[port == 0x20 ? 0 : 1];
	u32 irq_base = (port == 0x20) ? 0 : DP_PIC_IRQS_PER_CTR;
	u32 i;
	u8 ret = 0;
	u8 b = 1;
	if (picx->request_issr) {
		for (i = irq_base; i < irq_base + DP_PIC_IRQS_PER_CTR; i++) {
			if (pic->irqs[i].inservice)
				ret |= b;
			b <<= 1;
		}
	} else {
		for (i = irq_base; i < irq_base + DP_PIC_IRQS_PER_CTR; i++) {
			if (pic->irqs[i].active)
				ret |= b;
			b <<= 1;
		}
		if (irq_base == 0 && (pic->irq_check & 0xff00))
			ret |= 4;
	}
	return ret;
}

static u8 read_data(void *user_ptr, u32 port)
{
	struct dp_pic *pic = user_ptr;
	u32 irq_base = (port == 0x21) ? 0 : DP_PIC_IRQS_PER_CTR;
	u32 i;
	u8 ret = 0;
	u8 b = 1;

	for (i = irq_base; i <= irq_base + 7; i++) {
		if (pic->irqs[i].masked)
			ret |= b;
		b <<= 1;
	}

	return ret;
}


void (*write8)(void *user_ptr, u32 port, u8 val);

static struct dp_io_port command_io_desc = {
	.read8 = read_command,
	.write8 = write_command,
};

static struct dp_io_port data_io_desc = {
	.read8 = read_data,
	.write8 = write_data,
};

static enum dp_bool dp_pic_start_irq(struct dp_pic *pic, u32 i)
{
	u32 picnum;

	/* irqs on second pic only if irq 2 isn't masked */
	if (i > 7 && pic->irqs[2].masked)
		return DP_FALSE;
	pic->irqs[i].active = DP_FALSE;
	pic->irq_check &= ~(1 << i);

	pic->hw_intr_callback(pic->hw_intr_callback_user_ptr, pic->irqs[i].vector);

	picnum = (i & 8) >> 3;

	if (!pic->pics[picnum].auto_eoi) {	//irq 0-7 => pic 0 else pic 1
		pic->active = i;
		pic->irqs[i].inservice = DP_TRUE;
	} else if ((pic->pics[picnum].rotate_on_auto_eoi)) {
		DP_FAT("rotate on auto EOI not handled");
	}

	return DP_TRUE;
}


void dp_pic_init(struct dp_pic *pic, struct dp_logging *logging, struct dp_marshal *marshal, struct dp_io *io)
{
	int i;

	memset(pic, 0, sizeof(*pic));
	pic->logging = logging;
	pic->irq_check = 0;
	pic->index = 0;

	DP_INF("initializing PIC");

	pic->irq_check = 0;
	pic->active = DP_PIC_NO_IRQ_MASK;

	dp_marshal_register_pointee(marshal, pic, "pic");

	for (i = 0; i < DP_PIC_CTRS; i++) {
		pic->pics[i].masked = 0xff;
		pic->pics[i].auto_eoi = DP_FALSE;
		pic->pics[i].rotate_on_auto_eoi = DP_FALSE;
		pic->pics[i].request_issr = DP_FALSE;
		pic->pics[i].special = DP_FALSE;
		pic->pics[i].single = DP_FALSE;
		pic->pics[i].icw_index = 0;
		pic->pics[i].icw_words = 0;
	}

	for (i = 0; i < DP_PIC_IRQS_PER_CTR; i++) {
		pic->irqs[i].active = DP_FALSE;
		pic->irqs[i].masked = DP_TRUE;
		pic->irqs[i].inservice = DP_FALSE;
		pic->irqs[i + DP_PIC_IRQS_PER_CTR].active = DP_FALSE;
		pic->irqs[i + DP_PIC_IRQS_PER_CTR].masked = DP_TRUE;
		pic->irqs[i + DP_PIC_IRQS_PER_CTR].inservice = DP_FALSE;
		pic->irqs[i + DP_PIC_IRQS_PER_CTR].vector = 0x70 + i;
		pic->irqs[i].vector = DP_PIC_IRQS_PER_CTR + i;
	}

	pic->irqs[0].masked = DP_FALSE;	/* Enable system timer */
	pic->irqs[1].masked = DP_FALSE;	/* Enable Keyboard IRQ */
	pic->irqs[2].masked = DP_FALSE;	/* Enable second pic */
	pic->irqs[8].masked = DP_FALSE;	/* Enable RTC IRQ */

	dp_marshal_register_pointee(marshal, &command_io_desc, "piccmdiodesc");
	dp_marshal_register_pointee(marshal, &data_io_desc, "picdataiodesc");

	dp_io_register_ports(io, pic, &command_io_desc, 0x20, 1);
	dp_io_register_ports(io, pic, &data_io_desc, 0x21, 1);
	dp_io_register_ports(io, pic, &command_io_desc, 0xa0, 1);
	dp_io_register_ports(io, pic, &data_io_desc, 0xa1, 1);
}

void dp_pic_set_hw_intr_callback(struct dp_pic *pic, dp_pic_hw_intr_callback_t cb, void *user_ptr)
{
	pic->hw_intr_callback = cb;
	pic->hw_intr_callback_user_ptr = user_ptr;
}

void dp_pic_marshal(struct dp_pic *pic, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);
	dp_marshal_write(marshal, pic, offsetof(struct dp_pic, _marshal_sep));
}

void dp_pic_unmarshal(struct dp_pic *pic, struct dp_marshal *marshal)
{
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);
	dp_marshal_read(marshal, pic, offsetof(struct dp_pic, _marshal_sep));
	dp_marshal_read_ptr_fix(marshal, (void **)&pic->hw_intr_callback);
	dp_marshal_read_ptr_fix(marshal, (void **)&pic->hw_intr_callback_user_ptr);
}

double dp_pic_full_index(struct dp_pic *pic)
{
	DP_FAT("not implemented");

	return pic->index;
}


void dp_pic_activate_irq(struct dp_pic *pic, u32 irq)
{
	pic->irqs[irq].active = DP_TRUE;

	if (irq < DP_PIC_IRQS_PER_CTR) {
		if (!pic->irqs[irq].masked) {
			pic->irq_check |= (1 << irq);
		}
	} else if (irq < DP_PIC_IRQS) {
		if (!pic->irqs[irq].masked && !pic->irqs[2].masked) {
			pic->irq_check |= (1 << irq);
		}
	}
}

void dp_pic_deactivate_irq(struct dp_pic *pic, u32 irq)
{
	if (irq < DP_PIC_IRQS) {
		pic->irqs[irq].active = DP_FALSE;
		pic->irq_check &= ~(1 << irq);
	}
}

void dp_pic_run_irqs(struct dp_pic *pic)
{
	u16 activeIRQ = pic->active;
	if (activeIRQ == DP_PIC_NO_IRQ_MASK)
		activeIRQ = DP_PIC_IRQS;
	/* Get the priority of the active irq */
	u16 Priority_Active_IRQ = IRQ_priority_lookup[activeIRQ];
	u32 i, j;

	/* j is the priority (walker)
	 * i is the irq at the current priority */

	/* If one of the pic->pics is in special mode use a check that cares for that. */
	if (!pic->special_mode) {
		for (j = 0; j < Priority_Active_IRQ; j++) {
			i = IRQ_priority_order[j];
			if (!pic->irqs[i].masked && pic->irqs[i].active) {
				if ((dp_pic_start_irq(pic, i)))
					return;
			}
		}
	} else {		/* Special mode variant */
		for (j = 0; j < DP_PIC_IRQS; j++) {
			i = IRQ_priority_order[j];
			if ((j < Priority_Active_IRQ) || (pic->pics[((i & 8) >> 3)].special)) {
				if (!pic->irqs[i].masked && pic->irqs[i].active) {
					/* the irq line is active. it's not masked and
					 * the irq is allowed priority wise. So let's start it */
					/* If started successfully return, else go for the next */
					if (dp_pic_start_irq(pic, i))
						return;
				}
			}
		}
	}
}

void dp_pic_set_irq_mask(struct dp_pic *pic, u32 irq, enum dp_bool masked)
{
	enum dp_bool old_irq2_mask;
	u32 i;

	if (pic->irqs[irq].masked == masked)
		return;		/* Do nothing if mask doesn't change */

	old_irq2_mask = pic->irqs[2].masked;
	pic->irqs[irq].masked = masked;

	if (irq < DP_PIC_IRQS_PER_CTR) {
		if (pic->irqs[irq].active && !pic->irqs[irq].masked) {
			pic->irq_check |= (1 << (irq));
		} else {
			pic->irq_check &= ~(1 << (irq));
		}
	} else {
		if (pic->irqs[irq].active && !pic->irqs[irq].masked && !pic->irqs[2].masked) {
			pic->irq_check |= (1 << (irq));
		} else {
			pic->irq_check &= ~(1 << (irq));
		}
	}

	if (pic->irqs[2].masked != old_irq2_mask) {
		/* Irq 2 mask has changed recheck second pic */
		for (i = DP_PIC_IRQS_PER_CTR; i < DP_PIC_IRQS; i++) {
			if (pic->irqs[i].active && !pic->irqs[i].masked && !pic->irqs[2].masked)
				pic->irq_check |= (1 << (i));
			else
				pic->irq_check &= ~(1 << (i));
		}
	}
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING

