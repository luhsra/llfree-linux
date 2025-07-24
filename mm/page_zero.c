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
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kfifo.h>
#include <asm-generic/ioctl.h>

/// Optional size_t type
typedef struct optional_size_t {
	bool present : 1;
	size_t value : (sizeof(size_t) * 8) - 1;
} optional_size_t;
static inline optional_size_t optional_size(size_t value)
{
	return (optional_size_t){ .present = true, .value = value };
}
static inline optional_size_t optional_size_none(void)
{
	return (optional_size_t){ .present = false, .value = 0 };
}

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

static inline size_t num_pages_to_zero(struct zone *zone)
{
	size_t pages_to_zero = 0;
	ll_stats_t stats = llfree_stats(zone->llfree);
	// Zero X% of free pages
	if (stats.free_huge <= 8)
		return 0;
	pages_to_zero = stats.free_huge * percentage / 100;
	if (pages_to_zero <= stats.zeroed_huge)
		return 0;
	pages_to_zero -= stats.zeroed_huge;

	return pages_to_zero;
}

extern void memset_nt(void *dst, int value, size_t len);
static int start_zero_tasks(void);
static int stop_zero_tasks(void);

static bool enabled = true;

static struct task_struct **zero_tasks = NULL;
static size_t zero_tasks_len = 0;

static char device[128] = { '\0' };
static struct block_device *bdev = NULL;

static fmode_t mode = FMODE_READ | FMODE_EXCL;

struct bio_ctx {
	struct zone *zone;
	uint64_t frame;
	size_t order;
	size_t cpu;
	size_t offset;
	bool benchmark;
#ifdef CONFIG_LLZERO_NVME_DEBUG
	ktime_t start;
#endif
};

static int major;
static struct class *cls;

static struct bio_set bs;
#define LLZERO_BIO_POOL_SIZE 10

static uint32_t module_id = 0xdeadbeef;
static atomic_t blk_2mib_ctr = ATOMIC_INIT(0);
#define BLK_2MIB_COUNT 512
#define SECTOR_OFFSET_2MIB 4096

static atomic_t bio_inflight = ATOMIC_INIT(0);
static uint32_t iodepth = 0;

/*
** Benchmark Stuffs
*/
static size_t bench_num_pages;
static size_t bench_cpu;
static bool bench_const_sector;
static struct zone *bench_zone;
static atomic_t completion_ctr = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(bench_waitq);

struct benchmark_args {
	uint32_t num_pages;
	uint32_t iodepth;
	bool const_iosector;

	// Output
	uint64_t duration_ns;
};
#define IOCTL_MAGIC 0x55
#define ZERO_IOCTL_BENCHMARK _IOWR(IOCTL_MAGIC, 0x00, struct benchmark_args)

#ifdef CONFIG_LLZERO_NVME_DEBUG
static void _dump_page(size_t offset, uint64_t frame)
{
	struct page *page;
	void *vaddr;

	page = pfn_to_page(offset + frame);
	vaddr = page_address(page);
	pr_info("address dump: 0x%px\n", vaddr);
	print_hex_dump(KERN_INFO, "Read Data head: ", DUMP_PREFIX_OFFSET, 16, 1,
		       vaddr, 64, true);
	vaddr += (511 << PAGE_SHIFT);
	pr_info("address dump: 0x%px\n", vaddr);
	print_hex_dump(KERN_INFO, "Read Data tail: ", DUMP_PREFIX_OFFSET, 16, 1,
		       vaddr, 64, true);

	vaddr += (1 << PAGE_SHIFT);
	pr_info("address dump: 0x%px\n", vaddr);
	print_hex_dump(KERN_INFO, "Read Data out-head: ", DUMP_PREFIX_OFFSET,
		       16, 1, vaddr, 64, true);
	vaddr += (511 << PAGE_SHIFT);
	pr_info("address dump: 0x%px\n", vaddr);
	print_hex_dump(KERN_INFO, "Read Data out-tail: ", DUMP_PREFIX_OFFSET,
		       16, 1, vaddr, 64, true);

	vaddr += (1 << PAGE_SHIFT);
	pr_info("address dump: 0x%px\n", vaddr);
	print_hex_dump(KERN_INFO, "Read Data out2-head: ", DUMP_PREFIX_OFFSET,
		       16, 1, vaddr, 64, true);
	vaddr += (511 << PAGE_SHIFT);
	pr_info("address dump: 0x%px\n", vaddr);
	print_hex_dump(KERN_INFO, "Read Data out2-tail: ", DUMP_PREFIX_OFFSET,
		       16, 1, vaddr, 64, true);
}
#endif

