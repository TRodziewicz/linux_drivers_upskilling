#ifndef _MOBDEV_H_
#define _MOBDEV_H_

struct mob_dev {
	char *p_data; /* pointer to the memory allocated */
	size_t read_pos; /* postion to read data */
	size_t size; /* position to write data */
	struct cdev c_dev; /* char device structure */
	struct mutex lock; /* device lock for data access */
	wait_queue_head_t pollq; /* poll wait queue */
};

#endif /* _MOBDEV_H_ */