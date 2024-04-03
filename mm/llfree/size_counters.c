#include "linux/types.h"
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

#define ALLOCATION_LEN 100000000lu
enum SC_KIND { SC_ALLOC = 0, SC_MOVABLE = 1, SC_FREE = 2, SC_KINDS };

static int kind_from_flags(bool alloc, gfp_t flags)
{
	if (!alloc)
		return SC_FREE;
	if (flags & ___GFP_MOVABLE)
		return SC_MOVABLE;
	return SC_ALLOC;
}

// Count different allocation sizes
struct size_counters {
	u64 c[SC_KINDS][MAX_ORDER];
};
static DEFINE_PER_CPU(struct size_counters, size_counters);
static int size_counters_active = false;

static atomic_long_t allocations_idx = ATOMIC_LONG_INIT(0);
static u32 *allocations = NULL;

void size_counters_alloc(gfp_t flags, int order)
{
	if (size_counters_active) {
		int kind = kind_from_flags(true, flags);
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		sc->c[kind][order] += 1;
		put_cpu_ptr(sc);
	}
}
void size_counters_bulk_alloc(gfp_t flags, u64 inc)
{
	if (size_counters_active) {
		int kind = kind_from_flags(true, flags);
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		sc->c[kind][0] += inc;
		put_cpu_ptr(sc);
	}
}
void size_counters_free(int order)
{
	if (size_counters_active) {
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		sc->c[SC_FREE][order] += 1;
		put_cpu_ptr(sc);
	}
}
void size_counters_bulk_free(u64 inc)
{
	if (size_counters_active) {
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		sc->c[SC_FREE][0] += inc;
		put_cpu_ptr(sc);
	}
}

void size_counters_trace(bool alloc, gfp_t flags, int order, size_t pfn)
{
	if (size_counters_active && allocations) {
		int kind = kind_from_flags(alloc, flags);
		long idx = atomic_long_inc_return(&allocations_idx);
		BUG_ON(idx >= ALLOCATION_LEN || pfn >= (1 << 24));
		allocations[idx] = (kind << 30) | (order << 24) |
				   (pfn & 0xFFFFFF);
	}
}

#define _check_ret(ret)                                        \
	({                                                     \
		int __ret = ret;                               \
		if (__ret < 0) {                               \
			pr_err("Error reading size_counters"); \
			return -ENOMEM;                        \
		}                                              \
		__ret;                                         \
	});

static ssize_t sc_trace_read(struct file *file, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t len)
{
	pr_info("read trace %lld %zu of %zu", off, len, bin_attr->size);

	if (allocations != NULL)
		memcpy(buf, allocations + off, len);
	return bin_attr->size - min(bin_attr->size, (size_t)off);
}

static struct bin_attribute bin_attr_sc_trace = {
	.attr = { .name = "trace", .mode = 0444, },
	.read = sc_trace_read,
	.size = 0,
};

/// Output the size counters in csv format
static ssize_t size_counters_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	char ops[] = { 'a', 'm', 'f' };
	size_t len = 0;

	// csv header
	len += _check_ret(
		snprintf(buf + len, PAGE_SIZE - len, "op,order,count\n"));

	// csv body
	for (size_t kind = 0; kind < sizeof(ops) / sizeof(*ops); kind++) {
		for (size_t order = 0; order < MAX_ORDER; order++) {
			u64 count = 0;
			size_t cpu;

			for_each_possible_cpu(cpu) {
				struct size_counters *sc =
					per_cpu_ptr(&size_counters, cpu);
				count += sc->c[kind][order];
			}

			len += _check_ret(snprintf(buf + len, PAGE_SIZE - len,
						   "%c,%zu,%llu\n", ops[kind],
						   order, count));
		}
	}

	if (len < PAGE_SIZE)
		buf[len] = '\0';
	return len;
}

ssize_t size_counters_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	if (buf == NULL || count == 0) {
		pr_err("Invalid input");
		return -EINVAL;
	}

	if (*buf == '0') {
		pr_info("end");
		size_counters_active = false;
		if (allocations != NULL) {
			long len = atomic_long_read(&allocations_idx);
			pr_warn("trace end: %ld", len);
			bin_attr_sc_trace.size = len * sizeof(u32);
		}
		return count;
	} else if (*buf == '1' || *buf == '2') {
		pr_info("start");
		if (allocations != NULL)
			kvfree(allocations);

		// clear the buffer
		if (*buf == '1') {
			allocations = NULL;
		} else {
			pr_info("start trace");
			allocations = kvmalloc(ALLOCATION_LEN * sizeof(u32),
					       GFP_KERNEL);
		}

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

		// reset the index
		atomic_long_set(&allocations_idx, 0);
		bin_attr_sc_trace.size = 0;
		size_counters_active = true;
		return count;
	}

	pr_err("Invalid input");
	return -EINVAL;
}

#undef _check_ret

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

static int __init size_counters_init(void)
{
	int retval;
	pr_info("Initializing size_counters obj");

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
