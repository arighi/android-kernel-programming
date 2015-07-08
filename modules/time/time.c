#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/delay.h>

enum {
	BUSY_WAIT,
	CPU_BUSY_WAIT,
	SCHEDULER_BUSY_WAIT,
	SCHEDULER_WAIT,
	EVENT_WAIT,
	COMPLETION_WAIT,
};

static struct proc_dir_entry *busy_wait_file,
			*cpu_busy_wait_file,
			*scheduler_busy_wait_file,
			*scheduler_wait_file,
			*event_wait_file,
			*completion_wait_file;

static int delay = HZ;
module_param(delay, int, 0);
MODULE_PARM_DESC(delay, "Time to delay in jiffies");

static void busy_wait(u64 start, u64 end)
{
	mdelay(jiffies_to_msecs(end - start));
}

static void cpu_busy_wait(u64 start, u64 end)
{
	while (time_before64(get_jiffies_64(), end))
		cpu_relax();
}

static void scheduler_busy_wait(u64 start, u64 end)
{
	while (time_before64(get_jiffies_64(), end))
		schedule();
}

static void scheduler_wait(u64 start, u64 end)
{
	schedule_timeout_uninterruptible(end - start);
}

static void event_wait(u64 start, u64 end)
{
	wait_queue_head_t wait;

	init_waitqueue_head(&wait);
	wait_event_interruptible_timeout(wait, 0, end - start);
}

static void completion_wait(u64 start, u64 end)
{
	DECLARE_COMPLETION_ONSTACK(wait);

	wait_for_completion_interruptible_timeout(&wait, end - start);
}

static int time_read(char *buf, char **start, off_t off,
		int count, int *eof, void *data)
{
	struct timeval tv_start, tv_end;
	u64 time_start, time_end;
	char *out = buf;
	int len;

	time_start = get_jiffies_64();
	do_gettimeofday(&tv_start);
	switch ((long)data) {
	case BUSY_WAIT:
		busy_wait(time_start, time_start + delay);
		break;
	case CPU_BUSY_WAIT:
		cpu_busy_wait(time_start, time_start + delay);
		break;
	case SCHEDULER_BUSY_WAIT:
		scheduler_busy_wait(time_start, time_start + delay);
		break;
	case SCHEDULER_WAIT:
		scheduler_wait(time_start, time_start + delay);
		break;
	case EVENT_WAIT:
		event_wait(time_start, time_start + delay);
		break;
	case COMPLETION_WAIT:
		completion_wait(time_start, time_start + delay);
		break;
	default:
		WARN_ON(1);
		return 0;
	}
	do_gettimeofday(&tv_end);
	time_end = get_jiffies_64();

	out += sprintf(out, "%llu %lu\n",
			time_end - time_start,
			(tv_end.tv_sec - tv_start.tv_sec) * USEC_PER_SEC +
				(tv_end.tv_usec - tv_start.tv_usec));
	/* ignore "off" to get a real stream of data for subsequent calls */
	len = out - buf;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;
	*start = buf;

	return len;
}

static int __init time_init(void)
{
	busy_wait_file = create_proc_entry("busy_wait", 0444, NULL);
	if (unlikely(!busy_wait_file))
		return -ENOMEM;
	busy_wait_file->read_proc = time_read;
	busy_wait_file->data = BUSY_WAIT;

	cpu_busy_wait_file = create_proc_entry("cpu_busy_wait", 0444, NULL);
	if (unlikely(!cpu_busy_wait_file)) {
		remove_proc_entry("busy_wait", NULL);
		return -ENOMEM;
	}
	cpu_busy_wait_file->read_proc = time_read;
	busy_wait_file->data = (void *)CPU_BUSY_WAIT;

	scheduler_busy_wait_file = create_proc_entry("scheduler_busy_wait",
							0444, NULL);
	if (unlikely(!scheduler_busy_wait_file)) {
		remove_proc_entry("busy_wait", NULL);
		remove_proc_entry("cpu_busy_wait", NULL);
		return -ENOMEM;
	}
	scheduler_busy_wait_file->read_proc = time_read;
	scheduler_busy_wait_file->data = (void *)SCHEDULER_BUSY_WAIT;

	scheduler_wait_file = create_proc_entry("scheduler_wait", 0444, NULL);
	if (unlikely(!scheduler_wait_file)) {
		remove_proc_entry("busy_wait", NULL);
		remove_proc_entry("cpu_busy_wait", NULL);
		remove_proc_entry("scheduler_busy_wait", NULL);
		return -ENOMEM;
	}
	scheduler_wait_file->read_proc = time_read;
	scheduler_wait_file->data = (void *)SCHEDULER_WAIT;

	event_wait_file = create_proc_entry("event_wait", 0444, NULL);
	if (unlikely(!event_wait_file)) {
		remove_proc_entry("busy_wait", NULL);
		remove_proc_entry("cpu_busy_wait", NULL);
		remove_proc_entry("scheduler_busy_wait", NULL);
		remove_proc_entry("scheduler_wait", NULL);
		return -ENOMEM;
	}
	event_wait_file->read_proc = time_read;
	event_wait_file->data = (void *)EVENT_WAIT;

	completion_wait_file = create_proc_entry("completion_wait", 0444, NULL);
	if (unlikely(!completion_wait_file)) {
		remove_proc_entry("busy_wait", NULL);
		remove_proc_entry("cpu_busy_wait", NULL);
		remove_proc_entry("scheduler_busy_wait", NULL);
		remove_proc_entry("scheduler_wait", NULL);
		remove_proc_entry("event_wait", NULL);
		return -ENOMEM;
	}
	completion_wait_file->read_proc = time_read;
	completion_wait_file->data = (void *)COMPLETION_WAIT;

	return 0;
}

static void __exit time_exit(void)
{
	remove_proc_entry("busy_wait", NULL);
	remove_proc_entry("cpu_busy_wait", NULL);
	remove_proc_entry("scheduler_busy_wait", NULL);
	remove_proc_entry("scheduler_wait", NULL);
	remove_proc_entry("event_wait", NULL);
	remove_proc_entry("completion_wait", NULL);
}

module_init(time_init);
module_exit(time_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("timer test module");
MODULE_AUTHOR("Andrea Righi <arighi@develer.com");
