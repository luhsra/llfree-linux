#ifndef QEMU_LLFREE_IHPQ_H
#define QEMU_LLFREE_IHPQ_H

#include <linux/types.h>

typedef uint32_t hwaddr;

struct InflatedHugePageQueue;
typedef struct InflatedHugePageQueue InflatedHugePageQueue;

InflatedHugePageQueue *ihpq_create(void);
int ihpq_init(InflatedHugePageQueue *ihpq, uint32_t num_huge_pages);
void ihpq_free(InflatedHugePageQueue *ihpq);
void ihpq_enqueue(InflatedHugePageQueue *ihpq, hwaddr gpa_huge_frame);
hwaddr ihpq_dequeue(InflatedHugePageQueue *ihpq);
hwaddr ihpq_read_last(InflatedHugePageQueue *ihpq, uint32_t offset);
void ihpq_remove_last_enqueue(InflatedHugePageQueue *ihpq);
uint32_t ihpq_get_size(InflatedHugePageQueue *ihpq);

#endif
