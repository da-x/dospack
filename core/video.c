#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_VIDEO
#define DP_LOGGING           (video->logging)

#include <string.h>

#include "video.h"
#include "video_gl.h"
#include "video_null.h"

#define VIDEO_HZ   75

static void dp_video_update_frame(struct dp_video *video)
{
	void *pixels = video->ops->get_video_surface((void *)video->engine_data);
	void *vidmem = dp_memp_get_real_seg(video->cpu->memory, 0xa000);

	memcpy(pixels, vidmem, 64000);

	video->ops->put_video_surface((void *)video->engine_data, 0, 0, 320, 200);
	video->platform->ops->frame_ready(video->platform->ptr);
}

static void dp_video_set_mode(struct dp_video *video, u8 new_mode, int blank)
{
	struct dp_cpu *cpu = video->cpu;

	if (new_mode == 0x03) {
		DP_INF("setting text mode");
		video->actual_video_mode = new_mode;
		video->machine_video_mode = new_mode;
		return;
	}

	if (new_mode == 0x13) {
		if (video->actual_video_mode == new_mode) {
			DP_INF("resetting video mode to the same mode");
			void *vidmem = dp_memp_get_real_seg(video->cpu->memory, 0xa000);
			if (blank)
				memset(vidmem, 0, 64000);
			dp_video_update_frame(video);
		} else {
			DP_INF("setting graphic mode 320x200, 256 colors");
			video->ops->set_video_mode((void *)video->engine_data, 320, 200, 8);
		}

		video->actual_video_mode = new_mode;
		video->machine_video_mode = new_mode;
		return;
	}

	DP_FAT("unknown mode: %02x", dcr_reg_al);
}

static void dp_video_int10_set_mode(struct dp_video *video, struct dp_cpu *cpu)
{
	dp_video_set_mode(video, dcr_reg_al, 1);
}

static void dp_video_int10_vga_display_combination(struct dp_video *video, struct dp_cpu *cpu)
{
	DP_WRN("loosly unimplemented vga display combination: AL=%02x", dcr_reg_al);

	dcr_reg_bx = 0x08;
	dcr_reg_ax = 0x1A;
}

static void dp_video_int10_get_video_mode(struct dp_video *video, struct dp_cpu *cpu)
{
	DP_WRN("unimplemented get video mode: AL=%02x", dcr_reg_al);

	dcr_reg_bh = 0xff;
	dcr_reg_ah = 0xff;
	dcr_reg_al = 0xff;
}

static void dp_video_set_colors(struct dp_video *video, void *p, u32 index, u32 count)
{
	video->ops->set_colors((void *)video->engine_data, p, index, count);
}

static void dp_video_int10_set_block_of_dac_registers(struct dp_video *video, struct dp_cpu *cpu)
{
	phys_addr_t phys = dcr_reg_es.phys + dcr_reg_dx;
	u16 index = dcr_reg_bx;
	u16 count = dcr_reg_cx;
	void *p = dp_memp_get_phys(video->cpu->memory, phys);

	DP_DBG("setting color palette at %08x, offset %d, count %d", phys, index, count);

	/* TODO: bounds check */
	memcpy(&video->machine_palette[index*3], p, count*3);
	dp_video_set_colors(video, p, index, count);
}

static void dp_video_int10_palette(struct dp_video *video, struct dp_cpu *cpu)
{
	switch (dcr_reg_al) {
	case 0x12: dp_video_int10_set_block_of_dac_registers(video, cpu); break;
	default:
		DP_FAT("unhandled palette function: AL=%02x", dcr_reg_al);
		break;
	}
}

static u32 dp_video_int10_handler(void *ptr)
{
	struct dp_video *video = ptr;
	struct dp_cpu *cpu = video->cpu;

	switch (dcr_reg_ah) {
	case 0x00: dp_video_int10_set_mode(video, cpu); break;
	case 0x0f: dp_video_int10_get_video_mode(video, cpu); break;
	case 0x10: dp_video_int10_palette(video, cpu); break;
	case 0x1a: dp_video_int10_vga_display_combination(video, cpu); break;
	default:
		DP_FAT("unhandled int 10: AH=%02x", dcr_reg_ah);
		break;
	}

	return DP_CALLBACK_NONE;
}

