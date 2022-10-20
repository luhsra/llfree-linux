# In kbuild context
obj-y := nvalloc.o
nvalloc-objs := nvalloc_c.o nvalloc_rs.o

nvalloc-a := mm/nvalloc/target/x86_64-linux/release/libnvalloc_module.a

$(nvalloc-a):
	cd mm/nvalloc && cargo build -r

mm/nvalloc/nvalloc_rs.o: $(nvalloc-a)
	ld.lld -r -m elf_x86_64 --thinlto-jobs=4 --whole-archive $^ -o $@
	mv $(basename $(nvalloc-a)).d $(depfile); \
	sed -i '/^\#/d' $(depfile)
	printf '%s\n' 'cmd_$@ := $(make-cmd)' > $(dot-target).cmd

# Manual compilation
.PHONY: $(nvalloc-a)
