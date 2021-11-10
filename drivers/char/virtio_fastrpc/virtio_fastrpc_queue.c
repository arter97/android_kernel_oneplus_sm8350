// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include "virtio_fastrpc_queue.h"

static void *get_a_tx_buf(struct fastrpc_file *fl)
{
	struct fastrpc_apps *me = fl->apps;
	unsigned int len;
	void *ret;
	unsigned long flags;

	/* support multiple concurrent senders */
	spin_lock_irqsave(&me->svq.vq_lock, flags);
	/*
	 * either pick the next unused tx buffer
	 * (half of our buffers are used for sending messages)
	 */
	if (me->last_sbuf < me->num_bufs / 2)
		ret = me->sbufs + me->buf_size * me->last_sbuf++;
	/* or recycle a used one */
	else
		ret = virtqueue_get_buf(me->svq.vq, &len);
	spin_unlock_irqrestore(&me->svq.vq_lock, flags);
	return ret;
}

struct virt_fastrpc_msg *virt_alloc_msg(struct fastrpc_file *fl, int size)
{
	struct fastrpc_apps *me = fl->apps;
	struct virt_fastrpc_msg *msg;
	void *buf;
	unsigned long flags;
	int i;

	if (size > me->buf_size) {
		dev_err(me->dev, "message is too big (%d)\n", size);
		return NULL;
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return NULL;

	buf = get_a_tx_buf(fl);
	if (!buf) {
		dev_err(me->dev, "can't get tx buffer\n");
		kfree(msg);
		return NULL;
	}

	msg->txbuf = buf;
	init_completion(&msg->work);
	spin_lock_irqsave(&me->msglock, flags);
	for (i = 0; i < FASTRPC_MSG_MAX; i++) {
		if (!me->msgtable[i]) {
			me->msgtable[i] = msg;
			msg->msgid = i;
			break;
		}
	}
	spin_unlock_irqrestore(&me->msglock, flags);

	if (i == FASTRPC_MSG_MAX) {
		dev_err(me->dev, "message queue is full\n");
		kfree(msg);
		return NULL;
	}
	return msg;
}

void virt_free_msg(struct fastrpc_file *fl, struct virt_fastrpc_msg *msg)
{
	struct fastrpc_apps *me = fl->apps;
	unsigned long flags;

	spin_lock_irqsave(&me->msglock, flags);
	if (me->msgtable[msg->msgid] == msg)
		me->msgtable[msg->msgid] = NULL;
	else
		dev_err(me->dev, "can't find msg %d in table\n", msg->msgid);
	spin_unlock_irqrestore(&me->msglock, flags);

	kfree(msg);
}

int virt_fastrpc_invoke(struct fastrpc_file *fl, struct fastrpc_invoke_ctx *ctx)
{
	struct fastrpc_apps *me = fl->apps;
	struct virt_fastrpc_msg *msg = ctx->msg;
	struct virt_invoke_msg *vmsg;
	struct scatterlist sg[1];
	unsigned long flags;
	int err = 0;

	if (!msg) {
		dev_err(me->dev, "%s: ctx msg is NULL\n", __func__);
		err = -EINVAL;
		goto bail;
	}
	vmsg = (struct virt_invoke_msg *)msg->txbuf;
	if (!vmsg) {
		dev_err(me->dev, "%s: invoke msg is NULL\n", __func__);
		err = -EINVAL;
		goto bail;
	}

	sg_init_one(sg, vmsg, ctx->size);

	spin_lock_irqsave(&me->svq.vq_lock, flags);
	err = virtqueue_add_outbuf(me->svq.vq, sg, 1, vmsg, GFP_KERNEL);
	if (err) {
		dev_err(me->dev, "%s: fail to add output buffer\n", __func__);
		spin_unlock_irqrestore(&me->svq.vq_lock, flags);
		goto bail;
	}

	virtqueue_kick(me->svq.vq);
	spin_unlock_irqrestore(&me->svq.vq_lock, flags);
bail:
	return err;
}


int virt_fastrpc_munmap(struct fastrpc_file *fl, uintptr_t raddr,
				size_t size)
{
	struct fastrpc_apps *me = fl->apps;
	struct virt_munmap_msg *vmsg, *rsp = NULL;
	struct virt_fastrpc_msg *msg;
	struct scatterlist sg[1];
	int err;
	unsigned long flags;

	msg = virt_alloc_msg(fl, sizeof(*vmsg));
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_munmap_msg *)msg->txbuf;
	vmsg->hdr.pid = fl->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = fl->cid;
	vmsg->hdr.cmd = VIRTIO_FASTRPC_CMD_MUNMAP;
	vmsg->hdr.len = sizeof(*vmsg);
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;
	vmsg->vdsp = raddr;
	vmsg->size = size;
	sg_init_one(sg, vmsg, sizeof(*vmsg));

