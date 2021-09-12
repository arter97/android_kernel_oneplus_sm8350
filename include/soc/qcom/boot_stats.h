/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2011-2021, The Linux Foundation. All rights reserved.
 */
#ifndef __QCOM_BOOT_STATS_H__
#define __QCOM_BOOT_STATS_H__

#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
void place_marker(const char *name);
void destroy_marker(const char *name);
unsigned long long msm_timer_get_sclk_ticks(void);
static inline int boot_marker_enabled(void) { return 1; }
uint64_t get_sleep_exit_time(void);
#else
static inline int init_bootkpi(void) { return 0; }
static inline void exit_bootkpi(void) { };
static inline void place_marker(char *name) { };
static inline void destroy_marker(const char *name) { };
static inline int boot_marker_enabled(void) { return 0; }
static inline unsigned long long msm_timer_get_sclk_ticks(void) { return -EINVAL; }
#endif
#endif /* __QCOM_BOOT_STATS_H__ */
