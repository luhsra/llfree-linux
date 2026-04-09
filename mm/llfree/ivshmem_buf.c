#include "ivshmem_buf.h"

// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io.h>

#define DRV_NAME "ivshmem_hello"

/*
 * QEMU ivshmem PCI IDs
 * Vendor: 0x1af4 (Red Hat / virtio transitional)
 * Device: 0x1110 (ivshmem)
 */
#define IVSHMEM_VENDOR_ID 0x1af4
#define IVSHMEM_DEVICE_ID 0x1110
#define IVSHMEM_BAR 2

static struct llfree_ivshmem_buf *g_ivdev;

struct llfree_ivshmem_buf *llfree_ivshmem_get(void)
{
	return g_ivdev;
}
EXPORT_SYMBOL(llfree_ivshmem_get);

static int ivshmem_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	struct llfree_ivshmem_buf *ivdev;

	pr_info(DRV_NAME ": probe called\n");

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret)
		goto err_disable;

	pci_set_master(pdev);

	ivdev = kzalloc(sizeof(*ivdev), GFP_KERNEL);
	if (!ivdev) {
		ret = -ENOMEM;
		goto err_release;
	}

	ivdev->pdev = pdev;

	ivdev->len = pci_resource_len(pdev, IVSHMEM_BAR);
	ivdev->buffer = pci_iomap(pdev, IVSHMEM_BAR, 0);
	if (!ivdev->buffer) {
		pr_err(DRV_NAME ": failed to iomap BAR%d\n", IVSHMEM_BAR);
		ret = -EIO;
		goto err_free;
	}

	pci_set_drvdata(pdev, ivdev);

	pr_info(DRV_NAME ": BAR%d mapped: phys=%pa size=%pa\n", IVSHMEM_BAR,
		&pdev->resource[IVSHMEM_BAR].start, &ivdev->len);

	g_ivdev = ivdev;

	return 0;

err_free:
	kfree(ivdev);
err_release:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void ivshmem_remove(struct pci_dev *pdev)
{
	struct llfree_ivshmem_buf *ivdev = pci_get_drvdata(pdev);

	pr_info(DRV_NAME ": remove called\n");

	if (!ivdev)
		return;

	g_ivdev = NULL;

	if (ivdev->buffer)
		pci_iounmap(pdev, ivdev->buffer);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	kfree(ivdev);
}

static const struct pci_device_id ivshmem_ids[] = {
	{ PCI_DEVICE(IVSHMEM_VENDOR_ID, IVSHMEM_DEVICE_ID) },
	{
		0,
	}
};
MODULE_DEVICE_TABLE(pci, ivshmem_ids);

static struct pci_driver ivshmem_driver = {
	.name = DRV_NAME,
	.id_table = ivshmem_ids,
	.probe = ivshmem_probe,
	.remove = ivshmem_remove,
};

module_pci_driver(ivshmem_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lars Wrenger");
MODULE_DESCRIPTION("Minimal ivshmem driver that provides access to the buffer");
