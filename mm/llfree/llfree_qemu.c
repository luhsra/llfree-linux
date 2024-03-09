#include "linux/compiler_attributes.h"
#include "llfree.h"
#include "llfree_inner.h"
#include <linux/slab.h>
#include "asm/io.h"

#include "llfree_qemu.h"

typedef struct llfree_info_buffer {
	llfree_t qemu_llfree;
	uint32_t zone_start_pfn;
	uint32_t zone_type;
	uint32_t numa_node_id;
	_Atomic(int64_t) *zone_free_pages;
	_Atomic(int64_t) *zone_llfree_huge_page_counter;
	_Atomic(int64_t) *num_pagecache_reclaimable_pages;
} llfree_info_buffer_t;

void noinline llfree_create_buffer(void **buffer, size_t *buffer_len)
{
	*buffer = (llfree_info_buffer_t *)kzalloc(sizeof(llfree_info_buffer_t),
						  GFP_KERNEL);
	if (!buffer) {
		pr_err("llfree_create_buffer: could not allocate memory for llfree buffer\n");
		return;
	}

	*buffer_len = sizeof(llfree_info_buffer_t);
}

void noinline llfree_copy_into_buffer(llfree_zone_info_t *llfree_info,
				      void *buffer)
{
	if (!buffer) {
		pr_err("llfree_copy_into_buffer: buffer is null pointer\n");
		return;
	}

	llfree_info_buffer_t *llfree_info_buffer;
	llfree_t *qemu_llfree;

	// copying
	llfree_info_buffer = (llfree_info_buffer_t *)buffer;
	qemu_llfree = &llfree_info_buffer->qemu_llfree;
	*qemu_llfree = *llfree_info->qemu_llfree;
	qemu_llfree->local = (struct local *)virt_to_phys(qemu_llfree->local);
	llfree_info_buffer->zone_start_pfn = llfree_info->zone_start_pfn;
	llfree_info_buffer->zone_type = llfree_info->zone_type;
	llfree_info_buffer->numa_node_id = llfree_info->numa_node_id;
	llfree_info_buffer->zone_free_pages = llfree_info->zone_free_pages;
	llfree_info_buffer->zone_llfree_huge_page_counter =
		llfree_info->zone_llfree_huge_page_counter;
	llfree_info_buffer->num_pagecache_reclaimable_pages =
		llfree_info->num_pagecache_reclaimable_pages;

	// translating gva to gpa
	qemu_llfree->lower.children =
		(children_t *)virt_to_phys(qemu_llfree->lower.children);
	qemu_llfree->lower.fields =
		(bitfield_t *)virt_to_phys(qemu_llfree->lower.fields);
	qemu_llfree->trees =
		(_Atomic(tree_t) *)virt_to_phys(qemu_llfree->trees);
	llfree_info_buffer->zone_free_pages = (_Atomic(int64_t) *)virt_to_phys(
		llfree_info_buffer->zone_free_pages);
	llfree_info_buffer->zone_llfree_huge_page_counter =
		(_Atomic(int64_t) *)virt_to_phys(
			llfree_info_buffer->zone_llfree_huge_page_counter);
	llfree_info_buffer->num_pagecache_reclaimable_pages =
		(_Atomic(int64_t) *)virt_to_phys(
			llfree_info_buffer->num_pagecache_reclaimable_pages);
}
