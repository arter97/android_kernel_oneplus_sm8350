// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include "virtio_fastrpc_core.h"
#include "virtio_fastrpc_mem.h"
#include "virtio_fastrpc_queue.h"

static inline int64_t getnstimediff(struct timespec64 *start)
{
	int64_t ns;
	struct timespec64 ts, b;

	ktime_get_real_ts64(&ts);
	b = timespec64_sub(ts, *start);
	ns = timespec64_to_ns(&b);
	return ns;
}

static void context_list_ctor(struct fastrpc_ctx_lst *me)
{
	INIT_HLIST_HEAD(&me->interrupted);
	INIT_HLIST_HEAD(&me->pending);
}

struct fastrpc_file *fastrpc_file_alloc(void)
{
	int err = 0;
	struct fastrpc_file *fl = NULL;

	VERIFY(err, NULL != (fl = kzalloc(sizeof(*fl), GFP_KERNEL)));
	if (err)
		return NULL;
	context_list_ctor(&fl->clst);
	spin_lock_init(&fl->hlock);
	INIT_HLIST_HEAD(&fl->maps);
	INIT_HLIST_HEAD(&fl->cached_bufs);
	INIT_HLIST_HEAD(&fl->remote_bufs);
	fl->tgid = current->tgid;
	fl->mode = FASTRPC_MODE_SERIAL;
	fl->domain = -1;
	fl->cid = -1;
	fl->dsp_proc_init = 0;
	mutex_init(&fl->map_mutex);
	return fl;
}

static void context_free(struct fastrpc_invoke_ctx *ctx)
{
	int i;
	struct fastrpc_file *fl = ctx->fl;
	struct scatterlist sg[1];
	struct fastrpc_apps *me = fl->apps;
	struct virt_invoke_msg *rsp = NULL;
	unsigned long flags;
	int nbufs = REMOTE_SCALARS_INBUFS(ctx->sc) +
			REMOTE_SCALARS_OUTBUFS(ctx->sc);

	spin_lock(&fl->hlock);
	hlist_del_init(&ctx->hn);
	spin_unlock(&fl->hlock);

	mutex_lock(&fl->map_mutex);
	for (i = 0; i < nbufs; i++)
		fastrpc_mmap_free(fl, ctx->maps[i], 0);
	mutex_unlock(&fl->map_mutex);

	if (ctx->msg) {
		rsp = ctx->msg->rxbuf;
		if (rsp) {
			sg_init_one(sg, rsp, me->buf_size);

			spin_lock_irqsave(&me->rvq.vq_lock, flags);
			/* add the buffer back to the remote processor's virtqueue */
			if (virtqueue_add_inbuf(me->rvq.vq, sg, 1, rsp, GFP_KERNEL))
				dev_err(me->dev,
					"%s: fail to add input buffer\n", __func__);
			else
				virtqueue_kick(me->rvq.vq);
			spin_unlock_irqrestore(&me->rvq.vq_lock, flags);
		}

		virt_free_msg(fl, ctx->msg);
		ctx->msg = NULL;
	}
	if (ctx->desc) {
		for (i = 0; i < nbufs; i++) {
			if (ctx->desc[i].buf)
				fastrpc_buf_free(ctx->desc[i].buf, 1);
		}
		kfree(ctx->desc);
		ctx->desc = NULL;
	}

	kfree(ctx);
}

static void fastrpc_context_list_dtor(struct fastrpc_file *fl)
{
	struct fastrpc_ctx_lst *clst = &fl->clst;
	struct fastrpc_invoke_ctx *ictx = NULL, *ctxfree;
	struct hlist_node *n;

	do {
		ctxfree = NULL;
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(ictx, n, &clst->interrupted, hn) {
			hlist_del_init(&ictx->hn);
			ctxfree = ictx;
			break;
		}
		spin_unlock(&fl->hlock);
		if (ctxfree)
			context_free(ctxfree);
	} while (ctxfree);
	do {
		ctxfree = NULL;
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(ictx, n, &clst->pending, hn) {
			hlist_del_init(&ictx->hn);
			ctxfree = ictx;
			break;
		}
		spin_unlock(&fl->hlock);
		if (ctxfree)
			context_free(ctxfree);
	} while (ctxfree);
}

