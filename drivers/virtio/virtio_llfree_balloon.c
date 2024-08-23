#include "linux/align.h"
#include "linux/cpumask.h"
#include "linux/gfp_types.h"
#include "linux/mmzone.h"
#include "linux/spinlock.h"
#include "linux/types.h"
#include "linux/virtio_config.h"
#include "llfree.h"
#include <linux/virtio.h>
#include <linux/virtio_llfree_balloon.h>
#include <linux/swap.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/balloon_compaction.h>
#include <linux/oom.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/page_reporting.h>
#include <linux/vmscan.h>
#include <linux/delay.h>
#include <linux/drop_caches.h>

/*-----------------------------------------------------------------------------------------------
| Defines
-------------------------------------------------------------------------------------------------*/
#define DROP_PAGECACHE_DELAY 20

/*-----------------------------------------------------------------------------------------------
| Shared Structs and Enums
-------------------------------------------------------------------------------------------------*/
enum ll_zone_type {
	LLFREE_ZONE_DMA32,
	LLFREE_ZONE_NORMAL,
	LLFREE_ZONE_MOVABLE,
	LLFREE_ZONE_LEN,
	LLFREE_ZONE_NONE = -1,
};

typedef enum ll_notification {
	PAGECACHE_DROPPED,
} ll_notification_t;

typedef struct ll_install_info {
	uint32_t node;
	uint32_t zone;
	uint64_t frame;
} ll_install_info_t;

/*-----------------------------------------------------------------------------------------------
| Structs
-------------------------------------------------------------------------------------------------*/

struct ll_vq_buffer {
	void *buf;
	size_t len;
};

typedef struct ll_zone_info {
	llfree_meta_t llfree_meta;
	uint32_t start_pfn;
	uint32_t pages;
	uint32_t type;
	uint32_t node;
	_Atomic(int64_t) *free_pages;
	_Atomic(int64_t) *file_pages;
} ll_zone_info_t;

typedef struct ll_balloon {
	struct virtio_device *vdev;
	struct virtqueue *info_vq;
	struct virtqueue *notify_vq;
	struct virtqueue **install_vqs;
	struct work_struct shrink_pagecache_work;
	struct ll_vq_buffer vq_buffer;
	ll_notification_t guest_req;
	ll_install_info_t *install_info;
	enum ll_zone_type map_zone_type[LLFREE_ZONE_LEN];
} ll_balloon_t;

static ll_balloon_t *balloon;

/*-----------------------------------------------------------------------------------------------
| Enums
-------------------------------------------------------------------------------------------------*/
enum ll_balloon_vq {
	LL_BALLOON_VQ_INFO,
	LL_BALLOON_VQ_NOTIFY,
	LL_BALLOON_VQ_COUNT,
};

/*-----------------------------------------------------------------------------------------------
| Helper Functions
-------------------------------------------------------------------------------------------------*/
static void ll_init_zone_types(enum ll_zone_type *types)
{
	for (uint32_t i = 0; i < __MAX_NR_ZONES; i++) {
		types[i] = LLFREE_ZONE_NONE;
	}

#ifdef CONFIG_ZONE_DMA32
	types[ZONE_DMA32] = LLFREE_ZONE_DMA32;
#endif

	types[ZONE_NORMAL] = LLFREE_ZONE_NORMAL;
	types[ZONE_MOVABLE] = LLFREE_ZONE_MOVABLE;
}

static inline int32_t zone_get_type(struct zone *zone)
{
	struct pglist_data *node = zone->zone_pgdat;
	for (uint32_t i = 0; i < MAX_NR_ZONES; i++) {
		if (&node->node_zones[i] == zone) {
			return i;
		}
	}
	return -1;
}

void llfree_copy_into_buffer(ll_zone_info_t *llfree_info, void *buffer)
{
	ll_zone_info_t *dest;
	if (!buffer) {
		pr_err("llfree_copy_into_buffer: buffer is null pointer\n");
		return;
	}

	dest = (ll_zone_info_t *)buffer;
	dest->start_pfn = llfree_info->start_pfn;
	dest->pages = llfree_info->pages;
	dest->type = llfree_info->type;
	dest->node = llfree_info->node;

	// translating gva to gpa
	dest->llfree_meta.local =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.local);
	dest->llfree_meta.trees =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.trees);
	dest->llfree_meta.lower =
		(uint8_t *)virt_to_phys(llfree_info->llfree_meta.lower);

	dest->free_pages =
		(_Atomic(int64_t) *)virt_to_phys(llfree_info->free_pages);
	dest->file_pages =
		(_Atomic(int64_t) *)virt_to_phys(llfree_info->file_pages);
}

