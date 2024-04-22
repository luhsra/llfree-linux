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
#include "llfree_qemu.h"

/*-----------------------------------------------------------------------------------------------
| Defines
-------------------------------------------------------------------------------------------------*/
#define DROP_PAGECACHE_DELAY 20

/*-----------------------------------------------------------------------------------------------
| Macros
-------------------------------------------------------------------------------------------------*/
#define FEATURE_IS_ENABLED(vdev, bit) virtio_has_feature(vdev, bit)
#define FEATURE_IS_DISABLED(vdev, bit) !virtio_has_feature(vdev, bit)

/*-----------------------------------------------------------------------------------------------
| Shared Structs and Enums
-------------------------------------------------------------------------------------------------*/
enum llfree_zone_type {
	LLFREE_NONE_EXISTING,
	LLFREE_ZONE_DMA,
	LLFREE_ZONE_DMA32,
	LLFREE_ZONE_NORMAL,
	LLFREE_ZONE_HIGHMEM,
	LLFREE_ZONE_MOVABLE,
	LLFREE_ZONE_DEVICE,
	LLFREE_MAX_NR_ZONES
};

enum RequestType {
	SCHEDULE_LLFREE_BALLOON_UPDATE,
};
typedef enum RequestType RequestType;

struct LLFreeBalloonRequest {
	RequestType req;
};
typedef struct LLFreeBalloonRequest LLFreeBalloonRequest;

struct AutoDeflateInfo {
	uint32_t numa_node_id;
	uint32_t zone_type;
	uint32_t core;
	uint64_t frame;
};
typedef struct AutoDeflateInfo AutoDeflateInfo;
/*-----------------------------------------------------------------------------------------------
| Structs
-------------------------------------------------------------------------------------------------*/
struct llfree_vq_buffer {
	void *buf;
	size_t len;
};

struct virtio_llfree_balloon {
	struct virtio_device *vdev;
	struct virtqueue *llfree_info_vq;
	struct virtqueue *llfree_request_vq;
	struct virtqueue **llfree_auto_deflate_vqs;
	struct work_struct shrink_pagecache_work;
	struct task_struct *drop_pagecache_thread;
	struct llfree_vq_buffer vq_buffer;
	LLFreeBalloonRequest guest_req;
	AutoDeflateInfo *auto_deflate_info;
	llfree_zone_info_t qemu_info;
	enum llfree_zone_type map_zone_type[LLFREE_MAX_NR_ZONES];
};

static struct virtio_llfree_balloon *vb_llfree;
/*-----------------------------------------------------------------------------------------------
| Enums
-------------------------------------------------------------------------------------------------*/
enum virtio_llfree_balloon_vq {
	VIRTIO_LLFREE_BALLOON_LLFREE_INFO_VQ,
	VIRTIO_LLFREE_BALLOON_LLFREE_REQUEST_VQ,
	VIRTIO_LLFREE_BALLOON_VQ_MAX,
};

/*-----------------------------------------------------------------------------------------------
| Helper Functions
-------------------------------------------------------------------------------------------------*/
static void noinline
virtio_llfree_init_map_zone_types(enum llfree_zone_type *map_zone_type)
{
	for (uint32_t i = 0; i < LLFREE_MAX_NR_ZONES; i++) {
		map_zone_type[i] = LLFREE_NONE_EXISTING;
	}

#ifdef CONFIG_ZONE_DMA
	map_zone_type[ZONE_DMA] = LLFREE_ZONE_DMA;
#endif

#ifdef CONFIG_ZONE_DMA32
	map_zone_type[ZONE_DMA32] = LLFREE_ZONE_DMA32;
#endif

	map_zone_type[ZONE_NORMAL] = LLFREE_ZONE_NORMAL;

#ifdef CONFIG_HIGHMEM
	map_zone_type[ZONE_HIGHMEM] = LLFREE_ZONE_HIGHMEM;
#endif

	map_zone_type[ZONE_MOVABLE] = LLFREE_ZONE_MOVABLE;

#ifdef CONFIG_ZONE_DEVICE
	map_zone_type[ZONE_DEVICE] = LLFREE_ZONE_DEVICE;
#endif
}

