#include <stdbool.h>

#ifndef __DMA_PROXY_CHANNEL_INTERFACE_H__
// Max size of transfers to be done.
#define TEST_SIZE 1024 
#define __DMA_PROXY_CHANNEL_INTERFACE_H__

// Struct linking to kernel driver for DMA
struct dma_proxy_channel_interface {
	// Transmit channel in data
	unsigned char buffer[TEST_SIZE];

	// Status of DMA transfer
	enum proxy_status { PROXY_NO_ERROR = 0, PROXY_BUSY = 1, PROXY_TIMEOUT = 2, PROXY_ERROR = 3 } status;
	
	// Length of transfer for AXI DMA Status register
        /* From AXI DMA v7.1 LogiCORE IP Product Guide:
	  Optional Direct Register Mode
 	  (no scatter/gather support)
		A lower performance but less FPGA-resource-intensive mode can be enabled by 
		excluding the Scatter Gather engine. In this
		mode transfers are commanded by setting a 
		Source Address (for MM2S) or Destination Address (For S2MM) and then specifying a 
		byte count in a length register.
	Send and receive buffers will be same length
	Only the first "length" bytes of buffer will be used
	*/
	unsigned int length;

	// Channel ready for transfer
	bool ready;
};

#endif
