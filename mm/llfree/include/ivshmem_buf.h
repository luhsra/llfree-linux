#ifndef _LLFREE_IVSHMEM_BUF
#define _LLFREE_IVSHMEM_BUF

#include <linux/types.h>

struct llfree_ivshmem_buf {
	struct pci_dev *pdev;
	void __iomem *buffer;
	resource_size_t len;
};

struct llfree_ivshmem_buf *llfree_ivshmem_get(void);

#endif // _LLFREE_IVSHMEM_BUF
