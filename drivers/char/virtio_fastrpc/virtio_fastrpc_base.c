// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/virtio_config.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <soc/qcom/subsystem_notif.h>
#include <soc/qcom/subsystem_restart.h>
#include "virtio_fastrpc_core.h"
#include "virtio_fastrpc_mem.h"
#include "virtio_fastrpc_queue.h"

#define VIRTIO_ID_FASTRPC				34
/* indicates remote invoke with buffer attributes is supported */
#define VIRTIO_FASTRPC_F_INVOKE_ATTR			1
/* indicates remote invoke with CRC is supported */
#define VIRTIO_FASTRPC_F_INVOKE_CRC			2
/* indicates remote mmap/munmap is supported */
#define VIRTIO_FASTRPC_F_MMAP				3
/* indicates QOS setting is supported */
#define VIRTIO_FASTRPC_F_CONTROL			4
/* indicates version check is supported */
#define VIRTIO_FASTRPC_F_VERSION			5

#define NUM_CHANNELS			4 /* adsp, mdsp, slpi, cdsp0*/
#define NUM_DEVICES			2 /* adsprpc-smd, adsprpc-smd-secure */
#define MINOR_NUM_DEV			0
#define MINOR_NUM_SECURE_DEV		1

#define INIT_FILELEN_MAX		(2*1024*1024)
#define INIT_MEMLEN_MAX			(8*1024*1024)

#define MAX_FASTRPC_BUF_SIZE		(128*1024)
#define DEBUGFS_SIZE			3072
#define PID_SIZE			10
#define UL_SIZE				25

/*
 * Increase only for critical patches which must be consistent with BE,
 * if not, the basic function is broken.
 */
#define FE_MAJOR_VER 0x1
/* Increase for new features. */
#define FE_MINOR_VER 0x0
#define FE_VERSION (FE_MAJOR_VER << 16 | FE_MINOR_VER)
#define BE_MAJOR_VER(ver) (((ver) >> 16) & 0xffff)

struct virtio_fastrpc_config {
	u32 version;
} __packed;


static struct fastrpc_apps gfa;

static struct dentry *debugfs_root;

static ssize_t fastrpc_debugfs_read(struct file *filp, char __user *buffer,
					 size_t count, loff_t *position)
{
	struct fastrpc_file *fl = filp->private_data;
	struct fastrpc_mmap *map = NULL;
	struct fastrpc_buf *buf = NULL;
	struct hlist_node *n;
	struct fastrpc_invoke_ctx *ictx = NULL;
	char *fileinfo = NULL;
	unsigned int len = 0;
	int err = 0;
	char single_line[UL_SIZE] = "----------------";
	char title[UL_SIZE] = "=========================";

	fileinfo = kzalloc(DEBUGFS_SIZE, GFP_KERNEL);
	if (!fileinfo)
		goto bail;
	if (fl) {
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"\n%s %d\n", "CHANNEL =", fl->domain);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n======%s %s %s======\n", title,
			" LIST OF BUFS ", title);
		spin_lock(&fl->hlock);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-19s|%-19s|%-19s\n",
			"virt", "phys", "size");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n", single_line, single_line,
			single_line, single_line, single_line);
		hlist_for_each_entry_safe(buf, n, &fl->cached_bufs, hn) {
			len += scnprintf(fileinfo + len,
				DEBUGFS_SIZE - len,
				"0x%-17lX|0x%-17llX|%-19zu\n",
				(unsigned long)buf->va,
				(uint64_t)page_to_phys(buf->pages[0]),
				buf->size);
		}

		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n%s %s %s\n", title,
			" LIST OF PENDING CONTEXTS ", title);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-10s|%-10s|%-10s|%-20s\n",
			"sc", "pid", "tgid", "size", "handle");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n", single_line, single_line,
			single_line, single_line, single_line);
		hlist_for_each_entry_safe(ictx, n, &fl->clst.pending, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"0x%-18X|%-10d|%-10d|%-10zu|0x%-20X\n\n",
				ictx->sc, ictx->pid, ictx->tgid,
				ictx->size, ictx->handle);
		}

		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n%s %s %s\n", title,
			" LIST OF INTERRUPTED CONTEXTS ", title);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-10s|%-10s|%-10s|%-20s\n",
			"sc", "pid", "tgid", "size", "handle");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n", single_line, single_line,
			single_line, single_line, single_line);
		hlist_for_each_entry_safe(ictx, n, &fl->clst.interrupted, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"0x%-18X|%-10d|%-10d|%-10zu|0x%-20X\n\n",
				ictx->sc, ictx->pid, ictx->tgid,
				ictx->size, ictx->handle);
		}
		spin_unlock(&fl->hlock);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"\n=======%s %s %s======\n", title,
			" LIST OF MAPS ", title);
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%-20s|%-20s|%-20s\n", "va", "phys", "size");
		len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
			"%s%s%s%s%s\n",
			single_line, single_line, single_line,
			single_line, single_line);
		mutex_lock(&fl->map_mutex);
		hlist_for_each_entry_safe(map, n, &fl->maps, hn) {
			len += scnprintf(fileinfo + len, DEBUGFS_SIZE - len,
				"0x%-20lX|0x%-20llX|0x%-20zu\n\n",
				map->va, map->phys,
				map->size);
		}
		mutex_unlock(&fl->map_mutex);

	}

	if (len > DEBUGFS_SIZE)
		len = DEBUGFS_SIZE;
	err = simple_read_from_buffer(buffer, count, position, fileinfo, len);
	kfree(fileinfo);
