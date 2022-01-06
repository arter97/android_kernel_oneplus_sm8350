// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include "virtio_fastrpc_queue.h"

void *get_a_tx_buf(struct fastrpc_file *fl)
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

int fastrpc_txbuf_send(struct fastrpc_file *fl, void *data, unsigned int len)
{
	struct fastrpc_apps *me = fl->apps;
	struct scatterlist sg[1];
	unsigned long flags;
	int err = 0;

	sg_init_one(sg, data, len);

	spin_lock_irqsave(&me->svq.vq_lock, flags);
	err = virtqueue_add_outbuf(me->svq.vq, sg, 1, data, GFP_KERNEL);
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

void fastrpc_rxbuf_send(struct fastrpc_file *fl, void *data, unsigned int len)
{
	struct fastrpc_apps *me = fl->apps;
	struct scatterlist sg[1];
	unsigned long flags;
	int err = 0;

	sg_init_one(sg, data, len);

	spin_lock_irqsave(&me->rvq.vq_lock, flags);
	/* add the buffer back to the remote processor's virtqueue */
	err = virtqueue_add_inbuf(me->rvq.vq, sg, 1, data, GFP_KERNEL);
	if (err) {
		dev_err(me->dev,
			"%s: fail to add input buffer\n", __func__);
		spin_unlock_irqrestore(&me->rvq.vq_lock, flags);
		return;
	}
	virtqueue_kick(me->rvq.vq);
	spin_unlock_irqrestore(&me->rvq.vq_lock, flags);
}
