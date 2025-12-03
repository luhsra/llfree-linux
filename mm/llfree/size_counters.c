#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <size_counters.h>

#include <linux/printk.h>
#include <linux/gfp.h>
#include <linux/kobject.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/module.h>

MODULE_LICENSE("MIT");
MODULE_DESCRIPTION("LLFree Size Counters");
MODULE_AUTHOR("Lars Wrenger");

#define _check_ret(ret)                                          \
	({                                                       \
		int __ret = ret;                                 \
		if (__ret < 0) {                                 \
			pr_err("Error reading size_counters\n"); \
			return -ENOMEM;                          \
		}                                                \
		__ret;                                           \
	});

// -----------------------------------------------------------------------------
// Simple allocation size counters
// -----------------------------------------------------------------------------

enum SC_KIND {
	SC_FIXED,
	SC_FIXED_ZERO,
	SC_MOVABLE,
	SC_MOVABLE_ZERO,
	SC_FREE,
	SC_KINDS
};
static const char *sc_kind_names[] = {
	[SC_FIXED] = "k",	  [SC_FIXED_ZERO] = "kz", [SC_MOVABLE] = "m",
	[SC_MOVABLE_ZERO] = "mz", [SC_FREE] = "f",
};
static_assert(ARRAY_SIZE(sc_kind_names) == SC_KINDS,
	      "Number of sc_kind_names must match SC_KINDS");

static int kind_from_flags(bool alloc, gfp_t flags)
{
	int kind;
	if (!alloc)
		return SC_FREE;
	kind = (flags & __GFP_MOVABLE) ? SC_MOVABLE : SC_FIXED;
	if (flags & __GFP_ZERO)
		kind += 1;
	return kind;
}

// Count different allocation sizes
struct size_counters {
	u64 c[SC_KINDS][MAX_ORDER];
};
static DEFINE_PER_CPU(struct size_counters, size_counters);
static int size_counters_active = false;

void size_counters_alloc(gfp_t flags, int order)
{
	if (unlikely(size_counters_active)) {
		int kind = kind_from_flags(true, flags);
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		sc->c[kind][order] += 1;
		put_cpu_ptr(sc);
	}
}
void size_counters_bulk_alloc(gfp_t flags, u64 inc)
{
	if (unlikely(size_counters_active)) {
		int kind = kind_from_flags(true, flags);
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		sc->c[kind][0] += inc;
		put_cpu_ptr(sc);
	}
}
void size_counters_free(int order)
{
	if (unlikely(size_counters_active)) {
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		sc->c[SC_FREE][order] += 1;
		put_cpu_ptr(sc);
	}
}
void size_counters_bulk_free(u64 inc)
{
	if (unlikely(size_counters_active)) {
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		sc->c[SC_FREE][0] += inc;
		put_cpu_ptr(sc);
	}
}

/// Output the size counters in csv format
static ssize_t size_counters_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	size_t len = 0;

	// csv header
	len += _check_ret(
		snprintf(buf + len, PAGE_SIZE - len, "op,order,count\n"));

	// csv body
	for (size_t kind = 0; kind < SC_KINDS; kind++) {
		for (size_t order = 0; order < MAX_ORDER; order++) {
			u64 count = 0;
			size_t cpu;

			for_each_possible_cpu(cpu) {
				struct size_counters *sc =
					per_cpu_ptr(&size_counters, cpu);
				count += sc->c[kind][order];
			}

			len += _check_ret(snprintf(
				buf + len, PAGE_SIZE - len, "%s,%zu,%llu\n",
				sc_kind_names[kind], order, count));
		}
	}

	if (len < PAGE_SIZE)
		buf[len] = '\0';
	return len;
}

static ssize_t size_counters_store(struct kobject *kobj,
				   struct kobj_attribute *attr, const char *buf,
				   size_t count)
{
	if (buf == NULL || count == 0) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	if (*buf == '0') {
		pr_info("end\n");
		size_counters_active = false;
		return count;
	} else if (*buf == '1') {
		pr_info("start\n");

		// clear the counters
		for (size_t kind = 0; kind < SC_KINDS; kind++) {
			for (size_t order = 0; order < MAX_ORDER; order++) {
				size_t cpu;
				for_each_possible_cpu(cpu) {
					struct size_counters *sc = per_cpu_ptr(
						&size_counters, cpu);
					sc->c[kind][order] = 0;
				}
			}
		}
		size_counters_active = true;
		return count;
	}

	pr_err("Invalid input\n");
	return -EINVAL;
}

static struct kobj_attribute size_counters_attr =
	__ATTR(size_counters, 0664, size_counters_show, size_counters_store);

static struct attribute *size_counters_attrs[] = {
	&size_counters_attr.attr,
	NULL, /* need to NULL terminate the list of attributes */
};
static struct attribute_group size_counters_group = {
	.attrs = size_counters_attrs,
};
static struct kobject *size_counters_obj;

// -----------------------------------------------------------------------------
// Lifetime tracing
// -----------------------------------------------------------------------------

static bool alloc_trace_active = false;

/// Allocation trace
struct alloc_entry {
	// 1/10 seconds since boot, up to ~109min
	u64 time_s_10 : 16;
	// is allocation
	u64 alloc : 1;
	// is huge page (order >= 9)
	u64 is_huge : 1;

