#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define BUFF_LEN 256
#define DEVICE_NAME "/dev/mobdev_0"
#define TEST_COUNT 1000

void *write_worker(void *data)
{
   const char *msg = "UTX[p/<AnJFUr/s0tf6}-/oyZm-E7DFt#$Zs.mmLW=>_g?Kb02Gj";

   for (int i = 0; i < TEST_COUNT; ++i) {
      int fd = open(DEVICE_NAME, O_WRONLY);
      if (fd < 0) {
         printf("Failed to open the device for write\n");
         break;
      }
      for (int j = 0; j < TEST_COUNT; ++j) {
         if (write(fd, msg, strlen(msg)) < 0) {
            printf("Failed to write the message to the device.\n");
            break;
         }
         if (lseek(fd, 0, SEEK_SET) < 0) {
            printf("Failed to seek the device for write.\n");
            break;
         }
      }
      close(fd);
   }
   return NULL;
}

void *read_worker(void *data)
{
   char buff[BUFF_LEN] = {0};

   for (int i = 0; i < TEST_COUNT; ++i) {
      int fd = open(DEVICE_NAME, O_RDONLY);
      if (fd < 0) {
         printf("Failed to open the device for read\n");
         break;
      }
      for (int j = 0; j < TEST_COUNT; ++j) {
         if (read(fd, buff, BUFF_LEN) < 0) {
            printf("Failed to read the message from the device.\n");
            break;
         }
         if (lseek(fd, 0, SEEK_SET) < 0) {
            printf("Failed to seek the device for read.\n");
            break;
         }
      }
      close(fd);
   }
   return NULL;
}

int main()
{
   pthread_t read_th, write_th;

   pthread_create(&write_th, NULL, write_worker, NULL);
   pthread_create(&read_th, NULL, read_worker, NULL);

   pthread_join(write_th, NULL);
   pthread_join(read_th, NULL);

   return 0;
}