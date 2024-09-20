#ifndef PAGE_ZERO_H
#define PAGE_ZERO_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>

static int  zero_and_move_page(struct zone *zone, size_t order);
static int asyncZero_fn(void *data);
static int __init asyncZero_init(void);
static void __exit asyncZero_exit(void);

#endif //PAGE_ZERO_H
