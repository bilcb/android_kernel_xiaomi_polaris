/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMABUF Heaps Allocation Infrastructure
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 *
 * Backported to 4.9 (no xarray, old dma_buf_ops, old vm_fault_t).
 * Keeps the same kernel API as upstream v5.10 so heap drivers
 * can be shared / forward-ported easily.
 */

#ifndef _LINUX_DMA_HEAP_H
#define _LINUX_DMA_HEAP_H

struct dma_heap;

/**
 * struct dma_heap_ops - ops to operate on a given heap
 * @allocate:		allocate dmabuf and return fd
 *
 * allocate returns dmabuf fd on success, -errno on error.
 */
struct dma_heap_ops {
	int (*allocate)(struct dma_heap *heap,
			unsigned long len,
			unsigned long fd_flags,
			unsigned long heap_flags);
};

/**
 * struct dma_heap_export_info - information needed to export a new dmabuf heap
 * @name:	used for debugging/device-node name
 * @ops:	ops struct for this heap
 * @priv:	heap exporter private data
 *
 * Information needed to export a new dmabuf heap.
 */
struct dma_heap_export_info {
	const char *name;
	const struct dma_heap_ops *ops;
	void *priv;
};

/**
 * dma_heap_get_drvdata() - get per-heap driver data
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The per-heap data for the heap.
 */
void *dma_heap_get_drvdata(struct dma_heap *heap);

/**
 * dma_heap_add() - adds a heap to dmabuf heaps
 * @exp_info:	information needed to register this heap
 *
 * Returns &struct dma_heap on success, ERR_PTR on failure.
 * Creates /dev/dma_heap/<name>.
 */
struct dma_heap *dma_heap_add(const struct dma_heap_export_info *exp_info);

#endif /* _LINUX_DMA_HEAP_H */
