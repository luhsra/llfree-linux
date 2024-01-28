#include "linux/gfp_types.h"
#include "linux/mm.h"
#include <linux/time64.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/balloon_compaction.h>
#include "llfree_ihpq.h"
#include "automatic-llfree-balloon-tester.h"

#define BYTES_PER_BASE_PAGE 4096
#define BASE_PAGES_PER_HUGE_PAGE 512
#define BASE_PAGES_PER_MIB 256
#define BASE_PAGES_PER_GIB 1024 * BASE_PAGES_PER_MIB
#define HUGE_PAGES_PER_GIB 512
#define HUGE_PAGE_ORDER 9

uint64_t alloc_test_base_page(uint32_t num_gib)
{
	InflatedHugePageQueue *ihpq;
	uint64_t start_time_ns, end_time_ns, diff_time_ns;
	uint32_t pfn;
	struct page *page;

	printk("alloc_test_base_page\n");
	ihpq = ihpq_create();
	ihpq_init(ihpq, num_gib * BASE_PAGES_PER_GIB);

	start_time_ns = ktime_get_ns();
	for (uint32_t i = 0; i < num_gib * BASE_PAGES_PER_GIB; i++) {
		// same flags as virtio-balloon
		page = alloc_page(GFP_HIGHUSER_MOVABLE | __GFP_NOMEMALLOC |
				  __GFP_NORETRY | __GFP_NOWARN);
		// printk(KERN_INFO "got page %p\n", page);
		ihpq_enqueue(ihpq, page_to_pfn(page));
		// printk(KERN_INFO "enqueued page %p\n", page);
	}
	end_time_ns = ktime_get_ns();

	diff_time_ns = end_time_ns - start_time_ns;

	// cleanup
	for (uint32_t i = 0; i < num_gib * BASE_PAGES_PER_GIB; i++) {
		pfn = ihpq_dequeue(ihpq);
		put_page(pfn_to_page(pfn));
	}

	return diff_time_ns;
}

uint64_t alloc_test_huge_page(uint32_t num_gib)
{
	InflatedHugePageQueue *ihpq;
	uint64_t start_time_ns, end_time_ns, diff_time_ns;
	uint32_t pfn;
	struct page *page;

	printk("alloc_test_huge_page\n");
	ihpq = ihpq_create();
	ihpq_init(ihpq, num_gib * HUGE_PAGES_PER_GIB);

	start_time_ns = ktime_get_ns();
	for (uint32_t i = 0; i < num_gib * HUGE_PAGES_PER_GIB; i++) {
		// same flags as virtio-balloon
		page = alloc_pages(GFP_HIGHUSER_MOVABLE | __GFP_NOMEMALLOC |
					   __GFP_NORETRY | __GFP_NOWARN,
				   HUGE_PAGE_ORDER);
		ihpq_enqueue(ihpq, page_to_pfn(page));
	}
	end_time_ns = ktime_get_ns();

	diff_time_ns = end_time_ns - start_time_ns;

	// cleanup
	for (uint32_t i = 0; i < num_gib * HUGE_PAGES_PER_GIB; i++) {
		pfn = ihpq_dequeue(ihpq);
		__free_pages(pfn_to_page(pfn), HUGE_PAGE_ORDER);
	}

	return diff_time_ns;
}

uint64_t consume_test_base_page(uint32_t num_gib)
{
	InflatedHugePageQueue *ihpq;
	uint64_t start_time_ns, end_time_ns, diff_time_ns;
	uint32_t pfn;
	struct page *page;
	uint32_t *consume_ptr;

	printk("consume_test_base_page\n");
	ihpq = ihpq_create();
	ihpq_init(ihpq, num_gib * BASE_PAGES_PER_GIB);

	start_time_ns = ktime_get_ns();
	for (uint32_t i = 0; i < num_gib * BASE_PAGES_PER_GIB; i++) {
		// same flags as virtio-balloon
		page = alloc_page(GFP_HIGHUSER_MOVABLE | __GFP_NOMEMALLOC |
				  __GFP_NORETRY | __GFP_NOWARN);
		// printk(KERN_WARNING "ENQ PTR\n");
		ihpq_enqueue(ihpq, page_to_pfn(page));

		// printk(KERN_WARNING "CONSUME PTR\n");
		// consume page and (potentially) trigger ept violation
		consume_ptr = (uint32_t *)page_to_virt(page);
		*consume_ptr = 0xff;
	}
	end_time_ns = ktime_get_ns();

	diff_time_ns = end_time_ns - start_time_ns;
	// // cleanup
	for (uint32_t i = 0; i < num_gib * BASE_PAGES_PER_GIB; i++) {
		pfn = ihpq_dequeue(ihpq);
		put_page(pfn_to_page(pfn));
	}

	return diff_time_ns;
}

uint64_t consume_test_huge_page(uint32_t num_gib)
{
	InflatedHugePageQueue *ihpq;
	uint64_t start_time_ns, end_time_ns, diff_time_ns;
	uint32_t pfn;
	struct page *page;
	uint8_t *consume_ptr;

	printk("consume_test_huge_page\n");
	ihpq = ihpq_create();
	ihpq_init(ihpq, num_gib * HUGE_PAGES_PER_GIB);

	start_time_ns = ktime_get_ns();
	for (uint32_t i = 0; i < num_gib * HUGE_PAGES_PER_GIB; i++) {
		// same flags as virtio-balloon
		page = alloc_pages(GFP_HIGHUSER_MOVABLE | __GFP_NOMEMALLOC |
					   __GFP_NORETRY | __GFP_NOWARN,
				   HUGE_PAGE_ORDER);
		ihpq_enqueue(ihpq, page_to_pfn(page));

		// consume page and (potentially) trigger ept violation
		consume_ptr = (uint8_t *)page_to_virt(page);
		for (uint32_t i = 0; i < BASE_PAGES_PER_HUGE_PAGE; i++) {
			*consume_ptr = 0xff;
			consume_ptr += BYTES_PER_BASE_PAGE;
		}
	}
	end_time_ns = ktime_get_ns();

	diff_time_ns = end_time_ns - start_time_ns;

	// cleanup
	for (uint32_t i = 0; i < num_gib * HUGE_PAGES_PER_GIB; i++) {
		pfn = ihpq_dequeue(ihpq);
		__free_pages(pfn_to_page(pfn), HUGE_PAGE_ORDER);
	}

	return diff_time_ns;
}