bail:
	return err;
}

static const struct file_operations debugfs_fops = {
	.open = simple_open,
	.read = fastrpc_debugfs_read,
};

static inline void get_fastrpc_ioctl_mmap_64(
			struct fastrpc_ioctl_mmap_64 *mmap64,
			struct fastrpc_ioctl_mmap *immap)
{
	immap->fd = mmap64->fd;
	immap->flags = mmap64->flags;
	immap->vaddrin = (uintptr_t)mmap64->vaddrin;
	immap->size = mmap64->size;
}

static inline void put_fastrpc_ioctl_mmap_64(
			struct fastrpc_ioctl_mmap_64 *mmap64,
			struct fastrpc_ioctl_mmap *immap)
{
	mmap64->vaddrout = (uint64_t)immap->vaddrout;
}

static inline void get_fastrpc_ioctl_munmap_64(
			struct fastrpc_ioctl_munmap_64 *munmap64,
			struct fastrpc_ioctl_munmap *imunmap)
{
	imunmap->vaddrout = (uintptr_t)munmap64->vaddrout;
	imunmap->size = munmap64->size;
}

static long fastrpc_ioctl(struct file *file, unsigned int ioctl_num,
				 unsigned long ioctl_param)
{
	union {
		struct fastrpc_ioctl_invoke_crc inv;
		struct fastrpc_ioctl_mmap mmap;
		struct fastrpc_ioctl_mmap_64 mmap64;
		struct fastrpc_ioctl_munmap munmap;
		struct fastrpc_ioctl_munmap_64 munmap64;
		struct fastrpc_ioctl_munmap_fd munmap_fd;
		struct fastrpc_ioctl_init_attrs init;
		struct fastrpc_ioctl_control cp;
	} p;
	union {
		struct fastrpc_ioctl_mmap mmap;
		struct fastrpc_ioctl_munmap munmap;
	} i;
	void *param = (char *)ioctl_param;
	struct fastrpc_file *fl = (struct fastrpc_file *)file->private_data;
	struct fastrpc_apps *me = &gfa;
	int size = 0, err = 0;
	uint32_t info;

	p.inv.fds = NULL;
	p.inv.attrs = NULL;
	p.inv.crc = NULL;

	spin_lock(&fl->hlock);
	if (fl->file_close == 1) {
		err = -EBADF;
		dev_warn(me->dev, "fastrpc_device_release is happening, So not sending any new requests to DSP\n");
		spin_unlock(&fl->hlock);
		goto bail;
	}
	spin_unlock(&fl->hlock);

	switch (ioctl_num) {
	case FASTRPC_IOCTL_INVOKE:
		size = sizeof(struct fastrpc_ioctl_invoke);
		fallthrough;
	case FASTRPC_IOCTL_INVOKE_FD:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_invoke_fd);
		fallthrough;
	case FASTRPC_IOCTL_INVOKE_ATTRS:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_invoke_attrs);
		fallthrough;
	case FASTRPC_IOCTL_INVOKE_CRC:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_invoke_crc);

		K_COPY_FROM_USER(err, 0, &p.inv, param, size);
		if (err)
			goto bail;

		if (p.inv.attrs && !(me->has_invoke_attr)) {
			dev_err(me->dev, "invoke attr is not supported\n");
			err = -ENOTTY;
			goto bail;
		}
		if (p.inv.crc && !(me->has_invoke_crc)) {
			dev_err(me->dev, "invoke crc is not supported\n");
			err = -ENOTTY;
			goto bail;
		}

		VERIFY(err, 0 == (err = fastrpc_internal_invoke(fl, fl->mode,
								&p.inv)));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_MMAP:
		if (!me->has_mmap) {
			dev_err(me->dev, "mmap is not supported\n");
			err = -ENOTTY;
			goto bail;
		}

		K_COPY_FROM_USER(err, 0, &p.mmap, param,
						sizeof(p.mmap));
		if (err)
			goto bail;
		VERIFY(err, 0 == (err = fastrpc_internal_mmap(fl, &p.mmap)));
		if (err)
			goto bail;
		K_COPY_TO_USER(err, 0, param, &p.mmap, sizeof(p.mmap));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_MUNMAP:
		if (!(me->has_mmap)) {
			dev_err(me->dev, "munmap is not supported\n");
			err = -ENOTTY;
			goto bail;
		}

		K_COPY_FROM_USER(err, 0, &p.munmap, param,
						sizeof(p.munmap));
		if (err)
			goto bail;
		VERIFY(err, 0 == (err = fastrpc_internal_munmap(fl,
							&p.munmap)));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_MMAP_64:
		if (!(me->has_mmap)) {
			dev_err(me->dev, "mmap is not supported\n");
			err = -ENOTTY;
			goto bail;
		}

		K_COPY_FROM_USER(err, 0, &p.mmap64, param,
						sizeof(p.mmap64));
		if (err)
			goto bail;
		get_fastrpc_ioctl_mmap_64(&p.mmap64, &i.mmap);
		VERIFY(err, 0 == (err = fastrpc_internal_mmap(fl, &i.mmap)));
		if (err)
			goto bail;
		put_fastrpc_ioctl_mmap_64(&p.mmap64, &i.mmap);
		K_COPY_TO_USER(err, 0, param, &p.mmap64, sizeof(p.mmap64));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_MUNMAP_64:
		if (!(me->has_mmap)) {
			dev_err(me->dev, "munmap is not supported\n");
			err = -ENOTTY;
			goto bail;
		}

		K_COPY_FROM_USER(err, 0, &p.munmap64, param,
						sizeof(p.munmap64));
		if (err)
			goto bail;
		get_fastrpc_ioctl_munmap_64(&p.munmap64, &i.munmap);
		VERIFY(err, 0 == (err = fastrpc_internal_munmap(fl,
							&i.munmap)));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_MUNMAP_FD:
		K_COPY_FROM_USER(err, 0, &p.munmap_fd, param,
			sizeof(p.munmap_fd));
		if (err)
			goto bail;
		VERIFY(err, 0 == (err = fastrpc_internal_munmap_fd(fl,
			&p.munmap_fd)));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_SETMODE:
		switch ((uint32_t)ioctl_param) {
		case FASTRPC_MODE_PARALLEL:
		case FASTRPC_MODE_SERIAL:
			fl->mode = (uint32_t)ioctl_param;
			break;
		case FASTRPC_MODE_SESSION:
			err = -ENOTTY;
			dev_err(me->dev, "session mode is not supported\n");
			break;
		default:
			err = -ENOTTY;
			break;
		}
		break;
	case FASTRPC_IOCTL_CONTROL:
		K_COPY_FROM_USER(err, 0, &p.cp, param,
				sizeof(p.cp));
		if (err)
			goto bail;
		VERIFY(err, 0 == (err = fastrpc_internal_control(fl, &p.cp)));
		if (err)
			goto bail;
		if (p.cp.req == FASTRPC_CONTROL_KALLOC) {
			K_COPY_TO_USER(err, 0, param, &p.cp, sizeof(p.cp));
			if (err)
				goto bail;
		}
		break;
	case FASTRPC_IOCTL_GETINFO:
		K_COPY_FROM_USER(err, 0, &info, param, sizeof(info));
		if (err)
			goto bail;
		VERIFY(err, 0 == (err = fastrpc_ioctl_get_info(fl, &info)));
		if (err)
			goto bail;
		K_COPY_TO_USER(err, 0, param, &info, sizeof(info));
		if (err)
			goto bail;
		break;
	case FASTRPC_IOCTL_INIT:
		p.init.attrs = 0;
		p.init.siglen = 0;
		size = sizeof(struct fastrpc_ioctl_init);
		fallthrough;
	case FASTRPC_IOCTL_INIT_ATTRS:
		if (!size)
			size = sizeof(struct fastrpc_ioctl_init_attrs);
		K_COPY_FROM_USER(err, 0, &p.init, param, size);
		if (err)
			goto bail;
		VERIFY(err, p.init.init.filelen >= 0 &&
			p.init.init.filelen < INIT_FILELEN_MAX);
		if (err)
			goto bail;
		VERIFY(err, p.init.init.memlen >= 0 &&
			p.init.init.memlen < INIT_MEMLEN_MAX);
		if (err)
			goto bail;
		VERIFY(err, 0 == (err = fastrpc_init_process(fl, &p.init)));
		if (err)
			goto bail;
		break;

	default:
		err = -ENOTTY;
		dev_info(me->dev, "bad ioctl: %d\n", ioctl_num);
		break;
	}
 bail:
	return err;
}

