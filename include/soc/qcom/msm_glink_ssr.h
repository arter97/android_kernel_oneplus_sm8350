/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 */

#ifndef _MSM_GLINK_SSR_H__
#define _MSM_GLINK_SSR_H__

#if defined(CONFIG_DEEPSLEEP) && defined(CONFIG_RPMSG_QCOM_GLINK_RPM)
void glink_ssr_notify_rpm(void);
#endif

#endif
