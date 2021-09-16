#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#define FS_FG_INFO_PATH "fg_info"
#define FS_FG_UIDS "fg_uids"
#define FG_RW (S_IWUSR|S_IRUSR|S_IWGRP|S_IRGRP|S_IWOTH|S_IROTH)

static struct proc_dir_entry *fg_dir;

static int fg_uids_show(struct seq_file *m, void *v)
{
	return 0;
}

static int fg_uids_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, fg_uids_show, inode);
}

static ssize_t fg_uids_write(struct file *file, const char __user *buf,
							size_t count, loff_t *ppos)
{
	return count;
}

static const struct file_operations proc_fg_uids_operations = {
	.open       = fg_uids_open,
	.read       = seq_read,
	.write      = fg_uids_write,
	.llseek     = seq_lseek,
	.release    = single_release,
};

static void uids_proc_fs_init(struct proc_dir_entry *p_parent)
{
	if (!p_parent)
		return;

	proc_create(FS_FG_UIDS, FG_RW, p_parent, &proc_fg_uids_operations);
}

static int __init fg_uids_init(void)
{
	struct proc_dir_entry *p_parent;

	p_parent = proc_mkdir(FS_FG_INFO_PATH, fg_dir);
	if (!p_parent) {
		return -ENOMEM;
	}
	uids_proc_fs_init(p_parent);
	return 0;
}
module_init(fg_uids_init);
