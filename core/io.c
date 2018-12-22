/*
 * TODO:
 *
 * Seems some I/O handlers need to add virutal machine delay - so
 * games rely on it.
 */

#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_IO
#define DP_LOGGING           (io->logging)

#include <string.h>

#include "io.h"

static u8 dp_io_blocked_port_read8(void *user_ptr, u32 port)
{
	return 0xff;
}

static u16 dp_io_blocked_port_read16(void *user_ptr, u32 port)
{
	return 0xffff;
}

static u32 dp_io_blocked_port_read32(void *user_ptr, u32 port)
{
	return 0xffffffff;
}

static void dp_io_blocked_port_write8(void *user_ptr, u32 port, u8 val)
{
}

static void dp_io_blocked_port_write16(void *user_ptr, u32 port, u16 val)
{
}

static void dp_io_blocked_port_write32(void *user_ptr, u32 port, u32 val)
{
}


struct dp_io_port dp_io_blocked_port = {
	.read8 = dp_io_blocked_port_read8,
	.read16 = dp_io_blocked_port_read16,
	.read32 = dp_io_blocked_port_read32,
	.write8 = dp_io_blocked_port_write8,
	.write16 = dp_io_blocked_port_write16,
	.write32 = dp_io_blocked_port_write32,
};

u8 dp_io_default_port_read8(void *user_ptr, u32 port)
{
	struct dp_io *io = (struct dp_io *)user_ptr;

	DP_WRN("read from I/O port %d", port);

	io->port[port] = &dp_io_blocked_port;
	io->user_ptrs[port] = io;

	return 0xff;
}

u16 dp_io_default_port_read16(void *user_ptr, u32 port)
{
	struct dp_io *io = (struct dp_io *)user_ptr;

	return ((u16)(io->port[port]->read8(user_ptr, port)) |
		((u16)(io->port[port+1]->read8(user_ptr, port+1))) << 8);
}

u32 dp_io_default_port_read32(void *user_ptr, u32 port)
{
	struct dp_io *io = (struct dp_io *)user_ptr;

	return ((u32)(io->port[port]->read8(user_ptr, port)) |
		((u32)(io->port[port+2]->read8(user_ptr, port+2))) << 16);
}

void dp_io_default_port_write8(void *user_ptr, u32 port, u8 val)
{
	struct dp_io *io = (struct dp_io *)user_ptr;

	DP_WRN("write of 0x%08x to I/O port %d", val, port);

	io->port[port] = &dp_io_blocked_port;
	io->user_ptrs[port] = io;
}

void dp_io_default_port_write16(void *user_ptr, u32 port, u16 val)
{
	struct dp_io *io = (struct dp_io *)user_ptr;

	io->port[port  ]->write8(user_ptr, port  , val >> 0);
	io->port[port+1]->write8(user_ptr, port+1, val >> 8);
}

void dp_io_default_port_write32(void *user_ptr, u32 port, u32 val)
{
	struct dp_io *io = (struct dp_io *)user_ptr;

	io->port[port  ]->write16(user_ptr, port  , val >> 0);
	io->port[port+2]->write16(user_ptr, port+2, val >> 16);
}

struct dp_io_port dp_io_default_port = {
	.read8 = dp_io_default_port_read8,
	.read16 = dp_io_default_port_read16,
	.read32 = dp_io_default_port_read32,
	.write8 = dp_io_default_port_write8,
	.write16 = dp_io_default_port_write16,
	.write32 = dp_io_default_port_write32,
};


void dp_io_init(struct dp_io *io, struct dp_logging *logging, struct dp_marshal *marshal)
{
	int i;

	dp_marshal_register_pointee(marshal, io, "io");
	dp_marshal_register_pointee(marshal, &dp_io_default_port, "iodefp");
	dp_marshal_register_pointee(marshal, &dp_io_blocked_port, "ioblkp");

	memset(io, 0, sizeof(*io));
	io->logging = logging;

	DP_INF("initializing IO subsystem");

	for (i=0; i < DP_IO_MAX_PORTS; i++) {
		io->port[i] = &dp_io_default_port;
		io->user_ptrs[i] = io;
	}
}

void dp_io_marshal(struct dp_io *io, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);

	dp_marshal_write(marshal, io, offsetof(struct dp_io, _marshal_sep));
}

