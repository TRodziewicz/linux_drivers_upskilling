#include<stdio.h>
#include<errno.h>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>

#define BUFFER_LENGTH 256 // The buffer length
#define DEVICE_NAME "/dev/mobichar" // The name of the device in /dev
static char rcv_buff[BUFFER_LENGTH]; // The receive buffer from the module

int main()
{
   int ret, fd;
   char to_kernel[BUFFER_LENGTH] = {0};

   printf("Starting device test code example...\n");

   // Open the device with read/write access
   fd = open(DEVICE_NAME, O_RDWR);
   if (fd < 0) {
      printf("Failed to open the device\n");
      return errno;
   }
   printf("Type in a short string to send to the kernel module:\n");
   scanf("%255s", to_kernel); // Read in a string (with spaces)
   printf("Writing message to the device [%s].\n", to_kernel);
   // Send the string to the module
   ret = write(fd, to_kernel, strlen(to_kernel));
   if (ret < 0) {
      printf("Failed to write the message to the device.\n");
      return errno;
   }

   printf("Press ENTER to read back from the device...\n");
   getchar();
   getchar();

   printf("Reading from the device...\n");
   // Read the response from the module
   ret = read(fd, rcv_buff, BUFFER_LENGTH);
   if (ret < 0) {
      printf("Failed to read the message from the device.\n");
      return errno;
   }

   printf("The received message is: [%s]\n", rcv_buff);
   close(fd);
   printf("End of the program\n");

   return 0;
}