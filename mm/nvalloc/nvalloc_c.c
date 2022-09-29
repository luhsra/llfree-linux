#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "nvalloc.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

MODULE_LICENSE("MIT");
MODULE_DESCRIPTION("NVM Allocator");
MODULE_AUTHOR("Lars Wrenger");

// Functions needed by the allocator

/// Linux provided alloc function
u8 *nvalloc_linux_alloc(u64 node, u64 size, u64 align)
{
	return memblock_alloc_node(size, align, node);
}
/// Linux provided free function
void nvalloc_linux_free(u8 *ptr, u64 size, u64 align)
{
	memblock_free(ptr, size);
}
/// Linux provided printk function
void nvalloc_linux_printk(const u8 *format, const u8 *module_name,
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

static int nvalloc_show(struct seq_file *m, void *arg)
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
			len = min(len, (size_t)nvalloc_dump(zone->nvalloc, buf,
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

static const struct seq_operations nvalloc_op = {
	.start = frag_start,
	.next = frag_next,
	.stop = frag_stop,
	.show = nvalloc_show,
};

static int __init nvalloc_init_module(void)
{
	pr_info("Setup nvalloc debugging");
	proc_create_seq("nvalloc", 0444, NULL, &nvalloc_op);
	return 0;
}

static void nvalloc_cleanup_module(void)
{
	pr_info("uninit\n");
}

module_init(nvalloc_init_module);
module_exit(nvalloc_cleanup_module);
