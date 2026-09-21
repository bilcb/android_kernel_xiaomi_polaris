// SPDX-License-Identifier: GPL-2.0
/*
 * Framework for userspace DMA-BUF allocations
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 *
 * Backported to kernels without native dma_heap support:
 * - xarray -> idr (4.9 has no xarray)
 * - keep UAPI identical to upstream v5.10
 * - co-exists with ION (/dev/ion stays untouched)
 */

#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <uapi/linux/dma-heap.h>

#define DEVNAME "dma_heap"

#define NUM_HEAP_MINORS 128

/*
 * Upper bound for a single dma_heap allocation. Largest legitimate
 * gralloc/camera buffers on this platform are well under 100MB;
 * 512MB is generous while bounding unprivileged DoS via the system
 * heap (CMA/carveout heaps are additionally capped by pool size).
 * Upstream relies on memcg limits which 4.9 lacks.
 */
#define DMA_HEAP_MAX_ALLOC_SZ SZ_512M

/**
 * struct dma_heap - represents a dmabuf heap in the system
 * @name:		used for debugging/device-node name
 * @ops:		ops struct for this heap
 * @heap_devt:		heap device node
 * @list:		list head connecting to list of heaps
 * @heap_cdev:		heap char device
 * @priv:		private data for this heap
 * @minor:		minor number for this heap
 */
struct dma_heap {
	const char *name;
	const struct dma_heap_ops *ops;
	void *priv;
	dev_t heap_devt;
	struct list_head list;
	struct cdev heap_cdev;
	int minor;
};

static LIST_HEAD(heap_list);
static DEFINE_MUTEX(heap_list_lock);
static dev_t dma_heap_devt;
static struct class *dma_heap_class;
static DEFINE_IDR(dma_heap_minor_idr);
static DEFINE_MUTEX(dma_heap_minor_lock);

static int dma_heap_buffer_alloc(struct dma_heap *heap, size_t len,
				 unsigned int fd_flags,
				 unsigned int heap_flags)
{
	/*
	 * Allocations from all heaps have to begin
	 * and end on page boundaries.
	 */
	len = PAGE_ALIGN(len);
	if (!len)
		return -EINVAL;

	if (len > DMA_HEAP_MAX_ALLOC_SZ)
		return -ENOMEM;

	return heap->ops->allocate(heap, len, fd_flags, heap_flags);
}

static int dma_heap_open(struct inode *inode, struct file *file)
{
	struct dma_heap *heap;

	mutex_lock(&dma_heap_minor_lock);
	heap = idr_find(&dma_heap_minor_idr, iminor(inode));
	mutex_unlock(&dma_heap_minor_lock);
	if (!heap) {
		pr_err("dma_heap: minor %d unknown.\n", iminor(inode));
		return -ENODEV;
	}

	/* instance data as context */
	file->private_data = heap;
	nonseekable_open(inode, file);

	return 0;
}

static long dma_heap_ioctl_allocate(struct file *file, void *data)
{
	struct dma_heap_allocation_data *heap_allocation = data;
	struct dma_heap *heap = file->private_data;
	int fd;

	if (heap_allocation->fd)
		return -EINVAL;

	if (heap_allocation->fd_flags & ~DMA_HEAP_VALID_FD_FLAGS)
		return -EINVAL;

	if (heap_allocation->heap_flags & ~DMA_HEAP_VALID_HEAP_FLAGS)
		return -EINVAL;

	fd = dma_heap_buffer_alloc(heap, heap_allocation->len,
				   heap_allocation->fd_flags,
				   heap_allocation->heap_flags);
	if (fd < 0)
		return fd;

	heap_allocation->fd = fd;

	return 0;
}

static long dma_heap_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct dma_heap_allocation_data heap_allocation;
	int ret;

	/* 4.9 compat handling: kernel and user struct are identical,
	 * the extensible ioctl path from upstream is unnecessary here,
	 * but we still validate the command explicitly.
	 */
	if (cmd != DMA_HEAP_IOCTL_ALLOC)
		return -ENOTTY;

	if (copy_from_user(&heap_allocation, (void __user *)arg,
			   sizeof(heap_allocation)))
		return -EFAULT;

	ret = dma_heap_ioctl_allocate(file, &heap_allocation);
	if (ret)
		return ret;

	if (copy_to_user((void __user *)arg, &heap_allocation,
			 sizeof(heap_allocation)))
		return -EFAULT;

	return 0;
}

static const struct file_operations dma_heap_fops = {
	.owner          = THIS_MODULE,
	.open		= dma_heap_open,
	.unlocked_ioctl = dma_heap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= dma_heap_ioctl,
#endif
};

