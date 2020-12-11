#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include "dma-proxy.h"
#include <stdbool.h>
//#include <asm/uaccess.h>
#include <linux/uaccess.h>
#define CACHED_BUFFERS
#define INTERNAL_TEST

/* The following module parameter controls where the allocated interface memory area is cached or not
 * such that both can be illustrated.  Add cached_buffers=1 to the command line insert of the module
 * to cause the allocated memory to be cached.
 */
static unsigned cached_buffers = 0;
module_param(cached_buffers, int, S_IRUGO);

MODULE_LICENSE("GPL");

#define DRIVER_NAME 		"dma_proxy"
#define CHANNEL_COUNT 		2
#define ERROR 			-1
#define NOT_LAST_CHANNEL 	0
#define LAST_CHANNEL 		1

/* The following data structure represents a single channel of DMA, transmit or receive in the case
 * when using AXI DMA.  It contains all the data to be maintained for the channel.
 */
struct dma_proxy_channel {
	struct dma_proxy_channel_interface *interface_p;	/* user to kernel space interface */
	dma_addr_t interface_phys_addr;			
	
	struct device *dma_device_p;

	struct dma_chan *channel_p;				/* dma support */
	struct completion cmp;
	dma_cookie_t cookie;
	dma_addr_t dma_handle;
	u32 direction;						/* DMA_MEM_TO_DEV or DMA_DEV_TO_MEM */
};

struct dma_proxy_dev {
	struct dma_proxy_channel *tx_proxy_channel;
	struct dma_proxy_channel *rx_proxy_channel;
	struct cdev cdev;
	dev_t dev_node;	
	struct class *class_p;
	struct device *proxy_device_p;				/* character device support */
	struct dma_proxy_channel_interface *interface_p;
	
};

struct dma_proxy_dev *dma_proxy_device;

/* Handle a callback and indicate the DMA transfer is complete to another
 * thread of control
 */
static void sync_callback(void *completion)
{
	/* Indicate the DMA transaction completed to allow the other
	 * thread of control to finish processing
	 */ 
	complete(completion);
}

/* Prepare a DMA buffer to be used in a DMA transaction, submit it to the DMA engine 
 * to queued and return a cookie that can be used to track that status of the 
 * transaction
 */ 
static dma_cookie_t start_transfer(struct dma_proxy_channel *pchannel_p)
{
	
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_async_tx_descriptor *chan_desc;
	dma_cookie_t cookie;

	struct dma_proxy_channel_interface *interface_p = pchannel_p->interface_p;
	
	/* Create a buffer (channel)  descriptor for the buffer since only a  
	 * single buffer is being used for this transfer
	 */
	chan_desc = dmaengine_prep_slave_single(pchannel_p->channel_p, pchannel_p->dma_handle, 
						interface_p->length, 
						pchannel_p->direction,
						flags);

	/* Make sure the operation was completed successfully
	 */
	if (!chan_desc) {
		printk(KERN_ERR "dmaengine_prep_slave_single error\n");
		cookie = -EBUSY;
	} else {
		chan_desc->callback = sync_callback;
		chan_desc->callback_param = &pchannel_p->cmp;

		/* Initialize the completion for the transfer and before using it
		 * then submit the transaction to the DMA engine so that it's queued
		 * up to be processed later and get a cookie to track it's status
		 */
		init_completion(&pchannel_p->cmp);
		cookie = dmaengine_submit(chan_desc);

 		/* Start the DMA transaction which was previously queued up in the DMA engine
 		 */ 
		dma_async_issue_pending(pchannel_p->channel_p); 
	}
	return cookie;
}

/* Wait for a DMA transfer that was previously submitted to the DMA engine
 * wait for it complete, timeout or have an \error
 */
static void wait_for_transfer(struct dma_proxy_channel *pchannel_p)
{

	unsigned long timeout = msecs_to_jiffies(3000);
	enum dma_status status;

	pchannel_p->interface_p->status = PROXY_BUSY;

	/* Wait for the transaction to complete, or timeout, or get an error
	 */
	timeout = wait_for_completion_timeout(&pchannel_p->cmp, timeout);

	status = dma_async_is_tx_complete(pchannel_p->channel_p, pchannel_p->cookie, NULL, NULL);

	if (timeout == 0)  
	{
		pchannel_p->interface_p->status  = PROXY_TIMEOUT;
		printk(KERN_ERR "DMA timed out\n");
	}

	else if (status != DMA_COMPLETE) {
		pchannel_p->interface_p->status = PROXY_ERROR;
		printk(KERN_ERR "DMA returned completion callback status of: %s\n",
		       status == DMA_ERROR ? "error" : "in progress");
	}
	else 
	{
		pchannel_p->interface_p->status = PROXY_NO_ERROR;
		
	}
}

