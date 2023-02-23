DEBUG = y
ifeq ($(DEBUG),y)
EXTRA_CFLAGS += -O -g -DDEBUG
endif

obj-m+=mobdev.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
	$(CC) mobdev_test.c -o test

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
	rm test