/*-----------------------------------------------------------------------------------------------
| Virtqueue Sending Functions
-------------------------------------------------------------------------------------------------*/
static void noinline
virtio_llfree_send_llfree_info(struct virtio_llfree_balloon *vb)
{
	struct scatterlist sg;
	struct zone *zone;
	uint32_t len;

	// only UMA is currently supported
	struct pglist_data *pgdat = first_online_pgdat();

	for (uint32_t i = 0; i < MAX_NR_ZONES; i++) {
		zone = &pgdat->node_zones[i];

		if (!populated_zone(zone)) {
			continue;
		}

		vb->qemu_info.type = vb->map_zone_type[i];
		vb->qemu_info.start_pfn = zone->zone_start_pfn;
		vb->qemu_info.pages = zone->spanned_pages;
		vb->qemu_info.numa_node_id = pgdat->node_id;
		vb->qemu_info.llfree_meta = llfree_metadata(zone->llfree);
		vb->qemu_info.zone_free_pages =
			(_Atomic(int64_t) *)&zone->vm_stat[NR_FREE_PAGES];
		vb->qemu_info.zone_llfree_huge_page_counter =
			(_Atomic(int64_t) *)&zone
				->vm_stat[NR_INFLATED_HUGE_PAGES];
		vb->qemu_info.num_pagecache_reclaimable_pages =
			(_Atomic(int64_t) *)&zone->zone_pgdat
				->vm_stat[NR_FILE_PAGES];

		llfree_copy_into_buffer(&vb->qemu_info, vb->vq_buffer.buf);

		sg_init_one(&sg, vb->vq_buffer.buf, vb->vq_buffer.len);
		virtqueue_add_outbuf(vb->llfree_info_vq, &sg, 1, vb,
				     GFP_KERNEL);
		virtqueue_kick(vb->llfree_info_vq);

		// sync with virtio-device
		while (!virtqueue_get_buf(vb->llfree_info_vq, &len) &&
		       !virtqueue_is_broken(vb->llfree_info_vq))
			cpu_relax();
	}
}

#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_DEMAND_SHRINK_PAGECACHE
static void noinline virtio_llfree_send_request(
	struct virtio_llfree_balloon *vb, RequestType llfree_request)
{
	struct scatterlist sg;
	uint32_t len;

	if (FEATURE_IS_DISABLED(
		    vb->vdev,
		    VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE)) {
		return;
	}

	vb->guest_req.req = llfree_request;
	sg_init_one(&sg, &vb->guest_req, sizeof(LLFreeBalloonRequest));
	virtqueue_add_outbuf(vb->llfree_request_vq, &sg, 1, vb, GFP_KERNEL);
	virtqueue_kick(vb->llfree_request_vq);

	// sync with virtio-device
	while (!virtqueue_get_buf(vb->llfree_request_vq, &len) &&
	       !virtqueue_is_broken(vb->llfree_request_vq))
		cpu_relax();
}
#endif

#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_AUTO_DEFLATE
void noinline virtio_llfree_auto_deflate(struct zone *zone, uint64_t frame)
{
	struct scatterlist sg;
	uint32_t core, num_cores;
	uint32_t len;
	uint32_t numa_node_id, zone_type;
	unsigned long flags;

	zone_type = zone_get_type(zone);

	if (zone_type == -1 || zone->llfree == NULL) {
		printk("llfree_balloon: could not find zone_type!\n");
		return;
	}

	spin_lock_irqsave(&zone->auto_deflate_lock, flags);
	// skip if already inflated (might happen on parallel alloc)
	if (!llfree_is_inflated(zone->llfree, frame))
		goto out;

	// are core ids always continuous?
	num_cores = num_online_cpus();
	numa_node_id = zone->node;
	core = raw_smp_processor_id();

	if (core > num_cores) {
		printk("llfree_balloon: core > num_cores!\n");
		goto out;
	}

	// printk("auto-deflate: core %u zone %u threadid %u\n", core, zone_type,
	//        current->pid);
	vb_llfree->auto_deflate_info[core].numa_node_id = numa_node_id;
	vb_llfree->auto_deflate_info[core].zone_type =
		vb_llfree->map_zone_type[zone_type];
	vb_llfree->auto_deflate_info[core].core = core;
	vb_llfree->auto_deflate_info[core].frame = frame;

	// send information
	sg_init_one(&sg, &vb_llfree->auto_deflate_info[core],
		    sizeof(AutoDeflateInfo));
	virtqueue_add_outbuf(vb_llfree->llfree_auto_deflate_vqs[core], &sg, 1,
			     vb_llfree, GFP_KERNEL);
	virtqueue_kick(vb_llfree->llfree_auto_deflate_vqs[core]);

	// we can't sleep in this context
	// busy wait to sync with virtio-device
	while (!virtqueue_get_buf(vb_llfree->llfree_auto_deflate_vqs[core],
				  &len) &&
	       !virtqueue_is_broken(vb_llfree->llfree_auto_deflate_vqs[core]))
		cpu_relax();

out:
	spin_unlock_irqrestore(&zone->auto_deflate_lock, flags);
}
EXPORT_SYMBOL(virtio_llfree_auto_deflate);
#endif