/* For debug only, print out the channel details 
 */
void print_channel(struct dma_proxy_channel *pchannel_p)
{
	struct dma_proxy_channel_interface *interface_p = pchannel_p->interface_p;
}

/* Setup the DMA transfer for the channel by taking care of any cache operations
 * and the start it.
 */
static void transfer(struct dma_proxy_channel *pchannel_p)
{
	int i;

	struct dma_proxy_channel_interface *interface_p = pchannel_p->interface_p;
	u32 map_direction;

	print_channel(pchannel_p);


	/* Cached buffers need to be handled before starting the transfer so that
	 * any cached data is pushed to memory.
	 */
	pchannel_p->dma_handle = dma_map_single(pchannel_p->dma_device_p, 
						interface_p->buffer, 
						interface_p->length, 
						DMA_MEM_TO_DEV);

	/* Start the DMA transfer and make sure there were not any errors
	 */
	pchannel_p->cookie = start_transfer(pchannel_p);

	if (dma_submit_error(pchannel_p->cookie)) {
		printk(KERN_ERR "xdma_prep_buffer error\n");
		return;
	}

	wait_for_transfer(pchannel_p);


	if(pchannel_p->direction == DMA_MEM_TO_DEV) { //tx
        	dma_unmap_single(pchannel_p->dma_device_p, pchannel_p->dma_handle, pchannel_p->interface_p->length, DMA_TO_DEVICE);
	} 
	else //tx 
	{
        	dma_unmap_single(pchannel_p->dma_device_p, pchannel_p->dma_handle, pchannel_p->interface_p->length, DMA_FROM_DEVICE);	
	}
	
}

/* Map the memory for the channel interface into user space such that user space can 
 * access it taking into account if the memory is not cached.
 */
static int mmap(struct file *file_p, struct vm_area_struct *vma)
{

        struct dma_proxy_channel_interface *interface_p = (struct dma_proxy_channel_interface *)file_p->private_data;


	//pchannel_p->interface_p->length = 1024;
	/* The virtual address to map into is good, but the page frame will not be good since
 	 * user space passes a physical address of 0, so get the physical address of the buffer
 	 * that was allocated and convert to a page frame number.
 	 */

	void* ptr = (void*) interface_p;

	if (remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(ptr) >> PAGE_SHIFT,
		  	vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		return -EAGAIN;
	}

	return 0;
		
}

/* Open the device file and set up the data pointer to the proxy channel data for the 
 * proxy channel such that the ioctl function can access the data structure later.
 */
static int local_open(struct inode *ino, struct file *file)
{

        struct dma_proxy_dev *device = container_of(ino->i_cdev, struct dma_proxy_dev, cdev);
	file->private_data = device->interface_p;

	return 0;
}

/* Close the file and there's nothing to do for it
 */
static int release(struct inode *ino, struct file *file)
{
	return 0;
}

/* Perform I/O control to start a DMA transfer.
 */


static void send_tx(struct work_struct *unused)
{
	transfer(dma_proxy_device->tx_proxy_channel);
}

static long ioctl(struct file *file, unsigned int unused , unsigned long arg)
{

	struct work_struct work;

        struct dma_proxy_channel_interface *interface_p = (struct dma_proxy_channel_interface *)file->private_data;

	/* Step 2, call the transfer function for the channel to start the DMA and wait
 	 * for it to finish (blocking in the function).
 	 */

	int length = dma_proxy_device->interface_p->length;

	dma_proxy_device->tx_proxy_channel->interface_p->length = length;	
	dma_proxy_device->rx_proxy_channel->interface_p->length = length;	

	int i;

	for(i = 0; i < length; i++)
	{
		dma_proxy_device->tx_proxy_channel->interface_p->buffer[i] = dma_proxy_device->interface_p->buffer[i];
		dma_proxy_device->rx_proxy_channel->interface_p->buffer[i] = 0;
	}


	INIT_WORK(&work, send_tx);
	schedule_work(&work);

	transfer(dma_proxy_device->rx_proxy_channel);

	return 0;
}

