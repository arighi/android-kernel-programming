#include <linux/init.h>
#include <linux/module.h>

static const char hello_msg[] = "Hello World!";

static int __init hello_init(void)
{
	printk(KERN_INFO "%s\n", hello_msg);
	return 0;
}

static void __exit hello_exit(void)
{
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_DESCRIPTION("Hello world");
