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

static struct kmem_cache *mob_cache;

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
		seq_printf(s, "Mobdev_%zu, size = %zu\n", i, mdev->size);
		mutex_unlock(&mdev->lock);
	}
	seq_printf(s, "Finished mobdev_read_procsimple!\n");

	return 0;
}

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

	return 0;
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
	size_t size = 0;
	pr_debug_ratelimited("mobdev_read, len = %zu, off = %lld\n", len, *off);
	if (!mdev) {
		pr_warn("failed, no device data\n");
		return -EFAULT;
	}

	if (mutex_lock_interruptible(&mdev->lock))
		return -ERESTARTSYS;

	if (*off >= mdev->size) {
		goto out;
	}
	size = mdev->size - *off;
	if (size > len)
		size = len;

	//Copy the data from the kernel space to the user-space
	if (copy_to_user(buf, mdev->p_data + *off, size)) {
		pr_warn("copy to user failed !\n");
		mutex_unlock(&mdev->lock);
		return -EFAULT;
	}
	*off += size;
out:
	mutex_unlock(&mdev->lock);
	pr_debug_ratelimited("Data Read %zu: Done!\n", size);
	return size;
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
	pr_debug_ratelimited("Data Write %zu: Done!\n", len);
	return len;
}

loff_t mobdev_llseek(struct file *filp, loff_t off, int whence)
{
	struct mob_dev *mdev = (struct mob_dev *)filp->private_data;
	loff_t npos;
	pr_debug_ratelimited("mobdev_llseek, whence = %d, off = %lld\n", whence,
			     off);
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
	.llseek = mobdev_llseek,
	.read = mobdev_read,
	.write = mobdev_write,
	.unlocked_ioctl = mobdev_ioctl,
	.open = mobdev_open,
	.release = mobdev_release,
};

static void mobdev_clean_cdev(struct mob_dev *mdev)
{
	if (mdev->p_data)
		kmem_cache_free(mob_cache, mdev->p_data);

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

	mdev->p_data = kmem_cache_alloc(mob_cache, GFP_KERNEL);
	if (!mdev->p_data) {
		pr_warn("failed to alloc p_data\n");
		return -ENOMEM;
	}
	pr_debug("Allocated p_data\n");

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

void mobdev_cache_ctor(void *)
{
	pr_debug_ratelimited("In slab cache mem ctor!\n");
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

	// mob_cache = kmem_cache_create("mobdev", DATA_MAX_SIZE, 0,
	//   SLAB_HWCACHE_ALIGN, mobdev_cache_ctor);
	// Need to use *_usercopy version to enable copying from user space
	mob_cache =
		kmem_cache_create_usercopy("mobdev", DATA_MAX_SIZE, 0,
					   SLAB_HWCACHE_ALIGN, 0, DATA_MAX_SIZE,
					   mobdev_cache_ctor);
	if (!mob_cache) {
		result = -ENOMEM;
		goto clean_class;
	}

	/* Initialize each cdevice */
	for (i = 0; i < mob_devs_num; i++) {
		mutex_init(&mob_devices[i].lock);
		result = mobdev_init_cdev(&mob_devices[i], i);
		if (result < 0) {
			pr_warn("Error adding mob device %zu\n", i);
			goto clean_cdevs;
		}
	}

	mobdev_create_proc();
	pr_info("mobdev_init done\n");
	return 0;

clean_cdevs:
	for (i = 0; i < mob_devs_num; i++) {
		mobdev_clean_cdev(&mob_devices[i]);
		mutex_destroy(&mob_devices[i].lock);
	}
	kmem_cache_destroy(mob_cache);
clean_class:
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
	if (mob_cache)
		kmem_cache_destroy(mob_cache);

	class_destroy(mob_class);
	unregister_chrdev_region(MKDEV(dev_major, dev_minor_start),
				 mob_devs_num);
}

module_init(mobdev_init);
module_exit(mobdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kamil Wiatrowski");
MODULE_DESCRIPTION("Read/Write Character driver, using kmemcache.");
MODULE_VERSION("0.9");
