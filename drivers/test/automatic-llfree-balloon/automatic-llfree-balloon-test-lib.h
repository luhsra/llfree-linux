#ifndef QEMU_AUTOMATIC_LLFREE_BALLOON_TEST_H
#define QEMU_AUTOMATIC_LLFREE_BALLOON_TEST_H

uint64_t alloc_test_base_page(uint32_t num_gib);
uint64_t alloc_test_huge_page(uint32_t num_gib);
uint64_t consume_test_base_page(uint32_t num_gib);
uint64_t consume_test_huge_page(uint32_t num_gib);

#endif
