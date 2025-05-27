#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "llfree.h"
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define LLFREE_HUGE_ORDER HUGETLB_PAGE_ORDER
#define LLZERO_PER_RUN (CONFIG_LLZERO_PER_RUN << 30)
#define LLZERO_PERCENT CONFIG_LLZERO_PERCENTAGE
#define LLZERO_DELAY CONFIG_LLZERO_DELAY
#define LLZERO_ZONEMASK ((1 << ZONE_NORMAL) | (1 << ZONE_DMA32))

extern void memset_nt(void *dst, int value, size_t len);

static struct task_struct **zero_tasks = NULL;
static size_t zero_tasks_len = 0;

static int zero_and_move_page(struct zone *zone, size_t order)
{
	int cpu = get_cpu();
	size_t total_pages = atomic_long_read(&zone->managed_pages);
	size_t max_pages = (total_pages * LLZERO_PERCENT) / 100;
	size_t zeroed_pages = llfree_free_frames(zone->llfree_zeroed);
	ssize_t number_of_pages_to_zero = max_pages - zeroed_pages;
	// Ensure we do not zero more than LLZERO_PER_RUN
	number_of_pages_to_zero = min_t(ssize_t, number_of_pages_to_zero,
					LLZERO_PER_RUN / PAGE_SIZE);
	number_of_pages_to_zero /= (1 << order);

	for (int i = 0; i < number_of_pages_to_zero; i++) {
		size_t offset =
			ALIGN_DOWN(zone->zone_start_pfn, 1 << MAX_ORDER);
		struct page *page = NULL;
		llfree_result_t res;

		llflags_t llf = llflags(order);
		llf.movable = 1;

		res = llfree_get(zone->llfree_dirty, cpu, llf);
		if (res.error) {
			put_cpu();
			return 1;
		}

		page = pfn_to_page(offset + res.frame);
#ifdef CONFIG_LLZERO_NT
		memset_nt(page_address(page), 0, (1 << llf.order) * PAGE_SIZE);
#else
		__memset(page_address(page), 0, (1 << llf.order) * PAGE_SIZE);
#endif

		res = llfree_put(zone->llfree_zeroed, zone->llfree_dirty, cpu,
				 res.frame, llflags(order));
		BUG_ON(res.error);
	}
	put_cpu();
	return 0;
}

static int async_zero_fn(void *data)
{
	struct zone *zone = (struct zone *)data;

	ktime_t start, end;
	bool once = true;

	ssleep(10);

	start = ktime_get();

	while (!kthread_should_stop()) {
		if (zero_and_move_page(zone, LLFREE_HUGE_ORDER) == 1) {
			zero_and_move_page(zone, 0u);
		}

		if (once && (strcmp(zone->name, "Normal") == 0)) {
			s64 delta;
			end = ktime_get();
			delta = ktime_to_ns(ktime_sub(end, start));
			pr_info("First zeroing iteration in zone %s took: %lld ms\n",
				zone->name, delta / 1000000);
			once = false;
		}

		usleep_range(LLZERO_DELAY - LLZERO_DELAY / 10,
			     LLZERO_DELAY + LLZERO_DELAY / 10);
	}
	return 0;
}

static int __init async_zero_init(void)
{
	size_t num_zones = 0;

	struct zone *zone;
	for_each_populated_zone(zone) {
		if ((1 << zone_idx(zone)) & LLZERO_ZONEMASK)
			num_zones++;
	}
	zero_tasks_len = num_zones;

	pr_info("Creating %zu zero task\n", zero_tasks_len);
	zero_tasks = kcalloc(zero_tasks_len, sizeof(struct task_struct *),
			     GFP_KERNEL);
	num_zones = 0;
	for_each_populated_zone(zone) {
		if ((1 << zone_idx(zone)) & LLZERO_ZONEMASK) {
			zero_tasks[num_zones] = kthread_run(
				async_zero_fn, (void *)zone, zone->name);

			if (IS_ERR(zero_tasks[num_zones])) {
				pr_err("Failed to create zero task for zone %s\n",
				       zone->name);
				return PTR_ERR(zero_tasks[num_zones]);
			}
			set_user_nice(zero_tasks[num_zones], 20);
			num_zones++;
		}
	}

	return 0;
}

static void __exit async_zero_exit(void)
{
	if (zero_tasks) {
		for (size_t i = 0; i < zero_tasks_len; i++) {
			if (zero_tasks[i]) {
				kthread_stop(zero_tasks[i]);
				pr_info("Zero task %zu stopped\n", i);
				free_kthread_struct(zero_tasks[i]);
			}
		}
		kfree(zero_tasks);
		zero_tasks = NULL;
		zero_tasks_len = 0;
	}
}

module_init(async_zero_init);
module_exit(async_zero_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Henrik Cohrs");
MODULE_DESCRIPTION("Pre zeros pages for later use.");
