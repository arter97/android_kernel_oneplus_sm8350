// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/reboot.h>
#include <soc/qcom/subsystem_restart.h>
#include <soc/qcom/ramdump.h>
#include <soc/qcom/subsystem_notif.h>
#include <linux/highmem.h>
#include <linux/qtee_shmbridge.h>
#include <linux/suspend.h>

#include "peripheral-loader.h"
#include "../../misc/qseecom_kernel.h"
#include "pil_slate_intf.h"

#define INVALID_GPIO	-1
#define NUM_GPIOS	4
#define SECURE_APP	"slateapp"
#define desc_to_data(d)	container_of(d, struct pil_slate_data, desc)
#define subsys_to_data(d) container_of(d, struct pil_slate_data, subsys_desc)
#define SLATE_RAMDUMP_SZ	(0x00102000*8)
#define SLATE_MINIRAMDUMP_SZ	(0x400*40)
#define SLATE_VERSION_SZ	32
#define SLATE_CRASH_IN_TWM	-2
#define SLATE_RAMDUMP	3
#define SLATE_MINIDUMP	4

/**
 * struct pil_slate_data
 * @qseecom_handle: handle of TZ app
 * @slate_queue: private queue to schedule worker threads for bottom half
 * @restart_work: work struct for executing ssr
 * @reboot_blk: notification block for reboot event
 * @subsys_desc: subsystem descriptor
 * @subsys: subsystem device pointer
 * @gpios: array to hold all gpio handle
 * @desc: PIL descriptor
 * @address_fw: address where firmware binaries loaded
 * @ramdump_dev: ramdump device pointer
 * @minidump_dev: minidump device pointer
 * @size_fw: size of slate firmware binaries
 * @status_irq: irq to indicate slate status
 * @app_status: status of tz app loading
 * @is_ready: Is SLATE chip up
 * @err_ready: The error ready signal
 */

struct pil_slate_data {
	struct qseecom_handle *qseecom_handle;
	struct workqueue_struct *slate_queue;
	struct work_struct restart_work;
	struct notifier_block reboot_blk;
	struct subsys_desc subsys_desc;
	struct subsys_device *subsys;
	unsigned int gpios[NUM_GPIOS];
	int status_irq;
	struct pil_desc desc;
	phys_addr_t address_fw;
	void *ramdump_dev;
	void *minidump_dev;
	u32 cmd_status;
	size_t size_fw;
	int app_status;
	bool is_ready;
	struct completion err_ready;
};

static irqreturn_t slate_status_change(int irq, void *dev_id);
struct mutex cmdsync_lock;

/**
 * slate_app_shutdown_notify() - Toggle AP2SLATE err fatal gpio when
 * called by SSR framework.
 * @subsys: struct containing private SLATE data.
 *
 * Return: none.
 */
static void slate_app_shutdown_notify(const struct subsys_desc *subsys)
{
	struct pil_slate_data *slate_data = subsys_to_data(subsys);

	/* Disable irq if already SLATE is up */
	if (slate_data->is_ready) {
		disable_irq(slate_data->status_irq);
		slate_data->is_ready = false;
	}
	/* Toggle AP2SLATE err fatal gpio here to inform apps err fatal event */
	if (gpio_is_valid(slate_data->gpios[1])) {
		pr_debug("Sending Apps shutdown signal\n");
		gpio_set_value(slate_data->gpios[1], 1);
	}
}

/**
 * slate_app_reboot_notify() - Toggle AP2SLATE err fatal gpio.
 * @nb: struct containing private SLATE data.
 * @unused: unused as no data is passed.
 *
 * Return: NOTIFY_DONE indicating success.
 */
static int slate_app_reboot_notify(struct notifier_block *nb,
		unsigned long code, void *unused)
{
	struct pil_slate_data *slate_data = container_of(nb,
					struct pil_slate_data, reboot_blk);

	/* Disable irq if already SLATE is up */
	if (slate_data->is_ready) {
		disable_irq(slate_data->status_irq);
		slate_data->is_ready = false;
	}
	/* Toggle AP2SLATE err fatal gpio here to inform apps err fatal event */
	if (gpio_is_valid(slate_data->gpios[1])) {
		pr_debug("Sending reboot signal\n");
		gpio_set_value(slate_data->gpios[1], 1);
	}
	return NOTIFY_DONE;
}

