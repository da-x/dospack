#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_FILES
#define DP_LOGGING           (game_env->logging)

#include <string.h>

#include "files.h"

struct dp_fnode *dp_files_dir_lookup_name(struct dp_fdirectory *dir, const char *name)
{
	struct dp_fnode *fnode = dir->nodes;

	while (fnode->type != DP_FNODE_TYPE_NONE) {
		if (!strcasecmp(name, fnode->str_name)) {
			return fnode;
		}

		fnode++;
	}

	return NULL;
}

u32 const_get_size(struct dp_game_env *game_env, struct dp_fregfile *regfile)
{
	return regfile->u.fconst->size;
}

void const_set_size(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 set_size)
{
	DP_FAT("Cannot set size of const file");
}

s32 const_read(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 offset, void *data, u32 data_len)
{
	if (data_len + offset > regfile->u.fconst->size)
		data_len = regfile->u.fconst->size - offset;
	memcpy(data, regfile->u.fconst->data + offset, data_len);
	return data_len;
}

s32 const_write(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 offset, void *data, u32 data_len)
{
	DP_FAT("cannot modify const file");
	return -1;
}


static struct dp_fregfile_vtable const_vtable = {
	.get_size = const_get_size,
	.set_size = const_set_size,
	.read = const_read,
	.write = const_write,
};

static inline struct dp_fregfile_vtable *get_regfile_vtable(struct dp_fregfile*regfile)
{
	struct dp_fregfile_vtable *vtable = NULL;

	if (regfile->class == DP_FREGFILE_CLASS_CONST)
		vtable = &const_vtable;
	else if (regfile->class == DP_FREGFILE_CLASS_VTABLE)
		vtable = regfile->u.vtable;

	return vtable;
}

u32 dp_file_get_size(struct dp_game_env *game_env, struct dp_fregfile *regfile)
{
	struct dp_fregfile_vtable *vtable = get_regfile_vtable(regfile);
	return vtable->get_size(game_env, regfile);
}

void dp_file_set_size(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 set_size)
{
	struct dp_fregfile_vtable *vtable = get_regfile_vtable(regfile);
	vtable->set_size(game_env, regfile, set_size);
}

s32 dp_file_read(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 offset, void *data, u32 data_len)
{
	struct dp_fregfile_vtable *vtable = get_regfile_vtable(regfile);
	return vtable->read(game_env, regfile, offset, data, data_len);
}

s32 dp_file_write(struct dp_game_env *game_env, struct dp_fregfile *regfile, u32 offset, void *data, u32 data_len)
{
	struct dp_fregfile_vtable *vtable = get_regfile_vtable(regfile);
	return vtable->write(game_env, regfile, offset, data, data_len);
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
