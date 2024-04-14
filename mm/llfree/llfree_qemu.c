#include "linux/compiler_attributes.h"
#include "llfree.h"
#include "llfree_inner.h"
#include <linux/slab.h>
#include "asm/io.h"

#include "llfree_qemu.h"

void noinline llfree_create_buffer(void **buffer, size_t *buffer_len)
{
	*buffer = (llfree_zone_info_t *)kzalloc(sizeof(llfree_zone_info_t),
						GFP_KERNEL);
	if (!buffer) {
		pr_err("llfree_create_buffer: could not allocate memory for llfree buffer\n");
		return;
	}

	*buffer_len = sizeof(llfree_zone_info_t);
}

void noinline llfree_copy_into_buffer(llfree_zone_info_t *llfree_info,
				      void *buffer)
{
	if (!buffer) {
		pr_err("llfree_copy_into_buffer: buffer is null pointer\n");
		return;
	}

	llfree_zone_info_t *dest = (llfree_zone_info_t *)buffer;
	dest->start_pfn = llfree_info->start_pfn;
	dest->pages = llfree_info->pages;
	dest->type = llfree_info->type;
	dest->numa_node_id = llfree_info->numa_node_id;

	// translating gva to gpa
	dest->llfree_meta.local =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.local);
	dest->llfree_meta.trees =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.trees);
	dest->llfree_meta.lower =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.lower);

	dest->zone_free_pages =
		(_Atomic(int64_t) *)virt_to_phys(llfree_info->zone_free_pages);
	dest->zone_llfree_huge_page_counter = (_Atomic(int64_t) *)virt_to_phys(
		llfree_info->zone_llfree_huge_page_counter);
	dest->num_pagecache_reclaimable_pages =
		(_Atomic(int64_t) *)virt_to_phys(
			llfree_info->num_pagecache_reclaimable_pages);
}
