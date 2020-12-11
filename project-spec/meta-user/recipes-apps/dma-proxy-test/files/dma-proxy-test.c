#include<stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include "dma-proxy.h"
#include <unistd.h> 
#include <stdbool.h>

/*
	Because the DMA is in Direct Register Mode, the output and input 
	buffer must be the same length
*/


int main(int argc, char *argv[])
{
	int dma_fd = open("/dev/dma_proxy_dev", O_RDWR);
	if(dma_fd < 0)
	{
		printf( "Opening /dev/dma_proxy_dev failed" );
	}

	struct dma_proxy_channel_interface* interface = (struct dma_proxy_channel_interface*)mmap(NULL, sizeof(struct dma_proxy_channel_interface), PROT_READ | PROT_WRITE, MAP_SHARED, dma_fd, 0);

	if(interface == MAP_FAILED)
	{
		printf( "DMA mapping failed" );
	}

	unsigned char data[1024];
	for(int i = 0; i < 1024; i++)
	{
		data[i] = i;
	}
	
	interface->length = 1024;
	memcpy((void*)interface->buffer, data, 1024); 

	int dummy;
	ioctl(dma_fd, 0, &dummy);
	
	
	// ------------------------------- RESULTS --------------------------
	unsigned char data_out[1024];
	for(int i = 0; i < 1024; i++)
	{
		data_out[i] = 0;
	}
	read(dma_fd, data_out, 1024);
	
	for(int i = 0; i < 10; i++)
	{
		printf("data_out[%d]: %d\n", i, data_out[i]);
	}

	close(dma_fd);

	return 0;
}