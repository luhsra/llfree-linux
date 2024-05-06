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

typedef enum ll_request {
	SCHEDULE_LLFREE_BALLOON_UPDATE,
} ll_request_t;

typedef struct ll_deflate_info {
	uint32_t node;
	uint32_t zone;
	uint32_t core;
	uint64_t frame;
} deflate_info_t;

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

struct ll_balloon {
	struct virtio_device *vdev;
	struct virtqueue *info_vq;
	struct virtqueue *request_vq;
	struct virtqueue **deflate_vqs;
	struct work_struct shrink_pagecache_work;
	struct task_struct *drop_pagecache_thread;
	struct ll_vq_buffer vq_buffer;
	ll_request_t guest_req;
	deflate_info_t *deflate_info;
	enum ll_zone_type map_zone_type[LLFREE_ZONE_LEN];
};

static struct ll_balloon *vb_llfree;

/*-----------------------------------------------------------------------------------------------
| Enums
-------------------------------------------------------------------------------------------------*/
enum ll_balloon_vq {
	LL_BALLOON_VQ_INFO,
	LL_BALLOON_VQ_REQUEST,
	LL_BALLOON_VQ_COUNT,
};

/*-----------------------------------------------------------------------------------------------
| Helper Functions
-------------------------------------------------------------------------------------------------*/
static void noinline ll_init_zone_types(enum ll_zone_type *types)
{
	for (uint32_t i = 0; i < LLFREE_ZONE_LEN; i++) {
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

void noinline llfree_copy_into_buffer(ll_zone_info_t *llfree_info, void *buffer)
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
static void noinline ll_send_info(struct ll_balloon *vb)
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

static void noinline ll_send_request(struct ll_balloon *vb,
				     ll_request_t request)
{
	struct scatterlist sg;
	uint32_t len;

	if (!virtio_has_feature(
		    vb->vdev,
		    VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE)) {
		return;
	}

	vb->guest_req = request;
	sg_init_one(&sg, &vb->guest_req, sizeof(ll_request_t));
	virtqueue_add_outbuf(vb->request_vq, &sg, 1, vb, GFP_KERNEL);
	virtqueue_kick(vb->request_vq);

	// sync with virtio-device
	while (!virtqueue_get_buf(vb->request_vq, &len) &&
	       !virtqueue_is_broken(vb->request_vq))
		cpu_relax();
}

void noinline ll_request_mapping(struct zone *zone, uint64_t frame)
{
	struct scatterlist sg;
	uint32_t core;
	uint32_t num_cores;
	uint32_t len;
	unsigned long flags;

	uint32_t zone_type = zone_get_type(zone);
	if (zone_type == -1 || zone->llfree == NULL) {
		pr_warn("llfree_balloon: could not find zone_type!\n");
		return;
	}

	spin_lock_irqsave(&zone->auto_deflate_lock, flags);
	// skip if already mapped (might happen on parallel alloc)
	if (!llfree_is_unmapped(zone->llfree, frame))
		goto out;

	// are core ids always continuous?
	num_cores = num_online_cpus();
	core = raw_smp_processor_id();

	if (core > num_cores) {
		pr_warn("llfree_balloon: core > num_cores!\n");
		goto out;
	}

	// pr_warn("deflate frame=%x addr=0x%x\n", frame,
	// 	page_to_phys(pfn_to_page(
	// 		ALIGN_DOWN(zone->zone_start_pfn, 1 << MAX_ORDER) +
	// 		frame)));

	vb_llfree->deflate_info[core].node = zone->node;
	vb_llfree->deflate_info[core].zone =
		vb_llfree->map_zone_type[zone_type];
	vb_llfree->deflate_info[core].core = core;
	vb_llfree->deflate_info[core].frame = frame;

	// send information
	sg_init_one(&sg, &vb_llfree->deflate_info[core],
		    sizeof(deflate_info_t));
	virtqueue_add_outbuf(vb_llfree->deflate_vqs[core], &sg, 1, vb_llfree,
			     GFP_KERNEL);
	virtqueue_kick(vb_llfree->deflate_vqs[core]);

	// we can't sleep in this context
	// busy wait to sync with virtio-device
	while (!virtqueue_get_buf(vb_llfree->deflate_vqs[core], &len) &&
	       !virtqueue_is_broken(vb_llfree->deflate_vqs[core]))
		cpu_relax();

out:
	spin_unlock_irqrestore(&zone->auto_deflate_lock, flags);
}
EXPORT_SYMBOL(ll_request_mapping);

/*-----------------------------------------------------------------------------------------------
| Pagecache Shrinking Functions
-------------------------------------------------------------------------------------------------*/
static void shrink_pagecache_func(struct work_struct *work)
{
	struct ll_balloon *vb;
	uint32_t reclaimed_nr_pages;
	uint32_t shrink_pagecache_num_pages;
	uint32_t num_numa_node;

	vb = container_of(work, struct ll_balloon, shrink_pagecache_work);

	virtio_cread_le(vb->vdev, struct virtio_llfree_balloon_config,
			shrink_pagecache_num_pages,
			&shrink_pagecache_num_pages);

	virtio_cread_le(vb->vdev, struct virtio_llfree_balloon_config,
			num_numa_node, &num_numa_node);

	reclaimed_nr_pages = shrink_pagecache_for_reclaim(
		num_numa_node, shrink_pagecache_num_pages);

	shrink_pagecache_num_pages = 0;
	virtio_cwrite_le(vb->vdev, struct virtio_llfree_balloon_config,
			 shrink_pagecache_num_pages,
			 &shrink_pagecache_num_pages);

	// tell virtio-device to retry inflation now that we have shrunk
	// the pagecache
	ll_send_request(vb, SCHEDULE_LLFREE_BALLOON_UPDATE);
}

static int drop_pagecache_thread(void *arg)
{
	while (true) {
		drop_pagecache();
		ssleep(DROP_PAGECACHE_DELAY);
	}

	return 0;
}

/*-----------------------------------------------------------------------------------------------
| General Virtio Functions
-------------------------------------------------------------------------------------------------*/
static void ll_config_changed(struct virtio_device *vdev)
{
	struct ll_balloon *vb;
	uint32_t shrink_pagecache_num_pages;

	if (!virtio_has_feature(
		    vdev, VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE)) {
		return;
	}

	vb = (struct ll_balloon *)vdev->priv;

	// dispatch
	virtio_cread_le(vdev, struct virtio_llfree_balloon_config,
			shrink_pagecache_num_pages,
			&shrink_pagecache_num_pages);

	if (shrink_pagecache_num_pages > 0) {
		queue_work(system_freezable_wq, &vb->shrink_pagecache_work);
	}
}

static int init_vqs(struct ll_balloon *vb)
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
	callbacks[LL_BALLOON_VQ_REQUEST] = NULL;
	names[LL_BALLOON_VQ_REQUEST] = "llfree requests";

	for (uint32_t i = 0; i < num_cpus; i++) {
		callbacks[LL_BALLOON_VQ_COUNT + i] = NULL;
		names[LL_BALLOON_VQ_COUNT + i] = "llfree auto-deflate vq";
	}

	err = virtio_find_vqs(vb->vdev, num_vqs, vqs, callbacks, names, NULL);
	if (err)
		return err;

	vb->info_vq = vqs[LL_BALLOON_VQ_INFO];
	vb->request_vq = vqs[LL_BALLOON_VQ_REQUEST];

	for (uint32_t i = 0; i < num_cpus; i++) {
		vb->deflate_vqs[i] = vqs[LL_BALLOON_VQ_COUNT + i];
	}

	return 0;
}

static int ll_probe(struct virtio_device *vdev)
{
	struct ll_balloon *vb;
	int err;
	uint32_t __maybe_unused num_cores;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	vb = kzalloc(sizeof(*vb), GFP_KERNEL);
	if (vb == NULL) {
		err = -ENOMEM;
		return err;
	}
	vdev->priv = vb;

	if (virtio_has_feature(
		    vdev, VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE)) {
		INIT_WORK(&vb->shrink_pagecache_work, shrink_pagecache_func);
	}

	ll_init_zone_types((enum ll_zone_type *)&vb->map_zone_type);

	vb->vq_buffer.len = sizeof(ll_zone_info_t);
	vb->vq_buffer.buf = kzalloc(sizeof(ll_zone_info_t), GFP_KERNEL);
	if (vb->vq_buffer.buf == NULL) {
		err = -ENOMEM;
		return err;
	}

	vb->vdev = vdev;

	num_cores = num_online_cpus();
	vb->deflate_info =
		kzalloc(sizeof(deflate_info_t) * num_cores, GFP_KERNEL);
	vb->deflate_vqs =
		kzalloc(sizeof(struct virtqueue *) * num_cores, GFP_KERNEL);
	if (vb->deflate_info == NULL || vb->deflate_vqs == NULL) {
		err = -ENOMEM;
		goto cleanup;
	}

	err = init_vqs(vb);
	if (err)
		goto cleanup;

	// enable global auto-deflate function call...
	vb_llfree = vb;

	virtio_device_ready(vdev);

	// directly send our zone info to our virtio-device
	ll_send_info(vb);

	// optionally, start our pagecache drop thread
	if (virtio_has_feature(
		    vdev, VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE) &&
	    virtio_has_feature(vdev, VIRTIO_LLFREE_BALLOON_F_AUTO_MODE)) {
		vb->drop_pagecache_thread = kthread_run(
			drop_pagecache_thread, NULL, "drop-pagecache-thread");
	}

	return 0;

cleanup:
	if (vb->vq_buffer.buf != NULL)
		kfree(vb->vq_buffer.buf);
	if (vb->deflate_info != NULL)
		kfree(vb->deflate_info);
	if (vb->deflate_vqs != NULL)
		kfree(vb->deflate_vqs);
	kfree(vb);
	return err;
}

static void remove_common(struct ll_balloon *vb)
{
	/* Now we reset the device so we can clean up the queues. */
	virtio_reset_device(vb->vdev);
	vb->vdev->config->del_vqs(vb->vdev);
}

static void ll_remove(struct virtio_device *vdev)
{
	struct ll_balloon *vb = vdev->priv;
	kthread_stop(vb->drop_pagecache_thread);

	kfree(vb->deflate_vqs);
	kfree(vb->deflate_info);
	kfree(vb->vq_buffer.buf);
	remove_common(vb);
	kfree(vb);
}

static unsigned int features[] = {
	VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE,
	VIRTIO_LLFREE_BALLOON_F_AUTO_MODE,
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