#ifdef CONFIG_LZERO_NVME_DEBUG
#define BIG_NUM 999999999
static atomic_t min_latency = ATOMIC_INIT(BIG_NUM);
#endif

static void bio_end_io(struct bio *bio)
{
	llfree_result_t res;
	struct bio_ctx *ctx = (struct bio_ctx *)bio->bi_private;
	struct zone *zone = ctx->zone;
#ifdef CONFIG_LLZERO_NVME_DEBUG
	ktime_t end;
	uint32_t dur, curr_min, min;
#endif

	if (bio->bi_status != BLK_STS_OK) {
		pr_info("bio_end_io: Read failed with status %d\n",
			bio->bi_status);
		BUG_ON(bio->bi_status);
	}

	atomic_dec(&bio_inflight);

#ifdef CONFIG_LLZERO_NVME_DEBUG
	_dump_page(ctx->offset, ctx->frame);

	end = ktime_get();
	dur = (uint32_t)ktime_to_ns(ktime_sub(end, ctx->start));

	curr_min = atomic_read(&min_latency);
	min = min(dur, curr_min);
	atomic_set(&min_latency, min);
#endif
	if (unlikely(ctx->benchmark)) {
		uint32_t cnt = atomic_inc_return(&completion_ctr);
		/* uint32_t ctr = atomic_inc_return(&endio_ctr); */
		/* pr_info("bio endio ctr: %d", ctr); */

		if (cnt > bench_num_pages) {
			pr_err("WHAT?!!!!\n");
		}

		if (cnt == bench_num_pages) {
			wake_up(&bench_waitq);
#ifdef CONFIG_LLZERO_NVME_DEBUG
			pr_info("Min latency: %d ns\n", dur); // DEBUG
			atomic_set(&min_latency, BIG_NUM);
#endif
			pr_info("Benchmark completed: %zu ops\n",
				bench_num_pages);
		}
		/* res = llfree_put(zone->llfree_dirty, zone->llfree_dirty, */
		/* 		 ctx->cpu, ctx->frame, llflags(ctx->order)); */
	} else {
		// Free the entire huge page
		res = llfree_return(zone->llfree, res.frame, true);
		BUG_ON(res.error);
	}

	kfree(ctx);
	bio_put(bio);
}

static bool llzero_page_cpu(struct zone *zone, optional_size_t bench_frame)
{
	size_t offset = ALIGN_DOWN(zone->zone_start_pfn, 1 << MAX_ORDER);
	struct page *page = NULL;
	llfree_result_t res;

	if (bench_frame.present) {
		zone = bench_zone;
		res = llfree_ok(bench_frame.value, false, false);
	} else {
		int cpu = get_cpu();
		res = llfree_reclaim(zone->llfree, cpu, true, true, true);
		put_cpu();
		if (!llfree_is_ok(res)) {
			// Out of memory, we cannot zero more pages
			BUG_ON(res.error != LLFREE_ERR_MEMORY);
			return false;
		}
		BUG_ON(res.zeroed);
	}

	page = pfn_to_page(offset + res.frame);
#ifdef CONFIG_LLZERO_NT
	memset_nt(page_address(page), 0, PAGE_SIZE << LLFREE_HUGE_ORDER);
#else
	__memset(page_address(page), 0, PAGE_SIZE << LLFREE_HUGE_ORDER);
#endif

	if (unlikely(bench_frame.present)) {
		int cnt = atomic_inc_return(&completion_ctr);
		if (cnt == bench_num_pages) {
			wake_up(&bench_waitq);
			pr_info("Benchmark completed: %zu ops\n",
				bench_num_pages);
		}
	} else {
		res = llfree_return(zone->llfree, res.frame, true);
		BUG_ON(!llfree_is_ok(res));
	}
	return true;
}

