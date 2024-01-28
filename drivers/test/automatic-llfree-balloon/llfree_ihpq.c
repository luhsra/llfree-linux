#include "llfree_ihpq.h"
#include "linux/gfp_types.h"
#include <linux/printk.h>
#include <linux/slab.h>

// private struct definition
struct InflatedHugePageQueue {
    hwaddr *inflated_huge_pages;
    uint32_t first;
    uint32_t last;
    uint32_t size;
    uint32_t capacity;
};

typedef struct InflatedHugePageQueue InflatedHugePageQueue;


// queue implementation for storing inflated huge pages
InflatedHugePageQueue *ihpq_create(void) {
    InflatedHugePageQueue *ihpq;
    ihpq = (InflatedHugePageQueue *) kmalloc(sizeof(InflatedHugePageQueue), GFP_KERNEL);

    if (ihpq == NULL) {
      return NULL;
    }

    return ihpq;
}

int ihpq_init(InflatedHugePageQueue *ihpq, uint32_t capacity){
    if(ihpq == NULL) {
      return -1;
    }
  
      ihpq->inflated_huge_pages = (hwaddr *) kmalloc(capacity * sizeof(hwaddr), GFP_KERNEL);
      ihpq->size = 0;
      ihpq->capacity = capacity;
      ihpq->first = 0;
      ihpq->last = ihpq->capacity - 1;

      if(ihpq->inflated_huge_pages == NULL) {
        printk(KERN_WARNING "ihpq_init: could not allocate memory for ihpq array \n");
        return -1;
      }

      return 0;
  }

  void ihpq_enqueue(InflatedHugePageQueue *ihpq, hwaddr gpa_huge_frame) {
      if(ihpq->size == ihpq->capacity) {
          printk(KERN_WARNING "llfree_balloon_enqueue: queue is full, this should not be possible \n");
          return;
      }

      ihpq->last = (ihpq->last + 1) % ihpq->capacity;
      ihpq->inflated_huge_pages[ihpq->last] = gpa_huge_frame;
      ihpq->size += 1;
      return;
  }

  hwaddr ihpq_dequeue(InflatedHugePageQueue *ihpq) {
      hwaddr first_addr;

      if(ihpq->size == 0) {
          printk(KERN_WARNING "llfree_balloon_dequeue: trying to dequeue from empty queue, this should never happen \n");
          return 0;      
      }  

      first_addr = ihpq->inflated_huge_pages[ihpq->first];
      ihpq->first = (ihpq->first + 1) % ihpq->capacity;
      ihpq->size -= 1;

      return first_addr;
  }

  // read with an offset from the last queue item
  hwaddr ihpq_read_last(InflatedHugePageQueue *ihpq, uint32_t offset) {
    int32_t index;
    
    if(offset > ihpq->size) {
          printk(KERN_WARNING "virtio-llfree-balloon: trying to read past queue size limit\n");
          return 0;
    }

    index = ihpq->last - offset;

    // watch out for wrap around
    if(index < 0) {
      index = ihpq->capacity + index;
    }
    
    return ihpq->inflated_huge_pages[index];
  }

  // read with an offset from the last queue item
  void ihpq_remove_last_enqueue(InflatedHugePageQueue *ihpq) {
    int32_t index;
    
    if(ihpq->size == 0) {
          printk(KERN_WARNING "virtio-llfree-balloon: trying to remove element from empty queue");
  }

  index = ihpq->last - 1;

  if(index < 0) {
    index = ihpq->capacity + index;
  }

  ihpq->last = index;
  ihpq->size -= 1;
}

void ihpq_free(InflatedHugePageQueue *ihpq) {
  kfree(ihpq->inflated_huge_pages);
  kfree(ihpq);
}

uint32_t ihpq_get_size(InflatedHugePageQueue *ihpq) {
  return ihpq->size;
}