	spin_lock_irqsave(&me->svq.vq_lock, flags);
	err = virtqueue_add_outbuf(me->svq.vq, sg, 1, vmsg, GFP_KERNEL);
	if (err) {
		dev_err(me->dev, "%s: fail to add output buffer\n", __func__);
		spin_unlock_irqrestore(&me->svq.vq_lock, flags);
		goto bail;
	}

	virtqueue_kick(me->svq.vq);
	spin_unlock_irqrestore(&me->svq.vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	if (!rsp)
		goto bail;

	err = rsp->hdr.result;
bail:
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
	virt_free_msg(fl, msg);

	return err;
}

int virt_fastrpc_mmap(struct fastrpc_file *fl, uint32_t flags,
			uintptr_t va, struct scatterlist *table,
			unsigned int nents, size_t size, uintptr_t *raddr)
{
	struct fastrpc_apps *me = fl->apps;
	struct virt_mmap_msg *vmsg, *rsp = NULL;
	struct virt_fastrpc_msg *msg;
	struct virt_fastrpc_buf *sgbuf;
	struct scatterlist sg[1];
	int err, sgbuf_size, total_size;
	struct scatterlist *sgl = NULL;
	int sgl_index = 0;
	unsigned long int_flags;

	sgbuf_size = nents * sizeof(*sgbuf);
	total_size = sizeof(*vmsg) + sgbuf_size;

	msg = virt_alloc_msg(fl, total_size);
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_mmap_msg *)msg->txbuf;
	vmsg->hdr.pid = fl->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = fl->cid;
	vmsg->hdr.cmd = VIRTIO_FASTRPC_CMD_MMAP;
	vmsg->hdr.len = total_size;
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;
	vmsg->flags = flags;
	vmsg->size = size;
	vmsg->vapp = va;
	vmsg->vdsp = 0;
	vmsg->nents = nents;
	sgbuf = vmsg->sgl;

	for_each_sg(table, sgl, nents, sgl_index) {
		if (sg_dma_len(sgl)) {
			sgbuf[sgl_index].pv = sg_dma_address(sgl);
			sgbuf[sgl_index].len = sg_dma_len(sgl);
		} else {
			sgbuf[sgl_index].pv = page_to_phys(sg_page(sgl));
			sgbuf[sgl_index].len = sgl->length;
		}
	}

	sg_init_one(sg, vmsg, total_size);

	spin_lock_irqsave(&me->svq.vq_lock, int_flags);
	err = virtqueue_add_outbuf(me->svq.vq, sg, 1, vmsg, GFP_KERNEL);
	if (err) {
		dev_err(me->dev, "%s: fail to add output buffer\n", __func__);
		spin_unlock_irqrestore(&me->svq.vq_lock, int_flags);
		goto bail;
	}

	virtqueue_kick(me->svq.vq);
	spin_unlock_irqrestore(&me->svq.vq_lock, int_flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	if (!rsp)
		goto bail;

	err = rsp->hdr.result;
	if (err)
		goto bail;
	*raddr = (uintptr_t)rsp->vdsp;
bail:
	if (rsp) {
		sg_init_one(sg, rsp, me->buf_size);

		spin_lock_irqsave(&me->rvq.vq_lock, int_flags);
		/* add the buffer back to the remote processor's virtqueue */
		if (virtqueue_add_inbuf(me->rvq.vq, sg, 1, rsp, GFP_KERNEL))
			dev_err(me->dev,
				"%s: fail to add input buffer\n", __func__);
		else
			virtqueue_kick(me->rvq.vq);
		spin_unlock_irqrestore(&me->rvq.vq_lock, int_flags);
	}
	virt_free_msg(fl, msg);

	return err;
}

int virt_fastrpc_control(struct fastrpc_file *fl,
				struct fastrpc_ctrl_latency *lp)
{
	struct fastrpc_apps *me = fl->apps;
	struct virt_control_msg *vmsg, *rsp = NULL;
	struct virt_fastrpc_msg *msg;
	struct scatterlist sg[1];
	int err;
	unsigned long flags;

	msg = virt_alloc_msg(fl, sizeof(*vmsg));
	if (!msg)
		return -ENOMEM;

	vmsg = (struct virt_control_msg *)msg->txbuf;
	vmsg->hdr.pid = fl->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = fl->cid;
	vmsg->hdr.cmd = VIRTIO_FASTRPC_CMD_CONTROL;
	vmsg->hdr.len = sizeof(*vmsg);
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;
	vmsg->enable = lp->enable;
	vmsg->latency = lp->latency;
	sg_init_one(sg, vmsg, sizeof(*vmsg));

