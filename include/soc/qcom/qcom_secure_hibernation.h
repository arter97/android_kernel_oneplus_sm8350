/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef __SOC_QCOM_SECURE_HIBERNATION_H__
#define __SOC_QCOM_SECURE_HIBERNATION_H__

#define AES256_KEY_SIZE		32
#define NUM_KEYS		2
#define PAYLOAD_KEY_SIZE	(AES256_KEY_SIZE * NUM_KEYS)
#define RAND_INDEX_SIZE		8
#define NONCE_LENGTH		8
#define MAC_LENGTH		16
#define TIME_STRUCT_LENGTH	48
#define WRAP_PAYLOAD_LENGTH \
		(PAYLOAD_KEY_SIZE + RAND_INDEX_SIZE + TIME_STRUCT_LENGTH)
#define AAD_LENGTH		20
#define AAD_WITH_PAD_LENGTH	32
#define WRAPPED_KEY_SIZE \
		(AAD_WITH_PAD_LENGTH + WRAP_PAYLOAD_LENGTH + MAC_LENGTH + \
		NONCE_LENGTH)

struct qcom_crypto_params {
	unsigned int authsize;
	uint8_t *authslot_start;
	unsigned int authslot_count;
	unsigned char key_blob[WRAPPED_KEY_SIZE];
	unsigned char iv[12];
	unsigned char aad[12];
};

#if IS_ENABLED(CONFIG_QCOM_SECURE_HIBERNATION)
extern int encrypt_page(void *);
extern unsigned int get_authpage_count(void);
extern void populate_secure_params(struct qcom_crypto_params *params);
extern void get_auth_params(void **auth, int *num);
extern int init_aes_encrypt(void);
extern void deinit_aes_encrypt(void);
static inline int qcom_secure_hibernation_enabled(void) { return 1; }
#else
static inline int encrypt_page(void *page) { return 0; }
static inline unsigned int get_authpage_count(void) { return 0; }
static inline void populate_secure_params(struct qcom_crypto_params *params) {  }
static inline void get_auth_params(void **auth, int *num) { }
static inline int init_aes_encrypt(void){ return 0; }
static inline void deinit_aes_encrypt(void) { };
static inline int qcom_secure_hibernation_enabled(void) { return 0; }
#endif /* CONFIG_QCOM_SECURE_HIBERNATION */

#endif /* __SOC_QCOM_SECURE_HIBERNATION_H__ */
