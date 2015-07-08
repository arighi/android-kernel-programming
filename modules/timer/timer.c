#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/hardirq.h>

static void timer_handler(unsigned long __data);

/* TIMER_INITIALIZER(function, expires, data) */
static struct timer_list timer =
		TIMER_INITIALIZER(timer_handler, 0, 0);

static void fire_timer(void)
{
	mod_timer(&timer, jiffies + msecs_to_jiffies(1000));
}

static void timer_handler(unsigned long __data)
{
	printk(KERN_INFO "%s: %d %d\n", __func__,
			in_irq() != 0, in_softirq() != 0);
	fire_timer();
}

static int __init my_timer_init(void)
{
	init_timer_deferrable(&timer);
	fire_timer();

	return 0;
}

static void __exit my_timer_exit(void)
{
	del_timer_sync(&timer);
}

module_init(my_timer_init);
module_exit(my_timer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_DESCRIPTION("power-efficient kernel timer");
