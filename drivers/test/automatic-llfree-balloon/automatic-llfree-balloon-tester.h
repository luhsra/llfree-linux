/* 
 * chardev.h - the header file with the ioctl definitions. 
 * 
 * The declarations here have to be in a header file, because they need 
 * to be known both to the kernel module (in chardev2.c) and the process 
 * calling ioctl() (in userspace_ioctl.c). 
 */

#ifndef AUTOMATIC_LLFREE_BALLOON_TESTER_H
#define AUTOMATIC_LLFREE_BALLOON_TESTER_H

#include <linux/ioctl.h>

/* The major device number. We can not rely on dynamic registration 
 * any more, because ioctls need to know it. 
 */
#define MAJOR_NUM 100

/* Set the message of the device driver */
#define IOCTL_ALLOC_BASE_PAGE_TEST _IOWR(MAJOR_NUM, 0, int)
#define IOCTL_ALLOC_HUGE_PAGE_TEST _IOWR(MAJOR_NUM, 1, int)
#define IOCTL_CONSUME_BASE_PAGE_TEST _IOWR(MAJOR_NUM, 2, int)
#define IOCTL_CONSUME_HUGE_PAGE_TEST _IOWR(MAJOR_NUM, 3, int)
#define IOCTL_ALLOC_TEST_MULTITHREADED _IOWR(MAJOR_NUM, 4, int)

#define ALLOC_BASE_PAGE_TEST 0
#define ALLOC_HUGE_PAGE_TEST 1
#define CONSUME_BASE_PAGE_TEST 2
#define CONSUME_HUGE_PAGE_TEST 3
#define CONSUME_TEST_MULTITHREADED 3

/* The name of the device file */
#define DEVICE_FILE_NAME "automatic-llfree-balloon-tester"
#define DEVICE_PATH "/dev/automatic-llfree-balloon-tester"

#endif
