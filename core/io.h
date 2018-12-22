#ifndef _DOSPACK_IO_H__
#define _DOSPACK_IO_H__

#include "common.h"
#include "logging.h"
#include "marshal.h"

#define DP_IO_MAX_PORTS  (64*1024 + 3)

struct dp_io_port {
	u8 (*read8)(void *user_ptr, u32 port);
	u16 (*read16)(void *user_ptr, u32 port);
	u32 (*read32)(void *user_ptr, u32 port);

	void (*write8)(void *user_ptr, u32 port, u8 val);
	void (*write16)(void *user_ptr, u32 port, u16 val);
	void (*write32)(void *user_ptr, u32 port, u32 val);
};

struct dp_io_hook {
	enum dp_bool (*read8)(void *user_ptr, u32 port, u32 *value);
	enum dp_bool (*read16)(void *user_ptr, u32 port, u32 *value);
	enum dp_bool (*read32)(void *user_ptr, u32 port, u32 *value);

	enum dp_bool (*write8)(void *user_ptr, u32 port, u32 *value);
	enum dp_bool (*write16)(void *user_ptr, u32 port, u32 *value);
	enum dp_bool (*write32)(void *user_ptr, u32 port, u32 *value);
};

struct dp_io {
	struct dp_io_hook *rw_hook;
	void *rw_hook_user_ptr;

	struct dp_io_port *port[DP_IO_MAX_PORTS];
	void *user_ptrs[DP_IO_MAX_PORTS];

	char _marshal_sep[0];

	struct dp_logging *logging;
};

void dp_io_init(struct dp_io *io, struct dp_logging *logging, struct dp_marshal *marshal);
void dp_io_marshal(struct dp_io *io, struct dp_marshal *marshal);
void dp_io_unmarshal(struct dp_io *io, struct dp_marshal *marshal);

void dp_io_register_ports(struct dp_io *io, void *user_ptr, struct dp_io_port *port_desc, u32 start_port, u32 num_ports);
void dp_io_unregister_ports(struct dp_io *io, void *user_ptr, struct dp_io_port *port_desc, u32 start_port, u32 num_ports);

void dp_io_writeb(struct dp_io *io, u32 addr, u32 value);
void dp_io_writew(struct dp_io *io, u32 addr, u32 value);
void dp_io_writed(struct dp_io *io, u32 addr, u32 value);
u32 dp_io_readb(struct dp_io *io, u32 addr);
u32 dp_io_readw(struct dp_io *io, u32 addr);
u32 dp_io_readd(struct dp_io *io, u32 addr);

#endif
