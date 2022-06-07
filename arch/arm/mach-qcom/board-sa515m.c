// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include "board-dt.h"
#include <asm/mach/map.h>
#include <asm/mach/arch.h>

static const char *sa515m_dt_match[] __initconst = {
	"qcom,sa515m",
	NULL
};

static void __init sa515m_init(void)
{
	board_dt_populate(NULL);
}

DT_MACHINE_START(SA410M_DT,
	"Qualcomm Technologies, Inc. SA515M (Flattened Device Tree)")
	.init_machine		= sa515m_init,
	.dt_compat		= sa515m_dt_match,
MACHINE_END
