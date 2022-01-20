/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __VIRTIO_FASTRPC_QUEUE_H__
#define __VIRTIO_FASTRPC_QUEUE_H__

#include "virtio_fastrpc_core.h"

void *get_a_tx_buf(struct fastrpc_file *fl);
void fastrpc_rxbuf_send(struct fastrpc_file *fl, void *data, unsigned int len);
int fastrpc_txbuf_send(struct fastrpc_file *fl, void *data, unsigned int len);
#endif /*__VIRTIO_FASTRPC_QUEUE_H__*/
