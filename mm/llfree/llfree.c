#include "llfree_platform.h"

#include <linux/align.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/dax.h>

#include "llfree.h"
#include "llfree_inner.h"

MODULE_LICENSE("MIT");
MODULE_DESCRIPTION("LLFree Allocator");
MODULE_AUTHOR("Lars Wrenger");

void noinline llfree_panic(void)
{
	llfree_warn("panic");
}

llfree_t *llfree_node_init(size_t node, size_t cores, size_t start_pfn,
			   size_t pages)
{
	cores = 1; // only one core for now

	u64 offset = align_down(start_pfn, 1 << LLFREE_MAX_ORDER);
	pages += start_pfn - offset; // correct length

	pr_info("node=%" PRIuS ", offset=%" PRIu64 ", pages=%" PRIuS, node,
		offset, pages);

	llfree_t *self =
		memblock_alloc_node(sizeof(llfree_t), LLFREE_CACHE_SIZE, node);

	llfree_meta_size_t m = llfree_metadata_size(cores, pages);
	llfree_meta_t meta = {
		.local = memblock_alloc_node(m.local, LLFREE_CACHE_SIZE, node),
		.trees = memblock_alloc_node(m.trees, LLFREE_CACHE_SIZE, node),
		.lower = memblock_alloc_node(m.lower, LLFREE_CACHE_SIZE, node),
	};
	llfree_result_t res =
		llfree_init(self, cores, pages, LLFREE_INIT_ALLOC, meta);

	BUG_ON(!llfree_ok(res));

	return self;
}

static void *frag_start(struct seq_file *m, loff_t *pos)
{
	pg_data_t *pgdat;
	loff_t node = *pos;

	for (pgdat = first_online_pgdat(); pgdat && node;
	     pgdat = next_online_pgdat(pgdat))
		--node;

	return pgdat;
}

static void *frag_next(struct seq_file *m, void *arg, loff_t *pos)
{
	pg_data_t *pgdat = (pg_data_t *)arg;

	(*pos)++;
	return next_online_pgdat(pgdat);
}

static void frag_stop(struct seq_file *m, void *arg)
{
}

static void writer(void *arg, char *str)
{
	seq_printf((struct seq_file *)arg, "%s", str);
}

static int llfree_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		llfree_t *llfree = zone->llfree;

		if (!populated_zone(zone) || llfree == NULL)
			continue;

		llfree_print_debug(llfree, writer, m);
	}
	return 0;
}

static int llfree_frag_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		if (!populated_zone(zone))
			continue;

		for (size_t i = 0; i < llfree_frames(zone->llfree);
		     i += 1 << LLFREE_HUGE_ORDER) {
			size_t free = llfree_free_at(zone->llfree, i,
						     LLFREE_HUGE_ORDER);
			// [0, 9], where 0 is entirely allocated and 9 is free
			size_t level = free == 0 ? 0 : (free / 64 + 1);
			seq_printf(m, "%zu", level);
		}
		seq_printf(m, "\n");
	}
	return 0;
}

static const struct seq_operations llfree_op = {
	.start = frag_start,
	.next = frag_next,
	.stop = frag_stop,
	.show = llfree_show,
};

static const struct seq_operations llfree_frag_op = {
	.start = frag_start,
	.next = frag_next,
	.stop = frag_stop,
	.show = llfree_frag_show,
};

static int __init llfree_init_module(void)
{
	pr_info("Setup llfree debugging");
	proc_create_seq("llfree", 0444, NULL, &llfree_op);
	proc_create_seq("llfree_frag", 0444, NULL, &llfree_frag_op);
	return 0;
}
module_init(llfree_init_module);

#if 0
struct device *device_dax_driver_find_device_by_devt(dev_t devt);

static __init int find_dax_init(void)
{
	dev_t dax_id = MKDEV(252, 0); // /dev/dax0.0
	struct device *dax_dev;
	u8 *dax_begin;
	u64 dax_len;
	void *llfree;

	dax_dev = device_dax_driver_find_device_by_devt(dax_id);
	if (dax_dev == NULL) {
		pr_err("No dax device found");
		return 0;
	}

	pr_info("Found dax device %s", dax_dev->init_name);

	dax_begin = device_dax_find_address_range_by_devt(dax_id, &dax_len);
	pr_info("Range: %llx-%llx (%llu)", (u64)dax_begin,
		(u64)dax_begin + dax_len, dax_len);

	BUG_ON(!IS_ALIGNED((size_t)dax_begin, HPAGE_SIZE));

	// TODO: find old metadata
	llfree = llfree_node_init(0, num_online_cpus(),
				  page_to_pfn(virt_to_page(dax_begin)),
				  dax_len / PAGE_SIZE);
	BUG_ON(llfree == NULL);

	// llfree_print(llfree);

	return 0;
}
late_initcall(find_dax_init);
#endif

static void llfree_cleanup_module(void)
{
	pr_info("uninit\n");
}
module_exit(llfree_cleanup_module);

EXPORT_SYMBOL(llfree_free_frames);
EXPORT_SYMBOL(llfree_free_huge);
// EXPORT_SYMBOL(llfree_dump);
// EXPORT_SYMBOL(llfree_print);
