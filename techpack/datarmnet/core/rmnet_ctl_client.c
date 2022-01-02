// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * RMNET_CTL client handlers
 *
 */

#include <linux/debugfs.h>
#include "rmnet_ctl.h"
#include "rmnet_ctl_client.h"

#define RMNET_CTL_LOG_PAGE 10
#define RMNET_CTL_LOG_NAME "rmnet_ctl"

struct rmnet_ctl_client {
	struct rmnet_ctl_client_hooks hooks;
};

struct rmnet_ctl_endpoint {
	struct rmnet_ctl_dev __rcu *dev;
	struct rmnet_ctl_client __rcu *client;
};

#if defined(CONFIG_IPA_DEBUG) || defined(CONFIG_MHI_DEBUG)
#define CONFIG_RMNET_CTL_DEBUG 1
#endif

static DEFINE_SPINLOCK(client_lock);
static struct rmnet_ctl_endpoint ctl_ep;

void rmnet_ctl_endpoint_setdev(const struct rmnet_ctl_dev *dev)
{
	rcu_assign_pointer(ctl_ep.dev, dev);
}

void rmnet_ctl_endpoint_post(const void *data, size_t len)
{
	struct rmnet_ctl_client *client;
	struct sk_buff *skb;

	if (unlikely(!data || !len))
		return;

	if (len == 0xFFFFFFFF) {
		skb = (struct sk_buff *)data;
		rmnet_ctl_log_info("RX", skb->data, skb->len);

		rcu_read_lock();

		client = rcu_dereference(ctl_ep.client);
		if (client && client->hooks.ctl_dl_client_hook) {
			skb->protocol = htons(ETH_P_MAP);
			client->hooks.ctl_dl_client_hook(skb);
		} else {
			kfree(skb);
		}

		rcu_read_unlock();
	} else {
		rmnet_ctl_log_info("RX", data, len);

		rcu_read_lock();

		client = rcu_dereference(ctl_ep.client);
		if (client && client->hooks.ctl_dl_client_hook) {
			skb = alloc_skb(len, GFP_ATOMIC);
			if (skb) {
				skb_put_data(skb, data, len);
				skb->protocol = htons(ETH_P_MAP);
				client->hooks.ctl_dl_client_hook(skb);
			}
		}

		rcu_read_unlock();
	}
}

void *rmnet_ctl_register_client(struct rmnet_ctl_client_hooks *hook)
{
	struct rmnet_ctl_client *client;

	if (!hook)
		return NULL;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;
	client->hooks = *hook;

	spin_lock(&client_lock);

	/* Only support one client for now */
	if (rcu_dereference(ctl_ep.client)) {
		spin_unlock(&client_lock);
		kfree(client);
		return NULL;
	}

	rcu_assign_pointer(ctl_ep.client, client);

	spin_unlock(&client_lock);

	return client;
}
EXPORT_SYMBOL(rmnet_ctl_register_client);

int rmnet_ctl_unregister_client(void *handle)
{
	struct rmnet_ctl_client *client = (struct rmnet_ctl_client *)handle;

	spin_lock(&client_lock);

	if (rcu_dereference(ctl_ep.client) != client) {
		spin_unlock(&client_lock);
		return -EINVAL;
	}

	RCU_INIT_POINTER(ctl_ep.client, NULL);

	spin_unlock(&client_lock);

	synchronize_rcu();
	kfree(client);

	return 0;
}
EXPORT_SYMBOL(rmnet_ctl_unregister_client);

int rmnet_ctl_send_client(void *handle, struct sk_buff *skb)
{
	struct rmnet_ctl_client *client = (struct rmnet_ctl_client *)handle;
	struct rmnet_ctl_dev *dev;
	int rc = -EINVAL;

	if (client != rcu_dereference(ctl_ep.client)) {
		kfree_skb(skb);
		return rc;
	}

	rmnet_ctl_log_info("TX", skb->data, skb->len);

	rcu_read_lock();

	dev = rcu_dereference(ctl_ep.dev);
	if (dev && dev->xmit)
		rc = dev->xmit(dev, skb);
	else
		kfree_skb(skb);

	rcu_read_unlock();

	if (rc)
		rmnet_ctl_log_err("TXE", rc, NULL, 0);

	return rc;
}
EXPORT_SYMBOL(rmnet_ctl_send_client);

static struct rmnet_ctl_client_if client_if = {
	.reg = rmnet_ctl_register_client,
	.dereg = rmnet_ctl_unregister_client,
	.send = rmnet_ctl_send_client,
};

struct rmnet_ctl_client_if *rmnet_ctl_if(void)
{
	return &client_if;
}
EXPORT_SYMBOL(rmnet_ctl_if);