/**
 * get_cmd_rsp_buffers() - Function sets cmd & rsp buffer pointers and
 *                         aligns buffer lengths
 * @hdl:	index of qseecom_handle
 * @cmd:	req buffer - set to qseecom_handle.sbuf
 * @cmd_len:	ptr to req buffer len
 * @rsp:	rsp buffer - set to qseecom_handle.sbuf + offset
 * @rsp_len:	ptr to rsp buffer len
 *
 * Return: Success always .
 */
static int get_cmd_rsp_buffers(struct qseecom_handle *handle, void **cmd,
			uint32_t *cmd_len, void **rsp, uint32_t *rsp_len)
{
	*cmd = handle->sbuf;
	if (*cmd_len & QSEECOM_ALIGN_MASK)
		*cmd_len = QSEECOM_ALIGN(*cmd_len);

	*rsp = handle->sbuf + *cmd_len;
	if (*rsp_len & QSEECOM_ALIGN_MASK)
		*rsp_len = QSEECOM_ALIGN(*rsp_len);

	return 0;
}

/**
 * pil_load_slate_tzapp() - Called to load TZ app.
 * @pbd: struct containing private SLATE data.
 *
 * Return: 0 on success. Error code on failure.
 */
static int pil_load_slate_tzapp(struct pil_slate_data *pbd)
{
	int rc;

	/* return success if already loaded */
	if (pbd->qseecom_handle && !pbd->app_status)
		return 0;
	/* Load the APP */
	rc = qseecom_start_app(&pbd->qseecom_handle, SECURE_APP, SZ_4K);
	if (rc < 0) {
		dev_err(pbd->desc.dev, "SLATE TZ app load failure\n");
		pbd->app_status = RESULT_FAILURE;
		return -EIO;
	}
	pbd->app_status = RESULT_SUCCESS;
	return 0;
}

/**
 * slatepil_tzapp_comm() - Function called to communicate with TZ APP.
 * @pbd: struct containing private SLATE data.
 * @req: struct containing command and parameters.
 *
 * Return: 0 on success. Error code on failure.
 */
static long slatepil_tzapp_comm(struct pil_slate_data *pbd,
				struct tzapp_slate_req *req)
{
	struct tzapp_slate_req *slate_tz_req;
	struct tzapp_slate_rsp *slate_tz_rsp;
	int rc, req_len, rsp_len;
	unsigned char *ascii;

	/* Fill command structure */
	req_len = sizeof(struct tzapp_slate_req);
	rsp_len = sizeof(struct tzapp_slate_rsp);
	mutex_lock(&cmdsync_lock);
	rc = get_cmd_rsp_buffers(pbd->qseecom_handle,
		(void **)&slate_tz_req, &req_len,
		(void **)&slate_tz_rsp, &rsp_len);
	if (rc)
		goto end;

	slate_tz_req->tzapp_slate_cmd = req->tzapp_slate_cmd;
	slate_tz_req->address_fw = req->address_fw;
	slate_tz_req->size_fw = req->size_fw;
	rc = qseecom_send_command(pbd->qseecom_handle,
		(void *)slate_tz_req, req_len, (void *)slate_tz_rsp, rsp_len);
	mutex_unlock(&cmdsync_lock);
	pr_debug("SLATE PIL qseecom returned with value 0x%x and status 0x%x\n",
		rc, slate_tz_rsp->status);
	if (rc || slate_tz_rsp->status)
		pbd->cmd_status = slate_tz_rsp->status;
	else
		pbd->cmd_status = 0;
	/* if last command sent was SLATE_VERSION print the version*/
	if (req->tzapp_slate_cmd == SLATEPIL_GET_SLATE_VERSION) {
		int i;

		pr_info("SLATE FW version\n");
		for (i = 0; i < slate_tz_rsp->slate_info_len; i++) {
			pr_info("Version 0x%x\n", slate_tz_rsp->slate_info[i]);
			ascii = (unsigned char *)&slate_tz_rsp->slate_info[i];
		}
	}
end:
	return rc;
}

