#ifndef _LLFREE
#define _LLFREE

#ifdef CONFIG_LLFREE

#include <linux/compiler_types.h>
#include <linux/types.h>
#include <asm/page_types.h>

/// Unused functions and variables
#define _unused __attribute__((unused))

#ifdef __clang__
#define _warn_unused __attribute__((warn_unused_result))
#else
#define _warn_unused
#endif

/// Result type, to distinguish between normal integers
///
/// Errors are negative and the actual values are zero or positive.
typedef struct _warn_unused llfree_result {
	int64_t val;
} llfree_result_t;

typedef struct llfree llfree_t;

/// Create a new result
static inline llfree_result_t _unused llfree_result(int64_t v)
{
	return (llfree_result_t){ v };
}
/// Check if the result is ok (no error)
static inline bool _unused llfree_ok(llfree_result_t r)
{
	return r.val >= 0;
}

/// Error codes
enum {
	/// Success
	LLFREE_ERR_OK = 0,
	/// Not enough memory
	LLFREE_ERR_MEMORY = -1,
	/// Failed atomic operation, retry procedure
	LLFREE_ERR_RETRY = -2,
	/// Invalid address
	LLFREE_ERR_ADDRESS = -3,
	/// Allocator not initialized or initialization failed
	LLFREE_ERR_INIT = -4,
};

/// Create a new allocator instance for the given node
llfree_t *llfree_node_init(size_t node, size_t cores, uint64_t start_pfn,
			   size_t pages);

/// Uninitialize the allocator
void llfree_uninit(llfree_t *alloc);

/// Allocates a frame and returns its number, or a negative error code
llfree_result_t llfree_get(llfree_t *self, size_t core, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
llfree_result_t llfree_put(llfree_t *self, size_t core, uint64_t frame,
			   size_t order);

/// Returns the number of cores this allocator was initialized with
size_t llfree_cores(llfree_t *self);
/// Returns the total number of frames the allocator can allocate
size_t llfree_frames(llfree_t *self);

/// Returns number of currently free frames
size_t llfree_free_frames(llfree_t *self);
/// Returns number of currently free frames
size_t llfree_free_huge(llfree_t *self);

/// Checks if a frame is allocated, returning 0 if not
bool llfree_is_free(llfree_t *self, uint64_t frame, size_t order);
/// Returns the number of frames in the given chunk.
/// This is only implemented for 0, HUGE_ORDER and TREE_ORDER.
size_t llfree_free_at(llfree_t *self, uint64_t frame, size_t order);

/// Unreserves all cpu-local reservations
llfree_result_t llfree_drain(llfree_t *self, size_t core);

/// Prints the allocators state
void llfree_printk(llfree_t *self);
/// Prints the allocators state to the buffer
size_t llfree_dump(llfree_t *self, u8 *buf, size_t len);

/// kprint support for rust fmt
char *rust_fmt_argument(char *buf, char *end, void *ptr);

#endif // CONFIG_LLFREE
#endif // _LLFREE
