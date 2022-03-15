// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/genhd.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#define PATH_SIZE	32
#define SLOT_STR_LENGTH 3
#define MAX_STR_SIZE 255

static char active_slot[SLOT_STR_LENGTH];
static char backup_slot[SLOT_STR_LENGTH];
static int dp_enabled;
static char final_name[MAX_STR_SIZE];

static void update_firmware_node_status(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "android,vendor");
	if (node) {
		static struct property newstatus = {
			.name = "status",
			.value = "disabled",
			.length = sizeof("disabled"),
		};

		of_update_property(node, &newstatus);
		of_node_put(node);
		return;
	}

	pr_err("rename-devices: android,vendor is missing\n");
}

static int __init set_slot_suffix(char *str)
{
	if (str) {
		strlcpy(active_slot, str, SLOT_STR_LENGTH);
		strcmp(active_slot, "_a") ?
		strlcpy(backup_slot, "_a", SLOT_STR_LENGTH) :
		strlcpy(backup_slot, "_b", SLOT_STR_LENGTH);
		dp_enabled = 1;
	}
	return 1;
}
__setup("androidboot.slot_suffix=", set_slot_suffix);

static void get_slot_updated_name(char *name)
{
	int length = strlen(name);
	static int update_state;

	if ((!update_state) && dp_enabled) {
		update_firmware_node_status();
		update_state++;
	}

	memset(final_name, '\0', MAX_STR_SIZE);
	strlcpy(final_name, name, MAX_STR_SIZE);
	if (dp_enabled && (final_name[length-2] == '_')) {
		if (final_name[length-1] == 'a')
			final_name[length-1] = active_slot[1];
		else if (final_name[length-1] == 'b')
			final_name[length-1] = backup_slot[1];
	}
}

static void rename_net_device_name(struct device_node *np)
{
	struct device_node *net_node = np;
	int index = 0, err = 0;
	const char *src, *dst;
	struct net_device *net_dev = NULL;

	if (net_node) {
		while (!of_property_read_string_index(net_node, "actual-dev", index,
			&src) && !of_property_read_string_index(net_node,
				"rename-dev", index, &dst)) {
			if (rtnl_trylock()) {
				net_dev = dev_get_by_name(&init_net, src);
				if (net_dev) {
					err = dev_change_name(net_dev, dst);
					if (err != 0)
						pr_err("rename-devices: Net rename err = %d\n",
								 err);
				}
				else
					pr_err("rename-devices: no net_dev\n");
				rtnl_unlock();

			}
			index++;
		}
	}
}

static void rename_blk_device_name(struct device_node *np)
{
	dev_t dev;
	int index = 0, partno;
	struct gendisk *disk;
	struct device_node *node = np;
	char dev_path[PATH_SIZE];
	const char *actual_name;
	char *modified_name;

	while (!of_property_read_string_index(node, "actual-dev", index,
						&actual_name)) {
		memset(dev_path, '\0', PATH_SIZE);
		snprintf(dev_path, PATH_SIZE, "/dev/%s", actual_name);
		dev = name_to_dev_t(dev_path);
		if (!dev) {
			pr_err("rename-devices: No device path : %s\n", dev_path);
			return;
		}
		disk = get_gendisk(dev, &partno);
		if (!disk) {
			pr_err("rename-devices: No device with dev path : %s\n", dev_path);
			return;
		}
		if (!of_property_read_string_index(node, dp_enabled ?
					"rename-dev-ab" : "rename-dev",
				 index,	(const char **)&modified_name)) {
			get_slot_updated_name(modified_name);
			device_rename(disk_to_dev(disk), final_name);
		} else {
			pr_err("rename-devices: rename-dev for actual-dev = %s is missing\n",
								 actual_name);
			return;
		}
		index++;
	}
}

static int __init rename_devices_init(void)
{
	struct device_node *node = NULL, *child = NULL;
	const char *device_type;

	node = of_find_compatible_node(NULL, NULL, "qcom,rename-devices");

	if (!node) {
		pr_err("rename-devices: qcom,rename-devices node is missing\n");
		goto out;
	}

	for_each_child_of_node(node, child) {
		if (!of_property_read_string(child, "device-type", &device_type)) {
			if (strcmp(device_type, "block") == 0)
				rename_blk_device_name(child);
			else if (strcmp(device_type, "net") == 0)
				rename_net_device_name(child);
			else
				pr_err("rename-devices: unsupported device\n");
		} else
			pr_err("rename-devices: device-type is missing\n");
	}

out:
	of_node_put(node);
	return  0;
}

late_initcall(rename_devices_init);
MODULE_DESCRIPTION("Rename devices");
MODULE_LICENSE("GPL v2");
