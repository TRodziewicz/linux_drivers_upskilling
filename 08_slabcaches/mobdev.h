#ifndef _MOBDEV_H_
#define _MOBDEV_H_

struct mob_dev {
	char *p_data; /* pointer to the memory allocated */
	size_t size; /* actual number of bytes in data */
	struct cdev c_dev; /* char device structure */
	struct mutex lock; /* device lock for data access */
};

#endif /* _MOBDEV_H_ */