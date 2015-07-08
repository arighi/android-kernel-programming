#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>

#define TIME_MS	1000

static char mychar_str[32] = { [0 ... sizeof(mychar_str) - 1] = 'A' };

static void timer_handler(unsigned long __data);

/* TIMER_INITIALIZER(function, expires, data) */
static struct timer_list timer =
		TIMER_INITIALIZER(timer_handler, 0, 0);

static void fire_timer(void)
{
	mod_timer(&timer, jiffies + msecs_to_jiffies(TIME_MS));
}

static void update_buffer(void)
{
	int i;

	printk(KERN_INFO "%s: %d %d\n", __func__,
		in_irq() != 0, in_softirq() != 0);
	for (i = 0; i < sizeof(mychar_str) - 1; i++)
		mychar_str[i] = 'A' + ((mychar_str[i] - 'A') + 1) % ('Z' - 'A' + 1);
	mychar_str[sizeof(mychar_str) - 1] = '\0';
}

static void timer_handler(unsigned long __data)
{
	update_buffer();
	fire_timer();
}

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
	int ret;

	ret = misc_register(&mychar_dev);
	if (ret < 0)
		return ret;
	init_timer_deferrable(&timer);
	fire_timer();

	return 0;
}

static void __exit mychar_exit(void)
{
	del_timer_sync(&timer);
	misc_deregister(&mychar_dev);
}

module_init(mychar_init);
module_exit(mychar_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_DESCRIPTION("Enhanced character device example");
