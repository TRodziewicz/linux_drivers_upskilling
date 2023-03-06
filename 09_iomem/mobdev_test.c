#include<stdio.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>

#define BUFF_LEN 8
#define DEVICE_NAME "/dev/mobdev_0"
static char buff[BUFF_LEN] = {0};

int main()
{
   const char *msg = "A1234B";
   int fd = open(DEVICE_NAME, O_WRONLY);
   if (fd < 0) {
      printf("Failed to open the device for writing\n");
      return errno;
   }

   int ret = write(fd, msg, strlen(msg));
   if (ret < 0) {
      printf("Failed to write the message to the device.\n");
      close(fd);
      return errno;
   }
   close(fd);

   printf("Press ENTER to read back from the device...\n");
   getchar();

   fd = open(DEVICE_NAME, O_RDONLY);
   if (fd < 0) {
      printf("Failed to open the device for reading\n");
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