# Linux with NVAlloc

In addition to the general dependencies rust `nightly` version `1.64.0` (or newer) is needed.
> Rustup is recommended: [install rust](https://www.rust-lang.org/learn/get-started)

Then the Kernel can be build (with llvm):

```
make LLVM=1 ...
```

## Structure

The nvalloc module can be found in [mm/nvalloc](mm/nvalloc).
It consists of a rust library that uses the [nvalloc-rs](https://scm.sra.uni-hannover.de/research/nvalloc-rs) crate and a c wrapper.

## Problems

- Please increase KSYM_NAME_LEN both in kernel and kallsyms.c
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

Logging is automatically initialized with the allocator.
