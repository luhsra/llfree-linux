# In kbuild context
obj-$(CONFIG_LLFREE) += llfree.o
obj-$(CONFIG_LLFREE_SIZE_COUNTERS) += size_counters.o

llfree-objs := llfree_c.o llfree_rs.o

llfree-a := mm/llfree/target/x86_64-linux/release/libllfree_module.a

$(llfree-a):
	cd $(srctree)/mm/llfree && CARGO_TARGET_DIR=$(abs_objtree)/mm/llfree/target cargo build -r
	cd $(abs_objtree)

mm/llfree/llfree_rs.o: $(llfree-a)
	ld.lld -r -m elf_x86_64 --thinlto-jobs=4 --whole-archive $^ -o $@
	mv $(basename $(llfree-a)).d $(depfile); \
	sed -i '/^\#/d' $(depfile)
	printf '%s\n' 'cmd_$@ := $(make-cmd)' > $(dot-target).cmd

# Manual compilation
.PHONY: $(llfree-a)
