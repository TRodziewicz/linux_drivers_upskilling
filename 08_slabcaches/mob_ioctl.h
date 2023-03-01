#ifndef _MOB_IOCTL_H_
#define _MOB_IOCTL_H_

#include <linux/ioctl.h>

#define MOBDEV_MAX_SIZE 255

struct mobdev_data {
	__u32 off;
	__u32 size;
#ifdef __KERNEL__
	__u8 __user *data;
#else
	__u8 *data;
#endif
};

#define MOBDEV_IOC_MAGIC 'M'

#define MOBDEV_IOC_RESET _IO(MOBDEV_IOC_MAGIC, 0)
#define MOBDEV_IOC_SET _IOW(MOBDEV_IOC_MAGIC, 1, struct mobdev_data)
#define MOBDEV_IOC_GET _IOR(MOBDEV_IOC_MAGIC, 2, struct mobdev_data)

#endif /* _MOB_IOCTL_H_ */