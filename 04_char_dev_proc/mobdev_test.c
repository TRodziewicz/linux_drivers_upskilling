#include<stdio.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>

#define BUFF_LEN 256
#define DEVICE_NAME "/dev/mobdev_0"
static char buff[BUFF_LEN] = {0};

int main()
{
   const char *msg = "message to kernel";
   int fd = open(DEVICE_NAME, O_RDWR);
   if (fd < 0) {
      printf("Failed to open the device\n");
      return errno;
   }

   int ret = write(fd, msg, strlen(msg));
   if (ret < 0) {
      printf("Failed to write the message to the device.\n");
      close(fd);
      return errno;
   }

   printf("Press ENTER to read back from the device...\n");
   getchar();

   ret = lseek(fd, 0, SEEK_SET);
   if (ret < 0) {
      printf("Failed to seek the device.\n");
      close(fd);
      return errno;
   }

   printf("Reading from the device...\n");
   ret = read(fd, buff, BUFF_LEN);
   if (ret < 0) {
      printf("Failed to read the message from the device.\n");
      close(fd);
      return errno;
   }

   printf("The received message: %s\n", buff);
   close(fd);
   printf("MobDev test end.\n");

   return 0;
}