/**
 * dma_heap_get_drvdata() - get per-subdriver data for the heap
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The per-subdriver data for the heap.
 */
void *dma_heap_get_drvdata(struct dma_heap *heap)
{
	return heap->priv;
}
EXPORT_SYMBOL_GPL(dma_heap_get_drvdata);

/*
 * NOTE: heaps are builtin-only on this tree (all Kconfig entries are
 * bool); there is intentionally no dma_heap_remove()/kref path. The
 * file->private_data heap pointer therefore stays valid for the
 * lifetime of the system. Revisit if any heap ever becomes tristate.
 */
struct dma_heap *dma_heap_add(const struct dma_heap_export_info *exp_info)
{
	struct dma_heap *heap, *h, *err_ret;
	struct device *dev_ret;
	int minor;
	int ret;

	if (!exp_info->name || !strcmp(exp_info->name, "")) {
		pr_err("dma_heap: Cannot add heap without a name\n");
		return ERR_PTR(-EINVAL);
	}

	if (!exp_info->ops || !exp_info->ops->allocate) {
		pr_err("dma_heap: Cannot add heap with invalid ops struct\n");
		return ERR_PTR(-EINVAL);
	}

	/* check the name is unique */
	mutex_lock(&heap_list_lock);
	list_for_each_entry(h, &heap_list, list) {
		if (!strcmp(h->name, exp_info->name)) {
			mutex_unlock(&heap_list_lock);
			pr_err("dma_heap: Already registered heap named %s\n",
			       exp_info->name);
			return ERR_PTR(-EINVAL);
		}
	}
	mutex_unlock(&heap_list_lock);

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);

	heap->name = exp_info->name;
	heap->ops = exp_info->ops;
	heap->priv = exp_info->priv;

	/* Find unused minor number */
	mutex_lock(&dma_heap_minor_lock);
	minor = idr_alloc(&dma_heap_minor_idr, heap, 0, NUM_HEAP_MINORS,
			  GFP_KERNEL);
	mutex_unlock(&dma_heap_minor_lock);
	if (minor < 0) {
		pr_err("dma_heap: Unable to get minor number for heap\n");
		err_ret = ERR_PTR(minor);
		goto err0;
	}
	heap->minor = minor;

	/* Create device */
	heap->heap_devt = MKDEV(MAJOR(dma_heap_devt), minor);

	cdev_init(&heap->heap_cdev, &dma_heap_fops);
	ret = cdev_add(&heap->heap_cdev, heap->heap_devt, 1);
	if (ret < 0) {
		pr_err("dma_heap: Unable to add char device\n");
		err_ret = ERR_PTR(ret);
		goto err1;
	}

	dev_ret = device_create(dma_heap_class,
				NULL,
				heap->heap_devt,
				NULL,
				heap->name);
	if (IS_ERR(dev_ret)) {
		pr_err("dma_heap: Unable to create device\n");
		err_ret = ERR_CAST(dev_ret);
		goto err2;
	}
	/* Add heap to the list */
	mutex_lock(&heap_list_lock);
	list_add(&heap->list, &heap_list);
	mutex_unlock(&heap_list_lock);

	pr_info("dma_heap: added heap %s dev %d:%d\n",
		heap->name, MAJOR(heap->heap_devt), MINOR(heap->heap_devt));

	return heap;

err2:
	cdev_del(&heap->heap_cdev);
err1:
	mutex_lock(&dma_heap_minor_lock);
	idr_remove(&dma_heap_minor_idr, minor);
	mutex_unlock(&dma_heap_minor_lock);
err0:
	kfree(heap);
	return err_ret;
}
EXPORT_SYMBOL_GPL(dma_heap_add);

static char *dma_heap_devnode(struct device *dev, umode_t *mode)
{
	/*
	 * Default to world-accessible so HALs (graphics/camera) work out
	 * of the box; platform ueventd.rc / sepolicy should tighten this
	 * (e.g. 0660 system) where a stricter policy is wanted, which
	 * overrides the kernel default at userspace coldboot.
	 */
	if (mode)
		*mode = 0666;
	return kasprintf(GFP_KERNEL, "dma_heap/%s", dev_name(dev));
}

static int dma_heap_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dma_heap_devt, 0, NUM_HEAP_MINORS, DEVNAME);
	if (ret)
		return ret;

	dma_heap_class = class_create(THIS_MODULE, DEVNAME);
	if (IS_ERR(dma_heap_class)) {
		unregister_chrdev_region(dma_heap_devt, NUM_HEAP_MINORS);
		return PTR_ERR(dma_heap_class);
	}
	dma_heap_class->devnode = dma_heap_devnode;

	return 0;
}
subsys_initcall(dma_heap_init);