/**
 * wait_for_err_ready() - Called in power_up to wait for error ready.
 * Signal waiting function.
 * @slate_data: SLATE PIL private structure.
 *
 * Return: 0 on success. Error code on failure.
 */
static int wait_for_err_ready(struct pil_slate_data *slate_data)
{
	int ret;

	if ((!slate_data->status_irq))
		return 0;

	ret = wait_for_completion_timeout(&slate_data->err_ready,
			msecs_to_jiffies(10000));
	if (!ret) {
		pr_err("[%s]: Error ready timed out\n", slate_data->desc.name);
		return -ETIMEDOUT;
	}
	return 0;
}

/**
 * slate_powerup_notify() - Called by SSR framework on userspace invocation.
 * does load tz app and call peripheral loader.
 * @subsys: struct containing private SLATE data.
 *
 * Return: 0 indicating success. Error code on failure.
 */
static int slate_powerup_notify(const struct subsys_desc *subsys)
{
	bool value;
	struct pil_slate_data *slate_data = subsys_to_data(subsys);
	int ret = 0;

	init_completion(&slate_data->err_ready);
	if (!slate_data->qseecom_handle) {
		ret = pil_load_slate_tzapp(slate_data);
		if (ret) {
			dev_err(slate_data->desc.dev,
				"%s: SLATE TZ app load failure\n",
				__func__);
			return ret;
		}
	}
	pr_debug("slateapp loaded\n");
	slate_data->desc.fw_name = subsys->fw_name;
	value = gpio_get_value(slate_data->gpios[0]);
	if (!value) {
		/* Enable status and err fatal irqs */
		enable_irq(slate_data->status_irq);
		ret = pil_boot(&slate_data->desc);
		if (ret) {
			dev_err(slate_data->desc.dev,
				"%s: SLATE PIL Boot failed\n",
				 __func__);
			return ret;
		}
		ret = wait_for_err_ready(slate_data);
		if (ret) {
			dev_err(slate_data->desc.dev,
				"[%s:%d]: Timed out waiting for error ready: %s!\n",
				current->comm, current->pid, slate_data->desc.name);
			return ret;
		}
	}
	return ret;
}

/**
 * slate_powerup() - Called by SSR framework on userspace invocation.
 * does load tz app and call peripheral loader.
 * @subsys: struct containing private SLATE data.
 *
 * Return: 0 on success. Error code on failure.
 */
static int slate_powerup(const struct subsys_desc *subsys)
{
	struct pil_slate_data *slate_data = subsys_to_data(subsys);
	int ret;


	init_completion(&slate_data->err_ready);
	if (!slate_data->qseecom_handle) {
		ret = pil_load_slate_tzapp(slate_data);
		if (ret) {
			dev_err(slate_data->desc.dev,
				"%s: SLATE TZ app load failure\n",
				__func__);
			return ret;
		}
	}
	pr_debug("slateapp loaded\n");
	slate_data->desc.fw_name = subsys->fw_name;

	/* Enable status and err fatal irqs */
	enable_irq(slate_data->status_irq);
	ret = pil_boot(&slate_data->desc);
	if (ret) {
		dev_err(slate_data->desc.dev,
			"%s: SLATE PIL Boot failed\n", __func__);
		return ret;
	}
	ret = wait_for_err_ready(slate_data);
	if (ret) {
		dev_err(slate_data->desc.dev,
			"[%s:%d]: Timed out waiting for error ready: %s!\n",
			current->comm, current->pid, slate_data->desc.name);
		return ret;
	}
	return ret;
}

/**
 * slate_shutdown() - Called by SSR framework on userspace invocation.
 * disable status interrupt to avoid spurious signal during PRM exit.
 * @subsys: struct containing private SLATE data.
 * @force_stop: unused
 *
 * Return: always success
 */
static int slate_shutdown(const struct subsys_desc *subsys, bool force_stop)
{
	struct pil_slate_data *slate_data = subsys_to_data(subsys);

	if (slate_data->is_ready) {
		disable_irq(slate_data->status_irq);
		slate_data->is_ready = false;
	}
	return 0;
}

