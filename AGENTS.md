# Linux with LLFree Allocator

LLFree is a lock-free page frame allocator, optimized for the parallel allocation of base and hugepages and efficient hugepage fragmentation avoidance.

The core implementation is in the git submodule `mm/llfree/llc` (header `mm/llfree/llc/include/llfree.h`).
The kernel module in `mm/llfree` (headers `mm/llfree/include`) contain wrappers for Linux.

The allocator is integrated as a replacement of the buddy allocator into `mm/page_alloc.c`.
The `CONFIG_LLFREE` KConfig option enables LLFree.

## Building

The repo already contains minimal config files for LLFree and Baseline (Buddy) Linux in `build-llfree-vm` and `build-buddy-vm`.

Build the LLFree kernel:
```bash
make O=build-llfree-vm
```

Build the baseline kernel:
```bash
make O=build-buddy-vm
```

## Notes

- LLFree is self-contained, it has its own memory for its state and does not depend on the kernel's struct pages or other external data
  - Other memory subsystems still do rely on the struct pages, so the allocation state still needs to be synchronized
- LLFree already manages hugepages, no need to rely on other mechanisms to preserve them
- For LLFree, checking if memory of a given size is available is usually not faster than directly trying to allocate it
- LLFree internally uses atomics to synchronize its state, no external locking is necessary and would only slow down concurrent allocations
- We only want to support x86-64 bit for now
