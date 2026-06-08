#include "linux/gfp_types.h"
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

static unsigned int llfree_class_config = 0;
static int __init set_llfree_class_config(char *str)
{
	if (kstrtouint(str, 0, &llfree_class_config))
		return -EINVAL;
	if (llfree_class_config > 3) {
		pr_err("llfree_class_config must be 0, 1, 2 or 3\n");
		return -EINVAL;
	}
	return 0;
}
early_param("llfree_class_config", set_llfree_class_config);

void noinline llfree_panic(void)
{
	llfree_warn("panic");
}

/// Simple movable policy (3 classes: immovable=0, movable=1, huge=2)
static inline llfree_policy_t ll_unused llfree_linux_policy(uint8_t requested,
							    uint8_t target,
							    size_t free)
{
	if (requested > target)
		return (llfree_policy_t){ LLFREE_POLICY_STEAL, 0 };
	if (requested < target)
		return (llfree_policy_t){ LLFREE_POLICY_DEMOTE, 0 };
	/* matching class */
	if (free >= LLFREE_TREE_SIZE / 2)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, 1 };
	if (free >= LLFREE_TREE_SIZE / 64)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, UINT8_MAX };
	return (llfree_policy_t){ LLFREE_POLICY_MATCH, 2 };
}

// -----------------------------------------------------------------------------

static inline llfree_classing_t ll_unused llfree_class_config_0(size_t cores)
{
	return (llfree_classing_t){ .num_classes = 3,
				    .default_class = 2,
				    .policy = llfree_linux_policy,
				    .classes = {
					    // Immovable
					    { .class = 0, .count = cores },
					    // Movable
					    { .class = 1, .count = cores },
					    // Huge
					    { .class = 2, .count = cores },
				    } };
}
static inline llfree_request_t ll_unused llfree_request_0(uint8_t order,
							  gfp_t flags)
{
	ll_optional_t core = ll_some(raw_smp_processor_id());
	if (order >= LLFREE_HUGE_ORDER)
		return llreq(order, 2, core);
	if (flags & __GFP_MOVABLE)
		return llreq(order, 1, core);
	return llreq(order, 0, core);
}

// -----------------------------------------------------------------------------

static inline llfree_classing_t ll_unused llfree_class_config_1(size_t cores)
{
	return (llfree_classing_t){ .num_classes = 4,
				    .default_class = 3,
				    .policy = llfree_linux_policy,
				    .classes = {
					    // Immovable
					    { .class = 0, .count = cores },
					    // Movable
					    { .class = 1, .count = cores },
					    // Page Cache
					    { .class = 2, .count = cores },
					    // Huge
					    { .class = 3, .count = cores },
				    } };
}
static inline llfree_request_t ll_unused llfree_request_1(uint8_t order,
							  gfp_t flags)
{
	ll_optional_t core = ll_some(raw_smp_processor_id());
	if (order >= LLFREE_HUGE_ORDER)
		return llreq(order, 3, core);
	if (flags & __GFP_MOVABLE) {
		if (flags & ___GFP_PAGE_CACHE)
			return llreq(order, 2, core);
		return llreq(order, 1, core);
	}
	return llreq(order, 0, core);
}

// -----------------------------------------------------------------------------

static inline llfree_classing_t ll_unused llfree_class_config_2(size_t cores)
{
	return (llfree_classing_t){ .num_classes = 5,
				    .default_class = 4,
				    .policy = llfree_linux_policy,
				    .classes = {
					    // Immovable
					    { .class = 0, .count = cores },
					    // Movable
					    { .class = 1, .count = cores },
					    // Page Cache
					    { .class = 2, .count = cores },
					    // Long
					    { .class = 3, .count = cores },
					    // Huge
					    { .class = 4, .count = cores },
				    } };
}
static inline llfree_request_t ll_unused llfree_request_2(uint8_t order,
							  gfp_t flags)
{
	ll_optional_t core = ll_some(raw_smp_processor_id());
	if (order >= LLFREE_HUGE_ORDER)
		return llreq(order, 4, core);
	if (flags & __GFP_MOVABLE) {
		if (flags & ___GFP_PAGE_CACHE) {
			if (flags & (__GFP_NORETRY | __GFP_NOFAIL))
				return llreq(order, 3, core);
			return llreq(order, 2, core);
		}
		return llreq(order, 1, core);
	}
	return llreq(order, 0, core);
}