static int slate_shutdown_trusted(struct pil_desc *pil)
{
	struct pil_slate_data *slate_data = desc_to_data(pil);
	struct tzapp_slate_req slate_tz_req;
	int ret;

	slate_tz_req.tzapp_slate_cmd = SLATEPIL_SHUTDOWN;

	ret = slatepil_tzapp_comm(slate_data, &slate_tz_req);
	if (ret || slate_data->cmd_status) {
		pr_err("Slate pil shutdown failed\n");
		return ret;
	}

	if (slate_data->is_ready) {
		disable_irq(slate_data->status_irq);
		slate_data->is_ready = false;
	}
	return ret;
}

/**
 * slate_auth_metadata() - Called by Peripheral loader framework
 * send command to tz app for authentication of metadata.
 * @pil: pil descriptor.
 * @metadata: metadata load address
 * @size: size of metadata
 *
 * Return: 0 on success. Error code on failure.
 */
static int slate_auth_metadata(struct pil_desc *pil,
	const u8 *metadata, size_t size)
{
	struct pil_slate_data *slate_data = desc_to_data(pil);
	struct tzapp_slate_req slate_tz_req;
	struct qtee_shm shm;
	int ret;

	ret = qtee_shmbridge_allocate_shm(size, &shm);
	if (ret)
		pr_err("Shmbridge memory allocation failed\n");

	/* Make sure there are no mappings in PKMAP and fixmap */
	kmap_flush_unused();

	memcpy(shm.vaddr, metadata, size);

	slate_tz_req.tzapp_slate_cmd = SLATEPIL_AUTH_MDT;
	slate_tz_req.address_fw = shm.paddr;
	slate_tz_req.size_fw = size;

	ret = slatepil_tzapp_comm(slate_data, &slate_tz_req);
	if (ret || slate_data->cmd_status) {
		dev_err(pil->dev,
			"%s: SLATEPIL_AUTH_MDT qseecom call failed\n",
				__func__);
		return slate_data->cmd_status;
	}

	qtee_shmbridge_free_shm(&shm);
	pr_debug("SLATE MDT Authenticated\n");
	return 0;
}

/**
 * slate_get_firmware_addr() - Called by Peripheral loader framework
 * to get address and size of slate firmware binaries.
 * @pil: pil descriptor.
 * @addr: fw load address
 * @size: size of fw
 *
 * Return: 0 on success.
 */
static int slate_get_firmware_addr(struct pil_desc *pil,
					phys_addr_t addr, size_t size)
{
	struct pil_slate_data *slate_data = desc_to_data(pil);

	slate_data->address_fw = addr;
	slate_data->size_fw = size;
	pr_debug("SLATE PIL loads firmware blobs at 0x%x with size 0x%x\n",
		addr, size);
	return 0;
}

static int slate_get_version(const struct subsys_desc *subsys)
{
	struct pil_slate_data *slate_data = subsys_to_data(subsys);
	struct pil_desc desc = slate_data->desc;
	struct tzapp_slate_req slate_tz_req;
	int ret;

	desc.attrs = 0;
	desc.attrs |= DMA_ATTR_SKIP_ZEROING;

	slate_tz_req.tzapp_slate_cmd = SLATEPIL_GET_SLATE_VERSION;

	ret = slatepil_tzapp_comm(slate_data, &slate_tz_req);
	if (ret || slate_data->cmd_status) {
		dev_err(desc.dev, "%s: SLATE PIL get SLATE version failed error %d\n",
			__func__, slate_data->cmd_status);
		return slate_data->cmd_status;
	}

	return 0;
}

/**
 * slate_auth_and_xfer() - Called by Peripheral loader framework
 * to signal tz app to authenticate and boot slate chip.
 * @pil: pil descriptor.
 *
 * Return: 0 on success. Error code on failure.
 */