	spin_lock_irqsave(&me->svq.vq_lock, flags);
	err = virtqueue_add_outbuf(me->svq.vq, sg, 1, vmsg, GFP_KERNEL);
	if (err) {
		dev_err(me->dev, "%s: fail to add output buffer\n", __func__);
		spin_unlock_irqrestore(&me->svq.vq_lock, flags);
		goto bail;
	}

	virtqueue_kick(me->svq.vq);
	spin_unlock_irqrestore(&me->svq.vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	if (!rsp)
		goto bail;

	err = rsp->hdr.result;
bail:
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
	virt_free_msg(fl, msg);

	return err;
}

int virt_fastrpc_open(struct fastrpc_file *fl)
{
	struct fastrpc_apps *me = fl->apps;
	struct virt_open_msg *vmsg, *rsp = NULL;
	struct virt_fastrpc_msg *msg;
	struct scatterlist sg[1];
	int err;
	unsigned long flags;

	msg = virt_alloc_msg(fl, sizeof(*vmsg));
	if (!msg) {
		dev_err(me->dev, "%s: no memory\n", __func__);
		return -ENOMEM;
	}

	vmsg = (struct virt_open_msg *)msg->txbuf;
	vmsg->hdr.pid = fl->tgid;
	vmsg->hdr.tid = current->pid;
	vmsg->hdr.cid = -1;
	vmsg->hdr.cmd = VIRTIO_FASTRPC_CMD_OPEN;
	vmsg->hdr.len = sizeof(*vmsg);
	vmsg->hdr.msgid = msg->msgid;
	vmsg->hdr.result = 0xffffffff;
	vmsg->domain = fl->domain;
	vmsg->pd = fl->pd;
	sg_init_one(sg, vmsg, sizeof(*vmsg));

	spin_lock_irqsave(&me->svq.vq_lock, flags);
	err = virtqueue_add_outbuf(me->svq.vq, sg, 1, vmsg, GFP_KERNEL);
	if (err) {
		dev_err(me->dev, "%s: fail to add output buffer\n", __func__);
		spin_unlock_irqrestore(&me->svq.vq_lock, flags);
		goto bail;
	}

	virtqueue_kick(me->svq.vq);
	spin_unlock_irqrestore(&me->svq.vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	if (!rsp)
		goto bail;

	err = rsp->hdr.result;
	if (err)
		goto bail;
	if (rsp->hdr.cid < 0) {
		dev_err(me->dev, "channel id %d is invalid\n", rsp->hdr.cid);
		err = -EINVAL;
		goto bail;
	}
	fl->cid = rsp->hdr.cid;
bail:
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
	virt_free_msg(fl, msg);

	return err;
}

int virt_fastrpc_close(struct fastrpc_file *fl)
{
	struct fastrpc_apps *me = fl->apps;
	struct virt_msg_hdr *vmsg, *rsp = NULL;
	struct virt_fastrpc_msg *msg;
	struct scatterlist sg[1];
	int err;
	unsigned long flags;

	if (fl->cid < 0) {
		dev_err(me->dev, "channel id %d is invalid\n", fl->cid);
		return -EINVAL;
	}

	msg = virt_alloc_msg(fl, sizeof(*vmsg));
	if (!msg) {
		dev_err(me->dev, "%s: no memory\n", __func__);
		return -ENOMEM;
	}

	vmsg = (struct virt_msg_hdr *)msg->txbuf;
	vmsg->pid = fl->tgid;
	vmsg->tid = current->pid;
	vmsg->cid = fl->cid;
	vmsg->cmd = VIRTIO_FASTRPC_CMD_CLOSE;
	vmsg->len = sizeof(*vmsg);
	vmsg->msgid = msg->msgid;
	vmsg->result = 0xffffffff;
	sg_init_one(sg, vmsg, sizeof(*vmsg));

	spin_lock_irqsave(&me->svq.vq_lock, flags);
	err = virtqueue_add_outbuf(me->svq.vq, sg, 1, vmsg, GFP_KERNEL);
	if (err) {
		dev_err(me->dev, "%s: fail to add output buffer\n", __func__);
		spin_unlock_irqrestore(&me->svq.vq_lock, flags);
		goto bail;
	}

	virtqueue_kick(me->svq.vq);
	spin_unlock_irqrestore(&me->svq.vq_lock, flags);

	wait_for_completion(&msg->work);

	rsp = msg->rxbuf;
	if (!rsp)
		goto bail;

	err = rsp->result;
bail:
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
	virt_free_msg(fl, msg);

	return err;
}
