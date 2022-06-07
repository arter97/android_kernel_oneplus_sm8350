/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 - 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _CQHCI_CRYPTO_QTI_H
#define _CQHCI_CRYPTO_QTI_H

#include "cqhci-crypto.h"

void cqhci_crypto_qti_enable(struct cqhci_host *host);

void cqhci_crypto_qti_disable(struct cqhci_host *host);

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
int cqhci_crypto_qti_init_crypto(struct cqhci_host *host,
				 const struct keyslot_mgmt_ll_ops *ksm_ops);
#endif

int cqhci_crypto_qti_debug(struct cqhci_host *host);

#if IS_ENABLED(CONFIG_QTI_CRYPTO_FDE)
int cqhci_crypto_qti_prep_desc(struct cqhci_host *host,
				struct mmc_request *mrq,
				u64 *ice_ctx);
#endif

#if IS_ENABLED(CONFIG_MMC_CQHCI_CRYPTO_QTI)
void cqhci_crypto_qti_set_vops(struct cqhci_host *host);
#else
static inline void cqhci_crypto_qti_set_vops(struct cqhci_host *host)
{}
#endif /* CONFIG_MMC_CQHCI_CRYPTO_QTI) */

int cqhci_crypto_qti_resume(struct cqhci_host *host);

int cqhci_crypto_qti_recovery_finish(struct cqhci_host *host);

int cqhci_crypto_qti_restore_from_hibernation(struct cqhci_host *host);

#endif /* _CQHCI_CRYPTO_QTI_H */
