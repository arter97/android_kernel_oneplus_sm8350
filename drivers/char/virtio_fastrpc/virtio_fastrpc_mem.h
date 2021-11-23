/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __VIRTIO_FASTRPC_MEM_H__
#define __VIRTIO_FASTRPC_MEM_H__

#include <linux/dma-buf.h>
#include <linux/types.h>
#include "virtio_fastrpc_core.h"

struct fastrpc_mmap {
	struct hlist_node hn;
	struct fastrpc_file *fl;
	int fd;
	uint32_t flags;
	struct dma_buf *buf;
	struct sg_table *table;
	struct dma_buf_attachment *attach;
	uint64_t phys;
	size_t size;
	uintptr_t va;
	size_t len;
	uintptr_t raddr;
	int refs;
};

struct fastrpc_buf {
	struct hlist_node hn;
	struct hlist_node hn_rem;
	struct fastrpc_file *fl;
	size_t size;
	struct sg_table sgt;
	struct page **pages;
	void *va;
	unsigned long dma_attr;
	uintptr_t raddr;
	uint32_t flags;
	int remote;
};

enum fastrpc_buf_type {
	FASTRPC_BUF_TYPE_NORMAL,
	FASTRPC_BUF_TYPE_ION,
	FASTRPC_BUF_TYPE_INTERNAL,
};

struct fastrpc_buf_desc {
	enum fastrpc_buf_type type;
	struct fastrpc_buf *buf;
};

int fastrpc_mmap_create(struct fastrpc_file *fl, int fd,
	uintptr_t va, size_t len, int mflags, struct fastrpc_mmap **ppmap);

int fastrpc_mmap_find(struct fastrpc_file *fl, int fd,
		uintptr_t va, size_t len, int mflags, int refs,
		struct fastrpc_mmap **ppmap);

void fastrpc_mmap_free(struct fastrpc_file *fl,
		struct fastrpc_mmap *map, uint32_t flags);

int fastrpc_mmap_remove(struct fastrpc_file *fl, uintptr_t va,
		size_t len, struct fastrpc_mmap **ppmap);

void fastrpc_mmap_add(struct fastrpc_file *fl, struct fastrpc_mmap *map);

int fastrpc_buf_alloc(struct fastrpc_file *fl, size_t size,
				unsigned long dma_attr, uint32_t rflags,
				int remote, struct fastrpc_buf **obuf);

void fastrpc_buf_free(struct fastrpc_buf *buf, int cache);
#endif /*__VIRTIO_FASTRPC_MEM_H__*/
