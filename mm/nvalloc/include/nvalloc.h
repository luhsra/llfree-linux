#ifndef _NVALLOC
#define _NVALLOC

#ifdef CONFIG_NVALLOC

#include <asm/page_types.h>

/// Error codes
enum nvalloc_error_t {
	NVALLOC_ERROR_MEMORY = 1,
	NVALLOC_ERROR_CAS = 2,
	NVALLOC_ERROR_ADDRESS = 3,
	NVALLOC_ERROR_INIT = 4,
	NVALLOC_ERROR_CORRUPTION = 5,
};

/// Initialize the allocator for the given memory range.
/// Returns 0 on success or an error code.
void *nvalloc_init(u64 node, u32 cores, u8 persistent, void *start, u64 pages);
/// Uninitialize the allocator
void nvalloc_uninit(void *alloc);

/// Allocates 2^order pages. Returns >=PAGE_SIZE on success an error code.
u8 *nvalloc_get(void *alloc, u32 core, u32 order);
/// Frees a previously allocated page. Returns 0 on success or an error code.
u64 nvalloc_put(void *alloc, u32 core, u8 *addr, u32 order);
/// Checks if the page is free
u64 nvalloc_is_free(void *alloc, u8 *addr, u32 order);
/// Drain any CPU-local reservations
u64 nvalloc_drain(void *alloc, u32 core);

/// Debug: Return number of free pages.
u64 nvalloc_free_count(void *alloc);
/// Debug: Return number of free huge pages.
u64 nvalloc_free_huge_count(void *alloc);
/// Debug: KPrint the allocator state
void nvalloc_printk(void *alloc);
/// Debug: Print allocator state to the given buffer
u64 nvalloc_dump(void *alloc, u8 *buf, u64 len);
/// Debug: Execute f for every huge page with the number of free pages
void nvalloc_for_each_huge_page(void *alloc, void (*f)(void *, u16), void *arg);

static inline bool nvalloc_err(u64 ret)
{
	return 0 < ret && ret < PAGE_SIZE;
}

/// kprint support for rust fmt
char *rust_fmt_argument(char *buf, char *end, void *ptr);

#endif // CONFIG_NVALLOC
#endif // _NVALLOC
