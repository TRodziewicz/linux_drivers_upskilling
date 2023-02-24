#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/slab.h> // kmalloc()
#include <linux/uaccess.h> // copy_to/from_user()
#include <linux/err.h>
#include <linux/seq_file.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/poll.h>

#include "mobdev.h"
#include "mob_ioctl.h"

#define CLASS_NAME "mobica"
#define DEVICE_NAME "mobdev"

#define DATA_MAX_SIZE MOBDEV_MAX_SIZE

DEFINE_SPINLOCK(open_devs_slock);

static const unsigned int mob_devs_num = 4;
static int dev_minor_start = 0;
static int dev_major = 0;
static int open_devices = 0;

static struct mob_dev *mob_devices = NULL;
static struct class *mob_class = NULL;

int mobdev_read_procsimple(struct seq_file *s, void *v)
{
	size_t i;
	if (!mob_devices) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	// Check and log status just for test purposes
	if (spin_is_locked(&open_devs_slock)) {
		pr_info("Spinlock is locked!!!\n");
	}

	spin_lock(&open_devs_slock);
	seq_printf(s, "Number of open devices: %d\n", open_devices);
	spin_unlock(&open_devs_slock);

	for (i = 0; i < mob_devs_num; ++i) {
		struct mob_dev *mdev = mob_devices + i;
		if (!mutex_trylock(&mdev->lock)) {
			pr_info("failed ps read, busy\n");
			return -EBUSY;
		}
		seq_printf(s, "Mobdev_%zu, size = %zu, rpos = %zu\n", i,
			   mdev->size, mdev->read_pos);
		mutex_unlock(&mdev->lock);
	}
	seq_printf(s, "Finished mobdev_read_procsimple!\n");

	return 0;
}

/* Other way of using mobdev_read_procsimple with proc_ops and single_open */
// static int mobdev_procsimple_open(struct inode *inode, struct file *file)
// {
// 	return single_open(file, mobdev_read_procsimple, NULL);
// }

// static struct proc_ops procsimple_pops = {
// 	.proc_open    = mobdev_procsimple_open,
// 	.proc_read    = seq_read,
// 	.proc_lseek  = seq_lseek,
// 	.proc_release = single_release
// };

static void *mobdev_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= mob_devs_num || *pos < 0)
		return NULL; /* No more devs in array */
	return mob_devices + *pos;
}

static void *mobdev_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= mob_devs_num)
		return NULL;
	return mob_devices + *pos;
}

static void mobdev_seq_stop(struct seq_file *s, void *v)
{
	pr_info("mobdev: seq_stop\n");
	seq_printf(s, "Stopping proc seq access!\n");
}

static int mobdev_seq_show(struct seq_file *s, void *v)
{
	struct mob_dev *mdev = (struct mob_dev *)v;
	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	if (!mutex_trylock(&mdev->lock)) {
		pr_info("failed seq read, busy\n");
		return -EBUSY;
	}
	seq_printf(s, "Mobdev_%i, size = %zu\n", (int)(mdev - mob_devices),
		   mdev->size);
	mutex_unlock(&mdev->lock);
	return 0;
}

static struct seq_operations proc_sops = {
	.start = mobdev_seq_start,
	.next = mobdev_seq_next,
	.stop = mobdev_seq_stop,
	.show = mobdev_seq_show,
};

static int mobdev_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &proc_sops);
}

static struct proc_ops seq_pops = {
	.proc_open = mobdev_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};

static void mobdev_create_proc(void)
{
	struct proc_dir_entry *entry;
	// entry = proc_create("mobdev_simple", 0, NULL, &procsimple_pops);
	entry = proc_create_single("mobdev_simple", 0, NULL,
				   mobdev_read_procsimple);
	if (!entry)
		pr_warn("Proc simple entry not created!\n");
	entry = proc_create("mobdev_seq", 0, NULL, &seq_pops);
	if (!entry)
		pr_warn("Proc seq entry not created!\n");
}

static void mobdev_remove_proc(void)
{
	/* No problem if it was not registered */
	remove_proc_entry("mobdev_simple", NULL);
	remove_proc_entry("mobdev_seq", NULL);
}

static int mobdev_open(struct inode *inode, struct file *file)
{
	struct mob_dev *mdev;

	spin_lock(&open_devs_slock);
	open_devices++;
	spin_unlock(&open_devs_slock);
	pr_debug_ratelimited("device file open...!!!\n");
	mdev = container_of(inode->i_cdev, struct mob_dev, c_dev);
	file->private_data = mdev; /* for other methods */

	return nonseekable_open(inode, file);
}

static int mobdev_release(struct inode *inode, struct file *file)
{
	spin_lock(&open_devs_slock);
	open_devices--;
	spin_unlock(&open_devs_slock);
	pr_debug_ratelimited("device file released...!!!\n");
	return 0;
}

