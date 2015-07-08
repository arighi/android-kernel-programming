#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mman.h>
#include <linux/uaccess.h>

static int rawmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static struct file_operations fops = {
	.mmap = rawmem_mmap,
};

static struct miscdevice rawmem_dev = {
	.name = "rawmem",
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &fops,
};

static int __init rawmem_init(void)
{
	return misc_register(&rawmem_dev);
}

static void __exit rawmem_exit(void)
{
	misc_deregister(&rawmem_dev);
}
module_init(rawmem_init);
module_exit(rawmem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Righi <righi.andrea@gmail.com>");
MODULE_DESCRIPTION("Map physical memory areas to user-space");
