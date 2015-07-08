/*
 * fchar: fast character device
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
 * Copyright (C) 2015 Andrea Righi <righi.andrea@gmail.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/splice.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "fchar.h"

#define fchar_trace(msg...)					\
	do {							\
		if (unlikely(fchar_debug))			\
			printk(KERN_DEBUG "fchar: " msg);	\
	} while (0)
/*
 * FCHAR_VM_FAULT: set this to use the VM fault() method, instead of
 * remap_pfn_range(), to remap on a page-by-page basis (on-demand mapping).
 *
 * NOTE: one of the limitations of fault() is that it can only handle
 * situations where the relevant physical memory has a corresponding struct
 * page. Those structures exist for main memory, but they do not exist when the
 * memory is, for example, on a peripheral device and mapped into a PCI I/O
 * memory region. In such cases, drivers must explicitly map the memory into
 * user space with remap_pfn_range() instead of using the fault() method.
 */
#undef FCHAR_VM_FAULT

#define FCHAR_NR		1
#define DEFAULT_FCHAR_SIZE	(PAGE_SIZE * 8)

static int fchar_size = DEFAULT_FCHAR_SIZE;
module_param(fchar_size, int, 0);
MODULE_PARM_DESC(fchar_size, "Size of the device in bytes");

static int fchar_debug;
module_param(fchar_debug, int, 0644);
MODULE_PARM_DESC(fchar_debug, "Enable module debugging");

/* Fast-character device structures */
static int major;
static struct class *fchar_class;
static struct cdev fchar_cdev;
static unsigned char *fchar_data;

static inline size_t size_inside_page(unsigned long start,
				      unsigned long size)
{
	unsigned long sz;

	sz = PAGE_SIZE - (start & (PAGE_SIZE - 1));

	return min(sz, size);
}

static int fchar_open(struct inode *inode, struct file *filp)
{
	fchar_trace("%s\n", __func__);
	return 0;
}

static int fchar_release(struct inode *inode, struct file *filp)
{
	fchar_trace("%s\n", __func__);
	return 0;
}

static ssize_t fchar_read(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	ssize_t chunk, read;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	fchar_trace("%s(%zd, %lld)\n", __func__, count, *ppos);
	read = 0;
	while (count > 0) {
		loff_t p = *ppos;
		ssize_t copied;

		chunk = size_inside_page(p, count);
		if (p >= fchar_size)
			break;
		if (p + chunk > fchar_size)
			chunk = fchar_size - p;

		copied = copy_to_user(buf, fchar_data + p, chunk);

		read += chunk - copied;
		*ppos += chunk - copied;
		if (copied)
			break;
		if (signal_pending(current))
			return read ? read : -ERESTARTSYS;
		buf += chunk;
		count -= chunk;

		cond_resched();
	}
	return read;
}

static ssize_t fchar_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	ssize_t chunk, written;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	fchar_trace("%s(%zd, %lld)\n", __func__, count, *ppos);
	written = 0;
	while (count > 0) {
		loff_t p = *ppos;
		ssize_t copied;

		chunk = size_inside_page(p, count);
		if (p >= fchar_size)
			break;
		if (p + chunk > fchar_size)
			chunk = fchar_size - p;

		copied = copy_from_user(fchar_data + p, buf, chunk);
		written += chunk - copied;
		*ppos += chunk - copied;
		if (copied)
			break;
		if (signal_pending(current))
			return written ? written : -ERESTARTSYS;
		buf += chunk;
		count -= chunk;

		cond_resched();
	}
	return written;
}

static long fchar_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *ptr = (void __user *)arg;
	int ret;

	fchar_trace("%s(%#x, %#lx)\n", __func__, cmd, arg);

	if (_IOC_TYPE(cmd) != FCHAR_IOC_MAGIC ||
			_IOC_NR(cmd) > FCHAR_IOC_MAX_NR)
		return -EINVAL;
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (!access_ok(VERIFY_READ, ptr, _IOC_SIZE(cmd)))
			return -EFAULT;
	} else if (_IOC_DIR(cmd) & _IOC_READ) {
		if (!access_ok(VERIFY_WRITE, ptr, _IOC_SIZE(cmd)))
			return -EFAULT;
	}
	switch (cmd) {
	case FCHAR_IOCGSIZE:
		ret = __put_user(fchar_size, (int __user *)ptr);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

void fchar_vma_open(struct vm_area_struct *vma)
{
	fchar_trace("%s: virt = %#lx, phys = %#lx\n",
		__func__, vma->vm_start,
		vmalloc_to_pfn(fchar_data) << PAGE_SHIFT);
}

void fchar_vma_close(struct vm_area_struct *vma)
{
	fchar_trace("%s: virt = %#lx, phys = %#lx\n",
		__func__, vma->vm_start,
		vmalloc_to_pfn(fchar_data) << PAGE_SHIFT);
}

#ifdef FCHAR_VM_FAULT
static int fchar_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	const void *pos;

	if ((vmf->pgoff << PAGE_SHIFT) >= fchar_size)
		return VM_FAULT_SIGBUS;
	pos = fchar_data + (vmf->pgoff << PAGE_SHIFT);
	vmf->page = vmalloc_to_page(pos);

	get_page(vmf->page);

	return 0;
}
#else /* FCHAR_VM_FAULT */
#define fchar_vm_fault	NULL
#endif /* FCHAR_VM_FAULT  */

