#ifndef _LLFREE_SIZE_COUNTERS
#define _LLFREE_SIZE_COUNTERS

#include <linux/types.h>

#ifdef CONFIG_LLFREE_SIZE_COUNTERS

void size_counters_alloc(gfp_t flags, int order);
void size_counters_bulk_alloc(gfp_t flags, u64 inc);
void size_counters_free(int order);
void size_counters_bulk_free(u64 inc);
void size_counters_trace(bool alloc, gfp_t flags, int order, size_t pfn);

#else
static inline u64 size_counters_start(void)
{
	return 0;
}
static inline void size_counters_alloc(gfp_t flags, int order)
{
}
static inline void size_counters_bulk_alloc(gfp_t flags, u64 inc)
{
}
static inline void size_counters_free(int order)
{
}
static inline void size_counters_bulk_free(u64 inc)
{
}
static inline void size_counters_trace(bool alloc, gfp_t flags, int order, size_t pfn)
{
}
#endif // CONFIG_LLFREE_SIZE_COUNTERS

#endif // _LLFREE_SIZE_COUNTERS
