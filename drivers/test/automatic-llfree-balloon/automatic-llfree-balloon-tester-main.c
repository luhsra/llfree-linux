/* 
 * chardev2.c - Create an input/output character device 
 */

#include "linux/kern_levels.h"
#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <asm/errno.h>
#include "automatic-llfree-balloon-tester.h"
#include "automatic-llfree-balloon-test-lib.h"

#define SUCCESS 0
#define DEVICE_NAME "automatic-llfree-balloon-tester"
#define BUF_LEN 80

enum {
	CDEV_NOT_USED = 0,
	CDEV_EXCLUSIVE_OPEN = 1,
};

/* Is the device open right now? Used to prevent concurrent access into 
 * the same device 
 */
static atomic_t already_open = ATOMIC_INIT(CDEV_NOT_USED);

static struct class *cls;

/* This is called whenever a process attempts to open the device file */
static int device_open(struct inode *inode, struct file *file)
{
	pr_info("device_open(%p)\n", file);

	try_module_get(THIS_MODULE);
	return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file)
{
	pr_info("device_release(%p,%p)\n", inode, file);

	module_put(THIS_MODULE);
	return SUCCESS;
}

static long
device_ioctl(struct file *file, /* ditto */
	     unsigned int ioctl_num, /* number and param for ioctl */
	     unsigned long ioctl_param)
{
	long ret = SUCCESS;

	if (ioctl_param <= 0 || ioctl_param > 8) {
		return -1;
	}

	/* We don't want to talk to two processes at the same time. */
	if (atomic_cmpxchg(&already_open, CDEV_NOT_USED, CDEV_EXCLUSIVE_OPEN))
		return -EBUSY;

	/* Switch according to the ioctl called */
	switch (ioctl_num) {
	case IOCTL_ALLOC_BASE_PAGE_TEST:
		ret = alloc_test_base_page(ioctl_param);
		break;
	case IOCTL_ALLOC_HUGE_PAGE_TEST:
		ret = alloc_test_huge_page(ioctl_param);
		break;
	case IOCTL_CONSUME_BASE_PAGE_TEST:
		ret = consume_test_base_page(ioctl_param);
		break;
	case IOCTL_CONSUME_HUGE_PAGE_TEST:
		ret = consume_test_huge_page(ioctl_param);
		break;
	}

	printk(KERN_WARNING "Tested %u with parameter %lu: result %li ns\n",
	       ioctl_num, ioctl_param, ret);

	/* We're now ready for our next caller */
	atomic_set(&already_open, CDEV_NOT_USED);

	return ret;
}

static struct file_operations fops = {
	.unlocked_ioctl = device_ioctl,
	.open = device_open,
	.release = device_release,
};

static int __init automatic_llfree_balloon_tester_init(void)
{
	int ret_val = register_chrdev(MAJOR_NUM, DEVICE_NAME, &fops);

	if (ret_val < 0) {
		pr_alert("%s failed with %d\n",
			 "Sorry, registering the character device ", ret_val);
		return ret_val;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	cls = class_create(DEVICE_FILE_NAME);
#else
	cls = class_create(THIS_MODULE, DEVICE_FILE_NAME);
#endif
	device_create(cls, NULL, MKDEV(MAJOR_NUM, 0), NULL, DEVICE_FILE_NAME);

	pr_info("Device created on /dev/%s\n", DEVICE_FILE_NAME);

	return 0;
}

/* Cleanup - unregister the appropriate file from /proc */
static void __exit automatic_llfree_balloon_tester_exit(void)
{
	device_destroy(cls, MKDEV(MAJOR_NUM, 0));
	class_destroy(cls);

	/* Unregister the device */
	unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
}

module_init(automatic_llfree_balloon_tester_init);
module_exit(automatic_llfree_balloon_tester_exit);

MODULE_LICENSE("GPL");
