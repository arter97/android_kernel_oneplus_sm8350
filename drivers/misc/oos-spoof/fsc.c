#include <linux/module.h>
#include <linux/moduleparam.h>

static unsigned int enable;
module_param(enable, uint, 0644);

static int null_store(const char *buf, const struct kernel_param *kp)
{
	return 0;
}

static struct kernel_param_ops null_ops = {
	.set = null_store,
};
module_param_cb(allow_list_add, &null_ops, NULL, 0220);
module_param_cb(allow_list_del, &null_ops, NULL, 0220);
