#include <linux/init.h>
#include <linux/module.h>

static const char hello_msg[] = "Hello World!";

static int __init hello_init(void)
{
	printk(KERN_INFO "(%zu) jiffies_64 is at %p\n",
	       sizeof(unsigned long),
	       (void *)virt_to_phys(&jiffies_64));
	return 0;
}

static void __exit hello_exit(void)
{
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <andrea@betterlinux.com>");
MODULE_DESCRIPTION("Eudyptula Challenge task 01");
