/*
 * srd: kernel module to create a fast block device in RAM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Copyright (C) 2012 Andrea Righi <andrea@betterlinux.com>
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/hugetlb.h>
#include <linux/genhd.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/vmalloc.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/hdreg.h>

#define srd_trace(msg...)					\
	do {							\
		if (unlikely(srd_debug))			\
			printk(KERN_DEBUG "srd: " msg);		\
	} while (0)

#define RAMDISK_DEFAULT_SIZE	(PAGE_SIZE * 4)
#define SECTOR_SHIFT		9

static int srd_size = RAMDISK_DEFAULT_SIZE;
module_param(srd_size, int, 0);
MODULE_PARM_DESC(srd_size, "Size of ram disk in bytes");

static int srd_debug;
module_param(srd_debug, int, 0644);
MODULE_PARM_DESC(srd_debug, "Enable module debugging");

static int nr_scan_pages = 1024;
module_param(nr_scan_pages, int, 0644);
MODULE_PARM_DESC(nr_scan_pages, "Pages to scan for merging");

static int major;

static const char proc_filename[] = "ramdisk_debug";
static struct proc_dir_entry *proc_file;

/* Count the amount of allocated pages */
static atomic_t tot_alloc_pages = ATOMIC_INIT(0);

/* Count the amount of merged pages */
static atomic_t tot_merge_pages = ATOMIC_INIT(0);

struct srd_page {
	void *data;
	atomic_t refcnt;
	u32 checksum;
};

struct srd_device {
	struct gendisk *disk;
	struct request_queue *queue;
	struct srd_page **pages;
};

static struct srd_device device;

static DEFINE_SPINLOCK(srd_lock);

static void calc_checksum(struct srd_page *page)
{
	page->checksum = jhash2(page->data, PAGE_SIZE / sizeof(u32), 17);
}

static struct srd_page *srd_alloc_page(void)
{
	struct srd_page *page;

	page = kzalloc(sizeof(*page), GFP_KERNEL);
	if (unlikely(!page))
		return NULL;
	page->data = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (unlikely(!page->data)) {
		kfree(page);
		return NULL;
	}
	atomic_set(&page->refcnt, 1);
	calc_checksum(page);

	atomic_inc(&tot_alloc_pages);

	return page;
}

static void srd_free_page(struct srd_page *page)
{
	if (!page)
		return;
        if (atomic_dec_and_test(&page->refcnt)) {
                kfree(page->data);
		kfree(page);

		atomic_dec(&tot_alloc_pages);
	} else {
		atomic_dec(&tot_merge_pages);
	}
}

static bool
pages_identical(const struct srd_page *page1, const struct srd_page *page2)
{
	void *addr1, *addr2;

	addr1 = page1->data;
	addr2 = page2->data;

	if (page1->checksum != page2->checksum)
		return false;
	else
		return !memcmp(addr1, addr2, PAGE_SIZE);
}

static int last_idx;

static void merge_duplicate_pages(size_t size)
{
	unsigned long nr_to_scan = nr_scan_pages;
	int i, j, count;

	spin_lock(&srd_lock);
	for (count = 0; count < size >> PAGE_SHIFT; count++) {
		i = last_idx;
		for (j = i + 1; j < size >> PAGE_SHIFT; j++) {
			if (device.pages[i] == NULL || device.pages[j] == NULL)
				continue;
			if (device.pages[i] == device.pages[j])
				continue;
			if (!pages_identical(device.pages[i], device.pages[j]))
				continue;
			/* Merge pages */
			srd_free_page(device.pages[j]);

			device.pages[j] = device.pages[i];
			atomic_inc(&device.pages[i]->refcnt);

			atomic_inc(&tot_merge_pages);
		}
		last_idx = (last_idx + 1) % (size >> PAGE_SHIFT);
		if (!nr_to_scan--)
			break;
	}
	spin_unlock(&srd_lock);

	cond_resched();
}

