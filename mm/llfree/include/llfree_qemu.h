#ifndef _LLFREE_QEMU
#define _LLFREE_QEMU

#ifdef CONFIG_LLFREE

#include <llfree.h>
#include <llfree_types.h>

typedef struct ll_zone_info {
	llfree_meta_t llfree_meta;
	uint32_t start_pfn;
	uint32_t pages;
	uint32_t type;
	uint32_t node_id;
	_Atomic(int64_t) *free_pages;
	_Atomic(int64_t) *reclaimed_huge;
	_Atomic(int64_t) *file_pages;
} ll_zone_info_t;

void llfree_create_buffer(void **buffer, size_t *buffer_len);
void llfree_copy_into_buffer(ll_zone_info_t *qemu_info, void *buffer);

#endif // CONFIG_LLFREE
#endif // _LLFREE_QEMU
