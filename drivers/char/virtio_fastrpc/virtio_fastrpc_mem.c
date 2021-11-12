// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#include <linux/ion.h>
#include "virtio_fastrpc_mem.h"

static inline void fastrpc_free_pages(struct page **pages, int count)
{
	while (count--)
		__free_page(pages[count]);
	kvfree(pages);
}

static struct page **fastrpc_alloc_pages(unsigned int count, gfp_t gfp)
{
	struct page **pages;
	unsigned long order_mask = (2U << MAX_ORDER) - 1;
	unsigned int i = 0, array_size = count * sizeof(*pages);

	pages = kvzalloc(array_size, GFP_KERNEL);
	if (!pages)
		return NULL;

	/* IOMMU can map any pages, so himem can also be used here */
	gfp |= __GFP_NOWARN | __GFP_HIGHMEM;

	while (count) {
		struct page *page = NULL;
		unsigned int order_size;

		/*
		 * Higher-order allocations are a convenience rather
		 * than a necessity, hence using __GFP_NORETRY until
		 * falling back to minimum-order allocations.
		 */
		for (order_mask &= (2U << __fls(count)) - 1;
		     order_mask; order_mask &= ~order_size) {
			unsigned int order = __fls(order_mask);

			order_size = 1U << order;
			page = alloc_pages(order ?
					   (gfp | __GFP_NORETRY) &
						~__GFP_RECLAIM : gfp, order);
			if (!page)
				continue;
			if (!order)
				break;
			if (!PageCompound(page)) {
				split_page(page, order);
				break;
			} else if (!split_huge_page(page)) {
				break;
			}
			__free_pages(page, order);
		}
		if (!page) {
			fastrpc_free_pages(pages, i);
			return NULL;
		}
		count -= order_size;
		while (order_size--)
			pages[i++] = page++;
	}
	return pages;
}

static struct page **fastrpc_alloc_buffer(struct fastrpc_buf *buf, gfp_t gfp)
{
	struct page **pages;
	unsigned int count = PAGE_ALIGN(buf->size) >> PAGE_SHIFT;

	pages = fastrpc_alloc_pages(count, gfp);
	if (!pages)
		return NULL;

	if (sg_alloc_table_from_pages(&buf->sgt, pages, count, 0,
				buf->size, GFP_KERNEL))
		goto out_free_pages;

	if (!(buf->dma_attr & DMA_ATTR_NO_KERNEL_MAPPING)) {
		buf->va = vmap(pages, count, VM_USERMAP,
				pgprot_noncached(PAGE_KERNEL));
		if (!buf->va)
			goto out_free_sg;
	}
	return pages;

out_free_sg:
	sg_free_table(&buf->sgt);
out_free_pages:
	fastrpc_free_pages(pages, count);
	return NULL;
}

static inline void fastrpc_free_buffer(struct fastrpc_buf *buf)
{
	unsigned int count = PAGE_ALIGN(buf->size) >> PAGE_SHIFT;

	vunmap(buf->va);
	sg_free_table(&buf->sgt);
	fastrpc_free_pages(buf->pages, count);
}

void fastrpc_buf_free(struct fastrpc_buf *buf, int cache)
{
	struct fastrpc_file *fl = buf == NULL ? NULL : buf->fl;

	if (!fl)
		return;

	if (cache && buf->size < MAX_CACHE_BUF_SIZE) {
		spin_lock(&fl->hlock);
		hlist_add_head(&buf->hn, &fl->cached_bufs);
		spin_unlock(&fl->hlock);
		return;
	}

	if (buf->remote) {
		spin_lock(&fl->hlock);
		hlist_del_init(&buf->hn_rem);
		spin_unlock(&fl->hlock);
		buf->remote = 0;
		buf->raddr = 0;
	}

	if (!IS_ERR_OR_NULL(buf->pages))
		fastrpc_free_buffer(buf);
	kfree(buf);
}

int fastrpc_buf_alloc(struct fastrpc_file *fl, size_t size,
				unsigned long dma_attr, uint32_t rflags,
				int remote, struct fastrpc_buf **obuf)
{
	struct fastrpc_apps *me = fl->apps;
	struct fastrpc_buf *buf = NULL, *fr = NULL;
	struct hlist_node *n;
	int err = 0;

	VERIFY(err, size > 0);
	if (err)
		goto bail;

	if (!remote) {
		/* find the smallest buffer that fits in the cache */
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(buf, n, &fl->cached_bufs, hn) {
			if (buf->size >= size && (!fr || fr->size > buf->size))
				fr = buf;
		}
		if (fr)
			hlist_del_init(&fr->hn);
		spin_unlock(&fl->hlock);
		if (fr) {
			*obuf = fr;
			return 0;
		}
	}

	VERIFY(err, NULL != (buf = kzalloc(sizeof(*buf), GFP_KERNEL)));
	if (err)
		goto bail;
	buf->fl = fl;
	buf->size = size;
	buf->va = NULL;
	buf->dma_attr = dma_attr;
	buf->flags = rflags;
	buf->raddr = 0;
	buf->remote = 0;
	buf->pages = fastrpc_alloc_buffer(buf, GFP_KERNEL);
	if (IS_ERR_OR_NULL(buf->pages)) {
		err = -ENOMEM;
		dev_err(me->dev,
			"%s: %s: fastrpc_alloc_buffer failed for size 0x%zx, returned %ld\n",
			current->comm, __func__, size, PTR_ERR(buf->pages));
		goto bail;
	}

	if (remote) {
		INIT_HLIST_NODE(&buf->hn_rem);
		spin_lock(&fl->hlock);
		hlist_add_head(&buf->hn_rem, &fl->remote_bufs);
		spin_unlock(&fl->hlock);
		buf->remote = remote;
	}

	*obuf = buf;
 bail:
	if (err && buf)
		fastrpc_buf_free(buf, 0);
	return err;
}