/// Gets page from llfree and send to ssd to zero.
static bool llzero_page_ssd(struct zone *zone, optional_size_t bench_frame)
{
	size_t cpu;
	struct bio_ctx *ctx;
	struct bio *bio;
#ifdef CONFIG_LLZERO_NVME_DEBUG
	ktime_t start;
#endif
	int ret;
	size_t offset;
	struct page *page = NULL;
	llfree_result_t res;
	uint32_t curr_sector, curr_inflight;

	curr_inflight = atomic_inc_return(&bio_inflight);
	if (curr_inflight > iodepth) {
		atomic_dec(&bio_inflight);
		return false;
	}

	if (bench_frame.present) {
		zone = bench_zone;
		res = llfree_ok(bench_frame.value, false, false);
		cpu = bench_cpu;
	} else {
		cpu = get_cpu();
		put_cpu();
		res = llfree_reclaim(zone->llfree, cpu, true, true, true);
		if (res.error) {
			atomic_dec(&bio_inflight);
			return false;
		}
	}

	offset = ALIGN_DOWN(zone->zone_start_pfn, 1 << MAX_ORDER);

	page = pfn_to_page(offset + res.frame);

	ctx = kzalloc(sizeof(struct bio_ctx), GFP_KERNEL);
	ctx->zone = zone;
	ctx->frame = res.frame;
	ctx->order = HUGETLB_PAGE_ORDER;
	ctx->cpu = cpu;
	ctx->offset = offset;
	ctx->benchmark = bench_frame.present;

	if (bench_const_sector) {
		curr_sector = 0;
	} else {
		curr_sector = atomic_inc_return(&blk_2mib_ctr);
		curr_sector =
			(curr_sector % BLK_2MIB_COUNT) * SECTOR_OFFSET_2MIB;
	}

	bio = bio_alloc_bioset(bdev, 1, REQ_OP_READ, GFP_KERNEL, &bs);

	// NOTE: The nvme partition to give needs to be empty from
	//       the first sector!!!!
	bio->bi_iter.bi_sector = curr_sector;
	bio->bi_end_io = bio_end_io;
	bio->bi_private = (void *)ctx;

	ret = bio_add_page(bio, page, PAGE_SIZE << HUGETLB_PAGE_ORDER, 0);
	if (!ret) {
		pr_err("Failed to add page to bio.");
		kfree(ctx);
		bio_put(bio);
		if (!bench_frame.present) {
			llfree_result_t res2;
			res2 = llfree_put(zone->llfree, 0, res.frame,
					  llflags(HUGETLB_PAGE_ORDER));
			BUG_ON(res2.error);
		}
		atomic_dec(&bio_inflight);
		return false;
	}

#ifdef CONFIG_LLZERO_NVME_DEBUG
	start = ktime_get();
	ctx->start = start;
#endif

	submit_bio(bio);
	return true;
}

/// Returns true if only zero percent was zeroed but there are still pages to zero.
static bool llzero_pages(struct zone *zone, bool ssd)
{
	size_t pages_to_zero = num_pages_to_zero(zone);
	// Ensure we do not zero more than the limit
	size_t exceeds_limit = pages_to_zero > huge_page_limit();
	pages_to_zero = min_t(size_t, pages_to_zero, huge_page_limit());

	for (size_t i = 0; i < pages_to_zero; i++) {
		bool success;
		if (ssd)
			success = llzero_page_ssd(zone, optional_size_none());
		else
			success = llzero_page_cpu(zone, optional_size_none());
		if (!success)
			return true;
	}
	if (pages_to_zero > 0)
		pr_info("Zeroed %zd pages in zone %s\n", pages_to_zero,
			zone->name);

	return exceeds_limit;
}

static int llzero_task(void *data)
{
	struct zone *zone = (struct zone *)data;

	usleep_range(delay, 8 * delay);

	while (!kthread_should_stop()) {
		size_t d = delay;
		bool zeroes_left = llzero_pages(zone, bdev);
		// Sleep longer if there are no pages left to zero
		d *= (zeroes_left ? 1 : 8);
		usleep_range(d - (d / 10), d + (d / 10));
	}
	return 0;
}

static int device_set(const char *val, const struct kernel_param *kp)
{
	// Find newline or null terminator
	const char* end = strnchrnul(val, sizeof(device) - 1, '\n');
	size_t len = end - val;
	strncpy(device, val, len);
	device[len] = '\0';

	if (bdev != NULL) {
		// Wait for all bios to finish
		while (atomic_read(&bio_inflight) != 0) {
			msleep(1);
		}
		blkdev_put(bdev, mode);
		bdev = NULL;
	}
	if (len == 0) {
		pr_info("No device specified, using cpu.\n");
		return 0;
	}
	bdev = blkdev_get_by_path(device, mode, &module_id);
	if (IS_ERR(bdev)) {
		int err = PTR_ERR(bdev);
		pr_err("Failed to open block device %s: %d\n", device, err);
		bdev = NULL;
		return err;
	}
	pr_info("Opened block device %s\n", device);
	return 0;
}

