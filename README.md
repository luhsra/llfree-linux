# Linux with LLFree

First enable the `CONFIG_LLFREE` option (`make O=build-llfree-vm LLVM=1 menuconfig`).
Currently only x86_64 has been tested.

First, load git submodules:
```sh
git submodule update --init
```

Then the Kernel can be build as usual:
```sh
make O=build-llfree-vm
```

> For baseline Linux with the buddy allocator:
> ```sh
> make O=build-buddy-vm
> ```

## Structure

The llfree module can be found in [mm/llfree](mm/llfree).
It consists of the [llfree-c](https://github.com/luhsra/llfree-c) library and a small wrapper.
