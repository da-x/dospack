/**
 * Assuming that files are saved and loaded on the same architecture.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_MARSHAL
#define DP_LOGGING           (marshal->logging)

#define DP_MARSHAL_SIG       "DADP"
#define DP_MARSHAL_SIG_SIZE  4

#include "marshal.h"

void dp_marshal_init(struct dp_marshal *marshal, struct dp_logging *logging)
{
	marshal->num_ptes = 0;
	marshal->logging = logging;
}

void dp_marshal_register_pointee_range(struct dp_marshal *marshal, void *ptr, long size, const char *name, ...)
{
	struct dp_marshal_pointee *mpte;
	va_list ap;

	DP_ASSERT(marshal->num_ptes < DP_MARSHAL_MAX_PTES);
	mpte = &marshal->ptes[marshal->num_ptes];
	mpte->ptr = ptr;
	mpte->size = size;

	va_start(ap, name);
	vsnprintf(mpte->strname, sizeof(mpte->strname), name, ap);
	va_end(ap);

	marshal->num_ptes++;
}

void dp_marshal_register_pointee(struct dp_marshal *marshal, void *ptr, const char *name, ...)
{
	struct dp_marshal_pointee *mpte;
	va_list ap;

	DP_ASSERT(marshal->num_ptes < DP_MARSHAL_MAX_PTES);
	mpte = &marshal->ptes[marshal->num_ptes];
	mpte->ptr = ptr;
	mpte->size = 0;

	va_start(ap, name);
	vsnprintf(mpte->strname, sizeof(mpte->strname), name, ap);
	va_end(ap);

	marshal->num_ptes++;
}

static int mpte_cmp(const void *a, const void *b)
{
	const struct dp_marshal_pointee *mpte_a = a, *mpte_b = b;

	if (mpte_a->ptr > mpte_b->ptr)
		return 1;
	if (mpte_a->ptr < mpte_b->ptr)
		return -1;

	return 0;
}

static struct dp_marshal_pointee *dp_marshal_get_ptem_for_ptr(struct dp_marshal *marshal, void *ptr)
{
	int i;

	for (i = 0; i < marshal->old_num_ptes; i++) {
		struct dp_marshal_pointee *mpte = &marshal->old_ptes[i];

		if (ptr >= mpte->ptr  &&  ptr <= mpte->ptr + mpte->size)
			return mpte;
	}

	return NULL;
}

static struct dp_marshal_pointee *dp_marshal_get_new_ptem_for_old_ptem(struct dp_marshal *marshal,
								       struct dp_marshal_pointee *old_mpte)
{
	int i;

	for (i = 0; i < marshal->num_ptes; i++) {
		struct dp_marshal_pointee *mpte = &marshal->ptes[i];

		if (!strcmp(old_mpte->strname, mpte->strname))
			return mpte;
	}

	return NULL;
}

void dp_marshal_log_dump(struct dp_marshal *marshal)
{
	int i;

	for (i = 0; i < marshal->num_ptes; i++) {
		struct dp_marshal_pointee *mpte = &marshal->ptes[i];

		DP_INF("PTE: %016p %08x %-16s", mpte->ptr, mpte->size, mpte->strname);
	}
}

void dp_marshal_init_done(struct dp_marshal *marshal)
{
	qsort(marshal->ptes, marshal->num_ptes, sizeof(marshal->ptes[0]), mpte_cmp);
}

void dp_marshal_start_load(struct dp_marshal *marshal, const char *filename)
{
	char sig[DP_MARSHAL_SIG_SIZE];
	size_t s;

	DP_ASSERT(marshal->file == NULL);

	marshal->is_dump = 0;
	marshal->file = fopen(filename, "r");
	DP_ASSERT(marshal->file != NULL);

	s = fread(sig, DP_MARSHAL_SIG_SIZE, 1, marshal->file);
	DP_ASSERT(s == 1);
	DP_ASSERT(!memcmp(sig, DP_MARSHAL_SIG, DP_MARSHAL_SIG_SIZE));

	s = fread(&marshal->old_num_ptes, sizeof(marshal->old_num_ptes), 1, marshal->file);
	DP_ASSERT(s == 1);

	s = fread(marshal->old_ptes, sizeof(struct dp_marshal_pointee), marshal->old_num_ptes, marshal->file);
	DP_ASSERT(s == marshal->old_num_ptes);
}

void dp_marshal_start_dump(struct dp_marshal *marshal, const char *filename)
{
	size_t s;
	int i, j;

	DP_ASSERT(marshal->file == NULL);

	marshal->is_dump = 1;
	marshal->file = fopen(filename, "w");
	DP_ASSERT(marshal->file != NULL);

	s = fwrite(DP_MARSHAL_SIG, strlen(DP_MARSHAL_SIG), 1, marshal->file);
	DP_ASSERT(s == 1);

	s = fwrite(&marshal->num_ptes, sizeof(marshal->num_ptes), 1, marshal->file);
	DP_ASSERT(s == 1);

	s = fwrite(marshal->ptes, sizeof(struct dp_marshal_pointee), marshal->num_ptes, marshal->file);
	DP_ASSERT(s == marshal->num_ptes);

	for (i = 0; i < marshal->num_ptes; i++)
		for (j = i+1; j < marshal->num_ptes; j++) {
			if (!strcmp(marshal->ptes[i].strname, marshal->ptes[j].strname)) {
				DP_INF("pointer dup found: %s", marshal->ptes[j].strname);
			}
		}
}

void dp_marshal_write_version(struct dp_marshal *marshal, u32 version)
{
	size_t s;

	DP_ASSERT(marshal->file != NULL);
	DP_ASSERT(marshal->is_dump == 1);

	s = fwrite(&version, sizeof(version), 1, marshal->file);
	DP_ASSERT(s == 1);
}

void dp_marshal_write(struct dp_marshal *marshal, void *ptr, long size)
{
	size_t s;

	DP_ASSERT(marshal->file != NULL);
	DP_ASSERT(marshal->is_dump == 1);

	if (size == 0)
		return;

	s = fwrite(ptr, size, 1, marshal->file);
	DP_ASSERT(s == 1);
}

void dp_marshal_read(struct dp_marshal *marshal, void *ptr, long size)
{
	size_t s;

	DP_ASSERT(marshal->file != NULL);
	DP_ASSERT(marshal->is_dump == 0);

	if (size == 0)
		return;

	s = fread(ptr, size, 1, marshal->file);
	DP_ASSERT(s == 1);
}

void dp_marshal_read_version(struct dp_marshal *marshal, u32 *version)
{
	size_t s;

	DP_ASSERT(marshal->file != NULL);
	DP_ASSERT(marshal->is_dump == 0);

	s = fread(version, sizeof(*version), 1, marshal->file);
	DP_ASSERT(s == 1);
}

void dp_marshal_read_ptr_fix(struct dp_marshal *marshal, void **ptr)
{
	struct dp_marshal_pointee *old_mpte;
	struct dp_marshal_pointee *mpte;
	long diff;

	if (!*ptr)
		return;

	old_mpte = dp_marshal_get_ptem_for_ptr(marshal, *ptr);
	DP_ASSERTF(old_mpte != NULL, "could not find a ptr for %p", *ptr);

	mpte = dp_marshal_get_new_ptem_for_old_ptem(marshal, old_mpte);
	DP_ASSERTF(mpte != NULL, "could not find a ptr for %p", *ptr);

	diff = mpte->ptr - old_mpte->ptr;
	*(char **)(ptr) += diff;

	DP_DBG("fixing ptr at %p: delta 0x%x", ptr, diff);
}

void dp_marshal_end(struct dp_marshal *marshal)
{
	DP_ASSERT(marshal->file != NULL);

	fclose(marshal->file);
	marshal->file = NULL;
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
