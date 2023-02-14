#include <linux/init.h> // Macros used to mark up functions e.g., __init __exit
#include <linux/module.h> // Core header for loading LKMs into the kernel
#include <linux/device.h> // Header to support the kernel Driver Model
#include <linux/kernel.h> // Contains types, macros, functions for the kernel
#include <linux/fs.h> // Header for the Linux file system support
#include <linux/uaccess.h> // Required for the copy to user function

#define DEVICE_NAME "mobichar" // The device will appear as /dev/mobichar
#define CLASS_NAME "mobica" // The device class name - /sys/class/mobica/
#define MSG_MAX_SIZE 256

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kamil Wiatrowski");
MODULE_DESCRIPTION("A demo Linux char driver.");
MODULE_VERSION("0.1");

static int major_number; // major device number, determined automatically
static char data[MSG_MAX_SIZE] = { 0 }; // To store data from userspace
static short data_size; // Size of the stored data
static int num_opens = 0; // Number of times the device was opened
static struct class *mobi_class = NULL;
static struct device *mob_device = NULL;

static int dev_open(struct inode *inodep, struct file *filep)
{
	num_opens++;
	printk(KERN_INFO "Mobi char: Device has been opened %d time(s)\n",
	       num_opens);
	return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len,
			loff_t *offset)
{
	int count = 0;
	if (len > MSG_MAX_SIZE || len < data_size) {
		printk(KERN_WARNING
		       "Mobi char: wrong buffer size. Aborting read\n");
		return -EFBIG;
	}
	count = copy_to_user(buffer, data, data_size);

	if (count == 0) {
		printk(KERN_INFO "Mobi char: Sent %d characters to the user\n",
		       data_size);
		count = data_size;
		data_size = 0; // clear the position to the start
		return count;
	}
	printk(KERN_INFO
	       "Mobi char: Failed to send %d characters to the user\n",
	       count);
	return -EFAULT;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len,
			 loff_t *offset)
{
	int error_count = 0;
	if (len > MSG_MAX_SIZE) {
		printk(KERN_WARNING
		       "Mobi char: wrong buffer size %zu. Aborting write\n",
		       len);
		return -EFBIG;
	}
	error_count = copy_from_user(data, buffer, len);
	if (error_count != 0) {
		printk(KERN_INFO
		       "Mobi char: Failed to read %d characters from user\n",
		       error_count);
		return -EFAULT;
	}
	data_size = len; // store the length of the data
	printk(KERN_INFO "Mobi char: Received %zu characters from the user\n",
	       len);
	return len;
}

static int dev_release(struct inode *inodep, struct file *filep)
{
	printk(KERN_INFO "Mobi char: Device successfully closed\n");
	return 0;
}

static struct file_operations fops = {
	.open = dev_open,
	.read = dev_read,
	.write = dev_write,
	.release = dev_release,
};

static int __init mobchar_init(void)
{
	printk(KERN_INFO "Mobi character driver init\n");

	major_number = register_chrdev(0, DEVICE_NAME, &fops);
	if (major_number < 0) {
		printk(KERN_ALERT
		       "Mobi char: failed to register a major number\n");
		return major_number;
	}
	printk(KERN_INFO "Mobi char: registered with major number %d\n",
	       major_number);

	// Register the device class
	mobi_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(mobi_class)) {
		// Check for error and clean up if there is any
		unregister_chrdev(major_number, DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(mobi_class);
	}
	printk(KERN_INFO "Mobi char: device class registered correctly\n");

	// Register the device driver
	mob_device = device_create(mobi_class, NULL, MKDEV(major_number, 0),
				   NULL, DEVICE_NAME);
	if (IS_ERR(mob_device)) {
		// Clean up if there is an error
		class_destroy(mobi_class);
		unregister_chrdev(major_number, DEVICE_NAME);
		printk(KERN_ALERT "Failed to create the device\n");
		return PTR_ERR(mob_device);
	}
	printk(KERN_INFO "Mobi char: device class created correctly\n");

	return 0;
}

static void __exit mobchar_exit(void)
{
	printk(KERN_INFO "Mobi character driver exit\n");
	// remove the device
	device_destroy(mobi_class, MKDEV(major_number, 0));
	// remove the device class
	class_destroy(mobi_class);
	// unregister the major number
	unregister_chrdev(major_number, DEVICE_NAME);
	printk(KERN_INFO "Mobi character driver exit done\n");
}

module_init(mobchar_init);
module_exit(mobchar_exit);
