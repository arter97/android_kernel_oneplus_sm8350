// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#define pr_fmt(msg) "power_state:" msg

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/suspend.h>
#include <linux/ioctl.h>
#include <linux/kdev_t.h>
#include <soc/qcom/subsystem_restart.h>
#include <soc/qcom/subsystem_notif.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/pm_wakeup.h>
#include "linux/power_state.h"

#define POWER_STATE "power_state"
#define STRING_LEN 32
#define WAIT_TIME_MS 200

static struct class *ps_class;
struct device *ps_ret;
static  struct cdev ps_cdev;
static  dev_t ps_dev;
struct kobject *kobj_ref;
static ktime_t start_time;
static struct wakeup_source *notify_ws;

#ifdef CONFIG_DEEPSLEEP
struct suspend_stats stats;
#endif

enum power_states {
	ACTIVE,
	DEEPSLEEP,
	HIBERNATE,
};

static char * const power_state[] = {
	[ACTIVE] = "active",
	[DEEPSLEEP] = "deepsleep",
	[HIBERNATE] = "hibernate",
};

enum power_states current_state = ACTIVE;

enum ssr_domain_info {
	SSR_DOMAIN_MODEM,
	SSR_DOMAIN_ADSP,
	SSR_DOMAIN_MAX,
};

struct service_info {
	const char name[STRING_LEN];
	int domain_id;
	void *handle;
	struct notifier_block *nb;
};

static char *ssr_domains[] = {
	"modem",
	"adsp",
};

struct ps_event {
	enum ps_event_type event;
};

static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int len = strlen(power_state[current_state]);

	return scnprintf(buf, len + 2, "%s\n", power_state[current_state]);
}

struct kobj_attribute power_state_attr =
				__ATTR_RO_MODE(state, 0440);

static int send_uevent(struct ps_event *pse)
{
	char event_string[STRING_LEN];
	char *envp[2] = { event_string, NULL };

	snprintf(event_string, ARRAY_SIZE(event_string), "POWER_STATE_EVENT = %d", pse->event);
	return kobject_uevent_env(&ps_ret->kobj, KOBJ_CHANGE, envp);
}

static int start_timer(void)
{
	ktime_t start_time_ms;
	ktime_t end_time;
	int retval =  -1;

	start_time = ktime_get();
	start_time_ms = ktime_to_ms(start_time);
	pr_debug("Suspend Start Time: %lld ms\n", start_time_ms);

	do {
		end_time = ktime_get();
		if (current_state == DEEPSLEEP)
			return 0;
	} while ((start_time_ms + WAIT_TIME_MS) > ktime_to_ms(end_time));

	pr_debug("Suspend Time Out: %lld ms\n", ktime_to_ms(end_time));
	return retval;
}