static void fastrpc_remote_buf_list_free(struct fastrpc_file *fl)
{
	struct fastrpc_buf *buf, *free;

	do {
		struct hlist_node *n;

		free = NULL;
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(buf, n, &fl->remote_bufs, hn_rem) {
			free = buf;
			break;
		}
		spin_unlock(&fl->hlock);
		if (free)
			fastrpc_buf_free(free, 0);
	} while (free);
}

static void fastrpc_cached_buf_list_free(struct fastrpc_file *fl)
{
	struct fastrpc_buf *buf, *free;

	do {
		struct hlist_node *n;

		free = NULL;
		spin_lock(&fl->hlock);
		hlist_for_each_entry_safe(buf, n, &fl->cached_bufs, hn) {
			hlist_del_init(&buf->hn);
			free = buf;
			break;
		}
		spin_unlock(&fl->hlock);
		if (free)
			fastrpc_buf_free(free, 0);
	} while (free);
}

int fastrpc_file_free(struct fastrpc_file *fl)
{
	struct fastrpc_mmap *map = NULL, *lmap = NULL;

	if (!fl)
		return 0;

	virt_fastrpc_close(fl);

	kfree(fl->debug_buf);

	spin_lock(&fl->hlock);
	fl->file_close = 1;
	spin_unlock(&fl->hlock);

	fastrpc_context_list_dtor(fl);
	fastrpc_cached_buf_list_free(fl);
	fastrpc_remote_buf_list_free(fl);

	mutex_lock(&fl->map_mutex);
	do {
		struct hlist_node *n = NULL;

		lmap = NULL;
		hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
			hlist_del_init(&map->hn);
			lmap = map;
			break;
		}
		fastrpc_mmap_free(fl, lmap, 1);
	} while (lmap);
	mutex_unlock(&fl->map_mutex);

	mutex_destroy(&fl->map_mutex);
	kfree(fl);
	return 0;
}

static int context_restore_interrupted(struct fastrpc_file *fl,
					struct fastrpc_ioctl_invoke *invoke,
					struct fastrpc_invoke_ctx **po)
{
	int err = 0;
	struct fastrpc_apps *me = fl->apps;
	struct fastrpc_invoke_ctx *ctx = NULL, *ictx = NULL;
	struct hlist_node *n;

	spin_lock(&fl->hlock);
	hlist_for_each_entry_safe(ictx, n, &fl->clst.interrupted, hn) {
		if (ictx->pid == current->pid) {
			if (invoke->sc != ictx->sc || ictx->fl != fl) {
				err = -EINVAL;
				dev_err(me->dev,
					"interrupted sc (0x%x) or fl (%pK) does not match with invoke sc (0x%x) or fl (%pK)\n",
					ictx->sc, ictx->fl, invoke->sc, fl);
			} else {
				ctx = ictx;
				hlist_del_init(&ctx->hn);
				hlist_add_head(&ctx->hn, &fl->clst.pending);
			}
			break;
		}
	}
	spin_unlock(&fl->hlock);
	if (ctx)
		*po = ctx;
	return err;
}

static int context_alloc(struct fastrpc_file *fl,
			struct fastrpc_ioctl_invoke_crc *invokefd,
			struct fastrpc_invoke_ctx **po)
{
	int err = 0, bufs, size = 0;
	struct fastrpc_invoke_ctx *ctx = NULL;
	struct fastrpc_ctx_lst *clst = &fl->clst;
	struct fastrpc_ioctl_invoke *invoke = &invokefd->inv;