static int fastrpc_open(struct inode *inode, struct file *filp)
{
	int err = 0;
	struct dentry *debugfs_file;
	struct fastrpc_file *fl = NULL;
	struct fastrpc_apps *me = &gfa;
	char strpid[PID_SIZE];
	int buf_size = 0;

	/*
	 * Indicates the device node opened
	 * MINOR_NUM_DEV or MINOR_NUM_SECURE_DEV
	 */
	int dev_minor = MINOR(inode->i_rdev);

	VERIFY(err, ((dev_minor == MINOR_NUM_DEV) ||
			(dev_minor == MINOR_NUM_SECURE_DEV)));
	if (err) {
		dev_err(me->dev, "Invalid dev minor num %d\n", dev_minor);
		return err;
	}

	VERIFY(err, NULL != (fl = fastrpc_file_alloc()));
	if (err) {
		dev_err(me->dev, "Allocate fastrpc_file failed %d\n", dev_minor);
		return err;
	}

	snprintf(strpid, PID_SIZE, "%d", current->pid);
	buf_size = strlen(current->comm) + strlen("_") + strlen(strpid) + 1;
	VERIFY(err, NULL != (fl->debug_buf = kzalloc(buf_size, GFP_KERNEL)));
	if (err) {
		kfree(fl);
		return err;
	}
	snprintf(fl->debug_buf, UL_SIZE, "%.10s%s%d",
			current->comm, "_", current->pid);
	debugfs_file = debugfs_create_file(fl->debug_buf, 0644, debugfs_root,
						fl, &debugfs_fops);

	fl->dev_minor = dev_minor;
	fl->apps = me;
	if (debugfs_file != NULL)
		fl->debugfs_file = debugfs_file;

	filp->private_data = fl;
	return 0;
}