void dp_video_check_frame_update(struct dp_video *video)
{
	struct dp_timetrack *tt = video->timetrack;
	u32 cur_frame;

	cur_frame = tt->ticks / video->frame_size_in_ticks;
	if (cur_frame != video->cur_frame) {
		dp_video_update_frame(video);
		video->cur_frame = cur_frame;
	}
}

static struct dp_io_port palette_ports = {
};

static u8 vga_write_feature_ctl_read8(void *user_ptr, u32 port)
{
	/* NOTE: Must match logic in dp_video_get_ticks_until_vblank */

	struct dp_video *video = user_ptr;
	struct dp_timetrack *tt = video->timetrack;
	u8 ret = 0;
	u32 in_frame;

	in_frame = tt->ticks % video->frame_size_in_ticks;

	ret |= (video->frame_size_in_ticks / 100 > in_frame) ? 8 : 0;
	dp_video_check_frame_update(video);

	if (ret)
		ret |= 1;
	else
		ret |= ((in_frame % 40) < 11) ? 1 : 0;

	return ret;
}

hwticks_t dp_video_get_ticks_until_vblank(struct dp_video *video)
{
	/* NOTE: Must match logic in vga_write_feature_ctl_read8 */
	struct dp_timetrack *tt = video->timetrack;
	u32 in_frame;
	u32 cutoff;

	in_frame = tt->ticks % video->frame_size_in_ticks;
	cutoff = video->frame_size_in_ticks / 100;

	if (cutoff <= in_frame)
		return video->frame_size_in_ticks - in_frame;

	return 0;
}

static struct dp_io_port vga_write_feature_ctl = {
	.read8 = vga_write_feature_ctl_read8,
};

void dp_video_init(struct dp_video *video, struct dp_logging *logging, struct dp_marshal *marshal,
		   struct dp_cpu *cpu, struct dp_int_callback *int_callback, struct dp_io *io,
		   struct dp_timetrack *timetrack, struct dp_keyboard *keyboard, struct dp_ui *ui,
		   enum dp_bool full_screen, struct dp_platform_context *platform)
{
	enum dp_bool ret;
	struct dp_timetrack *tt = timetrack;

	memset(video, 0, sizeof(*video));
	video->logging = logging;
	video->cpu = cpu;
	video->timetrack = timetrack;
	video->keyboard = keyboard;
	video->full_screen = full_screen;
	video->platform = platform;
	video->cur_frame = 0;
	video->ui = ui;
	video->frame_size_in_ticks = ((tt->ticks_per_second / VIDEO_HZ / 14) * 14);

	DP_INF("initializing VIDEO");

	dp_marshal_register_pointee(marshal, video, "video");

	dp_io_register_ports(io, video, &palette_ports, VGAREG_PEL_MASK, 4);
	dp_marshal_register_pointee(marshal, &palette_ports, "videopalp1");

	dp_io_register_ports(io, video, &vga_write_feature_ctl, VGAREG_VGA_WRITE_FEATURE_CTL, 1);
	dp_marshal_register_pointee(marshal, &vga_write_feature_ctl, "videopalp2");

	dp_int_callback_register_inthandler(int_callback, 0x10, dp_video_int10_handler, video, DP_CB_TYPE_IRET);
	dp_marshal_register_pointee(marshal, dp_video_int10_handler, "video_int10");

	ret = DP_VIDEO_ENGINE(video);

	if (ret == DP_FALSE) {
		DP_FAT("video engine init failure");
	}
}

void dp_video_fini(struct dp_video *video)
{
	video->ops->exit((void *)video->engine_data);
}

void dp_video_marshal(struct dp_video *video, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);

	dp_marshal_write(marshal, video, offsetof(struct dp_video, _marshal_sep));
}

void dp_video_unmarshal(struct dp_video *video, struct dp_marshal *marshal)
{
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);

	dp_marshal_read(marshal, video, offsetof(struct dp_video, _marshal_sep));

	dp_video_set_mode(video, video->machine_video_mode, 0);
	dp_video_set_colors(video, video->machine_palette, 0, 256);
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