	bufs = REMOTE_SCALARS_LENGTH(invoke->sc);
	size = bufs * sizeof(*ctx->lpra) + bufs * sizeof(*ctx->maps) +
		sizeof(*ctx->fds) * (bufs) +
		sizeof(*ctx->attrs) * (bufs);

	VERIFY(err, NULL != (ctx = kzalloc(sizeof(*ctx) + size, GFP_KERNEL)));
	if (err)
		goto bail;

	INIT_HLIST_NODE(&ctx->hn);
	hlist_add_fake(&ctx->hn);
	ctx->fl = fl;
	ctx->maps = (struct fastrpc_mmap **)(&ctx[1]);
	ctx->lpra = (remote_arg_t *)(&ctx->maps[bufs]);
	ctx->fds = (int *)(&ctx->lpra[bufs]);
	ctx->attrs = (unsigned int *)(&ctx->fds[bufs]);

	K_COPY_FROM_USER(err, 0, (void *)ctx->lpra, invoke->pra,
			bufs * sizeof(*ctx->lpra));
	if (err)
		goto bail;

	if (invokefd->fds) {
		K_COPY_FROM_USER(err, 0, ctx->fds, invokefd->fds,
				bufs * sizeof(*ctx->fds));
		if (err)
			goto bail;
	} else {
		ctx->fds = NULL;
	}
	if (invokefd->attrs) {
		K_COPY_FROM_USER(err, 0, ctx->attrs, invokefd->attrs,
				bufs * sizeof(*ctx->attrs));
		if (err)
			goto bail;
	}
	ctx->sc = invoke->sc;
	ctx->handle = invoke->handle;
	ctx->pid = current->pid;
	ctx->tgid = fl->tgid;

	spin_lock(&fl->hlock);
	hlist_add_head(&ctx->hn, &clst->pending);
	spin_unlock(&fl->hlock);

	*po = ctx;
bail:
	if (ctx && err)
		context_free(ctx);
	return err;
}

static void context_save_interrupted(struct fastrpc_invoke_ctx *ctx)
{
	struct fastrpc_ctx_lst *clst = &ctx->fl->clst;

	spin_lock(&ctx->fl->hlock);
	hlist_del_init(&ctx->hn);
	hlist_add_head(&ctx->hn, &clst->interrupted);
	spin_unlock(&ctx->fl->hlock);
}

