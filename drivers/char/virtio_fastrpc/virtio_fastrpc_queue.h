/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __VIRTIO_FASTRPC_QUEUE_H__
#define __VIRTIO_FASTRPC_QUEUE_H__

#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/completion.h>
#include "virtio_fastrpc_core.h"

struct virt_msg_hdr {
	u32 pid;	/* GVM pid */
	u32 tid;	/* GVM tid */
	s32 cid;	/* channel id connected to DSP */
	u32 cmd;	/* command type */
	u32 len;	/* command length */
	u16 msgid;	/* unique message id */
	u32 result;	/* message return value */
} __packed;

struct virt_fastrpc_msg {
	struct completion work;
	u16 msgid;
	void *txbuf;
	void *rxbuf;
};

struct virt_open_msg {
	struct virt_msg_hdr hdr;	/* virtio fastrpc message header */
	u32 domain;			/* DSP domain id */
	u32 pd;				/* DSP PD */
} __packed;

struct virt_control_msg {
	struct virt_msg_hdr hdr;	/* virtio fastrpc message header */
	u32 enable;			/* latency control enable */
	u32 latency;			/* latency value */
} __packed;

struct virt_invoke_msg {
	struct virt_msg_hdr hdr;	/* virtio fastrpc message header */
	u32 handle;			/* remote handle */
	u32 sc;				/* scalars describing the data */
	struct virt_fastrpc_buf pra[0];	/* remote arguments list */
} __packed;

struct virt_mmap_msg {
	struct virt_msg_hdr hdr;	/* virtio fastrpc message header */
	u32 nents;                      /* number of map entries */
	u32 flags;			/* mmap flags */
	u64 size;			/* mmap length */
	u64 vapp;			/* application virtual address */
	u64 vdsp;			/* dsp address */
	struct virt_fastrpc_buf sgl[0]; /* sg list */
} __packed;

struct virt_munmap_msg {
	struct virt_msg_hdr hdr;	/* virtio fastrpc message header */
	u64 vdsp;			/* dsp address */
	u64 size;			/* mmap length */
} __packed;

int virt_fastrpc_invoke(struct fastrpc_file *fl, struct fastrpc_invoke_ctx *ctx);
int virt_fastrpc_munmap(struct fastrpc_file *fl, uintptr_t raddr,
				size_t size);
int virt_fastrpc_mmap(struct fastrpc_file *fl, uint32_t flags,
			uintptr_t va, struct scatterlist *table,
			unsigned int nents, size_t size, uintptr_t *raddr);
int virt_fastrpc_control(struct fastrpc_file *fl,
				struct fastrpc_ctrl_latency *lp);
int virt_fastrpc_open(struct fastrpc_file *fl);
int virt_fastrpc_close(struct fastrpc_file *fl);
struct virt_fastrpc_msg *virt_alloc_msg(struct fastrpc_file *fl, int size);
void virt_free_msg(struct fastrpc_file *fl, struct virt_fastrpc_msg *msg);
#endif /*__VIRTIO_FASTRPC_QUEUE_H__*/
