# Linux with LLFree

First enable the `CONFIG_LLFREE` option (`make O=build-llfree-vm LLVM=1 menuconfig`).
Currently only x86_64 has been tested.

Then the Kernel can be build as usual (with llvm):
```sh
make O=build-llfree-vm LLVM=1
```

## Structure

The llfree module can be found in [mm/llfree](mm/llfree).
It consists of the [llfree-c](https://github.com/luhsra/llfree-c) library and a small wrapper.
