#ifndef _DOSPACK_MARSHAL_H__
#define _DOSPACK_MARSHAL_H__

#include <stdio.h>

#include "common.h"
#include "logging.h"

#define DP_MARSHAL_MAX_PTES 0x100

struct dp_marshal_pointee {
	void *ptr;
	unsigned int size;
	char strname[16];
};

struct dp_marshal {
	/* List of pointees */
	struct dp_marshal_pointee ptes[DP_MARSHAL_MAX_PTES];
	u32 num_ptes;
	struct dp_marshal_pointee old_ptes[DP_MARSHAL_MAX_PTES];
	u32 old_num_ptes;

	FILE *file;
	int is_dump;
	struct dp_logging *logging;
};

void dp_marshal_init(struct dp_marshal *marshal, struct dp_logging *logging);
void dp_marshal_init_done(struct dp_marshal *marshal);
void dp_marshal_register_pointee_range(struct dp_marshal *marshal, void *ptr, long size, const char *name, ...);
void dp_marshal_register_pointee(struct dp_marshal *marshal, void *ptr, const char *name, ...);

void dp_marshal_start_load(struct dp_marshal *marshal, const char *filename);
void dp_marshal_start_dump(struct dp_marshal *marshal, const char *filename);
void dp_marshal_write_version(struct dp_marshal *marshal, u32 version);
void dp_marshal_write(struct dp_marshal *marshal, void *ptr, long size);
void dp_marshal_read_version(struct dp_marshal *marshal, u32 *version);
void dp_marshal_read(struct dp_marshal *marshal, void *ptr, long size);
void dp_marshal_read_ptr_fix(struct dp_marshal *marshal, void **ptr);
void dp_marshal_end(struct dp_marshal *marshal);

#endif