static long async_zero_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	ktime_t start, end;
	struct benchmark_args args;
	size_t cpu;
	u64 *bench_pages;

	llflags_t llf = llflags(LLFREE_HUGE_ORDER);
	llf.movable = 1;

	if (cmd != ZERO_IOCTL_BENCHMARK) {
		pr_err("Invalid ioctl command: %u\n", cmd);
		return -EINVAL;
	}
	if (enabled) {
		pr_err("Cannot run benchmark zeroing is enabled.\n");
		return -EINVAL;
	}
	if (bench_num_pages > 0) {
		pr_err("Cannot run benchmark while another is running.\n");
		return -EBUSY;
	}
	if (copy_from_user(&args, argp, sizeof(args))) {
		return -EFAULT;
	}

	// get normal zone
	for_each_populated_zone(bench_zone) {
		if (!strcmp(bench_zone->name, "Normal")) {
			break;
		}
	}
	cpu = get_cpu();
	put_cpu();

	bench_cpu = cpu;
	bench_num_pages = args.num_pages;

	iodepth = args.iodepth;
	bench_const_sector = args.const_iosector;

	atomic_set(&completion_ctr, 0);

	bench_pages = kcalloc(bench_num_pages, sizeof(u64), GFP_KERNEL);
	for (int i = 0; i < bench_num_pages; i++) {
		llfree_result_t res = llfree_get(bench_zone->llfree, cpu, llf);
		BUG_ON(!llfree_is_ok(res));
		bench_pages[i] = res.frame;
	}

	pr_info("running benchmark..\n");
	start = ktime_get();

	// start zeroing operation
	for (int i = 0; i < bench_num_pages; i++) {
		// TODO: Support cpu zeroing
		bool issued = llzero_page_ssd(bench_zone,
					      optional_size(bench_pages[i]));
		if (!issued) {
			size_t d = delay;
			usleep_range(d - (d / 10), d + (d / 10));
			i--;
		}
	}

	wait_event(bench_waitq,
		   atomic_read(&completion_ctr) == bench_num_pages);
	end = ktime_get();
	pr_info("benchmark done.\n");

	args.duration_ns = ktime_to_ns(ktime_sub(end, start));

	// Free llfree results
	for (int i = 0; i < bench_num_pages; i++) {
		llfree_result_t res;
		res = llfree_put(bench_zone->llfree, cpu, bench_pages[i], llf);
		BUG_ON(!llfree_is_ok(res));
	}
	kfree(bench_pages);
	bench_num_pages = 0;

	atomic_set(&completion_ctr, 0);
	iodepth = CONFIG_LLZERO_IODEPTH;
	bench_const_sector = false;

	kfree(zero_tasks);
	zero_tasks = NULL;
	zero_tasks_len = 0;

	msleep(500);

	return 0;
}

struct file_operations fops = {
	.unlocked_ioctl = async_zero_ioctl,
};

static const struct kernel_param_ops device_ops = {
	.set = device_set,
	.get = param_get_charp,
};
module_param_cb(device, &device_ops, &device, 0644);

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

	// Wait for all bios to finish
	while (atomic_read(&bio_inflight) != 0) {
		msleep(1);
	}

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

	if (bench_num_pages > 0) {
		pr_err("Cannot change enabled state while benchmark is running.\n");
		return -EBUSY;
	}

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

#define CDEV_NAME "async-zero"

static int __init llzero_init(void)
{
	initialized = true;

	major = register_chrdev(0, CDEV_NAME, &fops);
	if (major < 0) {
		pr_alert("Registering char device failed with %d\n", major);
		return major;
	}

	cls = class_create(THIS_MODULE, CDEV_NAME);
	device_create(cls, NULL, MKDEV(major, 0), NULL, CDEV_NAME);

	pr_info("Device created on /dev/%s\n", CDEV_NAME);

	if (bioset_init(&bs, LLZERO_BIO_POOL_SIZE, 0, BIOSET_NEED_BVECS))
		panic("llzero: can't allocate bios\n");

	if (enabled)
		return start_zero_tasks();

	return 0;
}

static void __exit llzero_exit(void)
{
	int ret = stop_zero_tasks();
	if (ret < 0)
		pr_err("Failed to stop zero tasks: %d\n", ret);

	bioset_exit(&bs);

	if (bdev) {
		blkdev_put(bdev, mode);
	}
	device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);
	unregister_chrdev(major, CDEV_NAME);

	initialized = false;
}

module_init(llzero_init);
module_exit(llzero_exit);

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Henrik Cohrs, Lars Wrenger <wrenger@sra.uni-hannover.de>");
MODULE_DESCRIPTION("Zero pages asynchronously in the background.");
