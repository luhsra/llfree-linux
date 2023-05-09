# Linux with LLFree

In addition to the general dependencies rust `nightly` version `1.64.0` (or newer) is needed.
> Rustup is recommended: [install rust](https://www.rust-lang.org/learn/get-started)

First enable the `CONFIG_LLFREE` and `CONFIG_LLFREE_FAST_FREE` options (`make LLVM=1 menuconfig`). It is currently limited to x86_64.
Then the Kernel can be build as usual (with llvm):

```sh
make O=build-llfree-vm LLVM=1 #...
# the resulting kernel can be tested in a vm
```

The `build-llfree-deb/.config` can be used to test the kernel on real hardware.
This config is based on the debian 11 config for the 6.0 Kernel.
The following commands build and install the kernel and its modules on the local Linux installation.

```sh
# compile everything
make O=build-llfree-deb LLVM=1 -j50
# install it
make O=build-llfree-deb LLVM=1 -j50 modules_install install
```

The Kernel can then be chosen via `grub` or a similar boot manager.


## Structure

The llfree module can be found in [mm/llfree](mm/llfree).
It consists of a rust library that uses the [llfree-rs](https://github.com/luhsra/llfree-rs) crate and a c wrapper.

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