/* Dispatch a single bvec of a bio */
static int srd_dispatch_bvec(struct page *page, unsigned int count,
		unsigned int off, int rw, unsigned long start)
{
	struct srd_page *srd_page, *new_srd_page;
	void *mem;
	int idx, offset, ret = 0;

	srd_trace("start = %lu, count = %u, op = %s\n",
			start, count, rw == READ ? "READ" : "WRITE");

	new_srd_page = srd_alloc_page();
	if (unlikely(!new_srd_page))
		return -ENOMEM;

	spin_lock(&srd_lock);

	idx = start >> PAGE_SHIFT;
	srd_page = device.pages[idx];

	/* Handle unallocated and copy-on-write pages */
	if (srd_page == NULL) {
		srd_page = new_srd_page;
		device.pages[idx] = srd_page;
	} else if (rw == WRITE && atomic_read(&srd_page->refcnt) > 1) {
		/* Copy on write */
		memcpy(new_srd_page->data, srd_page->data, PAGE_SIZE);
		srd_free_page(srd_page);
		srd_page = new_srd_page;
		device.pages[idx] = srd_page;
	}

	offset = start % PAGE_SIZE;
	WARN_ON_ONCE(offset + count > PAGE_SIZE);

	/*
	 * kmap/kunmap_atomic is faster than kmap/kunmap, because no global
	 * lock is needed and because the kmap code must perform a global TLB
	 * invalidation in flush_all_zero_pkmaps() when the kmap pool wraps.
	 *
	 * However, when holding an atomic kmap it is not legal to sleep, so
	 * atomic kmap is appropriate for short code paths only.
	 */
	mem = kmap_atomic(page, KM_USER1);
	if (rw == READ)
		memcpy(mem + off, srd_page->data + offset, count);
	else
		memcpy(srd_page->data + offset, mem + off, count);
	kunmap_atomic(mem, KM_USER1);

	if (rw == WRITE)
		calc_checksum(srd_page);

	spin_unlock(&srd_lock);

	if (new_srd_page != srd_page)
		srd_free_page(new_srd_page);

	return ret;
}

/*
 * This function hooks directly the creation of IO requests: no-queue mode.
 *
 * Instead of reordering requests into the request_queue it immediately
 * dispatches them, completely bypassing the IO scheduler/elevator layers.
 *
 * NOTE: request_queue is simply unused and bio can be dispatched in lockless
 * way.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
static int srd_make_request(struct request_queue *q, struct bio *bio)
#else
static void srd_make_request(struct request_queue *q, struct bio *bio)
#endif
{
	unsigned long start = bio->bi_sector << SECTOR_SHIFT;
	int rw = bio_rw(bio);
	struct bio_vec *bvec;
	int i;
	int ret = -EIO;

	if ((start + bio->bi_size) > srd_size)
		goto out;
	if (rw == READA)
		rw = READ;

	bio_for_each_segment(bvec, bio, i) {
		unsigned int len = bvec->bv_len;

		ret = srd_dispatch_bvec(bvec->bv_page, len,
				bvec->bv_offset, rw, start);
		if (ret)
			break;
		start += len;
	}
out:
	/* Signal the completion to the creator of the bio structure */
	bio_endio(bio, ret);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
	return 0;
#endif
}

static const struct block_device_operations srd_ops = {
	.owner  = THIS_MODULE,
};

static void *alloc_data(ssize_t size)
{
	void *mem;

	if (unlikely(size & (PAGE_SIZE - 1)))
		return NULL;
	size = (size >> PAGE_SHIFT) * sizeof(struct srd_page *);
	if (size < PAGE_SIZE)
		mem = kmalloc(size, GFP_KERNEL);
	else
		mem = vmalloc(size);
	if (likely(mem))
		memset(mem, 0, size);

	return mem;
}

static void free_data(struct srd_page **mem, ssize_t size)
{
	int i;

	if (!mem)
		return;
	for (i = 0; i < size >> PAGE_SHIFT; i++)
		srd_free_page(mem[i]);
	WARN_ON_ONCE(atomic_read(&tot_alloc_pages));
	size = (size >> PAGE_SHIFT) * sizeof(struct srd_page *);
	if (size < PAGE_SIZE)
		kfree(mem);
	else
		vfree(mem);
}

