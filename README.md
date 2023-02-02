This is a modified Linux kernel to enable the morsel implementation as loadable kernel module.
Morsels are self-contained memory primitives that use page-table subtrees as indivisble units moving the managed of memory from individual pages to higher lelvels without the drawbacks of huge/giant pages.

There are no special requirements to the kernel configuration except the 5-level paging feature `CONFIG_X86_5LEVEL=y`. It extends the virtual address width from 48 bit to 57 bit. Nevertheless the compiled kernel can still be booted on systems supporting only 4 paging levels. The _P4D_ level is then folded at runtime. The dynamic folding mechanism must be kept in mind for the morsel implementation in order to still achive compatibility with legacy machines. So far, _verliernix_ should be the only available server with native 5-level paging support. For debugging purposes, qemu can simulate 5-level paging on every machine at the expense of execution speed.

The current implementation targets the AMD64 architecture but the general concept could be applied to all architectures with paging support.
