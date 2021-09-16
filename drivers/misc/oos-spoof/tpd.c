#include <linux/module.h>
#include <linux/moduleparam.h>

static int null_store(const char *buf, const struct kernel_param *kp)
{
	return 0;
}

static struct kernel_param_ops null_ops = {
	.set = null_store,
};

module_param_cb(tpd_cmds, &null_ops, NULL, 0664);
module_param_cb(tpd_dynamic, &null_ops, NULL, 0664);

static int tpd_enable = 0;
module_param(tpd_enable, int, 0644);

module_param_cb(tpd_id, &null_ops, NULL, 0664);