static ssize_t ramdisk_debug_write(struct file *file,
                const char __user *ubuf, size_t count, loff_t *pos)
{
        if (!capable(CAP_SYS_ADMIN))
                return -EACCES;
	merge_duplicate_pages(srd_size);

	return count;
}

static int ramdisk_debug_show(struct seq_file *m, void *v)
{
	seq_printf(m, "alloc pages: %d\n", atomic_read(&tot_alloc_pages));
	seq_printf(m, "merge pages: %d\n", atomic_read(&tot_merge_pages));
	seq_printf(m, "last_idx: %d\n", last_idx);
	return 0;
}

static int ramdisk_debug_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, ramdisk_debug_show, inode);
}

static const struct file_operations ramdisk_debug_file_ops = {
	.open		= ramdisk_debug_open,
	.read		= seq_read,
	.write		= ramdisk_debug_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init srd_init(void)
{
	int ret = 0;

	if (!srd_size)
		return -EINVAL;

	/* Register the block device */
	major = register_blkdev(0, "srd");
	if (major < 0) {
		printk(KERN_WARNING "srd: failed to register block device\n");
		return -EIO;
	}

	/* Allocate pages */
	device.pages = alloc_data(srd_size);
	if (!device.pages) {
		printk(KERN_WARNING "srd: could not allocate memory\n");
		ret = -ENOMEM;
		goto out_unregister;
	}

	/* Allocate a request queue */
	device.queue = blk_alloc_queue(GFP_KERNEL);
	if (!device.queue) {
		printk(KERN_WARNING "srd: could not allocate request queue\n");
		ret = -ENOMEM;
		goto out_free_data;
	}
	blk_queue_make_request(device.queue, srd_make_request);
	/*
	 * Avoid usage of bounce buffers (temporary buffer to perform DMA
	 * operation, e.g., for high-memory pages).
	 */
	blk_queue_bounce_limit(device.queue, BLK_BOUNCE_ANY);
	/* Set as non-rotational device */
	queue_flag_set_unlocked(QUEUE_FLAG_VIRT, device.queue);

	/* No limit for discard requests */
	device.queue->limits.discard_granularity = PAGE_SIZE;
	device.queue->limits.max_discard_sectors = UINT_MAX;
	device.queue->limits.discard_zeroes_data = 1;
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, device.queue);

	/* Allocate the gendisk structure */
	device.disk = alloc_disk(1);
	if (!device.disk) {
		printk(KERN_WARNING "srd: failed to allocate gendisk\n");
		ret = -ENOMEM;
		goto out_free_queue;
	}
	device.disk->major = major;
	device.disk->first_minor = 0;
	device.disk->fops = &srd_ops;
	device.disk->private_data = &device;
	device.disk->queue = device.queue;
	device.disk->flags |= GENHD_FL_SUPPRESS_PARTITION_INFO;

	strcpy(device.disk->disk_name, "srd0");
	set_capacity(device.disk, srd_size >> SECTOR_SHIFT);

	proc_file = proc_create(proc_filename, 0644,
					NULL, &ramdisk_debug_file_ops);
	if (unlikely(!proc_file)) {
		printk(KERN_WARNING "srd: failed to create proc file\n");
		ret = -ENOMEM;
		goto out_free_disk;
	}

	add_disk(device.disk);
	printk(KERN_INFO "srd0: %d bytes (%d pages)\n",
		srd_size, srd_size >> PAGE_SHIFT);
out:
	return ret;

out_free_disk:
	put_disk(device.disk);
out_free_queue:
	blk_cleanup_queue(device.queue);
out_free_data:
	free_data(device.pages, srd_size);
out_unregister:
	unregister_blkdev(major, "srd");
	goto out;
}

static void __exit srd_exit(void)
{
	remove_proc_entry(proc_filename, NULL);
	del_gendisk(device.disk);
	put_disk(device.disk);
	blk_cleanup_queue(device.queue);
	unregister_blkdev(major, "srd");
	free_data(device.pages, srd_size);
}

module_init(srd_init);
module_exit(srd_exit);

MODULE_DESCRIPTION("create a fast block device in RAM");
MODULE_AUTHOR("Andrea Righi <andrea@betterlinux.com>");
MODULE_LICENSE("GPL");