static int get_args(struct fastrpc_invoke_ctx *ctx)
{
	struct fastrpc_file *fl = ctx->fl;
	struct fastrpc_apps *me = fl->apps;
	struct virt_invoke_msg *vmsg;
	int inbufs = REMOTE_SCALARS_INBUFS(ctx->sc);
	int outbufs = REMOTE_SCALARS_OUTBUFS(ctx->sc);
	int i, err = 0, bufs, handles, total;
	remote_arg_t *lpra = ctx->lpra;
	int *fds = ctx->fds;
	struct virt_fastrpc_buf *rpra;
	struct virt_fastrpc_dmahandle *handle;
	uint64_t *fdlist;
	unsigned int *attrs = ctx->attrs;
	struct fastrpc_mmap **maps = ctx->maps;
	size_t copylen = 0, size = 0, handle_len = 0, metalen;
	char *payload;

	bufs = inbufs + outbufs;
	handles = REMOTE_SCALARS_INHANDLES(ctx->sc)
		+ REMOTE_SCALARS_OUTHANDLES(ctx->sc);
	total = REMOTE_SCALARS_LENGTH(ctx->sc);

	/* calculate len required for copying */
	for (i = 0; i < bufs; i++) {
		size_t len = lpra[i].buf.len;

		if (!len)
			continue;
		if (fds && (fds[i] != -1)) {
			/* map ion buffers */
			mutex_lock(&fl->map_mutex);
			err = fastrpc_mmap_create(fl, fds[i],
					(uintptr_t)lpra[i].buf.pv,
					len, 0, &maps[i]);
			mutex_unlock(&fl->map_mutex);
			if (err)
				goto bail;
			len = maps[i]->table->nents *
				sizeof(struct virt_fastrpc_buf);
		}
		copylen += len;
		if (i < inbufs)
			ctx->outbufs_offset += len;
	}

	mutex_lock(&fl->map_mutex);
	for (i = bufs; i < total; i++) {
		int dmaflags = 0;

		if (attrs && (attrs[i] & FASTRPC_ATTR_NOMAP))
			dmaflags = FASTRPC_DMAHANDLE_NOMAP;
		if (fds && (fds[i] != -1)) {
			err = fastrpc_mmap_create(fl, fds[i],
					0, 0, dmaflags, &maps[i]);
			if (err) {
				mutex_unlock(&fl->map_mutex);
				goto bail;
			}
			handle_len += maps[i]->table->nents *
					sizeof(struct virt_fastrpc_buf);
		}
	}
	mutex_unlock(&fl->map_mutex);

	metalen = sizeof(*vmsg) + total * sizeof(*rpra)
		+ handles * sizeof(struct virt_fastrpc_dmahandle)
		+ sizeof(uint64_t) * M_FDLIST;
	size = metalen + copylen + handle_len;
	if (size > me->buf_size) {
		/* if user buffer contents exceed virtio buffer limits,
		 * try to alloc an internal buffer to copy
		 */
		copylen = 0;
		ctx->outbufs_offset = 0;
		ctx->desc = kcalloc(bufs, sizeof(*ctx->desc), GFP_KERNEL);
		if (!ctx->desc) {
			err = -ENOMEM;
			goto bail;
		}
		for (i = 0; i < bufs; i++) {
			size_t len = lpra[i].buf.len;

			if (maps[i]) {
				len = maps[i]->table->nents *
					sizeof(struct virt_fastrpc_buf);
				ctx->desc[i].type = FASTRPC_BUF_TYPE_ION;
			} else if (len < PAGE_SIZE) {
				ctx->desc[i].type = FASTRPC_BUF_TYPE_NORMAL;
			} else {
				ctx->desc[i].type = FASTRPC_BUF_TYPE_INTERNAL;
				len = PAGE_ALIGN(len);
				err = fastrpc_buf_alloc(fl, len, 0,
						0, 0, &ctx->desc[i].buf);
				if (err)
					goto bail;
				len = ctx->desc[i].buf->sgt.nents *
					sizeof(struct virt_fastrpc_buf);
			}
			copylen += len;
			if (i < inbufs)
				ctx->outbufs_offset += len;
		}
		size = metalen + copylen + handle_len;
	}

	ctx->msg = virt_alloc_msg(fl, size);
	if (!ctx->msg) {
		err = -ENOMEM;
		goto bail;
	}

	ctx->size = size;
	vmsg = (struct virt_invoke_msg *)ctx->msg->txbuf;
	vmsg->hdr.pid = fl->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = fl->cid;
	vmsg->hdr.cmd = VIRTIO_FASTRPC_CMD_INVOKE;
	vmsg->hdr.len = size;
	vmsg->hdr.msgid = ctx->msg->msgid;
	vmsg->hdr.result = 0xffffffff;
	vmsg->handle = ctx->handle;
	vmsg->sc = ctx->sc;
	rpra = (struct virt_fastrpc_buf *)vmsg->pra;
	handle = (struct virt_fastrpc_dmahandle *)&rpra[total];
	fdlist = (uint64_t *)&handle[handles];
	payload = (char *)&fdlist[M_FDLIST];

	memset(fdlist, 0, sizeof(uint64_t) * M_FDLIST);

	for (i = 0; i < bufs; i++) {
		size_t len = lpra[i].buf.len;
		struct sg_table *table;
		struct virt_fastrpc_buf *sgbuf;
		struct scatterlist *sgl = NULL;
		int index = 0;

		if (maps[i]) {
			table = maps[i]->table;
			rpra[i].pv = len;
			rpra[i].len = table->nents *
				sizeof(struct virt_fastrpc_buf);
			sgbuf = (struct virt_fastrpc_buf *)payload;
			for_each_sg(table->sgl, sgl, table->nents, index) {
				sgbuf[index].pv = sg_dma_address(sgl);
				sgbuf[index].len = sg_dma_len(sgl);
			}
			payload += rpra[i].len;
		} else if (ctx->desc &&
			   ctx->desc[i].type == FASTRPC_BUF_TYPE_INTERNAL) {
			table = &ctx->desc[i].buf->sgt;
			rpra[i].pv = len;
			rpra[i].len = table->nents *
				sizeof(struct virt_fastrpc_buf);
			sgbuf = (struct virt_fastrpc_buf *)payload;
			for_each_sg(table->sgl, sgl, table->nents, index) {
				sgbuf[index].pv = page_to_phys(sg_page(sgl));
				sgbuf[index].len = sgl->length;
			}
			if (i < inbufs && len) {
				K_COPY_FROM_USER(err, 0, ctx->desc[i].buf->va,
						lpra[i].buf.pv, len);
				if (err)
					goto bail;

			}
			payload += rpra[i].len;
		} else {
			/* copy non ion buffers */
			rpra[i].pv = 0;
			rpra[i].len = len;
			if (i < inbufs && len) {
				K_COPY_FROM_USER(err, 0, payload,
						lpra[i].buf.pv, len);
				if (err)
					goto bail;
			}
			payload += len;
		}
	}

	for (i = bufs; i < total; i++) {
		struct sg_table *table;
		struct virt_fastrpc_buf *sgbuf;
		struct scatterlist *sgl = NULL;
		int index = 0, hlist;

		if (fds && maps[i]) {
			/* fill in dma handle list */
			hlist = i - bufs;
			handle[hlist].fd = fds[i];
			handle[hlist].offset = (uint32_t)(uintptr_t)lpra[i].buf.pv;
			/* copy dma handle sglist to data area */
			table = maps[i]->table;
			rpra[i].pv = lpra[i].buf.len;
			rpra[i].len = table->nents *
				sizeof(struct virt_fastrpc_buf);
			sgbuf = (struct virt_fastrpc_buf *)payload;
			for_each_sg(table->sgl, sgl, table->nents, index) {
				sgbuf[index].pv = sg_dma_address(sgl);
				sgbuf[index].len = sg_dma_len(sgl);
			}
			payload += rpra[i].len;
		}
	}
bail:
	return err;
}

