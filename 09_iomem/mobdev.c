#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h> // kmalloc()
#include <linux/uaccess.h> // copy_to/from_user()
#include <linux/ioport.h>
#include <linux/err.h>
#include <asm/io.h>

#include "mobdev.h"

#define CLASS_NAME "mobica"
#define DEVICE_NAME "mobdev"

#define IO_MEM_SIZE 8

static const unsigned int mob_devs_num = 1;
static int dev_minor_start = 0;
static int dev_major = 0;

static struct mob_dev *mob_devices = NULL;
static struct class *mob_class = NULL;

// choose the correct address with help of /proc/iomem
static unsigned long base_mem = 0x0009f1ff;
// static unsigned long base_mem = 0x880000000;
static void __iomem *io_mem;

static int mobdev_open(struct inode *inode, struct file *file)
{
	pr_info("device file open...!!!\n");
	return nonseekable_open(inode, file);
}

static int mobdev_release(struct inode *inode, struct file *file)
{
	pr_info("device file released...!!!\n");
	return 0;
}

static ssize_t mobdev_read(struct file *filp, char __user *buf, size_t len,
			   loff_t *off)
{
	unsigned char lbuf[IO_MEM_SIZE];
	unsigned char *ptr = lbuf;
	void __iomem *add = io_mem;
	size_t size;
	pr_debug("mobdev_read, len = %zu, off = %lld\n", len, *off);

	if (*off > 0) {
		len = 0;
		goto out;
	}
	if (len > IO_MEM_SIZE)
		len = IO_MEM_SIZE;

	size = len;
	while (size) {
		*ptr = ioread8(add);
		rmb();
		add++;
		size--;
		ptr++;
	}

	// Copy the data from the kernel space to the user-space
	if (copy_to_user(buf, lbuf, len)) {
		pr_warn("copy to user failed !\n");
		return -EFAULT;
	}
	*off += len;
out:
	pr_info("Data Read %zu: Done!\n", len);
	return len;
}

static ssize_t mobdev_write(struct file *filp, const char __user *buf,
			    size_t len, loff_t *off)
{
	unsigned char lbuf[IO_MEM_SIZE];
	unsigned char *ptr = lbuf;
	void __iomem *add = io_mem;
	size_t size;
	pr_info("mobdev_write, len = %zu, off = %lld\n", len, *off);

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (*off > 0)
		return -EINVAL;

	if (len > IO_MEM_SIZE) {
		pr_warn("failed, wrong buffer size\n");
		return -EFBIG;
	}

	//Copy the data to kernel space from the user-space
	if (copy_from_user(lbuf, buf, len)) {
		pr_err("copy from user failed!\n");
		return -EFAULT;
	}

	size = len;
	while (size) {
		iowrite8(*ptr, add);
		wmb();
		add++;
		size--;
		ptr++;
	}

	*off += len;
	pr_info("Data Write %zu: Done!\n", len);
	return len;
}

/*
** File Operations structure
*/
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = mobdev_read,
	.write = mobdev_write,
	.open = mobdev_open,
	.release = mobdev_release,
};

static void mobdev_clean_cdev(struct mob_dev *mdev)
{
	device_destroy(mob_class, mdev->c_dev.dev);
	cdev_del(&mdev->c_dev);
}

static int mobdev_init_cdev(struct mob_dev *mdev, size_t index)
{
	int result = 0;
	struct device *mob_device = NULL;
	dev_t dev = MKDEV(dev_major, dev_minor_start + index);

	cdev_init(&mdev->c_dev, &fops);
	mdev->c_dev.owner = THIS_MODULE;

	result = cdev_add(&mdev->c_dev, dev, 1);
	if (result < 0) {
		pr_warn("failed to add cdev\n");
		return result;
	}

	mob_device = device_create(mob_class, NULL, dev, NULL, "%s_%zu",
				   DEVICE_NAME, index);
	if (IS_ERR(mob_device)) {
		pr_warn("failed to create device\n");
		return PTR_ERR(mob_device);
	}

	return 0;
}

/*
** Module Init function
*/
static __init int mobdev_init(void)
{
	int result = 0;
	size_t i;
	dev_t dev = 0;

	// To claim mem region is has to be available,
	// check /proc/iomem
	if (!request_mem_region(base_mem, IO_MEM_SIZE, "mobdev")) {
		pr_warn("mobdev: can't get I/O mem address 0x%lx\n", base_mem);
		return -ENODEV;
	}

	io_mem = ioremap(base_mem, IO_MEM_SIZE);
	if (!io_mem) {
		pr_warn("mobdev: failed to io remap address 0x%lx\n", base_mem);
		result = -ENODEV;
		goto rel_region;
	}
	pr_info("mobdev: io_mem = 0x%p\n", io_mem);

	result = alloc_chrdev_region(&dev, dev_minor_start, mob_devs_num,
				     DEVICE_NAME);
	if (result < 0) {
		pr_warn("Cannot allocate major number\n");
		goto rel_region;
	}

	dev_major = MAJOR(dev);

	mob_devices = kcalloc(mob_devs_num, sizeof(struct mob_dev), GFP_KERNEL);
	if (!mob_devices) {
		pr_warn("kcalloc failed for mob devices\n");
		result = -ENOMEM;
		goto clean_chrdev;
	}

	mob_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(mob_class)) {
		pr_warn("failed to create class\n");
		result = PTR_ERR(mob_class);
		goto clean_mobdevs;
	}

	/* Initialize each cdevice */
	for (i = 0; i < mob_devs_num; i++) {
		result = mobdev_init_cdev(&mob_devices[i], i);
		if (result < 0) {
			pr_warn("Error adding mob device %zu\n", i);
			goto clean_cdevs;
		}
	}

	pr_info("mobdev_init done\n");
	return 0;

clean_cdevs:
	for (i = 0; i < mob_devs_num; i++) {
		mobdev_clean_cdev(&mob_devices[i]);
	}
	class_destroy(mob_class);
clean_mobdevs:
	kfree(mob_devices);
clean_chrdev:
	unregister_chrdev_region(dev, mob_devs_num);
rel_region:
	iounmap(io_mem);
	release_region(base_mem, IO_MEM_SIZE);
	return result;
}

/*
** Module exit function
*/
static __exit void mobdev_exit(void)
{
	size_t i;
	pr_info("mobdev_exit\n");
	if (mob_devices) {
		for (i = 0; i < mob_devs_num; i++) {
			mobdev_clean_cdev(&mob_devices[i]);
		}
		kfree(mob_devices);
	}
	class_destroy(mob_class);
	unregister_chrdev_region(MKDEV(dev_major, dev_minor_start),
				 mob_devs_num);

	iounmap(io_mem);
	release_mem_region(base_mem, IO_MEM_SIZE);
}

module_init(mobdev_init);
module_exit(mobdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kamil Wiatrowski");
MODULE_DESCRIPTION("Character driver with read/write to memmory.");
MODULE_VERSION("0.1.2");