/*-----------------------------------------------------------------------------------------------
| Virtqueue Sending Functions
-------------------------------------------------------------------------------------------------*/
static void ll_send_info(ll_balloon_t *vb)
{
	ll_zone_info_t zone_info;
	// only UMA is currently supported
	struct pglist_data *pgdat = first_online_pgdat();

	for (uint32_t i = 0; i < MAX_NR_ZONES; i++) {
		struct scatterlist sg;
		uint32_t len;
		struct zone *zone = &pgdat->node_zones[i];

		if (!populated_zone(zone)) {
			continue;
		}

		zone_info.type = vb->map_zone_type[i];
		zone_info.start_pfn = zone->zone_start_pfn;
		zone_info.pages = zone->spanned_pages;
		zone_info.node = pgdat->node_id;
		zone_info.llfree_meta = llfree_metadata(zone->llfree);
		zone_info.free_pages =
			(_Atomic(int64_t) *)&zone->vm_stat[NR_FREE_PAGES];
		zone_info.file_pages = (_Atomic(int64_t) *)&zone->zone_pgdat
					       ->vm_stat[NR_FILE_PAGES];

		llfree_copy_into_buffer(&zone_info, vb->vq_buffer.buf);

		sg_init_one(&sg, vb->vq_buffer.buf, vb->vq_buffer.len);
		virtqueue_add_outbuf(vb->info_vq, &sg, 1, vb, GFP_KERNEL);
		virtqueue_kick(vb->info_vq);

		// sync with virtio-device
		while (!virtqueue_get_buf(vb->info_vq, &len) &&
		       !virtqueue_is_broken(vb->info_vq))
			cpu_relax();
	}
}

static void ll_notify(ll_balloon_t *vb, ll_notification_t request)
{
	struct scatterlist sg;
	uint32_t len;

	if (!virtio_has_feature(vb->vdev, LL_BALLOON_F_SHRINK_PAGECACHE)) {
		return;
	}

	vb->guest_req = request;
	sg_init_one(&sg, &vb->guest_req, sizeof(ll_notification_t));
	virtqueue_add_outbuf(vb->notify_vq, &sg, 1, vb, GFP_KERNEL);
	virtqueue_kick(vb->notify_vq);

	// sync with virtio-device
	while (!virtqueue_get_buf(vb->notify_vq, &len) &&
	       !virtqueue_is_broken(vb->notify_vq))
		cpu_relax();
}

// Assumes that preemption is already disabled!
void ll_request_install(struct zone *zone, uint64_t frame, size_t core)
{
	struct scatterlist sg;
	uint32_t len;
	unsigned long flags;

	ll_install_info_t *info = &balloon->install_info[core];
	struct virtqueue *vq = balloon->install_vqs[core];

	uint32_t zone_type = zone_get_type(zone);
	if (zone_type == -1 || zone->llfree == NULL) {
		pr_warn("llfree_balloon: could not find zone_type!\n");
		return;
	}

	local_irq_save(flags);

	// send information
	*info = (ll_install_info_t){
		.node = zone->node,
		.zone = balloon->map_zone_type[zone_type],
		.frame = frame,
	};
	sg_init_one(&sg, info, sizeof(ll_install_info_t));
	BUG_ON(virtqueue_add_outbuf(vq, &sg, 1, balloon, GFP_KERNEL));
	virtqueue_kick(vq);

	// we can't sleep in this context
	// busy wait to sync with virtio-device
	while (!virtqueue_get_buf(vq, &len) && !virtqueue_is_broken(vq))
		cpu_relax();

	local_irq_restore(flags);
}
EXPORT_SYMBOL(ll_request_install);

/*-----------------------------------------------------------------------------------------------
| Pagecache Shrinking Functions
-------------------------------------------------------------------------------------------------*/
static void shrink_pagecache_func(struct work_struct *work)
{
	ll_balloon_t *vb;
	uint32_t reclaimed_nr_pages;
	uint32_t shrink_pagecache_num_pages;
	uint32_t num_numa_node;

	vb = container_of(work, ll_balloon_t, shrink_pagecache_work);

	virtio_cread_le(vb->vdev, struct ll_balloon_config,
			shrink_pagecache_num_pages,
			&shrink_pagecache_num_pages);

	virtio_cread_le(vb->vdev, struct ll_balloon_config, num_numa_node,
			&num_numa_node);

	reclaimed_nr_pages = shrink_pagecache_for_reclaim(
		num_numa_node, shrink_pagecache_num_pages);

	shrink_pagecache_num_pages = 0;
	virtio_cwrite_le(vb->vdev, struct ll_balloon_config,
			 shrink_pagecache_num_pages,
			 &shrink_pagecache_num_pages);

	// tell virtio-device to retry inflation now that we have shrunk
	// the pagecache
	ll_notify(vb, PAGECACHE_DROPPED);
}

/*-----------------------------------------------------------------------------------------------
| General Virtio Functions
-------------------------------------------------------------------------------------------------*/
static void ll_config_changed(struct virtio_device *vdev)
{
	ll_balloon_t *vb;
	uint32_t shrink_pagecache_num_pages;

	if (!virtio_has_feature(vdev, LL_BALLOON_F_SHRINK_PAGECACHE)) {
		return;
	}

	vb = (ll_balloon_t *)vdev->priv;

	// dispatch
	virtio_cread_le(vdev, struct ll_balloon_config,
			shrink_pagecache_num_pages,
			&shrink_pagecache_num_pages);

	if (shrink_pagecache_num_pages > 0) {
		queue_work(system_freezable_wq, &vb->shrink_pagecache_work);
	}
}

