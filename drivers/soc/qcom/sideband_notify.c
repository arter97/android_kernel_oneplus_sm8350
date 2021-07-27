// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020,2021, The Linux Foundation. All rights reserved.
 */

#include <linux/notifier.h>
#include <soc/qcom/sb_notification.h>

static BLOCKING_NOTIFIER_HEAD(sb_notifier_list);

/**
 * sb_register_evt_listener - registers a notifier callback
 * @nb: pointer to the notifier block for the callback events
 */
int sb_register_evt_listener(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&sb_notifier_list, nb);
}
EXPORT_SYMBOL(sb_register_evt_listener);

/**
 * sb_unregister_evt_listener - un-registers a notifier callback
 * registered previously.
 * @nb: pointer to the notifier block for the callback events
 */
int sb_unregister_evt_listener(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&sb_notifier_list, nb);
}
EXPORT_SYMBOL(sb_unregister_evt_listener);

/**
 * sb_notifier_call_chain - send events to all registered listeners
 * as received from publishers.
 * @nb: pointer to the notifier block for the callback events
 */
int sb_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&sb_notifier_list, val, v);
}
EXPORT_SYMBOL(sb_notifier_call_chain);