static int put_args(struct fastrpc_invoke_ctx *ctx)
{
	int err = 0;
	struct fastrpc_apps *me = ctx->fl->apps;
	struct fastrpc_file *fl = ctx->fl;
	struct virt_fastrpc_msg *msg = ctx->msg;
	struct virt_invoke_msg *rsp = NULL;
	int inbufs = REMOTE_SCALARS_INBUFS(ctx->sc);
	int outbufs = REMOTE_SCALARS_OUTBUFS(ctx->sc);
	int i, bufs, handles, total;
	remote_arg_t *lpra = ctx->lpra;
	struct virt_fastrpc_buf *rpra;
	struct virt_fastrpc_dmahandle *handle;
	uint64_t *fdlist;
	struct fastrpc_mmap **maps = ctx->maps, *mmap = NULL;
	char *payload;

	if (!msg) {
		dev_err(me->dev, "%s: ctx msg is NULL\n", __func__);
		err = -EINVAL;
		goto bail;
	}
	rsp = msg->rxbuf;
	if (!rsp) {
		dev_err(me->dev, "%s: response invoke msg is NULL\n", __func__);
		err = -EINVAL;
		goto bail;
	}
	err = rsp->hdr.result;
	if (err)
		goto bail;

	bufs = inbufs + outbufs;
	handles = REMOTE_SCALARS_INHANDLES(ctx->sc)
		+ REMOTE_SCALARS_OUTHANDLES(ctx->sc);
	total = REMOTE_SCALARS_LENGTH(ctx->sc);

	rpra = (struct virt_fastrpc_buf *)rsp->pra;
	handle = (struct virt_fastrpc_dmahandle *)&rpra[total];
	fdlist = (uint64_t *)&handle[handles];
	payload = (char *)&fdlist[M_FDLIST] + ctx->outbufs_offset;

	for (i = inbufs; i < bufs; i++) {
		if (maps[i]) {
			mutex_lock(&fl->map_mutex);
			fastrpc_mmap_free(fl, maps[i], 0);
			mutex_unlock(&fl->map_mutex);
			maps[i] = NULL;
		} else if (ctx->desc &&
			   ctx->desc[i].type == FASTRPC_BUF_TYPE_INTERNAL) {
			K_COPY_TO_USER(err, 0, lpra[i].buf.pv,
					ctx->desc[i].buf->va, lpra[i].buf.len);
			if (err)
				goto bail;
		} else {
			K_COPY_TO_USER(err, 0, lpra[i].buf.pv,
					payload, rpra[i].len);
			if (err)
				goto bail;
		}
		payload += rpra[i].len;
	}

	mutex_lock(&fl->map_mutex);
	if (total) {
		for (i = 0; i < M_FDLIST; i++) {
			if (!fdlist[i])
				break;
			if (!fastrpc_mmap_find(fl, (int)fdlist[i], 0, 0,
						0, 0, &mmap))
				fastrpc_mmap_free(fl, mmap, 0);
		}
	}
	mutex_unlock(&fl->map_mutex);
bail:
	return err;
}

