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

struct nvalloc_zoneinfo {
	u8 skip;
	u8 persistent;
	void *start;
	u64 pages;
};

/// Initialize the allocator for the given memory range.
/// Returns 0 on success or an error code.
u64 nvalloc_init(u32 zones, u32 cores,
		 struct nvalloc_zoneinfo (*zoneinfo)(u32));
/// Uninitialize the allocator
void nvalloc_uninit(void);

/// Returns if the allocator was initialized
u32 nvalloc_initialized(void);
/// Allocates 2^order pages. Returns >=PAGE_SIZE on success an error code.
u8 *nvalloc_get(u32 zone, u32 core, u32 order);
/// Frees a previously allocated page. Returns 0 on success or an error code.
u64 nvalloc_put(u32 zone, u32 core, u8 *addr, u32 order);

/// Debug: Return number of free pages.
u64 nvalloc_free(u32 zone);
/// Debug: KPrint the allocator state
void nvalloc_dump(u32 zone);


static inline bool nvalloc_err(u64 ret)
{
	return 0 < ret && ret < PAGE_SIZE;
}

/// kprint support for rust fmt
char *rust_fmt_argument(char *buf, char *end, void *ptr);

#endif // CONFIG_NVALLOC
#endif // _NVALLOC