static ssize_t mobdev_read(struct file *filp, char __user *buf, size_t len,
			   loff_t *off)
{
	struct mob_dev *mdev = (struct mob_dev *)filp->private_data;
	pr_debug_ratelimited("mobdev_read, len = %zu, off = %lld\n", len, *off);
	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	if (mutex_lock_interruptible(&mdev->lock))
		return -ERESTARTSYS;

	if (mdev->read_pos >= mdev->size) {
		len = 0;
		goto out;
	}

	if (len > (mdev->size - mdev->read_pos))
		len = mdev->size - mdev->read_pos;

	//Copy the data from the kernel space to the user-space
	if (copy_to_user(buf, mdev->p_data + mdev->read_pos, len)) {
		pr_warn("copy to user failed !\n");
		mutex_unlock(&mdev->lock);
		return -EFAULT;
	}
	mdev->read_pos += len;
	*off += len;
out:
	mutex_unlock(&mdev->lock);
	pr_debug_ratelimited("Data Read %zu: Done!\n", len);
	return len;
}

static ssize_t mobdev_write(struct file *filp, const char __user *buf,
			    size_t len, loff_t *off)
{
	struct mob_dev *mdev = (struct mob_dev *)filp->private_data;
	pr_debug_ratelimited("mobdev_write, len = %zu, off = %lld\n", len,
			     *off);
	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	if (mutex_lock_interruptible(&mdev->lock))
		return -ERESTARTSYS;

	if (mdev->size >= DATA_MAX_SIZE) {
		mutex_unlock(&mdev->lock);
		pr_info("failed, buffer full\n");
		return -ENOSPC;
	}

	if (mdev->size + len >= DATA_MAX_SIZE) {
		len = DATA_MAX_SIZE - mdev->size;
	}

	//Copy the data to kernel space from the user-space
	if (copy_from_user(mdev->p_data + mdev->size, buf, len)) {
		mutex_unlock(&mdev->lock);
		pr_err("copy from user failed!\n");
		return -EFAULT;
	}

	mdev->size += len;
	/* Wake up the poll, reading possible */
	wake_up_interruptible(&mdev->pollq);
	mutex_unlock(&mdev->lock);

	*off += len;
	pr_debug_ratelimited("Data Write %zu: Done!\n", len);
	return len;
}

static unsigned int mobdev_poll(struct file *filp, poll_table *wait)
{
	struct mob_dev *mdev = (struct mob_dev *)filp->private_data;
	unsigned int mask = 0;

	pr_debug_ratelimited("Mobdev poll enter\n");
	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	mutex_lock(&mdev->lock);

	poll_wait(filp, &mdev->pollq, wait);

	if (mdev->size < DATA_MAX_SIZE) {
		pr_debug_ratelimited("poll, dev writable\n");
		mask |= POLLOUT | POLLWRNORM; /* writable */
	}
	if (mdev->read_pos < mdev->size) {
		pr_debug_ratelimited("poll, dev readable\n");
		mask |= POLLIN | POLLRDNORM; /* readable */
	}

	mutex_unlock(&mdev->lock);

	return mask;
}

long mobdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct mob_dev *mdev = (struct mob_dev *)filp->private_data;
	struct mobdev_data mdd;
	void __user *up = (void __user *)arg;

	if (_IOC_TYPE(cmd) != MOBDEV_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & (_IOC_READ | _IOC_WRITE)) {
		if (!access_ok((void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;

		if (_IOC_SIZE(cmd) > sizeof(mdd))
			return -ENOTTY;

		if (copy_from_user(&mdd, up, _IOC_SIZE(cmd))) {
			pr_err("copy from user failed!\n");
			return -EFAULT;
		}
	}

	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&mdev->lock))
		return -ERESTARTSYS;

	switch (cmd) {
	case MOBDEV_IOC_RESET:
		memset(mdev->p_data, 0, mdev->size);
		mdev->size = 0;
		mdev->read_pos = 0;
		/* Wake up the poll, writing possible */
		wake_up_interruptible(&mdev->pollq);
		break;

	case MOBDEV_IOC_SET:
		if (mdd.size + mdd.off > DATA_MAX_SIZE) {
			ret = -EFBIG;
		} else if (copy_from_user(mdev->p_data + mdd.off, mdd.data,
					  mdd.size)) {
			pr_err("copy from user failed!\n");
			ret = -EFAULT;
		} else if (mdd.size + mdd.off > mdev->size) {
			mdev->size = mdd.size + mdd.off;
		}
		break;

	case MOBDEV_IOC_GET:
		if (mdd.size + mdd.off > DATA_MAX_SIZE) {
			ret = -EFBIG;
		} else if (copy_to_user(mdd.data, mdev->p_data + mdd.off,
					mdd.size)) {
			pr_err("copy to user failed!\n");
			ret = -EFAULT;
		}
		break;

	default:
		ret = -ENOTTY;
	}

	mutex_unlock(&mdev->lock);

	return ret;
}

/*
** File Operations structure
*/
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = mobdev_read,
	.write = mobdev_write,
	.poll = mobdev_poll,
	.unlocked_ioctl = mobdev_ioctl,
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
		init_waitqueue_head(&mob_devices[i].pollq);
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
MODULE_DESCRIPTION("Read/Write Character driver, with poll.");
MODULE_VERSION("0.7");