int fastrpc_internal_invoke(struct fastrpc_file *fl,
			uint32_t mode, struct fastrpc_ioctl_invoke_crc *inv)
{
	struct fastrpc_ioctl_invoke *invoke = &inv->inv;
	struct fastrpc_apps *me = fl->apps;
	struct fastrpc_invoke_ctx *ctx = NULL;
	int err = 0, interrupted = 0;

	VERIFY(err, invoke->handle != FASTRPC_STATIC_HANDLE_KERNEL);
	if (err) {
		dev_err(me->dev, "user application %s trying to send a kernel RPC message to channel %d\n",
			current->comm, fl->domain);
		goto bail;
	}

	VERIFY(err, fl->domain >= 0 && fl->domain < me->num_channels);
	if (err) {
		dev_err(me->dev, "user application %s domain is not set\n",
				current->comm);
		err = -EBADR;
		goto bail;
	}

	VERIFY(err, 0 == context_restore_interrupted(fl, invoke, &ctx));
	if (err)
		goto bail;
	if (ctx)
		goto wait;

	VERIFY(err, 0 == context_alloc(fl, inv, &ctx));
	if (err)
		goto bail;

	VERIFY(err, 0 == get_args(ctx));
	if (err)
		goto bail;

	VERIFY(err, 0 == virt_fastrpc_invoke(fl, ctx));

	if (err)
		goto bail;

wait:
	interrupted = wait_for_completion_interruptible(&ctx->msg->work);
	VERIFY(err, 0 == (err = interrupted));
	if (err)
		goto bail;
	VERIFY(err, 0 == put_args(ctx));
	if (err)
		goto bail;
bail:
	if (ctx && interrupted == -ERESTARTSYS)
		context_save_interrupted(ctx);
	else if (ctx)
		context_free(ctx);

	return err;
}

int fastrpc_internal_munmap(struct fastrpc_file *fl,
				   struct fastrpc_ioctl_munmap *ud)
{
	int err = 0;
	struct fastrpc_apps *me = fl->apps;
	struct fastrpc_mmap *map = NULL;
	struct fastrpc_buf *rbuf = NULL, *free = NULL;
	struct hlist_node *n;

