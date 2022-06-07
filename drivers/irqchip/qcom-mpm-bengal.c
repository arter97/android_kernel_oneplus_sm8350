// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <soc/qcom/mpm.h>
const struct mpm_pin mpm_bengal_gic_chip_data[] = {
	{2, 190},
	{12, 422}, /* b3_lfps_rxterm_irq */
	{86, 183}, /* mpm_wake,spmi_m */
	{90, 260}, /* eud_p0_dpse_int_mx */
	{91, 260}, /* eud_p0_dmse_int_mx */
	{5, 296}, /* lpass_irq_out_sdc */
	{24, 79}, /* bi_px_lpi_1_aoss_mx */
	{-1},
};
