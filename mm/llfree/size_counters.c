#include <size_counters.h>

#include <linux/gfp.h>
#include <linux/kobject.h>
#include <linux/percpu.h>

// Count different allocation sizes
struct size_counters {
	u64 c[3][MAX_ORDER];
	u64 bulk[3];
};
static DEFINE_PER_CPU(struct size_counters, size_counters);
static int size_counters_active = false;

void size_counters_alloc(gfp_t flags, int order)
{
	if (size_counters_active) {
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		BUG_ON(sc == NULL);
		if (flags & ___GFP_SC_USER)
			sc->c[1][order] += 1;
		else
			sc->c[0][order] += 1;
		put_cpu_ptr(sc);
	}
}
void size_counters_bulk_alloc(gfp_t flags, u64 inc)
{
	if (size_counters_active) {
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		BUG_ON(sc == NULL);
		if (flags & ___GFP_SC_USER)
			sc->bulk[1] += inc;
		else
			sc->bulk[0] += inc;
		put_cpu_ptr(sc);
	}
}
void size_counters_free(int order)
{
	if (size_counters_active) {
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		BUG_ON(sc == NULL);
		sc->c[2][order] += 1;
		put_cpu_ptr(sc);
	}
}

void size_counters_bulk_free(u64 inc)
{
	if (size_counters_active) {
		struct size_counters *sc = get_cpu_ptr(&size_counters);
		BUG_ON(sc == NULL);
		sc->bulk[2] += inc;
		put_cpu_ptr(sc);
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

/// Output the size counters in csv format
static ssize_t size_counters_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	char ops[] = { 'a', 'u', 'f' };
	size_t len = 0;

	// csv header
	len += _check_ret(
		snprintf(buf + len, PAGE_SIZE - len, "op,order,count,bulk\n"));

	// csv body
	for (size_t o = 0; o < sizeof(ops) / sizeof(*ops); o++) {
		for (size_t order = 0; order < MAX_ORDER; order++) {
			u64 count = 0;
			u64 bulk = 0;
			size_t cpu;

			for_each_possible_cpu(cpu) {
				struct size_counters *sc =
					per_cpu_ptr(&size_counters, cpu);
				count += sc->c[o][order];
				if (order == 0)
					bulk += sc->bulk[o];
			}

			len += _check_ret(snprintf(buf + len, PAGE_SIZE - len,
						   "%c,%lld,%lld,%lld\n",
						   ops[o], order, count, bulk));
		}
	}

	if (len < PAGE_SIZE)
		buf[len] = '\0';
	return len;
}
#undef _check_ret

static struct kobj_attribute size_counters_attr =
	__ATTR(size_counters, 0444, size_counters_show, NULL);
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
	size_counters_active = true;
	return 0;
}
postcore_initcall(size_counters_init);
