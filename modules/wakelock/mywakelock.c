#include <linux/init.h>
#include <linux/module.h>
#include <linux/wakelock.h>

static struct wake_lock my_wake_lock;

static void my_wake_lock_start(void)
{
	wake_lock_init(&my_wake_lock, WAKE_LOCK_IDLE, "my_wake_lock");
	wake_lock(&my_wake_lock);
}

static void my_wake_lock_stop(void)
{
	wake_unlock(&my_wake_lock);
	wake_lock_destroy(&my_wake_lock);
}

static int __init my_wake_init(void)
{
	my_wake_lock_start();
	return 0;
}

static void __exit my_wake_exit(void)
{
	my_wake_lock_stop();
}

module_init(my_wake_init);
module_exit(my_wake_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <andrea@betterlinux.com>");
MODULE_DESCRIPTION("wake lock example");