static int slate_auth_and_xfer(struct pil_desc *pil)
{
	struct pil_slate_data *slate_data = desc_to_data(pil);
	struct tzapp_slate_req slate_tz_req;
	uint32_t ns_vmids[] = {VMID_HLOS};
	uint32_t ns_vm_perms[] = {PERM_READ | PERM_WRITE};
	u64 shm_bridge_handle;
	int ret;

	ret = qtee_shmbridge_register(slate_data->address_fw, slate_data->size_fw,
		ns_vmids, ns_vm_perms, 1, PERM_READ|PERM_WRITE,
		&shm_bridge_handle);

	if (ret) {
		pr_err("Failed to create shm bridge with physical address %p size  %ld ret=%d\n",
			slate_data->address_fw, (long)slate_data->size_fw, ret);
		pil_free_memory(&slate_data->desc);
		return ret;
	}

	slate_tz_req.tzapp_slate_cmd = SLATEPIL_IMAGE_LOAD;
	slate_tz_req.address_fw = slate_data->address_fw;
	slate_tz_req.size_fw = slate_data->size_fw;

	ret = slatepil_tzapp_comm(slate_data, &slate_tz_req);
	if (slate_data->cmd_status == SLATE_CRASH_IN_TWM) {
		/* Do ramdump and resend boot cmd */

		slate_data->subsys_desc.ramdump(true, &slate_data->subsys_desc);
		slate_tz_req.tzapp_slate_cmd = SLATEPIL_DLOAD_CONT;
		ret = slatepil_tzapp_comm(slate_data, &slate_tz_req);
	}
	if (ret || slate_data->cmd_status) {
		dev_err(pil->dev,
			"%s: SLATEPIL_IMAGE_LOAD qseecom call failed\n",
			__func__);
		qtee_shmbridge_deregister(shm_bridge_handle);
		pil_free_memory(&slate_data->desc);
		return slate_data->cmd_status;
	}
	ret = slate_get_version(&slate_data->subsys_desc);
	/* SLATE Transfer of image is complete, free up the memory */
	pr_debug("SLATE Firmware authentication and transfer done\n");
	qtee_shmbridge_deregister(shm_bridge_handle);
	pil_free_memory(&slate_data->desc);
	return 0;
}

/**
 * slate_ramdump() - Called by SSR framework to save dump of SLATE internal
 * memory, SLATE PIL does allocate region from dynamic memory and pass this
 * region to tz to dump memory content of SLATE.
 * @subsys: subsystem descriptor.
 *
 * Return: 0 on success. Error code on failure.
 */
static int slate_ramdump(int enable, const struct subsys_desc *subsys)
{
	struct pil_slate_data *slate_data = subsys_to_data(subsys);
	struct pil_desc desc = slate_data->desc;
	struct ramdump_segment *ramdump_segments;
	struct tzapp_slate_req slate_tz_req;
	uint32_t ns_vmids[] = {VMID_HLOS};
	uint32_t ns_vm_perms[] = {PERM_READ | PERM_WRITE};
	u64 shm_bridge_handle;
	phys_addr_t start_addr;
	void *region;
	int ret;
	struct device dev;
	unsigned long size = SLATE_RAMDUMP_SZ;
	uint32_t dump_info;

	dev.dma_ops = NULL;
	arch_setup_dma_ops(&dev, 0, 0, NULL, 0);

	desc.attrs = 0;
	desc.attrs |= DMA_ATTR_SKIP_ZEROING;
	slate_tz_req.tzapp_slate_cmd = SLATEPIL_DUMPINFO;
	if (!slate_data->qseecom_handle) {
		ret = pil_load_slate_tzapp(slate_data);
		if (ret) {
			dev_err(slate_data->desc.dev,
			"%s: SLATE TZ app load failure\n",
			 __func__);
		return ret;
		}
	}

	ret = slatepil_tzapp_comm(slate_data, &slate_tz_req);
	dump_info = slate_data->cmd_status;
	if (slate_data->cmd_status == SLATE_RAMDUMP)
		size = SLATE_RAMDUMP_SZ;
	else if (slate_data->cmd_status == SLATE_MINIDUMP)
		size = SLATE_MINIRAMDUMP_SZ;
	else if (ret || slate_data->cmd_status) {
		dev_dbg(desc.dev, "%s: SLATE PIL ramdump collection failed\n",
			 __func__);
		return slate_data->cmd_status;
	}
	region = dma_alloc_attrs(desc.dev, size,
				&start_addr, GFP_KERNEL, desc.attrs);
	if (region == NULL) {
		dev_dbg(desc.dev,
			"SLATE PIL failure to allocate ramdump region of size %zx\n",
			size);
		return -ENOMEM;
	}

	ret = qtee_shmbridge_register(start_addr, size,
		ns_vmids, ns_vm_perms, 1, PERM_READ|PERM_WRITE,
		&shm_bridge_handle);

	if (ret) {
		pr_err("Failed to create shm bridge with physical address %p size  %ld ret=%d\n",
		start_addr, (long)size, ret);
		dma_free_attrs(desc.dev, size, region,
			start_addr, desc.attrs);
		return ret;
	}

	ramdump_segments = kcalloc(1, sizeof(*ramdump_segments), GFP_KERNEL);
	if (!ramdump_segments)
		return -ENOMEM;
	slate_tz_req.tzapp_slate_cmd = SLATEPIL_RAMDUMP;
	slate_tz_req.address_fw = start_addr;
	slate_tz_req.size_fw = size;

	ret = slatepil_tzapp_comm(slate_data, &slate_tz_req);

	ramdump_segments->address = start_addr;
	ramdump_segments->size = size;
	ramdump_segments->v_address = region;
	if (slate_data->cmd_status == 0) {
		if (dump_info == SLATE_RAMDUMP)
			do_ramdump(slate_data->ramdump_dev, ramdump_segments, 1);
		else if (dump_info == SLATE_MINIDUMP)
			do_ramdump(slate_data->minidump_dev, ramdump_segments, 1);
	} else  if (ret || slate_data->cmd_status) {
		dev_dbg(desc.dev, "%s: SLATE PIL ramdump collection failed\n", __func__);
		return slate_data->cmd_status;
	}

	kfree(ramdump_segments);
	qtee_shmbridge_deregister(shm_bridge_handle);
	dma_free_attrs(desc.dev, size, region,
		       start_addr, desc.attrs);
	return 0;
}

