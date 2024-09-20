#include <linux/page_zero.h>

#include "llfree.h"
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define LLFREE_HUGE_ORDER 9u

extern void memset_nt(void *dst, int value, size_t len);

static struct task_struct *asyncZero_Normal;
// static struct task_struct *asyncZero_DMA;
// static struct task_struct *asyncZero_DMA32;

static int zero_and_move_page(struct zone *zone, size_t order)
{
	struct page *page = NULL;
	llfree_result_t res;
	size_t offset = ALIGN_DOWN(zone->zone_start_pfn, 1 << MAX_ORDER);
	int cpu = get_cpu();
	int number_of_pages_to_zero;

	llflags_t llf = llflags(order);

	if (llf.order == 0) {
		// number_of_pages_to_zero = 262144; // 1 GiB
		// number_of_pages_to_zero = 1048576; // 4 GiB
		number_of_pages_to_zero = 2097152; // 8 GiB
		// number_of_pages_to_zero = 4194304; // 16 GiB
	} else {
		// number_of_pages_to_zero = 512; // 1 GiB
		// number_of_pages_to_zero = 2048; // 4 GiB
		number_of_pages_to_zero = 4096; // 8 GiB
		// number_of_pages_to_zero = 8192; // 16 GiB
	}

	for(int i = 0; i < number_of_pages_to_zero; ++i) {
		res = llfree_get(zone->llfree_dirty, cpu, llf);

		if (res.error) {
			put_cpu();
			return 1;
		}

		page = pfn_to_page(offset + res.frame);
		__memset(page_address(page), 0, (1 << llf.order) * PAGE_SIZE);
		// memset_nt(page_address(page), 0, (1 << llf.order) * PAGE_SIZE);

		res = llfree_put(zone->llfree_zeroed, zone->llfree_dirty, cpu,
				 res.frame, llf);
	}
	put_cpu();
	return 0;
}

static int asyncZero_fn(void *data)
{
	struct zone *zone;
	const char *zone_name = (const char *)data; 
	bool zero_and_move = true; // enable or disable zeroing
	int sleep_time = 100000;

	ktime_t start, end;
	s64 delta;
	bool once = true;

	if (!zero_and_move) {
		return 0;
	}

	ssleep(10);

	for_each_populated_zone(zone) {
		if (strcmp(zone->name, zone_name) == 0) {
			break;
		}
	}

	start = ktime_get();

	while (!kthread_should_stop()) {
		// if(!((llfree_free_frames(zone->llfree_zeroed)) > 8388608)) { // 32GB
		if(zero_and_move_page(zone, LLFREE_HUGE_ORDER) == 1) {
			zero_and_move_page(zone, 0u);
		}

			if (once && (strcmp(zone_name, "Normal") == 0)) {
				end = ktime_get();
				delta = ktime_to_ns(ktime_sub(end, start));
				pr_info("\nOne zeroing iteration in zone %s took: %lld ms\n", zone_name, delta / 1000000);
				once = false;
			}

		// }
		usleep_range(sleep_time - 5000, sleep_time + 5000);
	}
	return 0;
}

static int __init asyncZero_init(void)
{
	const char *zone_name_Normal = "Normal";
	// const char *zone_name_DMA = "DMA";
	// const char *zone_name_DMA32 = "DMA32";

	pr_info("Creating asyncZero kernel task...\n");

	asyncZero_Normal = kthread_run(asyncZero_fn, (void *)zone_name_Normal, "asyncZero_Normal");
	// asyncZero_DMA = kthread_run(asyncZero_fn, (void *)zone_name_DMA, "asyncZero_DMA");
	// asyncZero_DMA32 = kthread_run(asyncZero_fn, (void *)zone_name_DMA32, "asyncZero_DMA32");

	if (IS_ERR(asyncZero_Normal)) {
		pr_err("Failed to create asyncZero_Normal kernel task\n");
		return PTR_ERR(asyncZero_Normal);
	}

	// if (IS_ERR(asyncZero_DMA)) {
	// 	pr_err("Failed to create asyncZero_Normal kernel task\n");
	// 	return PTR_ERR(asyncZero_Normal);
	// }

	// if (IS_ERR(asyncZero_DMA32)) {
	// 	pr_err("Failed to create asyncZero_Normal kernel task\n");
	// 	return PTR_ERR(asyncZero_Normal);
	// }

	set_user_nice(asyncZero_Normal, 20);
	// set_user_nice(asyncZero_DMA, 5);
	// set_user_nice(asyncZero_DMA32, 5);
	return 0;
}

static void __exit asyncZero_exit(void)
{
	if (asyncZero_Normal) {
		kthread_stop(asyncZero_Normal);
		pr_info("AsyncZero Normal Kernel task stopped\n");
	}
	// if (asyncZero_DMA) {
	// 	kthread_stop(asyncZero_DMA);
	// 	pr_info("AsyncZero DMA Kernel task stopped\n");
	// }
	// if (asyncZero_DMA32) {
	// 	kthread_stop(asyncZero_DMA32);
	// 	pr_info("AsyncZero DMA32 Kernel task stopped\n");
	// }
}

module_init(asyncZero_init);
module_exit(asyncZero_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Henrik Cohrs");
MODULE_DESCRIPTION("Pre zeros pages for later use.");