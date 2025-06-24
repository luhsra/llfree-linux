#include "linux/kconfig.h"
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "llfree.h"
#include <linux/moduleparam.h>
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define LLFREE_HUGE_ORDER HUGETLB_PAGE_ORDER
#define LLZERO_ZONEMASK ((1 << ZONE_NORMAL) | (1 << ZONE_DMA32))

/// Percentage of pages to zero in a zone
static unsigned int percentage = CONFIG_LLZERO_PERCENTAGE;
/// Delay between zeroing runs [ms]
static unsigned int delay = CONFIG_LLZERO_DELAY;
/// Maximum amount of memory to zero in one run [MiB]
static unsigned int limit = CONFIG_LLZERO_LIMIT;

static inline size_t huge_page_limit(void)
{
	return ((limit * 1024 * 1024) / PAGE_SIZE) >> LLFREE_HUGE_ORDER;
}

extern void memset_nt(void *dst, int value, size_t len);

static bool enabled = true;

static struct task_struct **zero_tasks = NULL;
static size_t zero_tasks_len = 0;

/// Returns true if only zero percent was zeroed but there are still pages to zero.
static bool llzero_pages(struct zone *zone)
{
	size_t pages_to_zero = 0;
	ll_stats_t stats = llfree_stats(zone->llfree);
	bool exceeds_limit = false;
	// Zero X% of free pages
	if (stats.free_huge <= 8)
		return false;
	pages_to_zero = stats.free_huge * percentage / 100;
	if (pages_to_zero <= stats.zeroed_huge)
		return false;
	pages_to_zero -= stats.zeroed_huge;

	// Ensure we do not zero more than the limit
	exceeds_limit = pages_to_zero > huge_page_limit();
	pages_to_zero = min_t(size_t, pages_to_zero, huge_page_limit());

	for (size_t i = 0; i < pages_to_zero; i++) {
		size_t offset =
			ALIGN_DOWN(zone->zone_start_pfn, 1 << MAX_ORDER);
		struct page *page = NULL;
		llfree_result_t res;
		int cpu = get_cpu();

		res = llfree_reclaim(zone->llfree, cpu, true, true);
		put_cpu();
		if (!llfree_is_ok(res)) {
			// Out of memory, we cannot zero more pages
			BUG_ON(res.error != LLFREE_ERR_MEMORY);
			return false;
		}
		BUG_ON(res.zeroed);

		page = pfn_to_page(offset + res.frame);
#ifdef CONFIG_LLZERO_NT
		memset_nt(page_address(page), 0,
			  PAGE_SIZE << LLFREE_HUGE_ORDER);
#else
		__memset(page_address(page), 0, PAGE_SIZE << LLFREE_HUGE_ORDER);
#endif

		res = llfree_return(zone->llfree, res.frame, true);
		BUG_ON(!llfree_is_ok(res));
	}
	pr_info("Zeroed %zd pages in zone %s\n", pages_to_zero, zone->name);

	return exceeds_limit;
}

static int llzero_task(void *data)
{
	struct zone *zone = (struct zone *)data;

#ifdef CONFIG_LLZERO_BENCH
	ktime_t start, end;
	bool once = true;
#endif
	usleep_range(delay, 8 * delay);

#ifdef CONFIG_LLZERO_BENCH
	start = ktime_get();
#endif

	while (!kthread_should_stop()) {
		bool zeroes_left = llzero_pages(zone);
		// Sleep longer if there are no pages left to zero
		size_t d = delay * (zeroes_left ? 1 : 8);

#ifdef CONFIG_LLZERO_BENCH
		if (once && (strcmp(zone->name, "Normal") == 0)) {
			s64 delta;
			end = ktime_get();
			delta = ktime_to_ns(ktime_sub(end, start));
			pr_info("First zeroing %s took: %lld ms\n", zone->name,
				delta / 1000000);
			once = false;
		}
#endif

		usleep_range(d - (d / 10), d + (d / 10));
	}
	return 0;
}

static int start_zero_tasks(void)
{
	size_t num_zones = 0;
	struct zone *zone;

	if (zero_tasks)
		return 0;

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
				llzero_task, (void *)zone, zone->name);

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

static int stop_zero_tasks(void)
{
	if (!zero_tasks)
		return 0;

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
	return 0;
}

static bool initialized = false;
static int enabled_set(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_bool(val, kp);
	if (ret < 0)
		return ret;

	if (!initialized)
		return 0;

	if (enabled) {
		return start_zero_tasks();
	} else {
		return stop_zero_tasks();
	}
}

static const struct kernel_param_ops enabled_ops = {
	.set = enabled_set,
	.get = param_get_bool,
};

module_param_cb(enabled, &enabled_ops, &enabled, 0664);

module_param(percentage, uint, 0664);
module_param(limit, uint, 0664);
module_param(delay, uint, 0664);

static int __init llzero_init(void)
{
	initialized = true;
	if (enabled)
		return start_zero_tasks();
	return 0;
}

static void __exit llzero_exit(void)
{
	int ret = stop_zero_tasks();
	if (ret < 0)
		pr_err("Failed to stop zero tasks: %d\n", ret);
	initialized = false;
}

module_init(llzero_init);
module_exit(llzero_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Henrik Cohrs, Lars Wrenger <wrenger@sra.uni-hannover.de>");
MODULE_DESCRIPTION("Zero pages asynchronously in the background.");
