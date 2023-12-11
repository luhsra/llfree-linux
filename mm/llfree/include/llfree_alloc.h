#ifndef _LLFREE
#define _LLFREE

#ifdef CONFIG_LLFREE

#include <llfree.h>

/// Create a new allocator instance for the given node
llfree_t *llfree_node_init(size_t node, size_t cores, size_t start_pfn,
			   size_t pages);

#endif // CONFIG_LLFREE
#endif // _LLFREE