	VERIFY(err, fl->dsp_proc_init == 1);
	if (err) {
		dev_err(me->dev, "%s: user application %s trying to unmap without initialization\n",
			 __func__, current->comm);
		err = -EBADR;
		goto bail;
	}

	spin_lock(&fl->hlock);
	hlist_for_each_entry_safe(rbuf, n, &fl->remote_bufs, hn_rem) {
		if (rbuf->raddr && (rbuf->flags == ADSP_MMAP_ADD_PAGES)) {
			if ((rbuf->raddr == ud->vaddrout) &&
				(rbuf->size == ud->size)) {
				free = rbuf;
				break;
			}
		}
	}
	spin_unlock(&fl->hlock);

	if (free) {
		VERIFY(err, !virt_fastrpc_munmap(fl, free->raddr, free->size));
		if (err)
			goto bail;
		fastrpc_buf_free(rbuf, 0);
		return err;
	}

	mutex_lock(&fl->map_mutex);
	VERIFY(err, !fastrpc_mmap_remove(fl, ud->vaddrout, ud->size, &map));
	mutex_unlock(&fl->map_mutex);
	if (err) {
		dev_err(me->dev, "mapping not found to unmap va 0x%lx, len 0x%x\n",
				ud->vaddrout, (unsigned int)ud->size);
		goto bail;
	}
	VERIFY(err, !virt_fastrpc_munmap(fl, map->raddr, map->size));
	if (err)
		goto bail;
	mutex_lock(&fl->map_mutex);
	fastrpc_mmap_free(fl, map, 0);
	mutex_unlock(&fl->map_mutex);
bail:
	if (err && map) {
		mutex_lock(&fl->map_mutex);
		fastrpc_mmap_add(fl, map);
		mutex_unlock(&fl->map_mutex);
	}
	return err;
}

int fastrpc_internal_munmap_fd(struct fastrpc_file *fl,
				struct fastrpc_ioctl_munmap_fd *ud)
{
	int err = 0;
	struct fastrpc_apps *me = fl->apps;
	struct fastrpc_mmap *map = NULL;

	VERIFY(err, fl->dsp_proc_init == 1);
	if (err) {
		dev_err(me->dev, "%s: user application %s trying to unmap without initialization\n",
			__func__, current->comm);
		err = -EBADR;
		goto bail;
	}
	mutex_lock(&fl->map_mutex);
	if (fastrpc_mmap_find(fl, ud->fd, ud->va, ud->len, 0, 0, &map)) {
		dev_err(me->dev, "mapping not found to unmap fd 0x%x, va 0x%lx, len 0x%x\n",
			ud->fd, ud->va, (unsigned int)ud->len);
		err = -1;
		mutex_unlock(&fl->map_mutex);
		goto bail;
	}
	if (map)
		fastrpc_mmap_free(fl, map, 0);
	mutex_unlock(&fl->map_mutex);
bail:
	return err;
}

int fastrpc_internal_mmap(struct fastrpc_file *fl,
				 struct fastrpc_ioctl_mmap *ud)
{
	struct fastrpc_apps *me = fl->apps;
	struct fastrpc_mmap *map = NULL;
	struct fastrpc_buf *rbuf = NULL;
	unsigned long dma_attr = 0;
	uintptr_t raddr = 0;
	int err = 0;

