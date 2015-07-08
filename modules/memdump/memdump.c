#include <linux/init.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/pfn.h>
#include <linux/kthread.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <linux/wakelock.h>

#define DBG(fmt, args...)						\
	do {								\
		printk(KERN_INFO "[memdump] "fmt"\n", ## args);		\
	} while (0)

#define SYSTEM_RAM_STRING	"System RAM"

extern struct resource iomem_resource;

static int tcp_port = 4444;
module_param(tcp_port, int, 0644);
MODULE_PARM_DESC(tcp_port, "Port used to send memory dump information");

static struct socket *server;
static struct socket *client;
static struct wake_lock memdump_wake_lock;

static void memdump_wake_lock_start(void)
{
	wake_lock_init(&memdump_wake_lock, WAKE_LOCK_SUSPEND,
		       "memdump_wake_lock");
	wake_lock(&memdump_wake_lock);
}

static void memdump_wake_lock_stop(void)
{
	wake_unlock(&memdump_wake_lock);
	wake_lock_destroy(&memdump_wake_lock);
}

static int setup_tcp(void)
{
	struct sockaddr_in saddr = {};
	mm_segment_t fs;
	int buffersize = PAGE_SIZE;
	int ret;

	ret = sock_create_kern(AF_INET, SOCK_STREAM, IPPROTO_TCP, &server);
	if (unlikely(ret < 0)) {
		DBG("error creating socket");
		return ret;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(tcp_port);
	saddr.sin_addr.s_addr = INADDR_ANY;

	fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sock_setsockopt(server, SOL_SOCKET, SO_SNDBUF,
			(void *)&buffersize, sizeof(buffersize));
	set_fs(fs);

	if (unlikely(ret < 0)) {
		DBG("error setting buffsize");
		goto out_err;
	}

	ret = server->ops->bind(server, (struct sockaddr *)&saddr,
				sizeof(saddr));
	if (unlikely(ret < 0)) {
		DBG("error binding socket");
		goto out_err;
	}

	ret = server->ops->listen(server, 1);
	if (unlikely(ret < 0)) {
		DBG("error listening on socket");
		goto out_err;
	}

	ret = sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &client);
	if (ret < 0) {
		DBG("error creating accept socket");
		goto out_err;
	}
out:
	return ret;

out_err:
	server->ops->shutdown(server, 0);
	server->ops->release(server);

	goto out;
}

static int dump_memory_range_tcp(struct resource *res)
{
	mm_segment_t fs;
	resource_size_t i, len;
	struct page *p;
	void *v;
	long s;
	struct iovec iov;
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
	int ret = 0;

	fs = get_fs();
	set_fs(KERNEL_DS);
	for (i = res->start; i <= res->end; i += PAGE_SIZE) {
		p = pfn_to_page((i) >> PAGE_SHIFT);
		len = min_t(size_t, PAGE_SIZE, (size_t) (res->end - i + 1));

		v = kmap(p);
		if (unlikely(!v)) {
			ret = -ENOMEM;
			break;
		}
		iov.iov_base = v;
		iov.iov_len = len;

		s = sock_sendmsg(client, &msg, len);

		kunmap(p);

		if (s != len) {
			DBG("error sending page");
			ret = s;
			break;
		}
	}
	set_fs(fs);

	return ret;
}

static int tcp_main_loop(void)
{
	struct resource *p;
	int ret = 0;

	ret = client->ops->accept(server, client, 0);
	if (ret < 0)
		goto out;
	for (p = iomem_resource.child; p ; p = p->sibling) {
		if (strncmp(p->name, SYSTEM_RAM_STRING,
				sizeof(SYSTEM_RAM_STRING)))
			continue;
		ret = dump_memory_range_tcp(p);
		if (unlikely(ret)) {
			DBG("write error");
			goto out;
		}
	}
out:
	client->ops->shutdown(client, 0);
	client->ops->release(client);

	return ret;
}

static struct task_struct *memory_dumper_task;

static int memory_dumper(void *dummy)
{
	int ret;

	set_user_nice(current, 0);
	set_current_state(TASK_INTERRUPTIBLE);

	ret = setup_tcp();
	if (unlikely(ret < 0))
		return ret;
	while (!kthread_should_stop())
		tcp_main_loop();
	if (server && server->ops) {
		server->ops->shutdown(server, 0);
		server->ops->release(server);
	}

	return 0;
}

static int __init memdump_init(void)
{
	memdump_wake_lock_start();
	memory_dumper_task = kthread_run(memory_dumper, NULL, "kmem_dumper");
	return memory_dumper_task ? 0 : -ENOMEM;
}

static void __exit memdump_exit(void)
{
	memdump_wake_lock_stop();
	force_sig(SIGKILL, memory_dumper_task);
	kthread_stop(memory_dumper_task);
}

module_init(memdump_init);
module_exit(memdump_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_DESCRIPTION("system memory dumper");
