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
#include <linux/soc/qcom/smem.h>
#include "minidump_private.h"

static struct md_global_toc *md_smem_global_toc;
static struct md_ss_toc *md_smem_ss_toc;

static bool md_smem_get_toc_init(void)
{
	return !!md_smem_global_toc->md_toc_init;
}

static u32 md_smem_get_revision(void)
{
	return md_smem_global_toc->md_revision;
}

static u32 md_smem_get_enable_status(void)
{
	return md_smem_ss_toc->md_ss_enable_status;
}

static void md_smem_set_ss_toc_init(u32 init)
{
	md_smem_ss_toc->md_ss_toc_init = init;
}

static u32 md_smem_get_ss_toc_init(void)
{
	return md_smem_ss_toc->md_ss_toc_init;
}

static void md_smem_set_ss_enable_status(bool enable)
{
	if (enable)
		md_smem_ss_toc->md_ss_enable_status = MD_SS_ENABLED;
	else
		md_smem_ss_toc->md_ss_enable_status = MD_SS_DISABLED;
}

static u32 md_smem_get_ss_enable_status(void)
{
	return md_smem_ss_toc->md_ss_enable_status;
}

static void md_smem_set_ss_encryption(bool required, u32 status)
{
	md_smem_ss_toc->encryption_status = status;

	if (required)
		md_smem_ss_toc->encryption_required = MD_SS_ENCR_REQ;
	else
		md_smem_ss_toc->encryption_required = MD_SS_ENCR_NOTREQ;
}

static void md_smem_set_ss_region_base(u64 base)
{
	md_smem_ss_toc->md_ss_smem_regions_baseptr = base;
}

static u64 md_smem_get_ss_region_base(void)
{
	return md_smem_ss_toc->md_ss_smem_regions_baseptr;
}

static void md_smem_set_ss_region_count(u32 count)
{
	md_smem_ss_toc->ss_region_count = count;
}

static u32 md_smem_get_ss_region_count(void)
{
	return md_smem_ss_toc->ss_region_count;
}

static const struct md_ops md_smem_ops = {
	.get_toc_init			= md_smem_get_toc_init,
	.get_revision			= md_smem_get_revision,
	.get_enable_status		= md_smem_get_enable_status,
	.set_ss_toc_init		= md_smem_set_ss_toc_init,
	.get_ss_toc_init		= md_smem_get_ss_toc_init,
	.set_ss_enable_status		= md_smem_set_ss_enable_status,
	.get_ss_enable_status		= md_smem_get_ss_enable_status,
	.set_ss_encryption		= md_smem_set_ss_encryption,
	.set_ss_region_base		= md_smem_set_ss_region_base,
	.get_ss_region_base		= md_smem_get_ss_region_base,
	.set_ss_region_count		= md_smem_set_ss_region_count,
	.get_ss_region_count		= md_smem_get_ss_region_count,
};

static struct md_init_data md_smem_init_data = {
	.ops = &md_smem_ops,
};

static int __init minidump_smem_init(void)
{
	size_t size;

	/* Get Minidump table from SMEM */
	md_smem_global_toc = qcom_smem_get(QCOM_SMEM_HOST_ANY, SBL_MINIDUMP_SMEM_ID,
				      &size);
	if (!md_smem_global_toc)
		return -ENODEV;

	if (IS_ERR(md_smem_global_toc))
		return PTR_ERR(md_smem_global_toc);

	md_smem_ss_toc = &md_smem_global_toc->md_ss_toc[MD_SS_HLOS_ID];

	return msm_minidump_probe(&md_smem_init_data);
}

subsys_initcall(minidump_smem_init);