static struct pil_reset_ops pil_ops_trusted = {
	.init_image = slate_auth_metadata,
	.mem_setup =  slate_get_firmware_addr,
	.auth_and_reset = slate_auth_and_xfer,
	.shutdown = slate_shutdown_trusted,
	.proxy_vote = NULL,
	.proxy_unvote = NULL,
};

/**
 * slate_restart_work() - scheduled by interrupt handler to carry
 * out ssr sequence
 * @work: work struct.
 *
 * Return: none.
 */
static void slate_restart_work(struct work_struct *work)
{
	struct pil_slate_data *drvdata =
		container_of(work, struct pil_slate_data, restart_work);
		subsystem_restart_dev(drvdata->subsys);
}

static irqreturn_t slate_status_change(int irq, void *dev_id)
{
	bool value;
	struct pil_slate_data *drvdata = (struct pil_slate_data *)dev_id;
	struct tzapp_slate_req slate_tz_req;
	int ret;

	if (!drvdata)
		return IRQ_HANDLED;

	value = gpio_get_value(drvdata->gpios[0]);
	if (value && !drvdata->is_ready) {
		dev_info(drvdata->desc.dev,
			"SLATE services are up and running: irq state changed 0->1\n");
		drvdata->is_ready = true;
		complete(&drvdata->err_ready);
		slate_tz_req.tzapp_slate_cmd = SLATEPIL_UP_INFO;
		ret = slatepil_tzapp_comm(drvdata, &slate_tz_req);
		if (ret || drvdata->cmd_status) {
			dev_err(drvdata->desc.dev, "%s: SLATE PIL get SLATE version failed error %d\n",
				__func__, drvdata->cmd_status);
			return drvdata->cmd_status;
		}
	} else if (!value && drvdata->is_ready) {
		dev_err(drvdata->desc.dev,
			"SLATE got unexpected reset: irq state changed 1->0\n");
		queue_work(drvdata->slate_queue, &drvdata->restart_work);
	} else {
		dev_err(drvdata->desc.dev,
			"SLATE status irq: unknown status\n");
	}

	return IRQ_HANDLED;
}

/**
 * setup_slate_gpio_irq() - called in probe to configure input/
 * output gpio.
 * @drvdata: private data struct for SLATE.
 *
 * Return: 0 on success. Error code on failure.
 */
static int setup_slate_gpio_irq(struct platform_device *pdev,
					struct pil_slate_data *drvdata)
{
	int ret = -1;
	int irq, i;

