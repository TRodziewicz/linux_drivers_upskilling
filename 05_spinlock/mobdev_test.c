#include <stdio.h>
// #include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define BUFF_LEN 256
#define DEVICE_NAME "/dev/mobdev_0"
#define TEST_COUNT 2000000

void *open_close_worker(void *)
{
   for (int i = 0; i < TEST_COUNT; ++i) {
      int fd = open(DEVICE_NAME, O_RDONLY);
      if (fd < 0) {
         printf("Failed to open the device for write\n");
         break;
      }
      close(fd);
   }
   return NULL;
}

int main()
{
   pthread_t test_th_1, test_th_2, test_th_3, test_th_4;

   pthread_create(&test_th_1, NULL, open_close_worker, NULL);
   pthread_create(&test_th_2, NULL, open_close_worker, NULL);
   pthread_create(&test_th_3, NULL, open_close_worker, NULL);
   pthread_create(&test_th_4, NULL, open_close_worker, NULL);

   pthread_join(test_th_1, NULL);
   pthread_join(test_th_2, NULL);
   pthread_join(test_th_3, NULL);
   pthread_join(test_th_4, NULL);

   return 0;
}