#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

static char mychar_str[32] = "hello world\n";

static ssize_t
mychar_read(struct file *filp, char __user *buffer, size_t len, loff_t *off)
{
	return simple_read_from_buffer(buffer, len, off,
				       mychar_str, strlen(mychar_str));
}

static ssize_t
mychar_write(struct file *filp, const char __user *buffer,
	     size_t len, loff_t *off)
{
	char buf[32] = {};
	int ret;

	ret = simple_write_to_buffer(buf, sizeof(buf), off, buffer, len);
	if (ret < 0)
		return ret;
	if (len >= sizeof(buf))
		return -E2BIG;
	strncpy(mychar_str, buf, sizeof(mychar_str));

	return -EINVAL;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = mychar_read,
	.write = mychar_write,
};

static struct miscdevice mychar_dev = {
	.name = "mychar",
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &fops,
};

static int __init mychar_init(void)
{
	return misc_register(&mychar_dev);
}

static void __exit mychar_exit(void)
{
	misc_deregister(&mychar_dev);
}

module_init(mychar_init);
module_exit(mychar_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_DESCRIPTION("Character device example");
