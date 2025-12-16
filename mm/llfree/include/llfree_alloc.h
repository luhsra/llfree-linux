#ifndef _LLFREE
#define _LLFREE

#ifdef CONFIG_LLFREE

#include <llfree.h>
#include <linux/gfp_types.h>

static inline llflags_t llflags_gfp(gfp_t gfp, int order)
{
#ifdef CONFIG_LLFREE_PAGE_CACHE
	// bool long_living =
	// 	(gfp & (__GFP_RECLAIMABLE | __GFP_WRITE | __GFP_NOFAIL |
	// 		__GFP_NORETRY | ___GFP_PAGE_CACHE)) ?
	// 		true :
	// 		false;
	bool long_living = (gfp & ___GFP_PAGE_CACHE) ? true : false;
#else
	bool long_living = false;
#endif
	// For now we only consider long_living for movable allocations
	// This makes prioritizing trees easier
	bool movable = (gfp & __GFP_MOVABLE) ? true : false;
#ifdef CONFIG_LLZERO
	bool zeroed = (gfp & __GFP_ZERO) ? true : false;
#else
	bool zeroed = false;
#endif
	return (llflags_t){ .order = (uint8_t)order,
			    .movable = movable,
			    .zeroed = zeroed,
			    .long_living = movable && long_living };
}

/// Create a new allocator instance for the given node
llfree_t *llfree_node_init(size_t node, size_t cores, size_t start_pfn,
			   size_t pages);

#endif // CONFIG_LLFREE
#endif // _LLFREE