ssize_t read(struct file *filp, char *buf, size_t bytes, loff_t *off)
{
	int ret;	
	ret = copy_to_user(buf, dma_proxy_device->rx_proxy_channel->interface_p->buffer, bytes);

	return ret;
}

static struct file_operations dm_fops = {
	.owner    = THIS_MODULE,
	.open     = local_open,
	.release  = release,
	.unlocked_ioctl = ioctl,
	.mmap	= mmap,
	.read = read
};


/* Initialize the driver to be a character device such that is responds to 
 * file operations.
 */
static int cdevice_init(void)
{


	int rc;
	static struct class *local_class_p = NULL;

	/* Allocate a character device from the kernel for this 
	 * driver
	 */
	rc = alloc_chrdev_region(&dma_proxy_device->dev_node, 0, 1, "dma_proxy-dev");

	if (rc) {
		printk(KERN_INFO "unable to get a char device number\n");
		return rc;
	}

	/* Initialize the ter device data structure before
	 * registering the character device with the kernel
	 */ 
	cdev_init(&dma_proxy_device->cdev, &dm_fops);
	dma_proxy_device->cdev.owner = THIS_MODULE;
	rc = cdev_add(&dma_proxy_device->cdev, dma_proxy_device->dev_node, 1);

	if (rc) {
		printk(KERN_INFO "unable to add char device\n");
		goto init_error1;
	}

	/* Only one class in sysfs is to be created for multiple channels,
 	 * create the device in sysfs which will allow the device node 
	 * in /dev to be created 
	 */
	if (!local_class_p) {
		local_class_p = class_create(THIS_MODULE, "dma_proxy_dev");
		
		if (IS_ERR(dma_proxy_device->class_p)) {
			printk(KERN_INFO, "unable to create class\n");
			rc = ERROR;
			goto init_error2;
		}	
	}
	dma_proxy_device->class_p = local_class_p;

	/* Create the device node in /dev so the device is accessible 
	 * as a character device
	 */
	dma_proxy_device->proxy_device_p = device_create(dma_proxy_device->class_p, NULL,
							dma_proxy_device->dev_node, NULL, "dma_proxy_dev");

	if (IS_ERR(dma_proxy_device->proxy_device_p)) {
		printk(KERN_INFO "unable to create the device\n");
		goto init_error3;
	}

	return 0;

init_error3:
	class_destroy(dma_proxy_device->class_p);

init_error2:
	cdev_del(&dma_proxy_device->cdev);

init_error1:
	unregister_chrdev_region(dma_proxy_device->dev_node, 1);
	return rc;
}

/* Exit the character device by freeing up the resources that it created and 
 * disconnecting itself from the kernel.
 */
static void cdevice_exit(void)
{
	/* Take everything down in the reverse order
	 * from how it was created for the char device
	 */
	if (dma_proxy_device->proxy_device_p) {
		device_destroy(dma_proxy_device->class_p, dma_proxy_device->dev_node);
		class_destroy(dma_proxy_device->class_p);

		cdev_del(&dma_proxy_device->cdev);
		unregister_chrdev_region(dma_proxy_device->dev_node, 1);
	}
}

/* Create a DMA channel by getting a DMA channel from the DMA Engine and then setting
 * up the channel as a character device to allow user space control.
 */
