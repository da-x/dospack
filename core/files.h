#ifndef _DOSPACK_FILES_H__
#define _DOSPACK_FILES_H__

#include "common.h"
#include "game_env.h"

enum dp_fnode_type {
	DP_FNODE_TYPE_NONE,
	DP_FNODE_TYPE_DIR,
	DP_FNODE_TYPE_REGFILE,
};

struct dp_fnode {
	enum dp_fnode_type type;
	char str_name[0x20];  /* length FIXME */

	union {
		struct dp_fdirectory *dir;
		struct dp_fregfile *regfile;
	} u;
};

struct dp_fdirectory {
	struct dp_fnode *nodes;
};

enum dp_fregfile_class {
	DP_FREGFILE_CLASS_CONST,
	DP_FREGFILE_CLASS_VTABLE,
};

struct dp_fregfile {
	enum dp_fregfile_class class;

	union {
		struct dp_fregfile_const *fconst;
		struct dp_fregfile_vtable *vtable;
	} u;
};

struct dp_fregfile_const {
	const u8 *data;
	int size;
};

struct dp_fregfile_vtable {
	u32 (*get_size)(struct dp_game_env *game_env, struct dp_fregfile *regfile);
	void (*set_size)(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 set_size);

	s32 (*read)(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 offset, void *data, u32 data_len);
	s32 (*write)(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 offset, void *data, u32 data_len);
};

struct dp_fnode *dp_files_dir_lookup_name(struct dp_fdirectory *dir, const char *name);

u32 dp_file_get_size(struct dp_game_env *game_env, struct dp_fregfile *regfile);
void dp_file_set_size(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 set_size);
s32 dp_file_read(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 offset, void *data, u32 data_len);
s32 dp_file_write(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 offset, void *data, u32 data_len);

#endif
