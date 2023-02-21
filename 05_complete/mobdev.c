#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h> // kmalloc()
#include <linux/uaccess.h> // copy_to/from_user()
#include <linux/err.h>
#include <linux/seq_file.h>

#include "mobdev.h"

#define CLASS_NAME "mobica"
#define DEVICE_NAME "mobdev"

DECLARE_COMPLETION(write_comp);

static const unsigned int mob_devs_num = 1;
static int dev_minor_start = 0;
static int dev_major = 0;

static struct mob_dev *mob_devices = NULL;
static struct class *mob_class = NULL;

int mobdev_read_procsimple(struct seq_file *s, void *v)
{
	size_t i;
	if (!mob_devices) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	for (i = 0; i < mob_devs_num; ++i) {
		struct mob_dev *mdev = mob_devices + i;
		if (!mutex_trylock(&mdev->lock)) {
			pr_info("failed ps read, busy\n");
			return -EBUSY;
		}
		seq_printf(s, "Mobdev_%zu, size = %zu\n", i, mdev->size);
		mutex_unlock(&mdev->lock);
	}
	seq_printf(s, "Finished mobdev_read_procsimple!\n");

	return 0;
}

static void mobdev_create_proc(void)
{
	struct proc_dir_entry *entry;
	entry = proc_create_single("mobdev_simple", 0, NULL,
				   mobdev_read_procsimple);
	if (!entry)
		pr_warn("Proc simple entry not created!\n");
}

static void mobdev_remove_proc(void)
{
	/* No problem if it was not registered */
	remove_proc_entry("mobdev_simple", NULL);
}

static int mobdev_open(struct inode *inode, struct file *file)
{
	struct mob_dev *mdev;
	// pr_info("device file open...!!!\n");
	mdev = container_of(inode->i_cdev, struct mob_dev, c_dev);
	file->private_data = mdev; /* for other methods */

	return 0;
}

static int mobdev_release(struct inode *inode, struct file *file)
{
	// pr_info("device file released...!!!\n");
	return 0;
}

static ssize_t mobdev_read(struct file *filp, char __user *buf, size_t len,
			   loff_t *off)
{
	struct mob_dev *mdev = (struct mob_dev *)filp->private_data;
	size_t size = 0;
	// pr_info("mobdev_read, len = %zu, off = %lld\n", len, *off);
	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	if (wait_for_completion_interruptible(&write_comp))
		return -ERESTARTSYS;

	if (mutex_lock_interruptible(&mdev->lock))
		return -ERESTARTSYS;

	if (*off >= mdev->size) {
		goto out;
	}
	size = mdev->size - *off;
	if (size > len)
		size = len;
	// if (size == 0)
	// goto out;

	// mdev->size -= size;

	//Copy the data from the kernel space to the user-space
	if (copy_to_user(buf, mdev->p_data + *off, size)) {
		pr_warn("copy to user failed !\n");
		mutex_unlock(&mdev->lock);
		return -EFAULT;
	}
	*off += size;
out:
	mutex_unlock(&mdev->lock);
	// pr_info("Data Read %zu: Done!\n", size);
	return size;
}

static ssize_t mobdev_write(struct file *filp, const char __user *buf,
			    size_t len, loff_t *off)
{
	struct mob_dev *mdev = (struct mob_dev *)filp->private_data;
	// pr_info("mobdev_write, len = %zu, off = %lld\n", len, *off);
	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	if (len + *off > DATA_MAX_SIZE) {
		pr_warn("failed, wrong buffer size\n");
		return -EFBIG;
	}

	if (mutex_lock_interruptible(&mdev->lock))
		return -ERESTARTSYS;

	//Copy the data to kernel space from the user-space
	if (copy_from_user(mdev->p_data + *off, buf, len)) {
		mutex_unlock(&mdev->lock);
		pr_err("copy from user failed!\n");
		return -EFAULT;
	}
	if (len + *off > mdev->size)
		mdev->size = len + *off;
	mutex_unlock(&mdev->lock);
	*off += len;

	complete(&write_comp);
	// pr_info("Data Write %zu: Done!\n", len);
	return len;
}

loff_t mobdev_llseek(struct file *filp, loff_t off, int whence)
{
	struct mob_dev *mdev = (struct mob_dev *)filp->private_data;
	loff_t npos;
	// pr_info("mobdev_llseek, whence = %d, off = %lld\n", whence, off);
	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	switch (whence) {
	case SEEK_SET:
		if (off >= DATA_MAX_SIZE)
			return -EFBIG;
		npos = off;
		break;

	case SEEK_CUR:
		if (filp->f_pos + off >= DATA_MAX_SIZE)
			return -EFBIG;
		npos = filp->f_pos + off;
		break;

	case SEEK_END:
		if (mutex_lock_interruptible(&mdev->lock))
			return -ERESTARTSYS;
		npos = mdev->size + off;
		mutex_unlock(&mdev->lock);
		if (npos >= DATA_MAX_SIZE)
			return -EFBIG;
		break;

	default: /* not expected value */
		return -EINVAL;
	}
	if (npos < 0)
		return -EINVAL;
	filp->f_pos = npos;
	return npos;
}

/*
** File Operations structure
*/
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.llseek = mobdev_llseek,
	.read = mobdev_read,
	.write = mobdev_write,
	.open = mobdev_open,
	.release = mobdev_release,
};

static void mobdev_clean_cdev(struct mob_dev *mdev)
{
	if (mdev->p_data)
		kfree(mdev->p_data);

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

	mdev->p_data = kcalloc(1, DATA_MAX_SIZE, GFP_KERNEL);
	if (!mdev->p_data) {
		pr_warn("failed to alloc p_data\n");
		return -ENOMEM;
	}

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

	result = alloc_chrdev_region(&dev, dev_minor_start, mob_devs_num,
				     DEVICE_NAME);
	if (result < 0) {
		pr_warn("Cannot allocate major number\n");
		return result;
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
		mutex_init(&mob_devices[i].lock);
		result = mobdev_init_cdev(&mob_devices[i], i);
		if (result < 0) {
			pr_warn("Error adding mob device %zu\n", i);
			goto clean_cdevs;
		}
		mutex_init(&mob_devices[i].lock);
	}

	mobdev_create_proc();
	pr_info("mobdev_init done\n");
	return 0;

clean_cdevs:
	for (i = 0; i < mob_devs_num; i++) {
		mobdev_clean_cdev(&mob_devices[i]);
		mutex_destroy(&mob_devices[i].lock);
	}
	class_destroy(mob_class);
clean_mobdevs:
	kfree(mob_devices);
clean_chrdev:
	unregister_chrdev_region(dev, mob_devs_num);
	return result;
}

/*
** Module exit function
*/
static __exit void mobdev_exit(void)
{
	size_t i;
	pr_info("mobdev_exit\n");
	mobdev_remove_proc();
	if (!completion_done(&write_comp)) {
		complete_all(&write_comp);
	}
	if (mob_devices) {
		for (i = 0; i < mob_devs_num; i++) {
			mobdev_clean_cdev(&mob_devices[i]);
			mutex_destroy(&mob_devices[i].lock);
		}
		kfree(mob_devices);
	}
	class_destroy(mob_class);
	unregister_chrdev_region(MKDEV(dev_major, dev_minor_start),
				 mob_devs_num);
}

module_init(mobdev_init);
module_exit(mobdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kamil Wiatrowski");
MODULE_DESCRIPTION("Read/Write Character driver with proc, sync.");
MODULE_VERSION("0.4");
