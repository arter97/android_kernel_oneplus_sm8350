// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/mount.h>
#include <linux/syscalls.h>

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

late_initcall(early_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Early user space launch driver");
