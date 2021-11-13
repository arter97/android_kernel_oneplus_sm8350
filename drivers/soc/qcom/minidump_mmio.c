// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include "minidump_private.h"

/* Global ToC registers offset, read-only for guest */
#define MD_GLOBAL_TOC_INIT          (0x00)
#define MD_GLOBAL_TOC_REVISION      (0x04)
#define MD_GLOBAL_TOC_ENABLE_STATUS (0x08)

/* 0x0C ~ 0x1C Reserved */

/* Subsystem ToC regiseters offset, read/write for guest */
#define MD_SS_TOC_INIT                 (0x20)
#define MD_SS_TOC_ENABLE_STATUS        (0x24)
#define MD_SS_TOC_ENCRYPTION_STATUS    (0x28)
#define MD_SS_TOC_ENCRYPTION_REQUIRED  (0x2C)
#define MD_SS_TOC_REGION_COUNT         (0x30)
#define MD_SS_TOC_REGION_BASE_LOW      (0x34)
#define MD_SS_TOC_REGION_BASE_HIGH     (0x38)

/* MMIO base address */
static void __iomem *md_mmio_base;

static bool md_mmio_get_toc_init(void)
{
	if (md_mmio_base)
		return !!readl_relaxed(md_mmio_base + MD_GLOBAL_TOC_INIT);
	else
		return false;
}

static u32 md_mmio_get_revision(void)
{
	return readl_relaxed(md_mmio_base + MD_GLOBAL_TOC_REVISION);
}

static u32 md_mmio_get_enable_status(void)
{
	return readl_relaxed(md_mmio_base + MD_GLOBAL_TOC_ENABLE_STATUS);
}

static void md_mmio_set_ss_toc_init(u32 init)
{
	writel_relaxed(init, md_mmio_base + MD_SS_TOC_INIT);
}

static u32 md_mmio_get_ss_toc_init(void)
{
	return readl_relaxed(md_mmio_base + MD_SS_TOC_INIT);
}

static void md_mmio_set_ss_enable_status(bool enable)
{
	if (enable)
		writel_relaxed(MD_SS_ENABLED, md_mmio_base + MD_SS_TOC_ENABLE_STATUS);
	else
		writel_relaxed(MD_SS_DISABLED, md_mmio_base + MD_SS_TOC_ENABLE_STATUS);
}

static u32 md_mmio_get_ss_enable_status(void)
{
	return readl_relaxed(md_mmio_base + MD_SS_TOC_ENABLE_STATUS);
}

static void md_mmio_set_ss_encryption(bool required, u32 status)
{
	writel_relaxed(status, md_mmio_base + MD_SS_TOC_ENCRYPTION_STATUS);

	if (required)
		writel_relaxed(MD_SS_ENCR_REQ, md_mmio_base + MD_SS_TOC_ENCRYPTION_REQUIRED);
	else
		writel_relaxed(MD_SS_ENCR_NOTREQ, md_mmio_base + MD_SS_TOC_ENCRYPTION_REQUIRED);
}

static void md_mmio_set_ss_region_base(u64 base)
{
	writel_relaxed((u32)base, md_mmio_base + MD_SS_TOC_REGION_BASE_LOW);
	writel_relaxed((u32)(base >> 32), md_mmio_base + MD_SS_TOC_REGION_BASE_HIGH);
}

static u64 md_mmio_get_ss_region_base(void)
{
	u64 address;

	address = readl_relaxed(md_mmio_base + MD_SS_TOC_REGION_BASE_LOW) +
			((u64)readl_relaxed(md_mmio_base + MD_SS_TOC_REGION_BASE_HIGH) << 32);

	return address;
}

static void md_mmio_set_ss_region_count(u32 count)
{
	writel_relaxed(count, md_mmio_base + MD_SS_TOC_REGION_COUNT);
}

u32 md_mmio_get_ss_region_count(void)
{
	return readl_relaxed(md_mmio_base + MD_SS_TOC_REGION_COUNT);
}

static const struct md_ops md_mmio_ops = {
	.get_toc_init			= md_mmio_get_toc_init,
	.get_revision			= md_mmio_get_revision,
	.get_enable_status		= md_mmio_get_enable_status,
	.set_ss_toc_init		= md_mmio_set_ss_toc_init,
	.get_ss_toc_init		= md_mmio_get_ss_toc_init,
	.set_ss_enable_status		= md_mmio_set_ss_enable_status,
	.get_ss_enable_status		= md_mmio_get_ss_enable_status,
	.set_ss_encryption		= md_mmio_set_ss_encryption,
	.set_ss_region_base		= md_mmio_set_ss_region_base,
	.get_ss_region_base		= md_mmio_get_ss_region_base,
	.set_ss_region_count		= md_mmio_set_ss_region_count,
	.get_ss_region_count		= md_mmio_get_ss_region_count,
};

static struct md_init_data md_mmio_init_data = {
	.ops = &md_mmio_ops,
};

static int minidump_mmio_probe(struct platform_device *pdev)
{
	md_mmio_base = devm_of_iomap(&pdev->dev, pdev->dev.of_node, 0, NULL);
	if (!md_mmio_base) {
		dev_err(&pdev->dev, "Failed to map address\n");
		return -ENODEV;
	}

	return msm_minidump_probe(&md_mmio_init_data);
}

static const struct of_device_id minidump_mmio_device_tbl[] = {
	{ .compatible = "qcom,virt-minidump", },
	{},
};

static struct platform_driver minidump_mmio_driver = {
	.probe = minidump_mmio_probe,
	.driver = {
		.name = "minidump_mmio",
		.of_match_table = minidump_mmio_device_tbl,
	},
};

static int __init minidump_mmio_init(void)
{
	return platform_driver_register(&minidump_mmio_driver);
}
subsys_initcall(minidump_mmio_init);

static void __exit minidump_mmio_exit(void)
{
	platform_driver_unregister(&minidump_mmio_driver);
}
module_exit(minidump_mmio_exit);

MODULE_DESCRIPTION("QTI minidump MMIO driver");
MODULE_LICENSE("GPL v2");