static int fastrpc_release(struct inode *inode, struct file *file)
{
	struct fastrpc_file *fl = (struct fastrpc_file *)file->private_data;

	if (fl) {
		debugfs_remove(fl->debugfs_file);
		fastrpc_file_free(fl);
		file->private_data = NULL;
	}
	return 0;
}

static const struct file_operations fops = {
	.open = fastrpc_open,
	.release = fastrpc_release,
	.unlocked_ioctl = fastrpc_ioctl,
	.compat_ioctl = compat_fastrpc_device_ioctl,
};

static int recv_single(struct virt_msg_hdr *rsp, unsigned int len)
{
	struct fastrpc_apps *me = &gfa;
	struct virt_fastrpc_msg *msg;

	if (len != rsp->len) {
		dev_err(me->dev, "msg %u len mismatch,expected %u but %d found\n",
				rsp->cmd, rsp->len, len);
		return -EINVAL;
	}
	spin_lock(&me->msglock);
	msg = me->msgtable[rsp->msgid];
	spin_unlock(&me->msglock);

	if (!msg) {
		dev_err(me->dev, "msg %u already free in table[%u]\n",
				rsp->cmd, rsp->msgid);
		return -EINVAL;
	}
	msg->rxbuf = (void *)rsp;

	complete(&msg->work);
	return 0;
}

static void recv_done(struct virtqueue *rvq)
{

	struct fastrpc_apps *me = &gfa;
	struct virt_msg_hdr *rsp;
	unsigned int len, msgs_received = 0;
	int err;
	unsigned long flags;

	spin_lock_irqsave(&me->rvq.vq_lock, flags);
	rsp = virtqueue_get_buf(rvq, &len);
	if (!rsp) {
		spin_unlock_irqrestore(&me->rvq.vq_lock, flags);
		dev_err(me->dev, "incoming signal, but no used buffer\n");
		return;
	}
	spin_unlock_irqrestore(&me->rvq.vq_lock, flags);

	while (rsp) {
		err = recv_single(rsp, len);
		if (err)
			break;

		msgs_received++;

		spin_lock_irqsave(&me->rvq.vq_lock, flags);
		rsp = virtqueue_get_buf(rvq, &len);
		spin_unlock_irqrestore(&me->rvq.vq_lock, flags);
	}
}