	if (gpio_request(drvdata->gpios[0], "SLATE2AP_STATUS")) {
		dev_err(&pdev->dev,
			"%s Failed to configure SLATE2AP_STATUS gpio\n",
				__func__);
		goto err;
	}
	gpio_direction_input(drvdata->gpios[0]);
	/* SLATE2AP STATUS IRQ */
	irq = gpio_to_irq(drvdata->gpios[0]);
	if (irq < 0) {
		dev_err(&pdev->dev,
		"%s: bad SLATE2AP_STATUS IRQ resource, err = %d\n",
			__func__, irq);
		goto err;
	}

	drvdata->status_irq = irq;
	ret = devm_request_threaded_irq(drvdata->desc.dev, drvdata->status_irq,
		NULL, slate_status_change,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"slate2ap_status", drvdata);

	if (ret < 0) {
		dev_err(drvdata->desc.dev,
			"%s: SLATE2AP_STATUS IRQ#%d re registration failed, err=%d\n",
			__func__, drvdata->status_irq, ret);
		return ret;
	}

	ret = irq_set_irq_wake(drvdata->status_irq, true);
	if (ret < 0) {
		dev_err(drvdata->desc.dev,
			"%s: SLATE2AP_STATUS IRQ#%d set wakeup capable failed, err=%d\n",
			__func__, drvdata->status_irq, ret);
		return ret;
	}

	if (gpio_request(drvdata->gpios[1], "AP2SLATE_STATUS")) {
		dev_err(&pdev->dev,
			"%s Failed to configure AP2SLATE_STATUS gpio\n",
				__func__);
		goto err;
	}
	/*
	 * Put status gpio in default high state which will
	 * make transition to low on any sudden reset case of msm
	 */
	gpio_direction_output(drvdata->gpios[1], 1);
	/* Inform SLATE that AP is up */
	gpio_set_value(drvdata->gpios[1], 1);
	return 0;
err:
	for (i = 0; i < NUM_GPIOS; ++i) {
		if (gpio_is_valid(drvdata->gpios[i]))
			gpio_free(drvdata->gpios[i]);
	}
	return ret;
}

/**
 * slate_dt_parse_gpio() - called in probe to parse gpio's
 * @drvdata: private data struct for SLATE.
 *
 * Return: 0 on success. Error code on failure.
 */
static int slate_dt_parse_gpio(struct platform_device *pdev,
				struct pil_slate_data *drvdata)
{
	int i, val;

