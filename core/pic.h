#ifndef _DOSPACK_PIC_H__
#define _DOSPACK_PIC_H__

#include "common.h"
#include "logging.h"
#include "marshal.h"
#include "io.h"

#define DP_PIC_IRQS          16
#define DP_PIC_MAX_IRQ       (DP_PIC_IRQS - 1)
#define DP_PIC_CTRS          2
#define DP_PIC_IRQS_PER_CTR  8
#define DP_PIC_NO_IRQ_MASK   0xff

struct dp_pic_irq {
	enum dp_bool masked;
	enum dp_bool active;
	enum dp_bool inservice;
	u32 vector;
};

struct dp_pic_controller {
	u32 icw_words;
	u32 icw_index;
	u32 masked;

	enum dp_bool special;
	enum dp_bool auto_eoi;
	enum dp_bool rotate_on_auto_eoi;
	enum dp_bool single;
	enum dp_bool request_issr;
	u8 vector_base;
};

typedef void (*dp_pic_hw_intr_callback_t)(void *user_ptr, u32 vector);

struct dp_pic {
	u32 irq_check;
	u64 index;
	u32 active;

	dp_pic_hw_intr_callback_t hw_intr_callback;
	void *hw_intr_callback_user_ptr;

	struct dp_pic_irq irqs[DP_PIC_IRQS];
	struct dp_pic_controller pics[DP_PIC_CTRS];
	enum dp_bool special_mode;

	char _marshal_sep[0];

	struct dp_logging *logging;
};

void dp_pic_init(struct dp_pic *pic, struct dp_logging *logging, struct dp_marshal *marshal, struct dp_io *io);
void dp_pic_set_hw_intr_callback(struct dp_pic *pic, dp_pic_hw_intr_callback_t cb, void *user_ptr);

void dp_pic_marshal(struct dp_pic *pic, struct dp_marshal *marshal);
void dp_pic_unmarshal(struct dp_pic *pic, struct dp_marshal *marshal);
double dp_pic_full_index(struct dp_pic *pic);

void dp_pic_activate_irq(struct dp_pic *pic, u32 irq);
void dp_pic_deactivate_irq(struct dp_pic *pic, u32 irq);
void dp_pic_run_irqs(struct dp_pic *pic);
void dp_pic_set_irq_mask(struct dp_pic *pic, u32 irq, enum dp_bool masked);

#endif