static void virt_init_vq(struct virt_fastrpc_vq *fastrpc_vq,
				struct virtqueue *vq)
{
	spin_lock_init(&fastrpc_vq->vq_lock);
	fastrpc_vq->vq = vq;
}

static int init_vqs(struct fastrpc_apps *me)
{
	struct virtqueue *vqs[2];
	static const char * const names[] = { "output", "input" };
	vq_callback_t *cbs[] = { NULL, recv_done };
	size_t total_buf_space;
	void *bufs;
	int err;

	err = virtio_find_vqs(me->vdev, 2, vqs, cbs, names, NULL);
	if (err)
		return err;

	virt_init_vq(&me->svq, vqs[0]);
	virt_init_vq(&me->rvq, vqs[1]);

	/* we expect symmetric tx/rx vrings */
	WARN_ON(virtqueue_get_vring_size(me->rvq.vq) !=
			virtqueue_get_vring_size(me->svq.vq));
	me->num_bufs = virtqueue_get_vring_size(me->rvq.vq) * 2;

	me->buf_size = MAX_FASTRPC_BUF_SIZE;
	total_buf_space = me->num_bufs * me->buf_size;
	me->order = get_order(total_buf_space);
	bufs = (void *)__get_free_pages(GFP_KERNEL,
				me->order);
	if (!bufs) {
		err = -ENOMEM;
		goto vqs_del;
	}

	/* half of the buffers is dedicated for RX */
	me->rbufs = bufs;

	/* and half is dedicated for TX */
	me->sbufs = bufs + total_buf_space / 2;
	return 0;

vqs_del:
	me->vdev->config->del_vqs(me->vdev);
	return err;
}

