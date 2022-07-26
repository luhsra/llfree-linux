# Linux with LLFree and Morsel

## Morsel

This is a modified Linux kernel to enable the morsel implementation as loadable kernel module.
Morsels are self-contained memory primitives that use page-table subtrees as indivisible units moving the managed of memory from individual pages to higher levels without the drawbacks of huge/giant pages.
=> *Sparsely populated huge pages*

The kernel must be build with 5-level paging `CONFIG_X86_5LEVEL=y`. It extends the virtual address width from 48 bit to 57 bit. Nevertheless the compiled kernel can still be booted on systems supporting only 4 paging levels. The _P4D_ level is then folded at runtime. The dynamic folding mechanism must be kept in mind for the morsel implementation in order to still achieve compatibility with legacy machines. So far, _verliernix_ should be the only available server with native 5-level paging support. For debugging purposes, qemu can simulate 5-level paging on every machine at the expense of execution speed.

Disable transparent huge pages `TRANSPARENT_HUGEPAGE=n` to prevent that the kernel routines interfere with morsel page tables.

The current implementation targets the AMD64 architecture but the general concept could be applied to all architectures with multi-level paging support.

## LLFree

First enable the `CONFIG_LLFREE` and `CONFIG_LLFREE_FAST_FREE` options (`make LLVM=1 menuconfig`). It is currently limited to x86_64.
Then the Kernel can be build as usual (with llvm):

```sh
make O=build-llfree-vm LLVM=1 #...
```

## Structure

The llfree module can be found in [mm/llfree](mm/llfree).
It consists of a rust library that uses the [llfree-rs](https://scm.sra.uni-hannover.de/research/llfree-rs) crate and a c wrapper.

## Problems

- Please increase `KSYM_NAME_LEN` both in kernel and kallsyms.c
- Non-allocatable sections: .llvmbc, .llvmcmd
- Unknown sections: .text.__rust_probestack, .eh_frame

## Support for printk

The exported function `rust_fmt_argument` is a custom formatter for the rust print formatting.
It has to be called in `lib/vsprintf.c:pointer` for the `%pA` format argument:

```c
char *pointer(const char *fmt, char *buf, char *end, void *ptr,
	      struct printf_spec spec)
{
    switch(*fmt) {
// ...
    case 'A':
        return rust_fmt_argument(buf, end, ptr);
// ...
    }
}
```

> Logging is automatically initialized with the allocator.
