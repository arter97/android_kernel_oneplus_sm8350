// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/scatterlist.h>
#include <crypto/aead.h>
#include <soc/qcom/qcom_secure_hibernation.h>
#include <../../../kernel/power/power.h>
#include "../../misc/qseecom_kernel.h"

#define AUTH_SIZE	16

struct s4app_time {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
};

struct wrap_req {
	struct s4app_time save_time;
};

struct wrap_rsp {
	uint8_t wrapped_key_buffer[WRAPPED_KEY_SIZE];
	uint32_t wrapped_key_size;
	uint8_t key_buffer[PAYLOAD_KEY_SIZE];
	uint32_t key_size;
};

struct unwrap_req {
	uint8_t wrapped_key_buffer[WRAPPED_KEY_SIZE];
	uint32_t wrapped_key_size;
	struct s4app_time curr_time;
};

struct unwrap_rsp {
	uint8_t key_buffer[PAYLOAD_KEY_SIZE];
	uint32_t key_size;
};

enum cmd_id {
	WRAP_KEY_CMD = 0,
	UNWRAP_KEY_CMD = 1,
};

struct cmd_req {
	enum cmd_id cmd;
	union {
		struct wrap_req wrapkey_req;
		struct unwrap_req unwrapkey_req;
	};
};

struct cmd_rsp {
	enum cmd_id cmd;
	union {
		struct wrap_rsp wrapkey_rsp;
		struct unwrap_rsp unwrapkey_rsp;
	};
	uint32_t status;
};

static struct qcom_crypto_params *params;
static struct crypto_aead *tfm;
static struct aead_request *req;
static u8 iv_size;
static u8 key[AES256_KEY_SIZE];
static struct qseecom_handle *app_handle;
static int first_encrypt;
static void *temp_out_buf;
static int pos;

static void init_sg(struct scatterlist *sg, void *data, unsigned int size)
{
	sg_init_table(sg, 2);
	sg_set_buf(&sg[0], params->aad, sizeof(params->aad));
	sg_set_buf(&sg[1], data, size);
}

static void save_auth(uint8_t *out_buf)
{
	memcpy(params->authslot_start + (pos * AUTH_SIZE), out_buf + PAGE_SIZE,
		AUTH_SIZE);
	pos++;
}

int encrypt_page(void *buf)
{
	struct scatterlist sg_in[2], sg_out[2];
	struct crypto_wait wait;
	int ret = 0;

	/* Allocate a request object */
	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto err_aead;
	}

	crypto_init_wait(&wait);
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				crypto_req_done, &wait);

	ret = crypto_aead_setauthsize(tfm, AUTH_SIZE);
	iv_size = crypto_aead_ivsize(tfm);
	if (iv_size && first_encrypt)
		memset(params->iv, 0xff, iv_size);

	ret = crypto_aead_setkey(tfm, key, AES256_KEY_SIZE);
	if (ret) {
		pr_err("Error setting key: %d\n", ret);
		goto out;
	}
	crypto_aead_clear_flags(tfm, ~0);

	memset(temp_out_buf, 0, 2 * PAGE_SIZE);
	init_sg(sg_in, buf, PAGE_SIZE);
	init_sg(sg_out, temp_out_buf, PAGE_SIZE + AUTH_SIZE);
	aead_request_set_ad(req, sizeof(params->aad));

	aead_request_set_crypt(req, sg_in, sg_out, PAGE_SIZE, params->iv);
	crypto_aead_encrypt(req);
	ret = crypto_wait_req(ret, &wait);
	if (ret) {
		pr_err("Error encrypting data: %d\n", ret);
		goto out;
	}

	memcpy(buf, temp_out_buf, PAGE_SIZE);
	save_auth(temp_out_buf);

	if (first_encrypt)
		first_encrypt = 0;
out:
	aead_request_free(req);
	return ret;
err_aead:
	free_pages((unsigned long)temp_out_buf, 1);
	return ret;
}
EXPORT_SYMBOL(encrypt_page);

unsigned int get_authpage_count(void)
{
	unsigned long total_auth_size;
	unsigned int num_auth_pages;

	total_auth_size = params->authslot_count * AUTH_SIZE;
	num_auth_pages = total_auth_size / PAGE_SIZE;
	if (total_auth_size % PAGE_SIZE)
		num_auth_pages += 1;

	return num_auth_pages;
}

void populate_secure_params(struct qcom_crypto_params *p)
{
	memcpy(p, params, sizeof(struct qcom_crypto_params));
}

void get_auth_params(void **authpage, int *authpage_count)
{
	*authpage = params->authslot_start;
	*authpage_count = get_authpage_count();
}

