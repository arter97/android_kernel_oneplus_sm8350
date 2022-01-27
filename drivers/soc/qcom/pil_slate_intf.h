/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef __SLATE_INTF_H_
#define __SLATE_INTF_H_

#define MAX_APP_NAME_SIZE 100
#define RESULT_SUCCESS 0
#define RESULT_FAILURE -1

/* tzapp command list.*/
enum slate_tz_commands {
	SLATEPIL_RAMDUMP,
	SLATEPIL_IMAGE_LOAD,
	SLATEPIL_AUTH_MDT,
	SLATEPIL_DLOAD_CONT,
	SLATEPIL_GET_SLATE_VERSION,
	SLATEPIL_SHUTDOWN,
	SLATEPIL_DUMPINFO,
	SLATEPIL_UP_INFO,
};

/* tzapp bg request.*/
struct tzapp_slate_req {
	uint8_t tzapp_slate_cmd;
	uint8_t padding[3];
	phys_addr_t address_fw;
	size_t size_fw;
} __attribute__ ((__packed__));

/* tzapp bg response.*/
struct tzapp_slate_rsp {
	uint32_t tzapp_slate_cmd;
	uint32_t slate_info_len;
	int32_t status;
	uint32_t slate_info[100];
} __attribute__ ((__packed__));

#endif
