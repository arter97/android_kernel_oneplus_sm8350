// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <uapi/linux/mount.h>
#include <linux/syscalls.h>

#define FIRMWARE_MOUNT_PATH	"/early_services/vendor/firmware_mnt"

static char saved_modem_name[64] __initdata;

static int mount_partition(char *part_name, char *mnt_point)
{
	int err = -ENOENT;
	int mountflags = MS_RDONLY | MS_SILENT;
	const char *fs = "vfat";

	if (!part_name[0]) {
		pr_err("Unknown partition\n");
		goto fail;
	}

	err = ksys_access((const char __user *) mnt_point, 0);
	if (err == 0)
		pr_info("early init : mount path  '%s' available\n", mnt_point);
	else {
		pr_err("early init : mount path '%s' not available!!\n", mnt_point);
		goto fail;
	}

	err = ksys_access((const char __user *) part_name, 0);
	if (err  == 0)
		pr_info("early init : node %s available\n", part_name);
	else {
		pr_err("early init : node %s not available, error : %d\n", part_name, err);
		goto fail;
	}
	err = ksys_mount((char __user *)part_name, (char __user *)mnt_point,
			(char __user *)fs,  (unsigned long)mountflags,
			(void __user *)NULL);
	if (err)
		pr_err("Mount Partition [%s] failed, error [%d]\n", part_name, err);

fail:
	return err;
}

static int __init early_init(void)
{
	int ret = 0;
	static char init_prog[128] = "/early_services/init_early";
	static char *init_prog_argv[2] = { init_prog, NULL };
	static char *init_envp[] = {
		"HOME=/",
		"TERM=linux",
		"PATH=/sbin:/usr/sbin:/bin:/usr/bin:/system/sbin:/system/bin:"
			"early_services/sbin:early_services/system/bin",
		NULL};

	devtmpfs_mount("dev");

	if (saved_modem_name[0] != '\0') {
		if (!mount_partition(saved_modem_name, FIRMWARE_MOUNT_PATH))
			pr_info("Firmwares partition ready\n");
		else
			pr_err("Firmwares partition mount fail!\n");
	} else
		pr_err("Firmware partition not found!\n");

	if (ksys_access((const char __user *) init_prog, 0) == 0) {
		ret = call_usermodehelper(init_prog, init_prog_argv, init_envp, 0);
		if (!ret)
			pr_info("%s launched\n", __func__);
		else
			pr_err("%s failed\n", __func__);
	} else {
		pr_err("%s: %s does not exist\n", __func__, init_prog);
	}

	return ret;
}

static int __init modem_dev_setup(char *line)
{
	strlcpy(saved_modem_name, line, sizeof(saved_modem_name));
	return 1;
}
__setup("modem=", modem_dev_setup);

late_initcall_sync(early_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Early user space launch driver");
