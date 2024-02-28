#include "automatic-llfree-balloon-test-lib.h"
#include "asm-generic/memory_model.h"
#include "linux/completion.h"
#include "linux/gfp_types.h"
#include "linux/kern_levels.h"
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

uint64_t alloc_test_base_page(uint32_t num_gib, gfp_t alloc_flags)
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
		page = alloc_page(alloc_flags);
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

uint64_t alloc_test_huge_page(uint32_t num_gib, gfp_t alloc_flags)
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
		page = alloc_pages(alloc_flags, HUGE_PAGE_ORDER);
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

uint64_t consume_test_base_page(uint32_t num_gib, gfp_t alloc_flags)
{
	uint64_t start_time_ns, end_time_ns, diff_time_ns;
	struct page *page;
	uint32_t *consume_ptr;
	uint32_t *ihpq;
	num_gib = 1;
	printk("consume_test_base_page\n");
	ihpq = kmalloc(num_gib * BASE_PAGES_PER_GIB * sizeof(uint32_t),
		       GFP_KERNEL);

	if (ihpq == NULL) {
		printk(KERN_ERR "NOT ENOUGH MEM NULL POINTER\n");
	}

	start_time_ns = ktime_get_ns();
	for (uint32_t i = 0; i < num_gib * BASE_PAGES_PER_GIB; i++) {
		// same flags as virtio-balloon
		page = alloc_page(alloc_flags);
		ihpq[i] = page_to_pfn(page);
	}

	for (uint32_t i = 0; i < num_gib * BASE_PAGES_PER_GIB; i++) {
		consume_ptr = (uint32_t *)page_to_virt(pfn_to_page(ihpq[i]));
		*consume_ptr = 0xff;
	}
	end_time_ns = ktime_get_ns();

	diff_time_ns = end_time_ns - start_time_ns;

	// cleanup
	for (uint32_t i = 0; i < num_gib * BASE_PAGES_PER_GIB; i++) {
		put_page(pfn_to_page(ihpq[i]));
	}

	kfree(ihpq);

	return diff_time_ns;
}

uint64_t consume_test_huge_page(uint32_t num_gib, gfp_t alloc_flags)
{
	uint64_t *ihpq;
	uint64_t start_time_ns, end_time_ns, diff_time_ns;
	struct page *page;
	uint8_t *consume_ptr;

	num_gib = 1;
	printk("consume_test_huge_page\n");
	ihpq = kmalloc(num_gib * HUGE_PAGES_PER_GIB * sizeof(uint64_t),
		       GFP_KERNEL);

	start_time_ns = ktime_get_ns();
	for (uint32_t i = 0; i < num_gib * HUGE_PAGES_PER_GIB; i++) {
		// same flags as virtio-balloon
		page = alloc_pages(alloc_flags, HUGE_PAGE_ORDER);
		ihpq[i] = page_to_pfn(page);
	}

	for (uint32_t i = 0; i < num_gib * HUGE_PAGES_PER_GIB; i++) {
		// consume page and (potentially) trigger ept violation
		consume_ptr = (uint8_t *)page_to_virt(pfn_to_page(ihpq[i]));
		for (uint32_t i = 0; i < BASE_PAGES_PER_HUGE_PAGE; i++) {
			*consume_ptr = 0xff;
			consume_ptr += BYTES_PER_BASE_PAGE;
		}
	}
	end_time_ns = ktime_get_ns();

	diff_time_ns = end_time_ns - start_time_ns;

	// cleanup
	for (uint32_t i = 0; i < num_gib * HUGE_PAGES_PER_GIB; i++) {
		__free_pages(pfn_to_page(ihpq[i]), HUGE_PAGE_ORDER);
	}

	kfree(ihpq);

	return diff_time_ns;
}

////////////////////////////////////////////////////////////////////////
// currently only two threads supported
#include "linux/mmzone.h"

#define NUM_THREADS 2
#define NUM_REPITITIONS 1000

struct alloc_thread_data {
	struct zone *zone;
	struct completion *comp;
	uint32_t num_gib;
	gfp_t alloc_flags;
};

// for comparisons we need to make sure that we
// auto deflate the same number of huge pages
extern void noinline virtio_llfree_auto_deflate(struct zone *zone);
void auto_deflate_test(struct zone *zone, uint32_t num_repititions)
{
	for (uint32_t i = 0; i < num_repititions; i++) {
		virtio_llfree_auto_deflate(zone);
	}
}

int multithreaded_test_wrapper(void *arg)
{
	struct alloc_thread_data *data;
	data = (struct alloc_thread_data *)arg;
	auto_deflate_test(data->zone, NUM_REPITITIONS);
	// alloc_test_base_page(data->num_gib, data->alloc_flags);
	// alloc_test_huge_page(data->num_gib, data->alloc_flags);
	complete(data->comp);
	return 0;
}

uint64_t alloc_test_multithreaded(uint32_t num_gib)
{
	uint64_t start_time_ns, end_time_ns, diff_time_ns;
	struct task_struct *alloc_threads[NUM_THREADS];
	struct alloc_thread_data alloc_threads_data[NUM_THREADS];
	struct completion comps[NUM_THREADS];

	for (uint32_t i = 0; i < NUM_THREADS; i++) {
		init_completion(&comps[i]);
		alloc_threads_data[i].comp = &comps[i];
		alloc_threads_data[i].num_gib = num_gib;
	}

	alloc_threads_data[0].alloc_flags = ZONE_NORMAL_FLAGS;
	alloc_threads_data[1].alloc_flags = ZONE_DMA_FLAGS;

	struct pglist_data *pgdat = NODE_DATA(first_online_node);
	alloc_threads_data[0].zone = &pgdat->node_zones[ZONE_NORMAL];
	alloc_threads_data[1].zone = &pgdat->node_zones[ZONE_NORMAL];

	start_time_ns = ktime_get_ns();
	for (uint32_t i = 0; i < NUM_THREADS; i++) {
		alloc_threads[i] = kthread_run(multithreaded_test_wrapper,
					       (void *)&alloc_threads_data[i],
					       "alloc-test-multithread");
	}

	for (uint32_t i = 0; i < NUM_THREADS; i++) {
		if (IS_ERR(alloc_threads[i])) {
			return 0;
		}
	}

	for (uint32_t i = 0; i < NUM_THREADS; i++) {
		wait_for_completion(&comps[i]);
	}

	end_time_ns = ktime_get_ns();
	diff_time_ns = end_time_ns - start_time_ns;

	printk("completed multithreaded measurement");
	return diff_time_ns;
}
