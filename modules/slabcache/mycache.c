#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>

struct my_object {
	int a;
	int b;
};

static struct kmem_cache *mycachep __read_mostly;

struct my_object *mycache_alloc(gfp_t flags)
{
	return kmem_cache_zalloc(mycachep, flags);
}
EXPORT_SYMBOL(mycache_alloc);

void mycache_free(struct my_object *item)
{
	return kmem_cache_free(mycachep, item);
}
EXPORT_SYMBOL(mycache_free);

static int __init slabcache_init(void)
{
	mycachep = kmem_cache_create("mycache",
				     sizeof(struct my_object),
				     0, 0, NULL);
        if (unlikely(!mycachep)) {
                printk(KERN_ERR "%s: failed to create slab cache\n", __func__);
                return -ENOMEM;
        }
        return 0;
}

static void __exit slabcache_exit(void)
{
	kmem_cache_destroy(mycachep);
}

module_init(slabcache_init);
module_exit(slabcache_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_DESCRIPTION("Custom SLAB cache allocator");