	VERIFY(err, fl->dsp_proc_init == 1);
	if (err) {
		dev_err(me->dev, "%s: user application %s trying to map without initialization\n",
			__func__, current->comm);
		err = -EBADR;
		goto bail;
	}
	if (ud->flags == ADSP_MMAP_ADD_PAGES) {
		if (ud->vaddrin) {
			err = -EINVAL;
			dev_err(me->dev, "%s: %s: ERROR: adding user allocated pages is not supported\n",
					current->comm, __func__);
			goto bail;
		}
		dma_attr = DMA_ATTR_NO_KERNEL_MAPPING;
		err = fastrpc_buf_alloc(fl, ud->size, dma_attr, ud->flags,
								1, &rbuf);
		if (err)
			goto bail;
		err = virt_fastrpc_mmap(fl, ud->flags, 0, rbuf->sgt.sgl,
					rbuf->sgt.nents, rbuf->size, &raddr);
		if (err)
			goto bail;
		rbuf->raddr = raddr;
	} else {
		uintptr_t va_to_dsp;

		mutex_lock(&fl->map_mutex);
		VERIFY(err, !fastrpc_mmap_create(fl, ud->fd,
				(uintptr_t)ud->vaddrin, ud->size,
				 ud->flags, &map));
		mutex_unlock(&fl->map_mutex);
		if (err)
			goto bail;

		if (ud->flags == ADSP_MMAP_HEAP_ADDR ||
			ud->flags == ADSP_MMAP_REMOTE_HEAP_ADDR)
			va_to_dsp = 0;
		else
			va_to_dsp = (uintptr_t)map->va;

		VERIFY(err, 0 == virt_fastrpc_mmap(fl, ud->flags, va_to_dsp,
					map->table->sgl, map->table->nents,
					map->size, &raddr));
		if (err)
			goto bail;
		map->raddr = raddr;
	}
	ud->vaddrout = raddr;
 bail:
	if (err && map) {
		mutex_lock(&fl->map_mutex);
		fastrpc_mmap_free(fl, map, 0);
		mutex_unlock(&fl->map_mutex);
	}
	return err;
}

int fastrpc_internal_control(struct fastrpc_file *fl,
					struct fastrpc_ioctl_control *cp)
{
	struct fastrpc_apps *me = fl->apps;
	int err = 0;

	VERIFY(err, !IS_ERR_OR_NULL(fl) && !IS_ERR_OR_NULL(fl->apps));
	if (err)
		goto bail;
	VERIFY(err, !IS_ERR_OR_NULL(cp));
	if (err)
		goto bail;

	switch (cp->req) {
	case FASTRPC_CONTROL_LATENCY:
		if (!(me->has_control)) {
			dev_err(me->dev, "qos setting is not supported\n");
			err = -ENOTTY;
			goto bail;
		}
		virt_fastrpc_control(fl, &cp->lp);
		break;
	case FASTRPC_CONTROL_KALLOC:
		cp->kalloc.kalloc_support = 1;
		break;
	default:
		err = -ENOTTY;
		break;
	}
bail:
	return err;
}

int fastrpc_ioctl_get_info(struct fastrpc_file *fl,
					uint32_t *info)
{
	int err = 0;
	uint32_t domain;

	VERIFY(err, fl != NULL);
	if (err)
		goto bail;
	if (fl->domain == -1) {
		domain = *info;
		VERIFY(err, domain < fl->apps->num_channels);
		if (err)
			goto bail;
		fl->domain = domain;
	}
	*info = 1;
bail:
	return err;
}

int fastrpc_init_process(struct fastrpc_file *fl,
				struct fastrpc_ioctl_init_attrs *uproc)
{
	int err = 0;
	struct fastrpc_ioctl_init *init = &uproc->init;

	if (init->flags == FASTRPC_INIT_ATTACH ||
			init->flags == FASTRPC_INIT_ATTACH_SENSORS) {
		fl->pd = GUEST_OS;
		err = virt_fastrpc_open(fl);
		if (err)
			goto bail;
	} else if (init->flags == FASTRPC_INIT_CREATE) {
		fl->pd = DYNAMIC_PD;
		err = virt_fastrpc_open(fl);
		if (err)
			goto bail;
	} else if (init->flags == FASTRPC_INIT_CREATE_STATIC) {
		fl->pd = STATIC_PD;
		err = virt_fastrpc_open(fl);
		if (err)
			goto bail;
	} else {
		err = -ENOTTY;
		goto bail;
	}
	fl->dsp_proc_init = 1;
bail:
	return err;
}
