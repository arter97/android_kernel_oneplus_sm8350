/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __VIRTIO_FASTRPC_CORE_H__
#define __VIRTIO_FASTRPC_CORE_H__

#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/virtio.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/completion.h>
#include "../adsprpc_compat.h"
#include "../adsprpc_shared.h"

#define ADSP_MMAP_HEAP_ADDR		4
#define ADSP_MMAP_REMOTE_HEAP_ADDR	8
#define ADSP_MMAP_ADD_PAGES		0x1000

#define FASTRPC_MSG_MAX			256

#define K_COPY_FROM_USER(err, kernel, dst, src, size) \
	do {\
		if (!(kernel))\
			VERIFY(err, 0 == copy_from_user((dst),\
			(void const __user *)(src),\
							(size)));\
		else\
			memmove((dst), (src), (size));\
	} while (0)

#define K_COPY_TO_USER(err, kernel, dst, src, size) \
	do {\
		if (!(kernel))\
			VERIFY(err, 0 == copy_to_user((void __user *)(dst),\
						(src), (size)));\
		else\
			memmove((dst), (src), (size));\
	} while (0)


struct virt_fastrpc_vq {
	/* protects vq */
	spinlock_t vq_lock;
	struct virtqueue *vq;
};

struct fastrpc_ctx_lst {
	struct hlist_head pending;
	struct hlist_head interrupted;
};

struct virt_fastrpc_msg {
	struct completion work;
	u16 msgid;
	void *txbuf;
	void *rxbuf;
};

struct fastrpc_apps {
	struct virtio_device *vdev;
	struct virt_fastrpc_vq rvq;
	struct virt_fastrpc_vq svq;
	void *rbufs;
	void *sbufs;
	unsigned int order;
	unsigned int num_bufs;
	unsigned int buf_size;
	unsigned int num_channels;
	int last_sbuf;

	bool has_invoke_attr;
	bool has_invoke_crc;
	bool has_mmap;
	bool has_control;

	struct device *dev;
	struct cdev cdev;
	struct class *class;
	dev_t dev_no;

	spinlock_t msglock;
	struct virt_fastrpc_msg *msgtable[FASTRPC_MSG_MAX];
};

struct fastrpc_file {
	spinlock_t hlock;
	struct hlist_head maps;
	struct hlist_head cached_bufs;
	struct hlist_head remote_bufs;
	struct fastrpc_ctx_lst clst;
	uint32_t mode;
	int tgid;
	int cid;
	int domain;
	int pd;
	int file_close;
	int dsp_proc_init;
	struct fastrpc_apps *apps;
	struct dentry *debugfs_file;
	struct mutex map_mutex;
	/* Identifies the device (MINOR_NUM_DEV / MINOR_NUM_SECURE_DEV) */
	int dev_minor;
	char *debug_buf;
};

struct fastrpc_invoke_ctx {
	struct virt_fastrpc_msg *msg;
	size_t size;
	struct fastrpc_buf_desc *desc;
	struct hlist_node hn;
	struct fastrpc_mmap **maps;
	remote_arg_t *lpra;
	int *fds;
	unsigned int outbufs_offset;
	unsigned int *attrs;
	struct fastrpc_file *fl;
	int pid;
	int tgid;
	uint32_t sc;
	uint32_t handle;
};

struct virt_msg_hdr {
	u32 pid;	/* GVM pid */
	u32 tid;	/* GVM tid */
	s32 cid;	/* channel id connected to DSP */
	u32 cmd;	/* command type */
	u32 len;	/* command length */
	u16 msgid;	/* unique message id */
	u32 result;	/* message return value */
} __packed;

struct fastrpc_file *fastrpc_file_alloc(void);
int fastrpc_file_free(struct fastrpc_file *fl);
int fastrpc_ioctl_get_info(struct fastrpc_file *fl,
					uint32_t *info);
int fastrpc_internal_control(struct fastrpc_file *fl,
					struct fastrpc_ioctl_control *cp);
int fastrpc_init_process(struct fastrpc_file *fl,
				struct fastrpc_ioctl_init_attrs *uproc);
int fastrpc_internal_mmap(struct fastrpc_file *fl,
				 struct fastrpc_ioctl_mmap *ud);
int fastrpc_internal_munmap(struct fastrpc_file *fl,
				   struct fastrpc_ioctl_munmap *ud);
int fastrpc_internal_munmap_fd(struct fastrpc_file *fl,
				struct fastrpc_ioctl_munmap_fd *ud);
int fastrpc_internal_invoke(struct fastrpc_file *fl,
		uint32_t mode, struct fastrpc_ioctl_invoke_crc *inv);
#endif /*__VIRTIO_FASTRPC_CORE_H__*/
