#include "linux/mmzone.h"
#include "linux/printk.h"
#include "nvalloc.h"

#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/memblock.h>

MODULE_LICENSE("MIT");
MODULE_DESCRIPTION("NVM Allocator");
MODULE_AUTHOR("Lars Wrenger");

#define MOD KBUILD_MODNAME ": "

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
void nvalloc_printk(const u8 *format, const u8 *module_name, const void *args)
{
	_printk(format, module_name, args);
}

static int __init nvalloc_init_module(void)
{
	int cpu;
	s64 ret;
	void *addr;

	pr_info(MOD "try allocation");

	cpu = get_cpu();
	addr = nvalloc_get(ZONE_NORMAL, cpu, 0);
	if (nvalloc_err((u64)(addr)))
	{
		pr_info(MOD "error alloc %ld\n", (u64)addr);
		put_cpu();
		return -ENOMEM;
	}
	put_cpu();

	pr_info(MOD "allocated %p on %d\n", addr, cpu);

	cpu = get_cpu();
	ret = nvalloc_put(ZONE_NORMAL, cpu, addr, 0);
	if (nvalloc_err(ret))
	{
		pr_info(MOD "error free %ld\n", ret);
		put_cpu();
		return -ENOMEM;
	}
	put_cpu();

	pr_info(MOD "success\n");
	return 0;
}

static void nvalloc_cleanup_module(void)
{
	pr_info(MOD "uninit\n");
}

module_init(nvalloc_init_module);
module_exit(nvalloc_cleanup_module);
