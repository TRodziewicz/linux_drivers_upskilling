#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#include "mob_ioctl.h"

#define BUFF_LEN 256
#define DEVICE_NAME "/dev/mobdev_0"
#define TEST_COUNT 1

int main()
{
   char *buff = "ABCD1234";
   struct mobdev_data mdd = {
      .off = 2,
      .size = strlen(buff),
      .data = buff
   };

   // String 'buff' with offset 2:
   // offset: 0 1 2 3 4 5 6 7 8 9 10
   // string:     A B C D 1 2 3 4 \0

   char ret_buff[2]; // Warning! Not null-terminated

   int fd = open(DEVICE_NAME, O_RDONLY);
   if (fd < 0) {
      printf("Failed to open the device for write\n");
      return -1;
   }

   // Clean the data in device buffer
   if (ioctl(fd, MOBDEV_IOC_RESET) < 0) {
      printf("MOBDEV_IOC_RESET failed, err=%d\n", errno);
   }

   // Write the string with offest to the device
   if (ioctl(fd, MOBDEV_IOC_SET, (struct mobdev_data*) &mdd) < 0) {
      printf("MOBDEV_IOC_SET failed, err=%d\n", errno);
   }

   // Check that there is 'CD' at offset 4
   mdd.size = 2;
   mdd.off = 4;
   mdd.data = ret_buff;
   if (ioctl(fd, MOBDEV_IOC_GET, (struct mobdev_data*) &mdd) < 0) {
      printf("MOBDEV_IOC_GET failed, err=%d\n", errno);
   } else {
      if (ret_buff[0] == 'C' && ret_buff[1] == 'D') {
         printf("TEST passed.\n");
      } else {
         printf("TEST failed, ret_buff=%c%c\n", ret_buff[0], ret_buff[1]);
      }
   }

   close(fd);

   return 0;
}