// -----------------------------------------------------------------------------
static inline llfree_classing_t ll_unused llfree_class_config_3(size_t cores)
{
	return (llfree_classing_t){ .num_classes = 4,
				    .default_class = 3,
				    .policy = llfree_linux_policy,
				    .classes = {
					    // Immovable
					    { .class = 0, .count = cores },
					    // Movable
					    { .class = 1, .count = cores },
					    // Page Cache
					    { .class = 2, .count = cores },
					    // Huge
					    { .class = 3, .count = cores },
				    } };
}
static inline llfree_request_t ll_unused llfree_request_3(uint8_t order,
							  gfp_t flags)
{
	if (order >= LLFREE_HUGE_ORDER) {
		ll_optional_t core = ll_some(raw_smp_processor_id());
		return llreq(order, 3, core);
	}
	ll_optional_t pid = ll_some(current->pid % num_possible_cpus());
	if (flags & __GFP_MOVABLE) {
		if (flags & ___GFP_PAGE_CACHE)
			return llreq(order, 2, pid);
		return llreq(order, 1, pid);
	}
	return llreq(order, 0, pid);
}

// -----------------------------------------------------------------------------

/// Create linux specific classing.
static inline llfree_classing_t ll_unused llfree_class_config_linux(size_t cores)
{
	pr_info("init classing level %u\n", llfree_class_config);

	if (llfree_class_config == 3)
		return llfree_class_config_3(cores);
	if (llfree_class_config == 2)
		return llfree_class_config_2(cores);
	if (llfree_class_config == 1)
		return llfree_class_config_1(cores);
	return llfree_class_config_0(cores);
}

llfree_request_t llfree_linux_request(uint8_t order, gfp_t flags)
{
	if (llfree_class_config == 3)
		return llfree_request_3(order, flags);
	if (llfree_class_config == 2)
		return llfree_request_2(order, flags);
	if (llfree_class_config == 1)
		return llfree_request_1(order, flags);
	return llfree_request_0(order, flags);
}

llfree_t *llfree_node_init(size_t node, size_t start_pfn, size_t pages)
{
	u64 offset = align_down(start_pfn, 1 << LLFREE_MAX_ORDER);
	pages += start_pfn - offset; // correct length

	pr_info("node=%" PRIuS ", offset=%" PRIu64 ", pages=%" PRIuS
		", trees=%" PRIuS ", children=%" PRIuS "\n",
		node, offset, pages, div_ceil(pages, LLFREE_TREE_SIZE),
		div_ceil(pages, LLFREE_CHILD_SIZE));

	llfree_t *self =
		memblock_alloc_node(sizeof(llfree_t), LLFREE_CACHE_SIZE, node);

	llfree_classing_t classing = llfree_class_config_linux(num_possible_cpus());

	llfree_meta_size_t m = llfree_metadata_size(&classing, pages);
	llfree_meta_t meta = {
		.local = memblock_alloc_node(m.local, LLFREE_CACHE_SIZE, node),
		.trees = memblock_alloc_node(m.trees, LLFREE_CACHE_SIZE, node),
		.lower = memblock_alloc_node(m.lower, LLFREE_CACHE_SIZE, node),
	};
	llfree_result_t res =
		llfree_init(self, pages, LLFREE_INIT_ALLOC, meta, &classing);

	BUG_ON(!llfree_is_ok(res));

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

static void writer(void *arg, const char *str)
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
			ll_stats_t stats = llfree_stats_at(
				zone->llfree, frame_id(i), LLFREE_HUGE_ORDER);
			// [0, 9], where 0 is entirely allocated and 9 is free
			size_t level = stats.free_frames == 0 ?
					       0 :
					       (stats.free_frames / 64 + 1);
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

EXPORT_SYMBOL(llfree_stats);
EXPORT_SYMBOL(llfree_stats_at);
EXPORT_SYMBOL(llfree_tree_stats);
// EXPORT_SYMBOL(llfree_dump);
// EXPORT_SYMBOL(llfree_print);
