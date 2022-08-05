#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "nvalloc.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>

MODULE_LICENSE("MIT");
MODULE_DESCRIPTION("NVM Allocator");
MODULE_AUTHOR("Lars Wrenger");

// Functions needed by the allocator

/// Linux provided alloc function
u8 *nvalloc_linux_alloc(u64 size, u64 align)
{
	return memblock_alloc(size, align);
}
/// Linux provided free function
void nvalloc_linux_free(u8 *ptr, u64 size, u64 align)
{
	memblock_free(ptr, size);
}
/// Linux provided printk function
void nvalloc_linux_printk(const u8 *format, const u8 *module_name, const void *args)
{
	_printk(format, module_name, args);
}

static int __init nvalloc_init_module(void)
{
	int cpu;
	s64 ret;
	void *addr;
	void *alloc;

	pr_info("try allocation");

	alloc = first_online_pgdat()->node_zones[ZONE_NORMAL].nvalloc;

	cpu = get_cpu();
	addr = nvalloc_get(alloc, cpu, 0);
	if (nvalloc_err((u64)(addr)))
	{
		put_cpu();
		pr_info("error alloc %ld\n", (u64)addr);
		return -ENOMEM;
	}
	put_cpu();

	pr_info("allocated %p on %d\n", addr, cpu);

	cpu = get_cpu();
	ret = nvalloc_put(alloc, cpu, addr, 0);
	if (nvalloc_err(ret))
	{
		put_cpu();
		pr_info("error free %ld\n", ret);
		return -ENOMEM;
	}
	put_cpu();

	pr_info("success\n");
	return 0;
}

static void nvalloc_cleanup_module(void)
{
	pr_info("uninit\n");
}

module_init(nvalloc_init_module);
module_exit(nvalloc_cleanup_module);