	// 23 is enough in our config without ksan
	u64 flags : 23;
	// 16 GiB worth of pages
	u64 pfn : 23;
};
static inline struct alloc_entry alloc_entry_new(bool alloc, gfp_t flags,
						 int order, size_t pfn)
{
	BUG_ON(order < 0 || order >= MAX_ORDER);
	BUG_ON(pfn >= (1 << 23));
	BUG_ON(flags >= (1 << 23));
	// maybe reduce pfn to zone offset?

	return (struct alloc_entry){
		// fmt
		.time_s_10 = ktime_get_mono_fast_ns() / (NSEC_PER_SEC / 10),
		.flags = flags,
		.alloc = alloc ? 1 : 0,
		.is_huge = order >= 9 ? 1 : 0,
		.pfn = pfn
	};
}

#define ALLOC_ENTRY_PER_PAGE (PAGE_SIZE / sizeof(struct alloc_entry))

/// Page of allocation entries, reserved per cpu
struct alloc_entry_page {
	struct alloc_entry entries[ALLOC_ENTRY_PER_PAGE];
} __aligned(PAGE_SIZE);

/// Shared index into the allocation trace buffer
static atomic_long_t alloc_page_idx = ATOMIC_LONG_INIT(0);

/// Preallocated buffer size: 1 GiB
#define ALLOC_PAGES_LEN ((1ULL << 30ULL) / PAGE_SIZE)
/// Buffer for allocation trace, NULL if tracing is disabled
static struct alloc_entry_page *alloc_pages_buf = NULL;

/// Per-CPU index into the current allocation trace page
struct reserved_alloc_entry {
	struct alloc_entry_page *page;
	size_t entry_idx;
};
static DEFINE_PER_CPU(struct reserved_alloc_entry, alloc_entry_local);

void size_counters_trace(bool alloc, gfp_t flags, int order, size_t pfn)
{
	if (unlikely(alloc_trace_active)) {
		struct reserved_alloc_entry *entry =
			get_cpu_ptr(&alloc_entry_local);
		if (entry->page == NULL ||
		    entry->entry_idx >= ALLOC_ENTRY_PER_PAGE) {
			// need a new page
			long page_idx = atomic_long_fetch_inc(&alloc_page_idx);
			BUG_ON(alloc_pages_buf == NULL);
			BUG_ON(page_idx >= ALLOC_PAGES_LEN);
			entry->page = &alloc_pages_buf[page_idx];
			entry->entry_idx = 0;
		}
		entry->page->entries[entry->entry_idx++] =
			alloc_entry_new(alloc, flags, order, pfn);
		put_cpu_ptr(entry);
	}
}

static ssize_t sc_trace_read(struct file *file, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t len)
{
	size_t to_copy;
	if (alloc_trace_active) {
		pr_err("Tracing is still active\n");
		return -EINVAL;
	}

	// pr_info("read trace %lld %zu of %zu\n", off, len, bin_attr->size);
	if (off >= bin_attr->size)
		return 0;

	to_copy = min_t(size_t, bin_attr->size - (size_t)off, len);

	if (alloc_pages_buf != NULL)
		memcpy(buf, ((u8 *)alloc_pages_buf) + off, to_copy);
	return to_copy;
}

static ssize_t sc_trace_write(struct file *file, struct kobject *kobj,
			      struct bin_attribute *bin_attr, char *buf,
			      loff_t off, size_t len)
{
	size_t cpu;
	uint8_t *input = (uint8_t *)buf;
	if (len < 1) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}
	if (input[0] == '1') {
		pr_info("start trace\n");
		if (alloc_trace_active) {
			pr_err("Trace already started\n");
			return -EINVAL;
		}

		if (alloc_pages_buf != NULL)
			kvfree(alloc_pages_buf);

		alloc_pages_buf =
			kvcalloc(ALLOC_PAGES_LEN, PAGE_SIZE, GFP_KERNEL);
		BUG_ON(alloc_pages_buf == NULL);
		BUG_ON(((size_t)alloc_pages_buf) % PAGE_SIZE != 0);

		// reset trace state
		atomic_long_set(&alloc_page_idx, 0);
		bin_attr->size = 0;
		for_each_possible_cpu(cpu) {
			struct reserved_alloc_entry *entry =
				per_cpu_ptr(&alloc_entry_local, cpu);
			entry->page = NULL;
			entry->entry_idx = ALLOC_ENTRY_PER_PAGE;
		}

		alloc_trace_active = true;

		return len;

	} else if (input[0] == '0') {
		size_t pages = atomic_long_read(&alloc_page_idx) + 1;
		pr_info("stop trace\n");
		if (!alloc_trace_active) {
			pr_err("Trace not started\n");
			return -EINVAL;
		}

		pr_info("Buffer used pages: %ld => %ld\n", pages,
			pages * PAGE_SIZE);

		alloc_trace_active = false;
		bin_attr->size = pages * PAGE_SIZE;

		return len;
	}

	return -EINVAL;
}

static struct bin_attribute bin_attr_sc_trace = __BIN_ATTR_RW(sc_trace, 0);

// -----------------------------------------------------------------------------
// Module init
// -----------------------------------------------------------------------------

static int __init size_counters_init(void)
{
	int retval;
	pr_info("Initializing size_counters obj\n");

	size_counters_obj = kobject_create_and_add(KBUILD_MODNAME, kernel_kobj);
	if (!size_counters_obj) {
		pr_err("size_counters_obj failed\n");
		return -ENOMEM;
	}
	retval = sysfs_create_group(size_counters_obj, &size_counters_group);
	if (retval) {
		pr_err("size_counters_obj group failed\n");
		kobject_put(size_counters_obj);
	}

	retval = sysfs_create_bin_file(size_counters_obj, &bin_attr_sc_trace);
	if (retval) {
		pr_err("sc_trace_obj bin file failed\n");
		kobject_put(size_counters_obj);
		return -ENOMEM;
	}

	return 0;
}
postcore_initcall(size_counters_init);