void dp_io_unmarshal(struct dp_io *io, struct dp_marshal *marshal)
{
	int i;
	u32 version = 0;
	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);

	dp_marshal_read(marshal, io, offsetof(struct dp_io, _marshal_sep));

	if (io->rw_hook) {
		dp_marshal_read_ptr_fix(marshal, (void **)&io->rw_hook);
	}
	if (io->rw_hook_user_ptr) {
		dp_marshal_read_ptr_fix(marshal, (void **)&io->rw_hook_user_ptr);
	}

	for (i=0; i < DP_IO_MAX_PORTS; i++) {
		dp_marshal_read_ptr_fix(marshal, (void **)&io->port[i]);
		dp_marshal_read_ptr_fix(marshal, (void **)&io->user_ptrs[i]);
	}
}

void dp_io_register_ports(struct dp_io *io, void *user_ptr, struct dp_io_port *port_desc, u32 start_port, u32 num_ports)
{
	int i;

	for (i = start_port; i < start_port + num_ports; i++) {
		io->port[i] = port_desc;
		io->user_ptrs[i] = user_ptr;
	}
}

void dp_io_unregister_ports(struct dp_io *io, void *user_ptr, struct dp_io_port *port_desc, u32 start_port, u32 num_ports)
{
	int i;

	for (i = start_port; i < start_port + num_ports; i++) {
		io->port[i] = &dp_io_default_port;
		io->user_ptrs[i] = io;
	}
}

void dp_io_writeb(struct dp_io *io, u32 addr, u32 value)
{
	DP_DBG("write IO 0x%08x <- byte 0x%02x", addr, value);

	if (io->rw_hook  &&  io->rw_hook->write8(io->rw_hook_user_ptr, addr, &value))
		return;

	if (!io->port[addr]->write8) {
		dp_io_default_port_write8(io, addr, value);
		return;
	}

	io->port[addr]->write8(io->user_ptrs[addr], addr, value);
}

void dp_io_writew(struct dp_io *io, u32 addr, u32 value)
{
	DP_DBG("write IO 0x%08x <- word 0x%04x", addr, value);

	if (io->rw_hook  &&  io->rw_hook->write16(io->rw_hook_user_ptr, addr, &value))
		return;

	if (!io->port[addr]->write16) {
		dp_io_default_port_write16(io, addr, value);
		return;
	}

	io->port[addr]->write16(io->user_ptrs[addr], addr, value);
}

void dp_io_writed(struct dp_io *io, u32 addr, u32 value)
{
	DP_DBG("write IO 0x%08x <- dword 0x%08x", addr, value);

	if (io->rw_hook  &&  io->rw_hook->write32(io->rw_hook_user_ptr, addr, &value))
		return;

	if (!io->port[addr]->write32) {
		dp_io_default_port_write32(io, addr, value);
		return;
	}

	io->port[addr]->write32(io->user_ptrs[addr], addr, value);
}

u32 dp_io_readb(struct dp_io *io, u32 addr)
{
	u32 ret = 0;

	if (!(io->rw_hook  && io->rw_hook->read8(io->rw_hook_user_ptr, addr, &ret))) {
		if (!io->port[addr]->read8) {
			ret = dp_io_default_port_read8(io, addr);
		} else {
			ret = io->port[addr]->read8(io->user_ptrs[addr], addr);
		}
	}

	DP_DBG("read phy 0x%08x -> byte 0x%02x", addr, ret & 0xff);
	return ret;
}

u32 dp_io_readw(struct dp_io * io, u32 addr)
{
	u32 ret = 0;

	if (!(io->rw_hook  && io->rw_hook->read16(io->rw_hook_user_ptr, addr, &ret))) {
		if (!io->port[addr]->read16) {
			ret = dp_io_default_port_read16(io, addr);
		} else {
			ret = io->port[addr]->read16(io->user_ptrs[addr], addr);
		}
	}

	DP_DBG("read phy 0x%08x -> word 0x%04x", addr, ret & 0xffff);
	return ret;
}

u32 dp_io_readd(struct dp_io * io, u32 addr)
{
	u32 ret = 0;

	if (!(io->rw_hook  && io->rw_hook->read32(io->rw_hook_user_ptr, addr, &ret))) {
		if (!io->port[addr]->read32) {
			ret = dp_io_default_port_read32(io, addr);
		} else {
			ret = io->port[addr]->read32(io->user_ptrs[addr], addr);
		}
	}

	DP_DBG("read phy 0x%08x -> dword 0x%08x", addr, ret & 0xffffffff);
	return ret;
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