/*-----------------------------------------------------------------------------------------------
| Pagecache Shrinking Functions
-------------------------------------------------------------------------------------------------*/
#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_DEMAND_SHRINK_PAGECACHE
static void shrink_pagecache_func(struct work_struct *work)
{
	struct virtio_llfree_balloon *vb;
	uint32_t reclaimed_nr_pages, shrink_pagecache_num_pages, num_numa_node;

	vb = container_of(work, struct virtio_llfree_balloon,
			  shrink_pagecache_work);

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
	virtio_llfree_send_request(vb, SCHEDULE_LLFREE_BALLOON_UPDATE);
}

static int drop_pagecache_thread(void *arg)
{
	while (true) {
		drop_pagecache();
		ssleep(DROP_PAGECACHE_DELAY);
	}

	return 0;
}
#endif

/*-----------------------------------------------------------------------------------------------
| General Virtio Functions
-------------------------------------------------------------------------------------------------*/
static void virtio_llfree_config_changed(struct virtio_device *vdev)
{
#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_DEMAND_SHRINK_PAGECACHE
	struct virtio_llfree_balloon *vb;
	uint32_t shrink_pagecache_num_pages;

	if (FEATURE_IS_DISABLED(
		    vdev, VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE)) {
		return;
	}

	vb = (struct virtio_llfree_balloon *)vdev->priv;

	// dispatch
	virtio_cread_le(vdev, struct virtio_llfree_balloon_config,
			shrink_pagecache_num_pages,
			&shrink_pagecache_num_pages);

	if (shrink_pagecache_num_pages > 0) {
		queue_work(system_freezable_wq, &vb->shrink_pagecache_work);
	}
#endif
}

static int init_vqs(struct virtio_llfree_balloon *vb)
{
	struct virtqueue **vqs;
	vq_callback_t **callbacks;
	const char **names;
	int err;
	uint32_t num_vqs;
#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_AUTO_DEFLATE
	uint32_t num_cpus;
#endif

	// we assume no cpu hotplug...
	num_vqs = VIRTIO_LLFREE_BALLOON_VQ_MAX;

#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_AUTO_DEFLATE
	num_cpus = num_online_cpus();
	num_vqs = VIRTIO_LLFREE_BALLOON_VQ_MAX + num_cpus;
#endif

	vqs = kzalloc((num_vqs * sizeof(struct virtqueue *)), GFP_KERNEL);
	callbacks = kzalloc(num_vqs * (sizeof(vq_callback_t *)), GFP_KERNEL);
	names = kzalloc((num_vqs * sizeof(char *)), GFP_KERNEL);

	callbacks[VIRTIO_LLFREE_BALLOON_LLFREE_INFO_VQ] = NULL;
	names[VIRTIO_LLFREE_BALLOON_LLFREE_INFO_VQ] = "llfree info";
	callbacks[VIRTIO_LLFREE_BALLOON_LLFREE_REQUEST_VQ] = NULL;
	names[VIRTIO_LLFREE_BALLOON_LLFREE_REQUEST_VQ] = "llfree requests";

#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_AUTO_DEFLATE
	for (uint32_t i = 0; i < num_cpus; i++) {
		callbacks[VIRTIO_LLFREE_BALLOON_VQ_MAX + i] = NULL;
		names[VIRTIO_LLFREE_BALLOON_VQ_MAX + i] =
			"llfree auto-deflate vq";
	}
#endif

	err = virtio_find_vqs(vb->vdev, num_vqs, vqs, callbacks, names, NULL);
	if (err)
		return err;

	vb->llfree_info_vq = vqs[VIRTIO_LLFREE_BALLOON_LLFREE_INFO_VQ];
	vb->llfree_request_vq = vqs[VIRTIO_LLFREE_BALLOON_LLFREE_REQUEST_VQ];

#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_AUTO_DEFLATE
	for (uint32_t i = 0; i < num_cpus; i++) {
		vb->llfree_auto_deflate_vqs[i] =
			vqs[VIRTIO_LLFREE_BALLOON_VQ_MAX + i];
	}
#endif

	return 0;
}