static int virt_fastrpc_probe(struct virtio_device *vdev)
{
	struct fastrpc_apps *me = &gfa;
	struct device *dev = NULL;
	struct device *secure_dev = NULL;
	struct virtio_fastrpc_config config;
	int err, i;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	memset(&config, 0x0, sizeof(config));
	if (virtio_has_feature(vdev, VIRTIO_FASTRPC_F_VERSION)) {
		virtio_cread(vdev, struct virtio_fastrpc_config, version, &config.version);
		if (BE_MAJOR_VER(config.version) != FE_MAJOR_VER) {
			dev_err(&vdev->dev, "vdev major version does not match 0x%x:0x%x\n",
					FE_VERSION, config.version);
			return -ENODEV;
		}
	}
	dev_info(&vdev->dev, "virtio fastrpc version 0x%x:0x%x\n",
			FE_VERSION, config.version);

	memset(me, 0, sizeof(*me));
	spin_lock_init(&me->msglock);

	if (virtio_has_feature(vdev, VIRTIO_FASTRPC_F_INVOKE_ATTR))
		me->has_invoke_attr = true;

	if (virtio_has_feature(vdev, VIRTIO_FASTRPC_F_INVOKE_CRC))
		me->has_invoke_crc = true;

	if (virtio_has_feature(vdev, VIRTIO_FASTRPC_F_MMAP))
		me->has_mmap = true;

	if (virtio_has_feature(vdev, VIRTIO_FASTRPC_F_CONTROL))
		me->has_control = true;

	vdev->priv = me;
	me->vdev = vdev;
	me->dev = vdev->dev.parent;

	err = init_vqs(me);
	if (err) {
		dev_err(&vdev->dev, "failed to initialized virtqueue\n");
		return err;
	}

	if (of_get_property(me->dev->of_node, "qcom,domain_num", NULL) != NULL) {
		err = of_property_read_u32(me->dev->of_node, "qcom,domain_num",
					&me->num_channels);
		if (err) {
			dev_err(&vdev->dev, "failed to read domain_num %d\n", err);
			goto alloc_chrdev_bail;
		}
	} else {
		dev_dbg(&vdev->dev, "set domain_num to default value\n");
		me->num_channels = NUM_CHANNELS;
	}

	debugfs_root = debugfs_create_dir("adsprpc", NULL);
	err = alloc_chrdev_region(&me->dev_no, 0, me->num_channels, DEVICE_NAME);
	if (err)
		goto alloc_chrdev_bail;

	cdev_init(&me->cdev, &fops);
	me->cdev.owner = THIS_MODULE;
	err = cdev_add(&me->cdev, MKDEV(MAJOR(me->dev_no), 0), NUM_DEVICES);
	if (err)
		goto cdev_init_bail;

	me->class = class_create(THIS_MODULE, "fastrpc");
	if (IS_ERR(me->class))
		goto class_create_bail;

	/*
	 * Create devices and register with sysfs
	 * Create first device with minor number 0
	 */
	dev = device_create(me->class, NULL,
				MKDEV(MAJOR(me->dev_no), MINOR_NUM_DEV),
				NULL, DEVICE_NAME);
	if (IS_ERR_OR_NULL(dev))
		goto device_create_bail;

	/* Create secure device with minor number for secure device */
	secure_dev = device_create(me->class, NULL,
				MKDEV(MAJOR(me->dev_no), MINOR_NUM_SECURE_DEV),
				NULL, DEVICE_NAME_SECURE);
	if (IS_ERR_OR_NULL(secure_dev))
		goto device_create_bail;

	virtio_device_ready(vdev);

	/* set up the receive buffers */
	for (i = 0; i < me->num_bufs / 2; i++) {
		struct scatterlist sg;
		void *cpu_addr = me->rbufs + i * me->buf_size;

		sg_init_one(&sg, cpu_addr, me->buf_size);
		err = virtqueue_add_inbuf(me->rvq.vq, &sg, 1, cpu_addr,
				GFP_KERNEL);
		WARN_ON(err); /* sanity check; this can't really happen */
	}

	/* suppress "tx-complete" interrupts */
	virtqueue_disable_cb(me->svq.vq);

	virtqueue_enable_cb(me->rvq.vq);
	virtqueue_kick(me->rvq.vq);

	dev_info(&vdev->dev, "Registered virtio fastrpc device\n");
	return 0;
device_create_bail:
	if (!IS_ERR_OR_NULL(dev))
		device_destroy(me->class, MKDEV(MAJOR(me->dev_no),
						MINOR_NUM_DEV));
	if (!IS_ERR_OR_NULL(secure_dev))
		device_destroy(me->class, MKDEV(MAJOR(me->dev_no),
						MINOR_NUM_SECURE_DEV));
	class_destroy(me->class);
class_create_bail:
	cdev_del(&me->cdev);
cdev_init_bail:
	unregister_chrdev_region(me->dev_no, me->num_channels);
alloc_chrdev_bail:
	vdev->config->del_vqs(vdev);
	return err;
}

static void virt_fastrpc_remove(struct virtio_device *vdev)
{
	struct fastrpc_apps *me = &gfa;

	device_destroy(me->class, MKDEV(MAJOR(me->dev_no), MINOR_NUM_DEV));
	device_destroy(me->class, MKDEV(MAJOR(me->dev_no),
					MINOR_NUM_SECURE_DEV));
	class_destroy(me->class);
	cdev_del(&me->cdev);
	unregister_chrdev_region(me->dev_no, me->num_channels);
	debugfs_remove_recursive(debugfs_root);

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
	free_pages((unsigned long)me->rbufs, me->order);
}

const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_FASTRPC, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_FASTRPC_F_INVOKE_ATTR,
	VIRTIO_FASTRPC_F_INVOKE_CRC,
	VIRTIO_FASTRPC_F_MMAP,
	VIRTIO_FASTRPC_F_CONTROL,
	VIRTIO_FASTRPC_F_VERSION,
};

static struct virtio_driver virtio_fastrpc_driver = {
	.feature_table		= features,
	.feature_table_size	= ARRAY_SIZE(features),
	.driver.name		= KBUILD_MODNAME,
	.driver.owner		= THIS_MODULE,
	.id_table		= id_table,
	.probe			= virt_fastrpc_probe,
	.remove			= virt_fastrpc_remove,
};

static int __init virtio_fastrpc_init(void)
{
	return register_virtio_driver(&virtio_fastrpc_driver);
}

static void __exit virtio_fastrpc_exit(void)
{
	unregister_virtio_driver(&virtio_fastrpc_driver);
}
module_init(virtio_fastrpc_init);
module_exit(virtio_fastrpc_exit);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio fastrpc driver");
MODULE_LICENSE("GPL v2");
