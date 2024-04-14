#ifndef _LLFREE_QEMU
#define _LLFREE_QEMU

#ifdef CONFIG_LLFREE

#include <llfree.h>
#include <llfree_types.h>

typedef struct llfree_zone_info {
	llfree_meta_t llfree_meta;
	uint32_t start_pfn;
	uint32_t pages;
	uint32_t type;
	uint32_t numa_node_id;
	_Atomic(int64_t) *zone_free_pages;
	_Atomic(int64_t) *zone_llfree_huge_page_counter;
	_Atomic(int64_t) *num_pagecache_reclaimable_pages;
} llfree_zone_info_t;

void llfree_create_buffer(void **buffer, size_t *buffer_len);
void llfree_copy_into_buffer(llfree_zone_info_t *qemu_info, void *buffer);

#endif // CONFIG_LLFREE
#endif // _LLFREE_QEMU