static int powerstate_pm_notifier(struct notifier_block *nb, unsigned long event, void *unused)
{
	struct ps_event pse;
	int ret = 0;

	switch (event) {
#ifdef CONFIG_DEEPSLEEP

	case PM_SUSPEND_PREPARE:
		pr_debug("PM_SUSPEND_PREPARE\n");

		if (mem_sleep_current == PM_SUSPEND_MEM) {
			pr_info("Deep Sleep entry\n");
			current_state = DEEPSLEEP;
			stats = suspend_stats;
		} else {
			pr_debug("Normal Suspend\n");
		}
		break;

	case PM_POST_SUSPEND:
		pr_debug("PM_POST_SUSPEND\n");

		if (mem_sleep_current == PM_SUSPEND_MEM) {

			if (suspend_stats.failed_freeze > stats.failed_freeze) {
				pr_info("Deep Sleep Aborted Due to Failed Freeze, Re-trying\n");
				ret = start_timer();
				if (ret < 0) {
					pr_err("Deep Sleep Enter Timed Out\n");
					/*Take Wakeup Source*/
					__pm_stay_awake(notify_ws);
					pse.event = EXIT_DEEP_SLEEP;
					send_uevent(&pse);
				}
			} else {
				pr_info("Deep Sleep exit\n");
				/*Take Wakeup Source*/
				__pm_stay_awake(notify_ws);
				pse.event = EXIT_DEEP_SLEEP;
				send_uevent(&pse);
			}
		} else {
			pr_debug("RBSC Resume\n");
		}
		break;
#endif

#ifdef CONFIG_HIBERNATION

	case PM_HIBERNATION_PREPARE:
		pr_debug("PM_HIBERNATION_PREPARE\n");

		pr_info("Hibernate entry\n");
		/*Swap Partition & Drop Caches*/
		pse.event = PREPARE_FOR_HIBERNATION;
		send_uevent(&pse);

		current_state = HIBERNATE;
		break;

	case PM_RESTORE_PREPARE:
		pr_debug("PM_RESTORE_PREPARE\n");
		pr_info("Hibernate restore\n");
		break;

	case PM_POST_HIBERNATION:
		pr_debug("PM_POST_HIBERNATION\n");
		pr_info("Hibernate exit\n");
		pse.event = EXIT_HIBERNATE;
		send_uevent(&pse);
		break;

	case PM_POST_RESTORE:
		pr_debug("PM_POST_RESTORE\n");
		pr_info("Hibernate error during restore\n");
		pse.event = EXIT_HIBERNATE;
		send_uevent(&pse);
		break;
#endif

	default:
		WARN_ONCE(1, "Default case: PM Notifier\n");
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block powerstate_pm_nb = {
	.notifier_call = powerstate_pm_notifier,
};

static int ssr_register(void);

static int ps_probe(struct platform_device *pdev)
{
	int ret;

	pr_debug("%s\n", __func__);

	ret = register_pm_notifier(&powerstate_pm_nb);
	if (ret) {
		dev_err(&pdev->dev, " %s: power state notif error %d\n", __func__, ret);
		return ret;
	}

	notify_ws = wakeup_source_register(&pdev->dev, "power-state");
	if (!notify_ws) {
		unregister_pm_notifier(&powerstate_pm_nb);
		return -ENOMEM;
	}

	ret = ssr_register();
	if (ret) {
		pr_err("Error registering ssr\n");
		wakeup_source_unregister(notify_ws);
		unregister_pm_notifier(&powerstate_pm_nb);
		return ret;
	}
	return 0;
}

static const struct of_device_id power_state_of_match[] = {
	{ .compatible = "qcom,power-state", },
	{ }
};
MODULE_DEVICE_TABLE(of, power_state_of_match);

static struct platform_driver ps_driver = {
	.probe = ps_probe,
	.driver = {
		.name = "power-state",
		.of_match_table = power_state_of_match,
	},
};

static int ps_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int ps_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long ps_ioctl(struct file *filp, unsigned int ui_power_state_cmd, unsigned long arg)
{
	int ret = 0;

	switch (ui_power_state_cmd) {

	case LPM_ACTIVE:
		pr_debug("Inside LPM_ACTIVE %s\n", __func__);
#ifdef CONFIG_DEEPSLEEP
		if (mem_sleep_current == PM_SUSPEND_MEM) {
			mem_sleep_current = PM_SUSPEND_STANDBY;
			/*Release Wakeup Source*/
			__pm_relax(notify_ws);
		}
#endif
		current_state = ACTIVE;
		break;

#ifdef CONFIG_DEEPSLEEP
	case ENTER_DEEPSLEEP:
		pr_debug("Enter Deep Sleep %s\n", __func__);
		/* Set /sys/power/mem_sleep to deep */
		mem_sleep_current = PM_SUSPEND_MEM;

		ret = start_timer();
		if (ret < 0) {
			pr_err("Deep Sleep Enter Timed Out\n");
			/*Take Wakeup Source*/
			__pm_stay_awake(notify_ws);
		}
		break;
#endif

#ifdef CONFIG_HIBERNATION
	case ENTER_HIBERNATE:
		pr_debug("Enter Hibernate %s\n", __func__);
		break;
#endif
	case MODEM_SUSPEND:
		pr_debug("Modem Subsys Suspend %s\n", __func__);
		/*To Modem subsys*/
		break;
	case ADSP_SUSPEND:
		pr_debug("ADSP Subsys Suspend %s\n", __func__);
		/*To ADSP subsys*/
		break;
	case MODEM_EXIT:
		pr_debug("Modem Subsys Suspend Exit %s\n", __func__);
		/*To Modem subsys*/
		break;
	case ADSP_EXIT:
		pr_debug("ADSP Subsys Suspend Exit %s\n", __func__);
		/*To ADSP subsys*/
		break;
	default:
		ret = -ENOIOCTLCMD;
		pr_err("Default - %s\n", __func__);
		break;
	}
	return ret;
}

static const struct file_operations ps_fops = {
	.owner = THIS_MODULE,
	.open = ps_open,
	.release = ps_release,
	.unlocked_ioctl = ps_ioctl,
};

static int __init init_power_state_func(void)
{
	int ret;

	pr_debug("Inside Init POWER_STATE Function\n");

	ret = alloc_chrdev_region(&ps_dev, 0, 1, POWER_STATE);
	if (ret  < 0) {
		pr_err("Alloc Chrdev Region Failed %d\n", ret);
		return ret;
	}

	pr_debug("Alloc Chrdev Region Successful\n");
	cdev_init(&ps_cdev, &ps_fops);
	ret = cdev_add(&ps_cdev, ps_dev, 1);
	if (ret < 0) {
		unregister_chrdev_region(ps_dev, 1);
		pr_err("Device Registration Failed\n");
		return ret;
	}

	pr_debug("Device Registration Successful\n");
	ps_class = class_create(THIS_MODULE, POWER_STATE);
	if (IS_ERR_OR_NULL(ps_class)) {
		cdev_del(&ps_cdev);
		unregister_chrdev_region(ps_dev, 1);
		pr_err("Class Creation Failed\n");
		return PTR_ERR(ps_class);
	}

	pr_debug("Class Creation Successful\n");
	ps_ret = device_create(ps_class, NULL, ps_dev, NULL, POWER_STATE);
	if (IS_ERR_OR_NULL(ps_ret)) {
		class_destroy(ps_class);
		cdev_del(&ps_cdev);
		unregister_chrdev_region(ps_dev, 1);
		pr_err("Device Creation Failed\n");
		return PTR_ERR(ps_ret);
	}

	pr_debug("Device Creation Successful\n");
	if (platform_driver_register(&ps_driver))
		pr_err("%s: Failed to Register ps_driver\n", __func__);

	kobj_ref = kobject_create_and_add("power_state", kernel_kobj);
	/*Creating sysfs file for power_state*/
	if (sysfs_create_file(kobj_ref, &power_state_attr.attr)) {
		pr_err("Cannot create sysfs file\n");
		kobject_put(kobj_ref);
		sysfs_remove_file(kernel_kobj, &power_state_attr.attr);
	}

	return 0;
}

static void __exit exit_power_state_func(void)
{
	kobject_put(kobj_ref);
	sysfs_remove_file(kernel_kobj, &power_state_attr.attr);
	device_destroy(ps_class, ps_dev);
	class_destroy(ps_class);
	cdev_del(&ps_cdev);
	unregister_chrdev_region(ps_dev, 1);
	platform_driver_unregister(&ps_driver);
}

/* SSR
 */

/* SSR Callback Functions
 *  modem_cb & adsp_cb
 */

static int ssr_modem_cb(struct notifier_block *this, unsigned long opcode, void *data)
{
	struct ps_event modeme;

	switch (opcode) {
	case SUBSYS_BEFORE_SHUTDOWN:
		pr_info("MODEM_BEFORE_SHUTDOWN\n");
		modeme.event = MDSP_BEFORE_POWERDOWN;
		send_uevent(&modeme);
		break;
	case SUBSYS_AFTER_POWERUP:
		pr_info("MODEM_AFTER_POWERUP\n");
		modeme.event = MDSP_AFTER_POWERUP;
		send_uevent(&modeme);
		break;
	default:
		pr_debug("Unknown SSR MODEM State\n");
		break;
	}
	return NOTIFY_DONE;
}

static int ssr_adsp_cb(struct notifier_block *this, unsigned long opcode, void *data)
{
	struct ps_event adspe;

	switch (opcode) {
	case SUBSYS_BEFORE_SHUTDOWN:
		pr_info("ADSP_BEFORE_SHUTDOWN\n");
		adspe.event = ADSP_BEFORE_POWERDOWN;
		send_uevent(&adspe);
		break;
	case SUBSYS_AFTER_POWERUP:
		pr_info("ADSP_AFTER_POWERUP\n");
		adspe.event = ADSP_AFTER_POWERUP;
		send_uevent(&adspe);
		break;
	default:
		pr_debug("Unknown SSR ADSP State\n");
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block ssr_modem_nb = {
	.notifier_call = ssr_modem_cb,
	.priority = 0,
};

static struct notifier_block ssr_adsp_nb = {
	.notifier_call = ssr_adsp_cb,
	.priority = 0,
};

static struct service_info service_data[2] = {
	{
		.name = "SSR_MODEM",
		.domain_id = SSR_DOMAIN_MODEM,
		.nb = &ssr_modem_nb,
		.handle = NULL,
	},
	{
		.name = "SSR_ADSP",
		.domain_id = SSR_DOMAIN_ADSP,
		.nb = &ssr_adsp_nb,
		.handle = NULL,
	},
};

/*
 * ssr_register - checks that domain id should be in range and register
 */

static int ssr_register(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(service_data); i++) {
		if ((service_data[i].domain_id < 0) ||
				(service_data[i].domain_id >= SSR_DOMAIN_MAX)) {
			pr_err("Invalid service ID = %d\n", service_data[i].domain_id);
		} else {
			service_data[i].handle = subsys_notif_register_notifier(
						ssr_domains[service_data[i].domain_id],
						service_data[i].nb);
			if (IS_ERR_OR_NULL(service_data[i].handle)) {
				pr_err("Subsys Register failed for id = %d\n",
					service_data[i].domain_id);
				service_data[i].handle = NULL;
			}
		}
	}
	return 0;
}

module_init(init_power_state_func);
module_exit(exit_power_state_func);
MODULE_LICENSE("GPL v2");