static int virtio_llfree_balloon_probe(struct virtio_device *vdev)
{
	struct virtio_llfree_balloon *vb;
	int err;
	uint32_t __maybe_unused num_cores;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}
	vdev->priv = vb = kzalloc(sizeof(*vb), GFP_KERNEL);
	if (!vb) {
		err = -ENOMEM;
		goto out;
	}

#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_DEMAND_SHRINK_PAGECACHE
	if (FEATURE_IS_ENABLED(
		    vdev, VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE)) {
		INIT_WORK(&vb->shrink_pagecache_work, shrink_pagecache_func);
	}
#endif

	virtio_llfree_init_map_zone_types(
		(enum llfree_zone_type *)&vb->map_zone_type);

	llfree_create_buffer(&vb->vq_buffer.buf, &vb->vq_buffer.len);
	if (!vb->vq_buffer.buf) {
		err = -ENOMEM;
		goto out;
	}

	vb->vdev = vdev;

#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_AUTO_DEFLATE
	num_cores = num_online_cpus();
	vb->auto_deflate_info =
		kzalloc(sizeof(AutoDeflateInfo) * num_cores, GFP_KERNEL);
	vb->llfree_auto_deflate_vqs =
		kzalloc(sizeof(struct virtqueue *) * num_cores, GFP_KERNEL);
	if (!vb->llfree_auto_deflate_vqs) {
		err = -ENOMEM;
		goto out;
	}
#endif

	err = init_vqs(vb);
	if (err)
		goto out_free_vb;

	// enable global auto-deflate function call...
	vb_llfree = vb;

	virtio_device_ready(vdev);

	// directly send our zone info to our virtio-device
	virtio_llfree_send_llfree_info(vb);

	// optionally, start our pagecache drop thread
	if (FEATURE_IS_ENABLED(
		    vdev, VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE) &&
	    FEATURE_IS_ENABLED(vdev, VIRTIO_LLFREE_BALLOON_F_AUTO_MODE)) {
		vb->drop_pagecache_thread = kthread_run(
			drop_pagecache_thread, NULL, "drop-pagecache-thread");
	}

	return 0;

out_free_vb:
	kfree(vb);
out:
	return err;
}

static void remove_common(struct virtio_llfree_balloon *vb)
{
	/* Now we reset the device so we can clean up the queues. */
	virtio_reset_device(vb->vdev);
	vb->vdev->config->del_vqs(vb->vdev);
}

static void virtio_llfree_remove(struct virtio_device *vdev)
{
	struct virtio_llfree_balloon *vb = vdev->priv;
	kthread_stop(vb->drop_pagecache_thread);

#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_AUTO_DEFLATE
	kfree(vb->llfree_auto_deflate_vqs);
#endif

	kfree(vb->auto_deflate_info);
	kfree(vb->vq_buffer.buf);
	remove_common(vb);
	kfree(vb);
}

static unsigned int features[] = {
#ifdef CONFIG_VIRTIO_LLFREE_BALLOON_DEMAND_SHRINK_PAGECACHE
	VIRTIO_LLFREE_BALLOON_F_DEMAND_SHRINK_PAGECACHE,
	VIRTIO_LLFREE_BALLOON_F_AUTO_MODE,
#endif
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
	.probe = virtio_llfree_balloon_probe,
	.remove = virtio_llfree_remove,
	.config_changed = virtio_llfree_config_changed
};

module_virtio_driver(virtio_llfree_balloon_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio llfree balloon driver");
MODULE_LICENSE("GPL");
