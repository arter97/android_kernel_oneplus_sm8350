// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/ipc_logging.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/dma-direction.h>
#include <linux/mhi.h>
#include <linux/mhi_ssr.h>

#define MAX_IPC_LOG_NAME_LEN 22
#define SSR_LOG_PAGES 20

#define SSR_DBG(__msg, ...) \
	ipc_log_string(ssr_info.ipc_log, \
		"[%s]: "__msg, __func__, ##__VA_ARGS__)

struct mhi_ssr_info {
	void	*ipc_log;
	struct	ssr_client_hook *ssr_hook;
	atomic_t	power_status;
	char	ipc_log_name[MAX_IPC_LOG_NAME_LEN];
};

static struct mhi_ssr_info ssr_info;

enum ssr_power_option {
	PCIE_POWER_OFF,
	PCIE_POWER_ON,
	SSR_MAX_POWER_OPTION,
};

static void ssr_sel_power_case(struct ssr_client_hook *ssr_hook,
					u32 powercase)
{
	switch (powercase) {
	case PCIE_POWER_OFF:
		SSR_DBG("SSR_debug:Processing PCIe Link turn off\n");
		(ssr_info.ssr_hook)->ssr_link_power_off(
				(ssr_info.ssr_hook)->priv,
				SSR_HOOK_MDM_CRASH);
		SSR_DBG("SSR_debug:PCIe Link turned off\n");
		break;
	case PCIE_POWER_ON:
		SSR_DBG("SSR_debug:Processing PCIe turn on\n");
		(ssr_info.ssr_hook)->ssr_link_power_on(
				(ssr_info.ssr_hook)->priv,
				SSR_HOOK_MDM_CRASH);
		SSR_DBG("SSR_debug:PCIe Link turned on\n");
		break;
	default:
		SSR_DBG("SSR_debug:Invalid powercase: %d\n", powercase);
		break;
	}
}

static ssize_t ssr_power_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			atomic_read(&ssr_info.power_status));
}

static ssize_t ssr_power_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t count)
{
	u32 powercase = -1;

	if (sysfs_streq(buf, "off")) {
		powercase = PCIE_POWER_OFF;
		SSR_DBG("SSR_debug: POWER_CASE:%d POWER_STATUS:%d\n",
				powercase, ssr_info.power_status);
		atomic_dec(&ssr_info.power_status);
	} else if (sysfs_streq(buf, "on")) {
		powercase = PCIE_POWER_ON;
		SSR_DBG("SSR_debug: POWER_CASE:%d POWER_STATUS:%d\n",
				powercase, ssr_info.power_status);
		atomic_inc(&ssr_info.power_status);
	}

	ssr_sel_power_case(ssr_info.ssr_hook, powercase);
	return count;
}
static DEVICE_ATTR_RW(ssr_power);

static struct attribute *mhi_ssr_attrs[] = {
	&dev_attr_ssr_power.attr,
	NULL,
};

static const struct attribute_group mhi_ssr_group = {
	.attrs = mhi_ssr_attrs,
};

int mhi_ssr_init(struct ssr_client_hook *client_hook)
{
	int ret;
	struct mhi_controller *mhi_cntrl;

	if (IS_ERR_OR_NULL(client_hook))
		return -EINVAL;

	atomic_inc(&(ssr_info.power_status));
	snprintf(ssr_info.ipc_log_name, MAX_IPC_LOG_NAME_LEN, "mhi-ssr-short");
	ssr_info.ipc_log = ipc_log_context_create(
				SSR_LOG_PAGES, ssr_info.ipc_log_name, 0);
	if (!ssr_info.ipc_log) {
		pr_err("%s: Unable to create IPC log context for %s\n",
				__func__, ssr_info.ipc_log_name);
		return -EINVAL;
	}

	ssr_info.ssr_hook = client_hook;
	SSR_DBG("SSR_debug: MHI_Controller Registered\n");
	mhi_cntrl = (ssr_info.ssr_hook)->priv;
	ret = sysfs_create_group(&mhi_cntrl->mhi_dev->dev.kobj, &mhi_ssr_group);

	if (ret) {
		pr_err("SSR_debug: Unable to create sysfs\n");
		ipc_log_context_destroy(ssr_info.ipc_log);
		return ret;
	}

	return 0;
}