	for (i = 0; i < NUM_GPIOS; i++)
		drvdata->gpios[i] = INVALID_GPIO;
	val = of_get_named_gpio(pdev->dev.of_node,
					"qcom,slate2ap-status-gpio", 0);
	if (val >= 0)
		drvdata->gpios[0] = val;
	else {
		pr_err("SLATE status gpio not found, error=%d\n", val);
		return -EINVAL;
	}
	val = of_get_named_gpio(pdev->dev.of_node,
					"qcom,ap2slate-status-gpio", 0);
	if (val >= 0)
		drvdata->gpios[1] = val;
	else {
		pr_err("ap2slate status gpio not found, error=%d\n", val);
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_DEEPSLEEP
static int pil_slate_driver_suspend(struct device *dev)
{
	if (mem_sleep_current == PM_SUSPEND_MEM) {
		struct pil_slate_data *slate_data = dev_get_drvdata(dev);

		qseecom_shutdown_app(&slate_data->qseecom_handle);
		slate_data->qseecom_handle = NULL;
	}
	return 0;
}

static int pil_slate_driver_resume(struct device *dev)
{
	return 0;
}
#endif

static int pil_slate_driver_probe(struct platform_device *pdev)
{
	struct pil_slate_data *slate_data;
	int rc;
	char md_node[20];

	slate_data = devm_kzalloc(&pdev->dev, sizeof(*slate_data), GFP_KERNEL);
	if (!slate_data)
		return -ENOMEM;
	platform_set_drvdata(pdev, slate_data);
	rc = of_property_read_string(pdev->dev.of_node,
			"qcom,firmware-name", &slate_data->desc.name);
	if (rc)
		return rc;
	mutex_init(&cmdsync_lock);
	slate_data->desc.dev = &pdev->dev;
	slate_data->desc.owner = THIS_MODULE;
	slate_data->desc.ops = &pil_ops_trusted;
	rc = pil_desc_init(&slate_data->desc);
	if (rc)
		return rc;
	/* Read gpio configuration */
	rc = slate_dt_parse_gpio(pdev, slate_data);
	if (rc)
		return rc;
	rc = setup_slate_gpio_irq(pdev, slate_data);
	if (rc < 0)
		return rc;
	slate_data->subsys_desc.name = slate_data->desc.name;
	slate_data->subsys_desc.owner = THIS_MODULE;
	slate_data->subsys_desc.dev = &pdev->dev;
	slate_data->subsys_desc.shutdown = slate_shutdown;
	slate_data->subsys_desc.powerup_notify = slate_powerup_notify;
	slate_data->subsys_desc.powerup = slate_powerup;
	slate_data->subsys_desc.ramdump = slate_ramdump;
	slate_data->subsys_desc.free_memory = NULL;
	slate_data->subsys_desc.crash_shutdown = slate_app_shutdown_notify;
	scnprintf(md_node, sizeof(md_node), "md_%s", slate_data->subsys_desc.name);
	slate_data->minidump_dev = create_ramdump_device(md_node, &pdev->dev);
	if (!slate_data->minidump_dev) {
		pr_err("%s: Unable to create a %s minidump device.\n",
			 __func__, slate_data->subsys_desc.name);
		rc = -ENOMEM;
		goto err_minidump;
	}
	slate_data->ramdump_dev = create_ramdump_device(slate_data->subsys_desc.name, &pdev->dev);
	if (!slate_data->ramdump_dev) {
		rc = -ENOMEM;
		goto err_ramdump;
	}
	slate_data->subsys = subsys_register(&slate_data->subsys_desc);
	if (IS_ERR(slate_data->subsys)) {
		rc = PTR_ERR(slate_data->subsys);
		goto err_subsys;
	}
	slate_data->reboot_blk.notifier_call = slate_app_reboot_notify;
	register_reboot_notifier(&slate_data->reboot_blk);

	slate_data->slate_queue = alloc_workqueue("slate_queue", 0, 0);
	if (!slate_data->slate_queue) {
		dev_err(&pdev->dev, "could not create slate_queue\n");
		subsys_unregister(slate_data->subsys);
		goto err_subsys;
	}
	INIT_WORK(&slate_data->restart_work, slate_restart_work);
	return 0;
err_subsys:
	destroy_ramdump_device(slate_data->minidump_dev);
	destroy_ramdump_device(slate_data->ramdump_dev);
err_ramdump:
err_minidump:
	pil_desc_release(&slate_data->desc);
	return rc;

}

static int pil_slate_driver_exit(struct platform_device *pdev)
{
	struct pil_slate_data *slate_data = platform_get_drvdata(pdev);

	subsys_unregister(slate_data->subsys);
	destroy_ramdump_device(slate_data->minidump_dev);
	destroy_ramdump_device(slate_data->ramdump_dev);
	pil_desc_release(&slate_data->desc);

	return 0;
}

static const struct dev_pm_ops pil_slate_pm_ops = {
#ifdef CONFIG_DEEPSLEEP
	.suspend = pil_slate_driver_suspend,
	.resume = pil_slate_driver_resume,
#endif
};

const struct of_device_id pil_slate_match_table[] = {
	{.compatible = "qcom,pil-slate"},
	{}
};

static struct platform_driver pil_slate_driver = {
	.probe = pil_slate_driver_probe,
	.remove = pil_slate_driver_exit,
	.driver = {
		.name = "subsys-pil-slate",
		.of_match_table = pil_slate_match_table,
		.pm = &pil_slate_pm_ops,
	},
};

static int __init pil_slate_init(void)
{
	return platform_driver_register(&pil_slate_driver);
}
module_init(pil_slate_init);

static void __exit pil_slate_exit(void)
{
	platform_driver_unregister(&pil_slate_driver);
}
module_exit(pil_slate_exit);

MODULE_DESCRIPTION("Support for booting QTI Slate SoC");
MODULE_LICENSE("GPL v2");