static struct vm_operations_struct fchar_mem_ops = {
	.open	= fchar_vma_open,
	.close	= fchar_vma_close,
	.fault = fchar_vm_fault,
};

static int fchar_mmap(struct file *filp, struct vm_area_struct *vma)
{
#ifndef FCHAR_VM_FAULT
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pfn;
	const void *pos;

	if (unlikely(size > fchar_size))
		return -EFAULT;

	fchar_trace("%s: virt = %#lx, phys = %#lx\n",
		__func__, vma->vm_start,
		vmalloc_to_pfn(fchar_data) << PAGE_SHIFT);

	pos = fchar_data;
	while (size > 0) {
		pfn = vmalloc_to_pfn(pos);
		if (remap_pfn_range(vma, start, pfn, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}
#endif /* FCHAR_USE_FAULT */

	/* remove from LRU scan and core dump */
	vma->vm_flags |= VM_LOCKED | VM_IO;
	vma->vm_ops = &fchar_mem_ops;
	fchar_vma_open(vma);

	return 0;
}

static const struct file_operations fchar_fops = {
	.open		= fchar_open,
	.release	= fchar_release,
	.read		= fchar_read,
	.write		= fchar_write,
	.unlocked_ioctl	= fchar_ioctl, /* don't need BKL */
	.mmap		= fchar_mmap,
	.owner		= THIS_MODULE,
};

static char *fchar_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "fchar/%s", dev_name(dev));
}

static void *alloc_data(ssize_t size)
{
	void *mem, *addr;

	WARN_ON(size & (PAGE_SIZE - 1));
	size = PAGE_ALIGN(size);

	mem = vmalloc(size);
	if (!mem)
		return NULL;
	if (unlikely((size_t)mem & (PAGE_SIZE - 1))) {
		vfree(mem);
		WARN_ON(1);
		return NULL;
	}

	memset(mem, 0, size);
	addr = mem;
	while (size > 0) {
		/*
		 * PG_reserved is set for special pages, which can never be
		 * swapped out or considered during LRU scan.
		 */
		SetPageReserved(vmalloc_to_page(addr));
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	return mem;
}

static void free_data(const void *mem, ssize_t size)
{
	const void *addr;

	if (!mem)
		return;

	BUG_ON(!is_vmalloc_addr(mem));
	BUG_ON(size & (PAGE_SIZE - 1));

	addr = mem;
	while (size > 0) {
		ClearPageReserved(vmalloc_to_page(addr));
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

static int __init fchar_init(void)
{
	dev_t dev_id;
	int ret;

	fchar_data = alloc_data(fchar_size);
	if (!fchar_data)
		return -ENOMEM;

	/* Register major/minor numbers */
	ret = alloc_chrdev_region(&dev_id, 0, FCHAR_NR, "fchar");
	if (ret)
		goto error_alloc;
	major = MAJOR(dev_id);

	/* Add the character device to the system */
	cdev_init(&fchar_cdev, &fchar_fops);
	ret = cdev_add(&fchar_cdev, dev_id, FCHAR_NR);
	if (ret) {
		kobject_put(&fchar_cdev.kobj);
		goto error_region;
	}

	/* Create a class structure */
	fchar_class = class_create(THIS_MODULE, "fchar");
	if (IS_ERR(fchar_class)) {
		printk(KERN_ERR "error creating fchar class\n");
		ret = PTR_ERR(fchar_class);
		goto error_cdev;
	}
	fchar_class->devnode = fchar_devnode;

	/* Register the device with sysfs */
	device_create(fchar_class, NULL, MKDEV(major, 0), NULL, "fcharctl");
	printk(KERN_INFO "register new fchar device: %d,%d\n", major, 0);

out:
	return ret;
error_cdev:
	cdev_del(&fchar_cdev);
error_region:
	unregister_chrdev_region(dev_id, FCHAR_NR);
error_alloc:
	free_data(fchar_data, fchar_size);
	goto out;
}

static void __exit fchar_exit(void)
{
	dev_t dev_id = MKDEV(major, 0);

        device_destroy(fchar_class, dev_id);
	class_destroy(fchar_class);
	cdev_del(&fchar_cdev);
	unregister_chrdev_region(dev_id, FCHAR_NR);
	free_data(fchar_data, fchar_size);
}

module_init(fchar_init);
module_exit(fchar_exit);

MODULE_DESCRIPTION("fast character device");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_LICENSE("GPL");