static int init_vqs(ll_balloon_t *vb)
{
	struct virtqueue **vqs;
	vq_callback_t **callbacks;
	const char **names;
	int err;
	uint32_t num_cpus = num_online_cpus();
	uint32_t num_vqs = LL_BALLOON_VQ_COUNT + num_cpus;

	vqs = kzalloc((num_vqs * sizeof(struct virtqueue *)), GFP_KERNEL);
	callbacks = kzalloc(num_vqs * (sizeof(vq_callback_t *)), GFP_KERNEL);
	names = kzalloc((num_vqs * sizeof(char *)), GFP_KERNEL);

	callbacks[LL_BALLOON_VQ_INFO] = NULL;
	names[LL_BALLOON_VQ_INFO] = "llfree info";
	callbacks[LL_BALLOON_VQ_NOTIFY] = NULL;
	names[LL_BALLOON_VQ_NOTIFY] = "llfree acknowledge pagecache";

	for (uint32_t i = 0; i < num_cpus; i++) {
		callbacks[LL_BALLOON_VQ_COUNT + i] = NULL;
		names[LL_BALLOON_VQ_COUNT + i] = "llfree auto-deflate vq";
	}

	err = virtio_find_vqs(vb->vdev, num_vqs, vqs, callbacks, names, NULL);
	if (err)
		return err;

	vb->info_vq = vqs[LL_BALLOON_VQ_INFO];
	vb->notify_vq = vqs[LL_BALLOON_VQ_NOTIFY];

	for (uint32_t i = 0; i < num_cpus; i++) {
		vb->install_vqs[i] = vqs[LL_BALLOON_VQ_COUNT + i];
	}

	return 0;
}

static int ll_probe(struct virtio_device *vdev)
{
	ll_balloon_t *ll_b;
	int err;
	uint32_t __maybe_unused num_cores;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	ll_b = kzalloc(sizeof(*ll_b), GFP_KERNEL);
	if (ll_b == NULL) {
		err = -ENOMEM;
		return err;
	}
	vdev->priv = ll_b;

	if (virtio_has_feature(vdev, LL_BALLOON_F_SHRINK_PAGECACHE)) {
		INIT_WORK(&ll_b->shrink_pagecache_work, shrink_pagecache_func);
	}

	ll_init_zone_types((enum ll_zone_type *)&ll_b->map_zone_type);

	ll_b->vq_buffer.len = sizeof(ll_zone_info_t);
	ll_b->vq_buffer.buf = kzalloc(sizeof(ll_zone_info_t), GFP_KERNEL);
	if (ll_b->vq_buffer.buf == NULL) {
		err = -ENOMEM;
		return err;
	}

	ll_b->vdev = vdev;

	num_cores = num_online_cpus();
	ll_b->install_info =
		kzalloc(sizeof(ll_install_info_t) * num_cores, GFP_KERNEL);
	ll_b->install_vqs =
		kzalloc(sizeof(struct virtqueue *) * num_cores, GFP_KERNEL);
	if (ll_b->install_info == NULL || ll_b->install_vqs == NULL) {
		err = -ENOMEM;
		goto cleanup;
	}

	err = init_vqs(ll_b);
	if (err)
		goto cleanup;

	// enable global auto-deflate function call...
	balloon = ll_b;

	virtio_device_ready(vdev);

	// directly send our zone info to our virtio-device
	ll_send_info(ll_b);

	return 0;

cleanup:
	if (ll_b->vq_buffer.buf != NULL)
		kfree(ll_b->vq_buffer.buf);
	if (ll_b->install_info != NULL)
		kfree(ll_b->install_info);
	if (ll_b->install_vqs != NULL)
		kfree(ll_b->install_vqs);
	kfree(ll_b);
	return err;
}

static void remove_common(ll_balloon_t *vb)
{
	/* Now we reset the device so we can clean up the queues. */
	virtio_reset_device(vb->vdev);
	vb->vdev->config->del_vqs(vb->vdev);
}

static void ll_remove(struct virtio_device *vdev)
{
	ll_balloon_t *vb = vdev->priv;

	kfree(vb->install_vqs);
	kfree(vb->install_info);
	kfree(vb->vq_buffer.buf);
	remove_common(vb);
	kfree(vb);
}

static unsigned int features[] = {
	LL_BALLOON_F_SHRINK_PAGECACHE,
	LL_BALLOON_F_AUTO_MODE,
};

// TODO: try again with own id
// IDs can't be arbitrarily chosen as it seems, for now
// say that we are virtio-balloon
static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_BALLOON, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_llfree_balloon_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = ll_probe,
	.remove = ll_remove,
	.config_changed = ll_config_changed
};

module_virtio_driver(virtio_llfree_balloon_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio llfree balloon driver");
MODULE_LICENSE("GPL");
