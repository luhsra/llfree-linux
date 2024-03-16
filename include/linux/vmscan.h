#ifndef __MM_VMSCAN_H__
#define __MM_VMSCAN_H__

#include <linux/types.h>

unsigned long shrink_pagecache_for_reclaim(uint32_t num_numa_node, uint32_t nr_to_reclaim);

#endif
