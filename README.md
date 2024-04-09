# LLZero: Linux

This repository contains a Linux 6.1 with integrated [LLFree](https://github.com/luhsra/llfree-c) and Zeroing.

- See: https://scm.sra.uni-hannover.de/research/llfree/llzero-bench

## Build

Download the llfree submodule.

```sh
git submodule update --init
```

Either use one of the provided configs under `build-*/.config` or enable the `CONFIG_LLFREE` option for LLFree `CONFIG_VIRTIO_LLFREE_BALLOON` for HyperAlloc (`make LLVM=1 menuconfig`) and `CONFIG_LLZERO` for zeroing.

```sh
make O=build-llfree-vm LLVM=1
```

The paper uses the following configs:
- `build-buddy-vm`: Minimal VM config without LLFree and HyperAlloc
- `build-buddy-huge`: Minimal VM config with modified virtio-balloon to use huge pages
- `build-llfree-vm`: Minimal VM config with LLFree and HyperAlloc

## Structure

The llfree module can be found in [mm/llfree](mm/llfree).
It contains the llfree allocator, which is used in [page_alloc.c](mm/page_alloc.c).

The HyperAlloc driver is in [virtio_llfree_balloon.h](include/uapi/linux/virtio_llfree_balloon.h) and [virtio_llfree_balloon.c](drivers/virtio/virtio_llfree_balloon.c).
The hypercalls for installing memory are issues in [page_alloc.c](mm/page_alloc.c).

## Testing Module

An additional kernel module is available in [drivers/test/automatic-llfree-balloon](drivers/test/automatic-llfree-balloon).
It can be used to measure and test (multi-threaded) guest triggered auto-deflate.
