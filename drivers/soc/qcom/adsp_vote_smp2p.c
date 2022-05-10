// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/of.h>
#include <linux/debugfs.h>

#define AWAKE_BITS	3
#define ADSP_VOTE	2
#define ADSP_UNVOTE	1

static struct qcom_smem_state *state;
static struct dentry *dent_adsp_vote, *adsp_vote;

static int adsp_vote_pm_notifier(struct notifier_block *nb,
				  unsigned long event, void *unused)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		pr_info("ADSP unvoting over smp2p initiated\n");
		qcom_smem_state_update_bits(state, AWAKE_BITS, ADSP_UNVOTE);
		break;
	case PM_POST_SUSPEND:
		pr_info("ADSP voting over smp2p initiated\n");
		qcom_smem_state_update_bits(state, AWAKE_BITS, ADSP_VOTE);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block adsp_vote_pm_nb = {
	.notifier_call = adsp_vote_pm_notifier,
};

static int adsp_manual_vote_op(void *data, u64 vote)
{
	if (vote == 0) {
		pr_info("ADSP unvoting initiated\n");
		qcom_smem_state_update_bits(state, AWAKE_BITS, ADSP_UNVOTE);
	} else if (vote == 1) {
		pr_info("ADSP voting initiated\n");
		qcom_smem_state_update_bits(state, AWAKE_BITS, ADSP_VOTE);
	} else
		pr_err("%s: Invalid vote type\n", __func__);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_vote, NULL, adsp_manual_vote_op, "%ull\n");

static int adsp_vote_smp2p_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	state = qcom_smem_state_get(&pdev->dev, 0, &ret);
	if (IS_ERR(state)) {
		dev_err(dev, "%qcom_smem_state_get fail\n");
		return PTR_ERR(state);
	}

	ret = register_pm_notifier(&adsp_vote_pm_nb);
	if (ret) {
		dev_err(dev, "%s: power state notif error %d\n", __func__, ret);
		return ret;
	}
	pr_info("ADSP vote device is registered successfully\n");

	dent_adsp_vote = debugfs_create_dir("adsp_vote_smp2p", NULL);
	if (IS_ERR_OR_NULL(dent_adsp_vote)) {
		dev_err(&pdev->dev, "%s:  debugfs create dir failed %d\n",
				__func__, ret);
		return ret;
	}

	adsp_vote = debugfs_create_file("vote", 0220, dent_adsp_vote, NULL,
			&fops_vote);
	if (IS_ERR_OR_NULL(adsp_vote)) {
		debugfs_remove(dent_adsp_vote);
		dent_adsp_vote = NULL;
		dev_err(&pdev->dev, "%s:  debugfs create file failed %d\n",
				__func__, ret);
	}
	return 0;
}

static const struct of_device_id adsp_vote_match_table[] = {
	{.compatible = "qcom,smp2p-hlos_sleep_state"},
	{},
};

static struct platform_driver adsp_vote_smp2p_driver = {
	.probe = adsp_vote_smp2p_probe,
	.driver = {
		.name = "adsp_vote_smp2p",
		.of_match_table = adsp_vote_match_table,
	},
};

static int __init adsp_vote_smp2p_init(void)
{
	int ret;

	ret = platform_driver_register(&adsp_vote_smp2p_driver);
	if (ret) {
		pr_err("%s: register failed %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

module_init(adsp_vote_smp2p_init);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ADSP Manual vote driver based on smp2p framework");