void fastrpc_mmap_add(struct fastrpc_file *fl, struct fastrpc_mmap *map)
{
	if (map->flags == ADSP_MMAP_HEAP_ADDR ||
				map->flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		struct fastrpc_apps *me = fl->apps;

		dev_err(me->dev, "%s ADSP_MMAP_HEAP_ADDR is not supported\n",
				__func__);
	} else {
		struct fastrpc_file *fl = map->fl;

		hlist_add_head(&map->hn, &fl->maps);
	}
}

int fastrpc_mmap_remove(struct fastrpc_file *fl, uintptr_t va,
		size_t len, struct fastrpc_mmap **ppmap)
{
	struct fastrpc_mmap *match = NULL, *map;
	struct hlist_node *n;

	hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
		if (map->raddr == va &&
			map->raddr + map->len == va + len) {
			match = map;
			hlist_del_init(&map->hn);
			break;
		}
	}
	if (match) {
		*ppmap = match;
		return 0;
	}
	return -ENOTTY;
}

void fastrpc_mmap_free(struct fastrpc_file *fl,
		struct fastrpc_mmap *map, uint32_t flags)
{
	struct fastrpc_apps *me = fl->apps;

	if (!map)
		return;

	if (map->flags == ADSP_MMAP_HEAP_ADDR ||
				map->flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		dev_err(me->dev, "%s ADSP_MMAP_HEAP_ADDR is not supported\n",
				__func__);
	} else {
		map->refs--;
		if (!map->refs)
			hlist_del_init(&map->hn);
		if (map->refs > 0 && !flags)
			return;
	}
	if (!IS_ERR_OR_NULL(map->table))
		dma_buf_unmap_attachment(map->attach, map->table,
				DMA_BIDIRECTIONAL);
	if (!IS_ERR_OR_NULL(map->attach))
		dma_buf_detach(map->buf, map->attach);
	if (!IS_ERR_OR_NULL(map->buf))
		dma_buf_put(map->buf);

	kfree(map);
}

int fastrpc_mmap_find(struct fastrpc_file *fl, int fd,
		uintptr_t va, size_t len, int mflags, int refs,
		struct fastrpc_mmap **ppmap)
{
	struct fastrpc_apps *me = fl->apps;
	struct fastrpc_mmap *match = NULL, *map = NULL;
	struct hlist_node *n;

	if ((va + len) < va)
		return -EOVERFLOW;
	if (mflags == ADSP_MMAP_HEAP_ADDR ||
				 mflags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		dev_err(me->dev, "%s ADSP_MMAP_HEAP_ADDR is not supported\n",
				__func__);
	} else {
		hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
			if (va >= map->va &&
				va + len <= map->va + map->len &&
				map->fd == fd) {
				if (refs) {
					if (map->refs + 1 == INT_MAX)
						return -ETOOMANYREFS;
					map->refs++;
				}
				match = map;
				break;
			}
		}
	}
	if (match) {
		*ppmap = match;
		return 0;
	}
	return -ENOTTY;
}

int fastrpc_mmap_create(struct fastrpc_file *fl, int fd,
	uintptr_t va, size_t len, int mflags, struct fastrpc_mmap **ppmap)
{
	struct fastrpc_apps *me = fl->apps;
	struct fastrpc_mmap *map = NULL;
	int err = 0, sgl_index = 0;
	unsigned long flags;
	struct scatterlist *sgl = NULL;

	if (!fastrpc_mmap_find(fl, fd, va, len, mflags, 1, ppmap))
		return 0;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	VERIFY(err, !IS_ERR_OR_NULL(map));
	if (err)
		goto bail;

	INIT_HLIST_NODE(&map->hn);
	map->flags = mflags;
	map->refs = 1;
	map->fl = fl;
	map->fd = fd;
	if (mflags == ADSP_MMAP_HEAP_ADDR ||
			mflags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		dev_err(me->dev, "%s ADSP_MMAP_HEAP_ADDR is not supported\n",
				__func__);
		err = -EINVAL;
		goto bail;
	} else {
		VERIFY(err, !IS_ERR_OR_NULL(map->buf = dma_buf_get(fd)));
		if (err) {
			dev_err(me->dev, "can't get dma buf fd %d\n", fd);
			goto bail;
		}
		VERIFY(err, !dma_buf_get_flags(map->buf, &flags));
		if (err) {
			dev_err(me->dev, "can't get dma buf flags %d\n", fd);
			goto bail;
		}

		VERIFY(err, !IS_ERR_OR_NULL(map->attach =
					dma_buf_attach(map->buf, me->dev)));
		if (err) {
			dev_err(me->dev, "can't attach dma buf\n");
			goto bail;
		}

		if (!(flags & ION_FLAG_CACHED))
			map->attach->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;
		VERIFY(err, !IS_ERR_OR_NULL(map->table =
					dma_buf_map_attachment(map->attach,
					DMA_BIDIRECTIONAL)));
		if (err) {
			dev_err(me->dev, "can't get sg table of dma buf\n");
			goto bail;
		}
		map->phys = sg_dma_address(map->table->sgl);
		for_each_sg(map->table->sgl, sgl, map->table->nents, sgl_index)
			map->size += sg_dma_len(sgl);
		map->va = va;
	}

	map->len = len;
	fastrpc_mmap_add(fl, map);
	*ppmap = map;
bail:
	if (err && map)
		fastrpc_mmap_free(fl, map, 0);
	return err;
}
