obj-$(CONFIG_LLFREE) := llfree.o
obj-$(CONFIG_LLFREE_SIZE_COUNTERS) := size_counters.o

ccflags-$(CONFIG_LLFREE) += -I$(srctree)/mm/llfree/private -I$(srctree)/mm/llfree/llc/src
ccflags-remove-$(CONFIG_LLFREE) := -Wdeclaration-after-statement

llfree-objs := llfree.o llc/src/llfree.o llc/src/bitfield.o llc/src/child.o llc/src/local.o llc/src/lower.o llc/src/tree.o
