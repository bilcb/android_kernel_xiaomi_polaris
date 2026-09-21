// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF System heap exporter - 4.9 backport
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 *
 * Registers "system" (cached) and "system-uncached" (writecombine
 * mmap) so both old and new Android HALs work. Co-exists with ION;
 * allocation is plain alloc_page(GFP_HIGHUSER | __GFP_ZERO), zeroed,
 * same semantics as upstream.
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/page.h>

#include "heap-helpers.h"

static void system_heap_free(struct heap_helper_buffer *buffer)
{
	pgoff_t pg;

	for (pg = 0; pg < buffer->pagecount; pg++)
		__free_page(buffer->pages[pg]);
	kfree(buffer->pages);
	kfree(buffer);
}

static int __system_heap_allocate(bool uncached, unsigned long len,
				  unsigned long fd_flags,
				  struct dma_heap *heap)
{
	struct heap_helper_buffer *helper_buffer;
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;
	pgoff_t pg;

	helper_buffer = kzalloc(sizeof(*helper_buffer), GFP_KERNEL);
	if (!helper_buffer)
		return -ENOMEM;

	init_heap_helper_buffer(helper_buffer, system_heap_free);
	helper_buffer->heap = heap;
	helper_buffer->size = len;
	helper_buffer->uncached = uncached;

	helper_buffer->pagecount = len / PAGE_SIZE;
	helper_buffer->pages = kmalloc_array(helper_buffer->pagecount,
					     sizeof(*helper_buffer->pages),
					     GFP_KERNEL);
	if (!helper_buffer->pages) {
		ret = -ENOMEM;
		goto err0;
	}

	for (pg = 0; pg < helper_buffer->pagecount; pg++) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current))
			goto err1;

		helper_buffer->pages[pg] =
			alloc_page(GFP_HIGHUSER | __GFP_ZERO);
		if (!helper_buffer->pages[pg])
			goto err1;
	}

	/* create the dmabuf */
	dmabuf = heap_helper_export_dmabuf(helper_buffer, fd_flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err1;
	}

	helper_buffer->dmabuf = dmabuf;

	ret = dma_buf_fd(dmabuf, fd_flags);
	if (ret < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
		return ret;
	}

	return ret;

err1:
	while (pg > 0)
		__free_page(helper_buffer->pages[--pg]);
	kfree(helper_buffer->pages);
err0:
	kfree(helper_buffer);

	return ret;
}

static int system_heap_allocate(struct dma_heap *heap,
				unsigned long len,
				unsigned long fd_flags,
				unsigned long heap_flags)
{
	return __system_heap_allocate(false, len, fd_flags, heap);
}

static int system_uncached_heap_allocate(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags)
{
	return __system_heap_allocate(true, len, fd_flags, heap);
}

static const struct dma_heap_ops system_heap_ops = {
	.allocate = system_heap_allocate,
};

static const struct dma_heap_ops system_uncached_heap_ops = {
	.allocate = system_uncached_heap_allocate,
};

static int system_heap_create(void)
{
	struct dma_heap_export_info exp_info;
	struct dma_heap *heap;
	int ret = 0;

	exp_info.name = "system";
	exp_info.ops = &system_heap_ops;
	exp_info.priv = NULL;

	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		pr_err("system_heap: failed to add heap\n");
		return PTR_ERR(heap);
	}

	exp_info.name = "system-uncached";
	exp_info.ops = &system_uncached_heap_ops;
	exp_info.priv = NULL;

	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		pr_err("system-uncached_heap: failed to add heap\n");
		ret = PTR_ERR(heap);
	}

	return ret;
}
module_init(system_heap_create);
MODULE_DESCRIPTION("DMA-BUF System Heap (4.9 backport)");
MODULE_LICENSE("GPL v2");
