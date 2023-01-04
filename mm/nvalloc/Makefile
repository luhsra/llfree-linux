# In kbuild context
obj-$(CONFIG_NVALLOC) += nvalloc.o
obj-$(CONFIG_NVALLOC_SIZE_COUNTERS) += size_counters.o

nvalloc-objs := nvalloc_c.o nvalloc_rs.o

nvalloc-a := mm/nvalloc/target/x86_64-linux/release/libnvalloc_module.a

$(nvalloc-a):
	cd $(srctree)/mm/nvalloc && CARGO_TARGET_DIR=$(abs_objtree)/mm/nvalloc/target cargo build -r
	cd $(abs_objtree)

mm/nvalloc/nvalloc_rs.o: $(nvalloc-a)
	ld.lld -r -m elf_x86_64 --thinlto-jobs=4 --whole-archive $^ -o $@
	mv $(basename $(nvalloc-a)).d $(depfile); \
	sed -i '/^\#/d' $(depfile)
	printf '%s\n' 'cmd_$@ := $(make-cmd)' > $(dot-target).cmd

# Manual compilation
.PHONY: $(nvalloc-a)
