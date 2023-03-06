#ifndef _MOBDEV_H_
#define _MOBDEV_H_

#include <linux/cdev.h>

#define DATA_MAX_SIZE 255

struct mob_dev {
	struct cdev c_dev; /* char device structure */
};

#endif /* _MOBDEV_H_ */