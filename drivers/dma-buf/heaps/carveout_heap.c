// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF carveout heap exporter
 *
 * Each instance binds one reusable reserved-memory region
 * (via the memory-region property) and exposes it as a dma_heap
 * so reserved pools previously reachable only through ION can
 * also be allocated from generic dma_heap clients.
 *
 * Regions without a kernel mapping (no-map) and secure regions
 * are refused here on purpose; those stay ION-only.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-heap.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>

#include "heap-helpers.h"

struct carveout_heap {
	struct dma_heap *heap;
	struct device *dev;
	bool uncached;
	unsigned long attrs;
};

struct carveout_buffer_priv {
	void *cpu_addr;
	dma_addr_t dma_addr;
	unsigned long attrs;
};

static void carveout_heap_free(struct heap_helper_buffer *buffer)
{
	struct carveout_heap *cheap = dma_heap_get_drvdata(buffer->heap);
	struct carveout_buffer_priv *priv = buffer->priv_virt;

	dma_free_attrs(cheap->dev, buffer->size,
		       priv->cpu_addr, priv->dma_addr, priv->attrs);
	kfree(buffer->pages);
	kfree(priv);
	kfree(buffer);
}

static int carveout_heap_allocate(struct dma_heap *heap,
				  unsigned long len,
				  unsigned long fd_flags,
				  unsigned long heap_flags)
{
	struct carveout_heap *cheap = dma_heap_get_drvdata(heap);
	struct heap_helper_buffer *helper_buffer;
	struct carveout_buffer_priv *priv;
	struct dma_buf *dmabuf;
	size_t size = PAGE_ALIGN(len);
	int ret = -ENOMEM;
	pgoff_t pg;

	helper_buffer = kzalloc(sizeof(*helper_buffer), GFP_KERNEL);
	if (!helper_buffer)
		return -ENOMEM;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		kfree(helper_buffer);
		return -ENOMEM;
	}

	if (cheap->uncached)
		priv->cpu_addr = dma_alloc_writecombine(cheap->dev, size,
							&priv->dma_addr,
							GFP_KERNEL);
	else
		priv->cpu_addr = dma_alloc_attrs(cheap->dev, size,
						 &priv->dma_addr, GFP_KERNEL,
						 cheap->attrs);
	if (!priv->cpu_addr) {
		dev_err(cheap->dev, "failed to allocate %zu bytes\n", size);
		goto free_priv;
	}
	priv->attrs = cheap->attrs;

	memset(priv->cpu_addr, 0, size);

	init_heap_helper_buffer(helper_buffer, carveout_heap_free);
	helper_buffer->heap = heap;
	helper_buffer->size = size;
	helper_buffer->uncached = cheap->uncached;
	helper_buffer->priv_virt = priv;

	helper_buffer->pagecount = size >> PAGE_SHIFT;
	helper_buffer->pages = kmalloc_array(helper_buffer->pagecount,
					     sizeof(*helper_buffer->pages),
					     GFP_KERNEL);
	if (!helper_buffer->pages) {
		ret = -ENOMEM;
		goto free_dma;
	}

	for (pg = 0; pg < helper_buffer->pagecount; pg++) {
		if (fatal_signal_pending(current))
			goto free_dma;
		/*
		 * Derive pages from the dma address, not the cpu address:
		 * coherent/writecombine allocations may be remapped,
		 * so virt_to_page(cpu_addr) would be wrong here.
		 */
		helper_buffer->pages[pg] =
			pfn_to_page(PFN_DOWN(priv->dma_addr) + pg);
	}

	/* create the dmabuf */
	dmabuf = heap_helper_export_dmabuf(helper_buffer, fd_flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_dma;
	}

	helper_buffer->dmabuf = dmabuf;

	ret = dma_buf_fd(dmabuf, fd_flags);
	if (ret < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
		return ret;
	}

	return ret;

free_dma:
	if (helper_buffer->pages)
		kfree(helper_buffer->pages);
	dma_free_attrs(cheap->dev, size,
		       priv->cpu_addr, priv->dma_addr, priv->attrs);
free_priv:
	kfree(priv);
	kfree(helper_buffer);
	return ret;
}

static const struct dma_heap_ops carveout_heap_ops = {
	.allocate = carveout_heap_allocate,
};

static const struct of_device_id carveout_heap_of_match[] = {
	{ .compatible = "dma-heap-carveout" },
	{ },
};
MODULE_DEVICE_TABLE(of, carveout_heap_of_match);

static int carveout_heap_probe(struct platform_device *pdev)
{
	struct carveout_heap *cheapo;
	struct dma_heap_export_info exp_info;
	struct device_node *mem_node;
	const char *heap_name;
	int ret;

	if (of_property_read_string(pdev->dev.of_node,
				    "heap-name", &heap_name)) {
		dev_err(&pdev->dev, "missing heap-name property\n");
		return -EINVAL;
	}

	/* Refuse regions without a kernel mapping; they stay ION-only */
	mem_node = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!mem_node) {
		dev_err(&pdev->dev, "missing memory-region property\n");
		return -EINVAL;
	}
	if (of_property_read_bool(mem_node, "no-map")) {
		dev_info(&pdev->dev,
			 "region has no kernel mapping, leaving it ION-only\n");
		of_node_put(mem_node);
		return -ENODEV;
	}
	of_node_put(mem_node);

	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret && ret != -EBUSY) {
		dev_err(&pdev->dev,
			"failed to bind reserved memory: %d\n", ret);
		return ret;
	}

	cheapo = devm_kzalloc(&pdev->dev, sizeof(*cheapo), GFP_KERNEL);
	if (!cheapo) {
		ret = -ENOMEM;
		goto release_rmem;
	}

	cheapo->dev = &pdev->dev;
	cheapo->uncached = of_property_read_bool(pdev->dev.of_node,
						 "uncached");
	cheapo->attrs = cheapo->uncached ?
		DMA_ATTR_WRITE_COMBINE : DMA_ATTR_FORCE_COHERENT;

	exp_info.name = heap_name;
	exp_info.ops = &carveout_heap_ops;
	exp_info.priv = cheapo;

	cheapo->heap = dma_heap_add(&exp_info);
	if (IS_ERR(cheapo->heap)) {
		ret = PTR_ERR(cheapo->heap);
		dev_err(&pdev->dev, "failed to register heap \"%s\": %d\n",
			heap_name, ret);
		goto release_rmem;
	}

	platform_set_drvdata(pdev, cheapo);
	dev_info(&pdev->dev, "registered \"%s\" carveout heap (%s)\n",
		 heap_name, cheapo->uncached ? "uncached" : "cached");
	return 0;

release_rmem:
	of_reserved_mem_device_release(&pdev->dev);
	return ret;
}

static int carveout_heap_remove(struct platform_device *pdev)
{
	of_reserved_mem_device_release(&pdev->dev);
	return 0;
}

static struct platform_driver carveout_heap_driver = {
	.probe = carveout_heap_probe,
	.remove = carveout_heap_remove,
	.driver = {
		.name = "dma-heap-carveout",
		.of_match_table = carveout_heap_of_match,
	},
};
module_platform_driver(carveout_heap_driver);

MODULE_DESCRIPTION("DMA-BUF carveout heap over reserved memory");
MODULE_LICENSE("GPL v2");
