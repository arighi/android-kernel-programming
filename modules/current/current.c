#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>

static int __init current_init(void)
{
	printk(KERN_INFO "The current process is \"%s\" (pid %i)\n",
	       current->comm, current->pid);
	return 0;
}

static void __exit current_exit(void)
{
}

module_init(current_init);
module_exit(current_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_DESCRIPTION("Show current task");
