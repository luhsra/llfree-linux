#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "llfree.h"

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

MODULE_LICENSE("MIT");
MODULE_DESCRIPTION("NVM Allocator");
MODULE_AUTHOR("Lars Wrenger");

// Functions needed by the allocator

/// Linux provided alloc function
u8 *llfree_linux_alloc(u64 node, u64 size, u64 align)
{
	return memblock_alloc_node(size, align, node);
}
/// Linux provided free function
void llfree_linux_free(u8 *ptr, u64 size, u64 align)
{
	memblock_free(ptr, size);
}
/// Linux provided printk function
void llfree_linux_printk(const u8 *format, const u8 *module_name,
			  const void *args)
{
	_printk(format, module_name, args);
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

static int llfree_show(struct seq_file *m, void *arg)
{
	pg_data_t *pgdat = (pg_data_t *)arg;
	struct zone *zone;
	struct zone *node_zones = pgdat->node_zones;

	for (zone = node_zones; zone - node_zones < MAX_NR_ZONES; ++zone) {
		char *buf;
		size_t len;

		if (!populated_zone(zone))
			continue;

		len = seq_get_buf(m, &buf);
		if (len > 0) {
			preempt_disable();
			len = min(len, (size_t)llfree_dump(zone->llfree, buf,
							    len));
			preempt_enable();
			seq_commit(m, len);
		} else {
			seq_commit(m, -1);
			pr_err("buf empty\n");
		}
	}
	return 0;
}

static const struct seq_operations llfree_op = {
	.start = frag_start,
	.next = frag_next,
	.stop = frag_stop,
	.show = llfree_show,
};

static int __init llfree_init_module(void)
{
	pr_info("Setup llfree debugging");
	proc_create_seq("llfree", 0444, NULL, &llfree_op);
	return 0;
}
module_init(llfree_init_module);

static __init int find_dax_init(void) {
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

	llfree = llfree_init(0, num_online_cpus(), true, dax_begin,
			       dax_len / PAGE_SIZE);
	BUG_ON(llfree_err((u64)llfree));

	llfree_printk(llfree);

	return 0;
}
late_initcall(find_dax_init);

static void llfree_cleanup_module(void)
{
	pr_info("uninit\n");
}
module_exit(llfree_cleanup_module);

EXPORT_SYMBOL(llfree_free_count);
EXPORT_SYMBOL(llfree_free_huge_count);
EXPORT_SYMBOL(llfree_dump);
EXPORT_SYMBOL(llfree_printk);
EXPORT_SYMBOL(llfree_for_each_huge_page);
