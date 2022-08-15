# In kbuild context
obj-y := nvalloc.o
nvalloc-objs := nvalloc_c.o nvalloc_rs.o

# LINUXINCLUDE += -I$(srctree)/mm/nvalloc/include

nvalloc-a := mm/nvalloc/target/x86_64-linux/release/libnvalloc_module.a

$(nvalloc-a):
	cd mm/nvalloc && cargo build -r

mm/nvalloc/nvalloc_rs.o: $(nvalloc-a)
	ld.lld -r -m elf_x86_64 --thinlto-jobs=4 --whole-archive $^ -o $@

# Manual compilation
.PHONY: $(nvalloc-a)
