#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_MEMORY
#define DP_LOGGING           (memory->logging)

#include <string.h>
#include <stdio.h>

#include "memory.h"

void dp_mem_init(struct dp_memory *memory, struct dp_logging *logging, u32 size, void *data, struct dp_marshal *marshal)
{
	dp_marshal_register_pointee(marshal, memory, "memory");

	memset(memory, 0, sizeof(*memory));
	memory->logging = logging;
	memory->size = size;
	memory->data = data;

	dp_marshal_register_pointee_range(marshal, data, size, "memdata");

	DP_INF("initializing memory (%d bytes)", size);
}

void dp_mem_marshal(struct dp_memory *memory, struct dp_marshal *marshal)
{
	dp_marshal_write_version(marshal, 1);
	dp_marshal_write(marshal, &memory->size, sizeof(memory->size));
	dp_marshal_write(marshal, memory->data, memory->size);
}

void dp_mem_unmarshal(struct dp_memory *memory, struct dp_marshal *marshal)
{
	u32 version = 0;
	u32 size;

	dp_marshal_read_version(marshal, &version);
	DP_ASSERT(version == 1);

	dp_marshal_read(marshal, &size, sizeof(size));
	if (size > memory->size) {
		DP_FAT("memory resize not implemented");
	}

	memory->size = size;
	dp_marshal_read(marshal, memory->data, memory->size);
}

void dp_mem_dump(struct dp_memory *memory)
{
	FILE *f = fopen("memory.dump", "w");
	if (f == NULL) {
		DP_FAT("cannot dump memory");
	}
	fwrite(memory->data, 1, memory->size, f);
	fclose(f);
}

real_pt_addr_t dp_memp_get_realvec(struct dp_memory *memory, u32 vec)
{
	return dp_memp_readd(memory, vec << 2);
}

void dp_memp_set_realvec(struct dp_memory *memory, u32 vec, real_pt_addr_t real_addr)
{
	dp_memp_writed(memory, vec << 2, real_addr);
}

void dp_memv_block_write(struct dp_memory *memory, phys_addr_t off, const void *p, u32 size)
{
	if (off + size > memory->size) {
		DP_FAT("block write exceeded physical memory");
	}

	DP_DBG("write virt 0x%08x -> %d bytes from %p", off, size, p);
	memcpy(&memory->data[off], p, size);
}

void dp_memv_block_read(struct dp_memory *memory, phys_addr_t off, void *p, u32 size)
{
	if (off + size > memory->size) {
		DP_FAT("block write exceeded physical memory");
	}

	DP_DBG("read virt 0x%08x -> %d bytes from %p", off, size, p);
	memcpy(p, &memory->data[off], size);
}

void dp_memv_readstr(struct dp_memory *memory, phys_addr_t off, char *c, u32 max_size)
{
	int l = 0;

	while (off < memory->size  &&  memory->data[off] != 0  &&  l < (s32)max_size - 1) {
		*c = memory->data[off];
		c++;
		l++;
		off++;
	}

	if (off >= memory->size)
		DP_FAT("str read exceeded physical memory");

	*c = '\0';
}


u8 dp_memv_readb(struct dp_memory *memory, phys_addr_t off)
{
	u8 ret = *(u8 *) & memory->data[off];
	DP_DBG("read virt 0x%08x -> byte 0x%02x", off, ret);
	return ret;
}

u16 dp_memv_readw(struct dp_memory * memory, phys_addr_t off)
{
	u16 ret = le16toh(*(u16 *) & memory->data[off]);
	DP_DBG("read virt 0x%08x -> word 0x%04x", off, ret);
	return ret;
}

u32 dp_memv_readd(struct dp_memory * memory, phys_addr_t off)
{
	u32 ret = le32toh(*(u32 *) & memory->data[off]);
	DP_DBG("read virt 0x%08x -> dword 0x%08x", off, ret);
	return ret;
}

void dp_memv_writeb(struct dp_memory *memory, phys_addr_t off, u8 val)
{
	DP_DBG("write virt 0x%08x <- byte 0x%02x", off, val);
	*(u8 *) & memory->data[off] = val;
}

void dp_memv_writew(struct dp_memory *memory, phys_addr_t off, u16 val)
{
	DP_DBG("write virt 0x%08x <- word 0x%04x", off, val);
	*(u16 *) & memory->data[off] = htole16(val);
}

void dp_memv_writed(struct dp_memory *memory, phys_addr_t off, u32 val)
{
	DP_DBG("write virt 0x%08x <- dword 0x%08x", off, val);
	*(u32 *) & memory->data[off] = htole32(val);
}

/***/

u8 dp_memp_readb(struct dp_memory *memory, phys_addr_t off)
{
	u8 ret = *(u8 *) & memory->data[off];
	DP_DBG("read phy 0x%08x -> byte 0x%02x", off, ret);
	return ret;
}

u16 dp_memp_readw(struct dp_memory * memory, phys_addr_t off)
{
	u16 ret = le16toh(*(u16 *) & memory->data[off]);
	DP_DBG("read phy 0x%08x -> word 0x%04x", off, ret);
	return ret;
}

u32 dp_memp_readd(struct dp_memory * memory, phys_addr_t off)
{
	u32 ret = le32toh(*(u32 *) & memory->data[off]);
	DP_DBG("read phy 0x%08x -> dword 0x%08x", off, ret);
	return ret;
}

void dp_memp_writestr(struct dp_memory *memory, phys_addr_t off, const char *str)
{
	int l = strlen(str), i;

	for (i = 0; i < l; i++)
		dp_memp_writeb(memory, off + i, str[i]);
}

void dp_memp_writeb(struct dp_memory *memory, phys_addr_t off, u8 val)
{
	DP_DBG("write phy 0x%08x <- byte 0x%02x", off, val);
	*(u8 *) & memory->data[off] = val;
}

void dp_memp_writew(struct dp_memory *memory, phys_addr_t off, u16 val)
{
	DP_DBG("write phy 0x%08x <- word 0x%04x", off, val);
	*(u16 *) & memory->data[off] = htole16(val);
}

void dp_memp_writed(struct dp_memory *memory, phys_addr_t off, u32 val)
{
	DP_DBG("write phy 0x%08x <- dword 0x%08x", off, val);
	*(u32 *) & memory->data[off] = htole32(val);
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
