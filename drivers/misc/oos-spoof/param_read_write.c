#include <linux/fs.h>
#include <linux/miscdevice.h>

static ssize_t param_read(struct file *file, char __user * buff, size_t count, loff_t * pos)
{
	return count;
}

static ssize_t param_write(struct file *file, const char __user * buff, size_t count, loff_t * pos)
{
	return count;
}

static const struct file_operations param_fops = {
	.owner = THIS_MODULE,
	.read = param_read,
	.write = param_write,
	.llseek = default_llseek,
};

struct miscdevice param_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "param",
	.fops = &param_fops,
};

static int __init param_device_init(void)
{
	int ret;
	ret = misc_register(&param_misc);
	if (ret) {
		pr_err("misc_register failure %d\n", ret);
		return -1;
	}
	return ret;
}
device_initcall(param_device_init);