static int create_channels(struct platform_device *pdev)
{
	// Init device and area for proxy interface 
	dma_proxy_device = (struct dma_proxy_dev*)kmalloc(sizeof(struct dma_proxy_dev), GFP_KERNEL);
	dma_proxy_device->interface_p = (struct dma_proxy_channel_interface *)kmalloc(sizeof(struct dma_proxy_channel_interface), GFP_KERNEL);
	int rc;

	// TX Channel initialization
	dma_proxy_device->tx_proxy_channel = (struct dma_proxy_channel*)kmalloc(sizeof(struct dma_proxy_dev), GFP_KERNEL);	
	dma_proxy_device->tx_proxy_channel->channel_p = dma_request_slave_channel(&pdev->dev, "axidma0");
	if (!dma_proxy_device->tx_proxy_channel->channel_p) {
		printk(KERN_INFO "DMA channel request error\n");
		return ERROR;
	}
	dma_proxy_device->tx_proxy_channel->dma_device_p = &dma_proxy_device->tx_proxy_channel->channel_p->dev->device;
	dma_proxy_device->tx_proxy_channel->direction = DMA_MEM_TO_DEV;
	dma_proxy_device->tx_proxy_channel->interface_p = (struct dma_proxy_channel_interface *)
				kzalloc(sizeof(struct dma_proxy_channel_interface),  
	                     	GFP_KERNEL);
	//printk(KERN_INFO "Allocating cached memory at 0x%08X\n", 
	//			(unsigned int)dma_proxy_device->tx_proxy_channel->interface_p);
	if (!dma_proxy_device->tx_proxy_channel->interface_p) {
		printk(KERN_INFO "DMA allocation error\n");
		return ERROR;
	}
	
	// RX Channel initialization
	dma_proxy_device->rx_proxy_channel = (struct dma_proxy_channel*)kmalloc(sizeof(struct dma_proxy_dev), GFP_KERNEL);
	dma_proxy_device->rx_proxy_channel->channel_p = dma_request_slave_channel(&pdev->dev, "axidma1");
	if (!dma_proxy_device->rx_proxy_channel->channel_p) {
		printk(KERN_INFO "DMA channel request error\n");
		return ERROR;
	}

	dma_proxy_device->rx_proxy_channel->dma_device_p = &dma_proxy_device->rx_proxy_channel->channel_p->dev->device;

	dma_proxy_device->rx_proxy_channel->direction = DMA_DEV_TO_MEM;

	dma_proxy_device->rx_proxy_channel->interface_p = (struct dma_proxy_channel_interface *)
				kzalloc(sizeof(struct dma_proxy_channel_interface),  
	                     	GFP_KERNEL);
	//printk(KERN_INFO "Allocating cached memory at 0x%08X\n", 
	//			(unsigned int)dma_proxy_device->rx_proxy_channel->interface_p);
	if (!dma_proxy_device->rx_proxy_channel->interface_p) {
		printk(KERN_INFO "DMA allocation error\n");
		return ERROR;
	}


	/* Initialize the character device for the dma proxy channel 
 	 */
	rc = cdevice_init();
	if (rc) {
		printk(KERN_INFO "Device init failed");
		return rc;
	}
	
	//printk(KERN_INFO "Still have not failed\n");

	return 0;
}

int dma_driver_probe(struct platform_device *pdev)
{
	//printk(KERN_INFO "Probing\n");

	dma_proxy_device = (struct dma_proxy_dev*)kmalloc(sizeof(struct dma_proxy_dev), GFP_KERNEL);

	int rc;

	rc = create_channels(pdev);
	
	return rc;


}

static const struct of_device_id dma_driver_of_ids[] = {
        { .compatible = "jf,axi-dma-proxy-dev-1.00.a",},
        {}
};

static struct platform_driver dma_driver = {
        .driver = {
                .name = "dmaproxy-dev",
                .owner = THIS_MODULE,
                .of_match_table = dma_driver_of_ids,
        },
	.probe = dma_driver_probe
};


/* Initialize the dma proxy device driver module.
 */
static int __init dma_proxy_init(void)
{


	int rc;


	/* Create 2 channels, the first is a transmit channel 
 	 * the second is the receive channel.
 	 */ 

	rc = platform_driver_register(&dma_driver);	


#ifdef INTERNAL_TEST
	//test();
#endif

	return 0;
}

/* Exit the dma proxy device driver module.
 */
static void __exit dma_proxy_exit(void)
{

	int i;


	/* Take care of the char device infrastructure for each
 	 * channel except for the last channel. Handle the last
 	 * channel seperately.
 	 */
	cdevice_exit();
	
	/* Take care of the DMA channels and the any buffers allocated 
 	 * for the DMA transfers. The DMA buffers are using managed
 	 * memory such that it's automatically done.
	 */
	if (dma_proxy_device->tx_proxy_channel->channel_p)	
		dma_release_channel(dma_proxy_device->tx_proxy_channel->channel_p);

	if (dma_proxy_device->tx_proxy_channel->interface_p && cached_buffers)
		kfree((void *)dma_proxy_device->tx_proxy_channel->interface_p); 

	if (dma_proxy_device->rx_proxy_channel->channel_p)	
		dma_release_channel(dma_proxy_device->rx_proxy_channel->channel_p);

	if (dma_proxy_device->rx_proxy_channel->interface_p && cached_buffers)
		kfree((void *)dma_proxy_device->rx_proxy_channel->interface_p);

	platform_driver_unregister(&dma_driver);
	 
	
}

module_init(dma_proxy_init);
module_exit(dma_proxy_exit);
MODULE_LICENSE("GPL");