static int get_key_from_ta(void)
{
	int ret;
	int req_len, rsp_len;

	struct cmd_req *req = (struct cmd_req *)app_handle->sbuf;
	struct cmd_rsp *rsp = NULL;

	req_len = sizeof(struct cmd_req);
	if (req_len & QSEECOM_ALIGN_MASK)
		req_len = QSEECOM_ALIGN(req_len);

	rsp = (struct cmd_rsp *)(app_handle->sbuf + req_len);
	rsp_len = sizeof(struct cmd_rsp);
	if (rsp_len & QSEECOM_ALIGN_MASK)
		rsp_len = QSEECOM_ALIGN(rsp_len);

	memset(req, 0, req_len);
	memset(rsp, 0, rsp_len);

	req->cmd = WRAP_KEY_CMD;
	req->wrapkey_req.save_time.hour = 4;
	rsp->wrapkey_rsp.wrapped_key_size = WRAPPED_KEY_SIZE;

	ret = qseecom_send_command(app_handle, req, req_len, rsp, rsp_len);
	if (!ret) {
		memcpy(params->key_blob, rsp->wrapkey_rsp.wrapped_key_buffer,
			rsp->wrapkey_rsp.wrapped_key_size);
		memcpy(key, rsp->wrapkey_rsp.key_buffer,
			rsp->wrapkey_rsp.key_size);
	}
	return ret;
}

static int init_aead(void)
{
	if (!tfm) {
		tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
		if (IS_ERR(tfm)) {
			pr_err("Error crypto_alloc_aead: %d\n",	PTR_ERR(tfm));
			return PTR_ERR(tfm);
		}
	}
	return 0;
}

static int init_ta_and_set_key(void)
{
	static int ta_init_done;
	const uint32_t shared_buffer_len = 4096;
	int ret;

	if (!ta_init_done) {
		ret = qseecom_start_app(&app_handle, "secs2d", shared_buffer_len);
		if (ret) {
			pr_err("qseecom_start_app failed: %d\n", ret);
			return ret;
		}

		ret = get_key_from_ta();
		if (ret) {
			pr_err("set_key returned %d\n", ret);
			goto err;
		}
		ta_init_done = 1;
	}
	return 0;
err:
	qseecom_shutdown_app(&app_handle);
	return ret;
}

static int alloc_auth_memory(void)
{
	unsigned long total_auth_size;

	/* Number of Auth slots is equal to the number of image pages */
	params->authslot_count = snapshot_get_image_size();
	total_auth_size = params->authslot_count * AUTH_SIZE;

	params->authslot_start = vmalloc(total_auth_size);
	if (!(params->authslot_start))
		return -ENOMEM;
	return 0;
}

void deinit_aes_encrypt(void)
{
	free_pages((unsigned long)temp_out_buf, 1);
	memset(params->key_blob, 0, WRAPPED_KEY_SIZE);
	memset(key, 0, AES256_KEY_SIZE);
	crypto_free_aead(tfm);
	kfree(params);
}

int init_aes_encrypt(void)
{
	int ret;

	params = kmalloc(sizeof(struct qcom_crypto_params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	ret = init_aead();
	if (ret) {
		pr_err("%s: Failed init_aead(): %d\n", __func__, ret);
		goto err_aead;
	}

	ret = init_ta_and_set_key();
	if (ret) {
		pr_err("%s: Failed to init TA: %d\n", __func__, ret);
		goto err_setkey;
	}

	/*
	 * Encryption results in two things:
	 * 1. Encrypted data
	 * 2. Auth
	 * Save the Auth data of all pages locally and return only the
	 * encrypted page to the caller. Allocate memory to save the auth.
	 */
	ret = alloc_auth_memory();
	if (ret) {
		pr_err("%s: Failed alloc_auth_memory %d\n", __func__, ret);
		goto err_auth;
	}

	first_encrypt = 1;
	pos = 0;
	temp_out_buf = (void *)__get_free_pages(GFP_KERNEL, 1);
	if (!temp_out_buf) {
		pr_err("%s: Failed alloc_auth_memory %d\n", __func__, ret);
		ret = -1;
		goto err_auth;
	}
	memcpy(params->aad, "SECURE_S2D!!", sizeof(params->aad));
	params->authsize = AUTH_SIZE;
	return 0;
err_auth:
	qseecom_shutdown_app(&app_handle);
	memset(params->key_blob, 0, WRAPPED_KEY_SIZE);
	memset(key, 0, AES256_KEY_SIZE);
err_setkey:
	crypto_free_aead(tfm);
err_aead:
	kfree(params);
	return ret;
}

MODULE_DESCRIPTION("Framework to encrypt a page using a trusted application");
MODULE_LICENSE("GPL v2");
