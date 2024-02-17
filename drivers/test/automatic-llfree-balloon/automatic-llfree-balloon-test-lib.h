#ifndef QEMU_AUTOMATIC_LLFREE_BALLOON_TEST_H
#define QEMU_AUTOMATIC_LLFREE_BALLOON_TEST_H

#include "linux/types.h"
#define ZONE_NORMAL_FLAGS (GFP_HIGHUSER_MOVABLE | __GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN)
#define ZONE_DMA_FLAGS (GFP_DMA32)

uint64_t alloc_test_base_page(uint32_t num_gib, gfp_t alloc_flags);
uint64_t alloc_test_huge_page(uint32_t num_gib, gfp_t alloc_flags);
uint64_t consume_test_base_page(uint32_t num_gib, gfp_t alloc_flags);
uint64_t consume_test_huge_page(uint32_t num_gib, gfp_t alloc_flags);
uint64_t alloc_test_multithreaded(uint32_t num_gib);

#endif
