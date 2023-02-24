#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <pthread.h>

#include "mob_ioctl.h"

#define BUFF_LEN 256
#define DEVICE_NAME0 "/dev/mobdev_0"
#define DEVICE_NAME1 "/dev/mobdev_1"
#define FD_NUM 2
#define WRITE_SIZE MOBDEV_MAX_SIZE
#define READ_SIZE 100
#define POLL_TIMEOUT_S 5

struct th_arg {
   int fd[FD_NUM];
};

void *reset_worker(void *p)
{
   struct th_arg *devs = (struct th_arg *)p;
   sleep(POLL_TIMEOUT_S - 1);

   printf("Reset the device buffers\n");
   // Clean the data in device buffers to enable writing
   if (ioctl(devs->fd[0], MOBDEV_IOC_RESET) < 0) {
      printf("MOBDEV_IOC_RESET 0 failed, err=%d\n", errno);
   }
   if (ioctl(devs->fd[1], MOBDEV_IOC_RESET) < 0) {
      printf("MOBDEV_IOC_RESET 1 failed, err=%d\n", errno);
   }

   return NULL;
}

int main()
{
   int ret = 0;
   char write_buff[WRITE_SIZE];
   char read_buff[READ_SIZE] = {0};

   memset(write_buff, 'x', WRITE_SIZE);
   write_buff[WRITE_SIZE - 1] = '\0';

   struct pollfd pfds[FD_NUM] = {0};
   int fd0, fd1;
   struct th_arg devs;

   pthread_t reset_th;

   fd0 = open(DEVICE_NAME0, O_RDWR);
   if (fd0 < 0) {
      printf("Failed to open the device for write\n");
      return -1;
   }

   fd1 = open(DEVICE_NAME1, O_RDWR);
   if (fd1 < 0) {
      printf("Failed to open the device for write\n");
      ret = -1;
      goto out;
   }

   devs.fd[0] = fd0;
   devs.fd[1] = fd1;

   pthread_create(&reset_th, NULL, reset_worker, &devs);

   // Clean the data in device buffers
   if (ioctl(fd0, MOBDEV_IOC_RESET) < 0) {
      printf("MOBDEV_IOC_RESET 0 failed, err=%d\n", errno);
   }
   if (ioctl(fd1, MOBDEV_IOC_RESET) < 0) {
      printf("MOBDEV_IOC_RESET 1 failed, err=%d\n", errno);
   }

   pfds[0].fd = fd0;
   pfds[1].fd = fd1;
   pfds[0].events = ( POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM );
   pfds[1].events = ( POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM );

   while(1) {
      ret = poll(pfds, 2, POLL_TIMEOUT_S * 1000);

      if (ret < 0) {
         printf("Error, poll returned %d, %d\n", ret, errno);
         break;
      }
      if (ret == 0) {
         printf("poll timeout\n");
         break;
      }

      for (int i = 0; i < FD_NUM; i++) {
         if( ( pfds[i].revents & POLLIN ) == POLLIN ) {
            printf("Reading the device %d\n", i);
            ret = read(pfds[i].fd, read_buff, READ_SIZE - 1);
            if (ret < 0) {
               printf("Failed to read the message %d, %d.\n", ret, errno);
            } else if (ret == 0) {
               printf("Read returned zero\n");
            } else {
               read_buff[ret] = '\0';
               printf("Read %d bytes: %s\n", ret, read_buff);
            }
         }
         if( ( pfds[i].revents & POLLOUT ) == POLLOUT ) {
            printf("Writing to the device %d\n", i);
            ret = write(pfds[i].fd, write_buff, WRITE_SIZE);
            if (ret < 0) {
               printf("Failed to write the message %d, %d.\n", ret, errno);
            } else {
               printf("Written %d bytes\n", ret);
            }
         }
      }
   }

   pthread_join(reset_th, NULL);

out:
   close(fd0);
   close(fd1);

   return ret;
}