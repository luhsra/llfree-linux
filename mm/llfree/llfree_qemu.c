#include "linux/compiler_attributes.h"
#include "llfree.h"
#include "llfree_inner.h"
#include <linux/slab.h>
#include "asm/io.h"

#include "llfree_qemu.h"

void noinline llfree_copy_into_buffer(ll_zone_info_t *llfree_info,
				      void *buffer)
{
	if (!buffer) {
		pr_err("llfree_copy_into_buffer: buffer is null pointer\n");
		return;
	}

	ll_zone_info_t *dest = (ll_zone_info_t *)buffer;
	dest->start_pfn = llfree_info->start_pfn;
	dest->pages = llfree_info->pages;
	dest->type = llfree_info->type;
	dest->node_id = llfree_info->node_id;

	// translating gva to gpa
	dest->llfree_meta.local =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.local);
	dest->llfree_meta.trees =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.trees);
	dest->llfree_meta.lower =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.lower);

	dest->free_pages =
		(_Atomic(int64_t) *)virt_to_phys(llfree_info->free_pages);
	dest->reclaimed_huge = (_Atomic(int64_t) *)virt_to_phys(
		llfree_info->reclaimed_huge);
	dest->file_pages =
		(_Atomic(int64_t) *)virt_to_phys(
			llfree_info->file_pages);
}
