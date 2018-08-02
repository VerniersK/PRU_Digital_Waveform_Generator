/*
 * Kevin Verniers (DRAMCO) - 1/08/2018
 *
 * Kumar's original BeagleLogic kernel code was used as the basis to create a
 * digital waveform generator via the PRU subsystem. The original BeagleLogic code 
 * supported only read operation and permissions. Now, it's converted to support the
 * writing operation. 
 *
 * Further adaptations:
 *		- Memory allocation: last (or only) buffer's size can deviate from buffer
 *							 unit size (bufunitsize).
 *
 *		- Non-used sysfs attributes from the original code such as sampleunit, 
 *		  samplerate, and triggerflags are removed in this code. 
 *		
 *		- DMA transfer direction: adapted DMA_FROM_DEVICE --> DMA_TO_DEVICE
 *
 *		- Request stop is not implemented in the PRU0 assembler code
 *
 *		- Data block transfers from RAM memory to PRU core is 64 bytes instead of 
 *		  32 bytes in original BeagleLogic code
 *
 *----------------------------------------------------------------------------
 *
 * Kernel module for BeagleLogic - a logic analyzer for the BeagleBone [Black]
 * Designed to be used in conjunction with a modified pru_rproc driver
 *
 * Copyright (C) 2014-17 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <asm/atomic.h>
#include <asm/uaccess.h>

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/poll.h>

#include <linux/platform_device.h>
#include <linux/pruss.h>
#include <linux/remoteproc.h>
#include <linux/miscdevice.h>

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

#include <linux/kobject.h>
#include <linux/string.h>

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#include <linux/sysfs.h>
#include <linux/fs.h>

#include "beaglelogic.h"

/* Buffer states */
enum bufstates {
	STATE_BL_BUF_ALLOC,
	STATE_BL_BUF_MAPPED,
	STATE_BL_BUF_UNMAPPED,
	STATE_BL_BUF_DROPPED
};

/* PRU Commands */
#define CMD_GET_VERSION 1   /* Firmware version */
#define CMD_GET_MAX_SG  2   /* Get the max number of bufferlist entries */
#define CMD_SET_CONFIG  3   /* Get the context pointer */
#define CMD_START       4   /* Arm the waveform generator (start sampling) */

/* PRU-side sample buffer descriptor */
struct buflist {
	uint32_t dma_start_addr;
	uint32_t dma_end_addr;
};

/* Shared structure containing PRU attributes */
struct capture_context {
	/* Magic bytes */
#define BL_FW_MAGIC	0xBEA61E10
	uint32_t magic;         // Magic bytes, should be 0xBEA61E10

	uint32_t cmd;           // Command from Linux host to us
	uint32_t resp;          // Response code

	// Samplediv, sampleunit, and triggerflags are not needed
	
	struct buflist list_head;
};

/* Forward declaration */
static const struct file_operations pru_beaglelogic_fops;

/* Buffers are arranged as an array but are
 * also circularly linked to simplify reads */
struct logic_buffer {
	void *buf;
	dma_addr_t phys_addr;
	size_t size;

	unsigned short state;
	unsigned short index;

	struct logic_buffer *next;
};

struct beaglelogic_private_data {
	const char *fw_names[PRUSS_NUM_PRUS];
};

struct beaglelogicdev {
	/* Misc device descriptor */
	struct miscdevice miscdev;

	/* Handle to pruss structure and PRU0 SRAM */
	struct pruss *pruss;
	struct rproc *pru0, *pru1;
	struct pruss_mem_region pru0sram;
	const struct beaglelogic_private_data *fw_data;

	/* IRQ numbers */
	int to_bl_irq;
	int from_bl_irq_1;
	int from_bl_irq_2;

	/* Private data */
	struct device *p_dev; /* Parent platform device */

	/* Locks */
	struct mutex mutex;

	/* Buffer management */
	struct logic_buffer *buffers;
	struct logic_buffer *lastbufready;
	struct logic_buffer *bufbeingread;
	uint32_t bufcount;
	wait_queue_head_t wait;

	/* Firmware capabilities */
	struct capture_context *cxt_pru;

	/* Device capabilities */
	uint32_t maxbufcount;	/* Max buffer count supported by the PRU FW */
	uint32_t bufunitsize;  	/* Size of 1 Allocation unit */

	/* State */
	uint32_t state;
	uint32_t lasterror;
};

struct logic_buffer_reader {
	struct beaglelogicdev *bldev;
	struct logic_buffer *buf;

	uint32_t pos;
	uint32_t remaining;
};

#define to_beaglelogicdev(dev)	container_of((dev), \
		struct beaglelogicdev, miscdev)

#define DRV_NAME	"beaglelogic"
#define DRV_VERSION	"0.1"

/* Begin Buffer Management section */

/* Allocate DMA buffers for the PRU core
 * This method acquires & releases the device mutex */
static int beaglelogic_memalloc(struct device *dev, uint32_t bufsize)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int i, cnt;
	unsigned int AllocSize, modulo;
	void *buf;

	// Check if device is available
	if (!mutex_trylock(&bldev->mutex))
		return -EBUSY;

	// Buffer amount to allocate (no ping pong action anymore)
	cnt = DIV_ROUND_UP(bufsize, bldev->bufunitsize);

	if (cnt > bldev->maxbufcount) {
		dev_err(dev, "Not enough memory\n");
		return -ENOMEM;
	}

	bldev->bufcount = cnt;

	bldev->buffers = devm_kzalloc(dev, sizeof(struct logic_buffer) * (cnt),
			GFP_KERNEL);
	if (!bldev->buffers)
		goto failnomem;

	// DMA buffers allocation. Last (or only) buffer's size can deviate from bufunitsize
	for (i = 0; i < cnt; i++) {
		// Last buffer?
		if (i == (cnt - 1)){
			// Determine last (or only) buffer's size
			AllocSize = bufsize - i * bldev->bufunitsize;
			
			// Check buffer's size is an integer value of 64.
			// If not, append extra memory until it fits an integer of 64.
			// If this step is omitted, PRU0 shall copy at the end of the last
			// buffer some data that does not belong to the code, therefore
			// producing an undesired result.
			modulo = AllocSize % 64;
			if (modulo != 0) {
				AllocSize += (64 - modulo);
			}
		} else {
			// Use bufunitsize
			AllocSize = bldev->bufunitsize;
		}
				
		buf = kmalloc(AllocSize, GFP_KERNEL);
		if (!buf)
			goto failrelease;

		// Buffer's values set to zero
		memset(buf, 0x00, AllocSize);

		// Set specific data buffers
		bldev->buffers[i].buf = buf;
		bldev->buffers[i].phys_addr = virt_to_phys(buf);
		bldev->buffers[i].size = AllocSize;
		bldev->buffers[i].index = i;

		// Circularly link the buffers  --> Modulo necessary to link last buffer with first buffer
		bldev->buffers[i].next = &bldev->buffers[(i + 1) % cnt];
	}

	dev_info(dev, "Allocated %d buffers to allocate %d bytes. %d buffers contain each %d bytes (equals %d bytes in total), the last buffer has %d bytes",
		cnt, bufsize, cnt-1, bldev->bufunitsize, (cnt-1) * bldev->bufunitsize, bldev->buffers[cnt-1].size);
	
	/* Write log and unlock */
	mutex_unlock(&bldev->mutex);

	/* Done */
	return 0;
failrelease:
	for (i = 0; i < cnt; i++) {
		if (bldev->buffers[i].buf)
			kfree(bldev->buffers[i].buf);
	}
	devm_kfree(dev, bldev->buffers);
	bldev->bufcount = 0;
	bldev->buffers = NULL;
	dev_err(dev, "Sample buffer allocation:");
failnomem:
	dev_err(dev, "Not enough memory\n");
	mutex_unlock(&bldev->mutex);
	return -ENOMEM;
}

/* Frees the DMA buffers and the bufferlist */
static void beaglelogic_memfree(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int i;

	mutex_lock(&bldev->mutex);
	if (bldev->buffers) {
		for (i = 0; i < bldev->bufcount; i++)
			if (bldev->buffers[i].buf)
				kfree(bldev->buffers[i].buf);

		devm_kfree(dev, bldev->buffers);
		bldev->buffers = NULL;
		bldev->bufcount = 0;
	}
	mutex_unlock(&bldev->mutex);
}

/* No argument checking for the map/unmap functions */
static int beaglelogic_map_buffer(struct device *dev, struct logic_buffer *buf)
{
	dma_addr_t dma_addr;

	/* If already mapped, do nothing */
	if (buf->state == STATE_BL_BUF_MAPPED)
		return 0;

	dma_addr = dma_map_single(dev, buf->buf, buf->size, DMA_TO_DEVICE);							// Changed DMA direction 
	if (dma_mapping_error(dev, dma_addr))
		goto fail;
	else {
		buf->phys_addr = dma_addr;
		buf->state = STATE_BL_BUF_MAPPED;
	}

	return 0;
fail:
	dev_err(dev, "DMA Mapping error. \n");
	return -1;
}

static void beaglelogic_unmap_buffer(struct device *dev,
                                     struct logic_buffer *buf)
{
	dma_unmap_single(dev, buf->phys_addr, buf->size, DMA_TO_DEVICE);							// Changed DMA direction
	buf->state = STATE_BL_BUF_UNMAPPED;
}

/* Map all the buffers. This is done just before beginning a waveform generation
 * NOTE: PRUs are halted at this time */
static int beaglelogic_map_and_submit_all_buffers(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	struct buflist *pru_buflist = &bldev->cxt_pru->list_head;
	int i, j;
	dma_addr_t addr;

	if (!pru_buflist)
		return -1;

	for (i = 0; i < bldev->bufcount;i++) {
		if (beaglelogic_map_buffer(dev, &bldev->buffers[i]))
			goto fail;
	}

	/* Write buffer table to the PRU memory, and null terminate */
	for (i = 0; i < bldev->bufcount; i++) {
		addr = bldev->buffers[i].phys_addr;
		pru_buflist[i].dma_start_addr = addr;
		pru_buflist[i].dma_end_addr = addr + bldev->buffers[i].size;
	}
	pru_buflist[i].dma_start_addr = 0;
	pru_buflist[i].dma_end_addr = 0;

	/* Update state to ready */
	if (i)
		bldev->state = STATE_BL_ARMED;

	return 0;
fail:
	/* Unmap the buffers */
	for (j = 0; j < i; j++)
		beaglelogic_unmap_buffer(dev, &bldev->buffers[j]);

	dev_err(dev, "DMA Mapping failed at i=%d\n", i);

	bldev->state = STATE_BL_ERROR;
	return 1;
}

/* End Buffer Management section */

/* Send command to the PRU firmware */
static int beaglelogic_send_cmd(struct beaglelogicdev *bldev, uint32_t cmd)
{
#define TIMEOUT     200
	uint32_t timeout = TIMEOUT;

	bldev->cxt_pru->cmd = cmd;

	/* Wait for firmware to process the command */
	while (--timeout && bldev->cxt_pru->cmd != 0)
		cpu_relax();

	if (timeout == 0)
		return -1;

	return bldev->cxt_pru->resp;
}

/* Request the PRU firmware to stop capturing */
static void beaglelogic_request_stop(struct beaglelogicdev *bldev)
{
	/* Trigger interrupt */
	pruss_intc_trigger(bldev->to_bl_irq);
}

/* This is [to be] called from a threaded IRQ handler */
irqreturn_t beaglelogic_serve_irq(int irqno, void *data)
{
	struct beaglelogicdev *bldev = data;
	struct device *dev = bldev->miscdev.this_device;
	uint32_t state = bldev->state;
	unsigned int i;
	
	dev_dbg(dev, "Beaglelogic IRQ #%d\n", irqno);
	if (irqno == bldev->from_bl_irq_1) {
		
		// Unmap all buffers and change state
		for(i = 0; i < bldev->bufcount; i++){
				beaglelogic_unmap_buffer(dev, &bldev->buffers[i]);
		}	
		
		bldev->state = STATE_BL_INITIALIZED;
		wake_up_interruptible(&bldev->wait);
	} else if (irqno == bldev->from_bl_irq_2) {	// Now only used when PRU1 'configuration' is done
		state = bldev->state;
		if (state <= STATE_BL_ARMED) {
			dev_dbg(dev, "config written, BeagleLogic ready\n");
			return IRQ_HANDLED;
		}
		else if (state != STATE_BL_REQUEST_STOP &&
				state != STATE_BL_RUNNING) {
			dev_err(dev, "Unexpected stop request \n");
			bldev->state = STATE_BL_ERROR;
			return IRQ_HANDLED;
		}
		bldev->state = STATE_BL_INITIALIZED;
		wake_up_interruptible(&bldev->wait);
	}
	return IRQ_HANDLED;
}

/* Write configuration into the PRU [via downcall] (assume mutex is held) */
// Not necessary anymore in transmitter setup as most sysfs attributes are omitted.
int beaglelogic_write_configuration(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int ret;

	ret = beaglelogic_send_cmd(bldev, CMD_SET_CONFIG);

	dev_dbg(dev, "PRU Config written, err code = %d\n", ret);
	return 0;
}

/* Begin the waveform generation [This takes the mutex] */
int beaglelogic_start(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	/* This mutex will be locked for the entire duration BeagleLogic runs */
	mutex_lock(&bldev->mutex);
	if (beaglelogic_write_configuration(dev)) {
		mutex_unlock(&bldev->mutex);
		return -1;
	}
	bldev->bufbeingread = &bldev->buffers[0];
	beaglelogic_send_cmd(bldev, CMD_START);

	/* All set now. Start the PRUs and wait for IRQs */
	bldev->state = STATE_BL_RUNNING;
	bldev->lasterror = 0;

	dev_info(dev, "Waveform generation started");
	return 0;
}

/* Request stop. Stop will effect only after the last buffer is written out */
void beaglelogic_stop(struct device *dev)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	if (mutex_is_locked(&bldev->mutex)) {
		if (bldev->state == STATE_BL_RUNNING)
		{
			beaglelogic_request_stop(bldev);
			bldev->state = STATE_BL_REQUEST_STOP;

			/* Wait for the PRU to signal completion */
			wait_event_interruptible(bldev->wait,
					bldev->state == STATE_BL_INITIALIZED);
		}
		/* Release */
		mutex_unlock(&bldev->mutex);

		dev_info(dev, "Waveform generation session stopped\n");
	}
}

/* fops */
static int beaglelogic_f_open(struct inode *inode, struct file *filp)
{
	struct logic_buffer_reader *reader;
	struct beaglelogicdev *bldev = to_beaglelogicdev(filp->private_data);
	struct device *dev = bldev->miscdev.this_device;

	if (bldev->bufcount == 0)
		return -ENOMEM;

	reader = devm_kzalloc(dev, sizeof(*reader), GFP_KERNEL);
	reader->bldev = bldev;
	reader->buf = NULL;
	reader->pos = 0;
	reader->remaining = 0;

	filp->private_data = reader;

	if(!bldev->buffers)
		return -ENOMEM;

	beaglelogic_map_buffer(dev, &bldev->buffers[0]);

	return 0;
}

// Write operation of a user space buffer
ssize_t beaglelogic_f_write (struct file *filp, const char __user *buf,
                           size_t sz, loff_t *offset)
{
 	int count;
 	struct logic_buffer_reader *reader = filp->private_data;
 	struct beaglelogicdev *bldev = reader->bldev;

 	if (bldev->state == STATE_BL_ERROR)
 		return -EIO;

 	if (reader->pos > 0)
 		goto perform_copy;

 	if (reader->buf == NULL) {
 		/* First time init */
 		reader->buf = &reader->bldev->buffers[0];
		reader->pos = 0;							
 		reader->remaining = reader->buf->size;
 	} else {
 		if (reader->buf == bldev->buffers &&
 				bldev->state == STATE_BL_INITIALIZED)
 			return 0;
 	}

 perform_copy:
 	count = min(reader->remaining, sz);

 	if (copy_from_user(reader->buf->buf + reader->pos, buf, count))
 		return -EFAULT;

 	reader->pos += count;
 	reader->remaining -= count;

 	if (reader->remaining == 0) {
 		/* Change the buffer */
 		reader->buf = reader->buf->next;
 		reader->pos = 0;
 		reader->remaining = reader->buf->size;
 	}
 	return count;
}

/* Configuration through ioctl */
// Number of ioctl calls cropped since most BeagleLogic's sysfs attributes are omitted 
static long beaglelogic_f_ioctl(struct file *filp, unsigned int cmd,
		  unsigned long arg)
{
	struct logic_buffer_reader *reader = filp->private_data;
	struct beaglelogicdev *bldev = reader->bldev;
	struct device *dev = bldev->miscdev.this_device;

	uint32_t val;

	dev_dbg(dev, "BeagleLogic: IOCTL called cmd = %08X, "\
			"arg = %08lX\n", cmd, arg);

	switch (cmd) {
		case IOCTL_BL_GET_VERSION:
			return 0;

		case IOCTL_BL_GET_BUFFER_SIZE:
			// Adapted to display correct allocated n° bytes
			val = bldev->bufunitsize * (bldev->bufcount-1) + bldev->buffers[bldev->bufcount-1].size;
			if (copy_to_user((void * __user)arg,
					&val,
					sizeof(val)))
				return -EFAULT;
			return 0;

		case IOCTL_BL_SET_BUFFER_SIZE:
			beaglelogic_memfree(dev);
			val = beaglelogic_memalloc(dev,arg);
			if (!val)
				return beaglelogic_map_and_submit_all_buffers(dev);
			return val;

		case IOCTL_BL_GET_BUFUNIT_SIZE:
			if(copy_to_user((void * __user)arg,
					&bldev->bufunitsize,
					sizeof(bldev->bufunitsize)))
				return -EFAULT;
			return 0;

		case IOCTL_BL_SET_BUFUNIT_SIZE:
			// Data block transfer 64 bytes instead of 32 bytes in original BeagleLogic code
			if ((uint32_t)arg < 64)
				return -EINVAL;
			bldev->bufunitsize = round_up(arg, 64);
			beaglelogic_memfree(dev);
			return 0;

		case IOCTL_BL_START:
			/* Reset and reconfigure the reader object and then start */
			reader->buf = &bldev->buffers[0];
			reader->pos = 0;
			reader->remaining = reader->buf->size;

			beaglelogic_start(dev);
			return 0;

	}
	return -ENOTTY;
}

/* Device file close handler */
static int beaglelogic_f_release(struct inode *inode, struct file *filp)
{
	struct logic_buffer_reader *reader = filp->private_data;
	struct beaglelogicdev *bldev = reader->bldev;
	struct device *dev = bldev->miscdev.this_device;

	/* Stop & Release */
	beaglelogic_stop(dev);
	devm_kfree(dev, reader);

	return 0;
}

/* File operations struct */
static const struct file_operations pru_beaglelogic_fops = {
	.owner = THIS_MODULE,
	.open = beaglelogic_f_open,
	.unlocked_ioctl = beaglelogic_f_ioctl,
	.write = beaglelogic_f_write,
//	.poll = beaglelogic_f_poll,
	.release = beaglelogic_f_release,
};
/* fops */

/* begin sysfs attrs */
static ssize_t bl_bufunitsize_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bldev->bufunitsize);
}

static ssize_t bl_bufunitsize_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	uint32_t val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	if (val < 64)
		return -EINVAL;

	bldev->bufunitsize = round_up(val, 64);

	/* Free up previously allocated buffers */
	beaglelogic_memfree(dev);

	return count;
}

static ssize_t bl_memalloc_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	// Adapted to show correct amount of bytes allocated
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			(bldev->bufcount-1) * bldev->bufunitsize + bldev->buffers[bldev->bufcount-1].size);
}

static ssize_t bl_memalloc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	uint32_t val;
	int ret;

	if(kstrtouint(buf, 10, &val))
		return -EINVAL;

	if (val > bldev->maxbufcount * bldev->bufunitsize)
		return -EINVAL;

	beaglelogic_memfree(dev);
	ret = beaglelogic_memalloc(dev, val);

	if (!ret)
		beaglelogic_map_and_submit_all_buffers(dev);
	else
		return ret;
	return count;
}

// Not handled at the moment
static ssize_t bl_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	uint32_t state = bldev->state;
	struct logic_buffer *buffer = bldev->bufbeingread;

	if (state == STATE_BL_RUNNING) {
		/* State blocks and returns last buffer read */
		wait_event_interruptible(bldev->wait,
				buffer->state == STATE_BL_BUF_UNMAPPED);
		return scnprintf(buf, PAGE_SIZE, "%d\n", buffer->index);
	}

	/* Identify non-buffer debug states with a -ve value */
	return scnprintf(buf, PAGE_SIZE, "-%d\n", -bldev->state);
}

// Not handled at the moment
static ssize_t bl_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint32_t val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	/* State going to 1 starts the sampling operation, 0 aborts*/
	if (val > 1)
		return -EINVAL;

	if (val == 1)
		beaglelogic_start(dev);
	else
		beaglelogic_stop(dev);

	return count;
}

static ssize_t bl_buffers_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);
	int i, c, cnt;

	for (i = 0, c = 0, cnt = 0; i < bldev->bufcount; i++) {
		c = scnprintf(buf, PAGE_SIZE, "%08x,%u\n",
				(uint32_t)bldev->buffers[i].phys_addr,
				bldev->buffers[i].size);
		cnt += c;
		buf += c;
	}

	return cnt;
}

// Not handled at the moment
static ssize_t bl_lasterror_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct beaglelogicdev *bldev = dev_get_drvdata(dev);

	wait_event_interruptible(bldev->wait,
			bldev->state != STATE_BL_RUNNING);


	return scnprintf(buf, PAGE_SIZE, "%d\n", bldev->lasterror);
}

static DEVICE_ATTR(bufunitsize, S_IWUSR | S_IRUGO,
		bl_bufunitsize_show, bl_bufunitsize_store);

static DEVICE_ATTR(memalloc, S_IWUSR | S_IRUGO,
		bl_memalloc_show, bl_memalloc_store);

static DEVICE_ATTR(state, S_IWUSR | S_IRUGO,
		bl_state_show, bl_state_store);

static DEVICE_ATTR(buffers, S_IRUGO,
		bl_buffers_show, NULL);

static DEVICE_ATTR(lasterror, S_IRUGO,
		bl_lasterror_show, NULL);

static struct attribute *beaglelogic_attributes[] = {
	&dev_attr_bufunitsize.attr,
	&dev_attr_memalloc.attr,
	&dev_attr_state.attr,
	&dev_attr_buffers.attr,
	&dev_attr_lasterror.attr,
	NULL
};

static struct attribute_group beaglelogic_attr_group = {
	.attrs = beaglelogic_attributes
};
/* end sysfs attrs */

static const struct of_device_id beaglelogic_dt_ids[];

static int beaglelogic_probe(struct platform_device *pdev)
{
	struct beaglelogicdev *bldev;
	struct device *dev;
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match;
	int ret;

	if (!node)
		return -ENODEV; /* No support for non-DT platforms */

	match = of_match_device(beaglelogic_dt_ids, &pdev->dev);
	if (!match)
		return -ENODEV;

	/* Allocate memory for our private structure */
	bldev = kzalloc(sizeof(*bldev), GFP_KERNEL);
	if (!bldev) {
		ret = -1;
		goto fail;
	}

	bldev->fw_data = match->data;
	bldev->miscdev.fops = &pru_beaglelogic_fops;
	bldev->miscdev.minor = MISC_DYNAMIC_MINOR;
	bldev->miscdev.mode = S_IRWXUGO;										// Change read permissions to also support write and execute for all users
	bldev->miscdev.name = "beaglelogic";

	/* Link the platform device data to our private structure */
	bldev->p_dev = &pdev->dev;
	dev_set_drvdata(bldev->p_dev, bldev);

	/* Get a handle to the PRUSS structures */
	dev = &pdev->dev;
	bldev->pruss = pruss_get(dev, NULL);
	if (IS_ERR(bldev->pruss)) {
		ret = PTR_ERR(bldev->pruss);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to get pruss handle.\n");
		goto fail_free;
	}

	bldev->pru0 = pruss_rproc_get(bldev->pruss, PRUSS_PRU0);
	if (IS_ERR(bldev->pru0)) {
		ret = PTR_ERR(bldev->pru0);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to get PRU0.\n");
		goto fail_pruss_put;
	}

	bldev->pru1 = pruss_rproc_get(bldev->pruss, PRUSS_PRU1);
	if (IS_ERR(bldev->pru1)) {
		ret = PTR_ERR(bldev->pru1);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Unable to get PRU0.\n");
		goto fail_pru0_put;
	}

	ret = pruss_request_mem_region(bldev->pruss, PRUSS_MEM_DRAM0,
		&bldev->pru0sram);
	if (ret) {
		dev_err(dev, "Unable to get PRUSS RAM.\n");
		goto fail_putmem;
	}

	/* Get interrupts and install interrupt handlers */
	bldev->from_bl_irq_1 = platform_get_irq_byname(pdev, "from_bl_1");		// PRU0_TO_ARM_A
	if (bldev->from_bl_irq_1 <= 0) {
		ret = bldev->from_bl_irq_1;
		if (ret == -EPROBE_DEFER)
			goto fail_putmem;
	}
	bldev->from_bl_irq_2 = platform_get_irq_byname(pdev, "from_bl_2");		// PRU0_TO_ARM_B
	if (bldev->from_bl_irq_2 <= 0) {
		ret = bldev->from_bl_irq_2;
		if (ret == -EPROBE_DEFER)
			goto fail_putmem;
	}
	bldev->to_bl_irq = platform_get_irq_byname(pdev, "to_bl");				// ARM_TO_PRU0
	if (bldev->to_bl_irq<= 0) {
		ret = bldev->to_bl_irq;
		if (ret == -EPROBE_DEFER)
			goto fail_putmem;
	}

	ret = request_irq(bldev->from_bl_irq_1, beaglelogic_serve_irq,
		IRQF_ONESHOT, dev_name(dev), bldev);								// PRU0_TO_ARM_A
	if (ret) goto fail_putmem;

	ret = request_irq(bldev->from_bl_irq_2, beaglelogic_serve_irq,
		IRQF_ONESHOT, dev_name(dev), bldev);								// PRU0_TO_ARM_B
	if (ret) goto fail_free_irq1;

	/* Set firmware and boot the PRUs */
	ret = rproc_set_firmware(bldev->pru0, bldev->fw_data->fw_names[0]);
	if (ret) {
		dev_err(dev, "Failed to set PRU0 firmware %s: %d\n",
			bldev->fw_data->fw_names[0], ret);
		goto fail_free_irqs;
	}

	ret = rproc_set_firmware(bldev->pru1, bldev->fw_data->fw_names[1]);
	if (ret) {
		dev_err(dev, "Failed to set PRU1 firmware %s: %d\n",
			bldev->fw_data->fw_names[1], ret);
		goto fail_free_irqs;
	}

	ret = rproc_boot(bldev->pru0);
	if (ret) {
		dev_err(dev, "Failed to boot PRU0: %d\n", ret);
		goto fail_free_irqs;
	}

	ret = rproc_boot(bldev->pru1);
	if (ret) {
		dev_err(dev, "Failed to boot PRU1: %d\n", ret);
		goto fail_shutdown_pru0;
	}

	printk("BeagleLogic loaded and initializing\n");

	/* Once done, register our misc device and link our private data */
	ret = misc_register(&bldev->miscdev);
	if (ret)
		goto fail_shutdown_prus;
	dev = bldev->miscdev.this_device;
	dev_set_drvdata(dev, bldev);

	/* Set up locks */
	mutex_init(&bldev->mutex);
	init_waitqueue_head(&bldev->wait);

	/* Power on in disabled state */
	bldev->state = STATE_BL_DISABLED;

	/* Capture context structure is at location 0000h in PRU0 SRAM */
	bldev->cxt_pru = bldev->pru0sram.va + 0;

	if (bldev->cxt_pru->magic == BL_FW_MAGIC)
		dev_info(dev, "Valid PRU capture context structure "\
				"found at offset %04X\n", 0);
	else {
		dev_err(dev, "Firmware error!\n");
		goto faildereg;
	}

	/* Get firmware properties */
	ret = beaglelogic_send_cmd(bldev, CMD_GET_VERSION);
	if (ret != 0) {
		dev_info(dev, "BeagleLogic PRU Firmware version: %d.%d\n",
				ret >> 8, ret & 0xFF);
	} else {
		dev_err(dev, "Firmware error!\n");
		goto faildereg;
	}

	ret = beaglelogic_send_cmd(bldev, CMD_GET_MAX_SG);
	if (ret > 0 && ret < 256) { /* Let's be reasonable here */
		dev_info(dev, "Device supports max %d vector transfers\n", ret);
		bldev->maxbufcount = ret;
	} else {
		dev_err(dev, "Firmware error!\n");
		goto faildereg;
	}

	// Apply buffer unit size, currently up to 163 MiB, if higher value desired, increase this.
	bldev->bufunitsize = 640000;

	/* We got configuration from PRUs, now mark device init'd */
	bldev->state = STATE_BL_INITIALIZED;

	/* Display our init'ed state */
	dev_info(dev, "Device driver initialized with unit buffer size: %d\n", bldev->bufunitsize);

	/* Once done, create device files */
	ret = sysfs_create_group(&dev->kobj, &beaglelogic_attr_group);
	if (ret) {
		dev_err(dev, "Registration failed.\n");
		goto faildereg;
	}

	return 0;
faildereg:
	misc_deregister(&bldev->miscdev);
fail_shutdown_prus:
	rproc_shutdown(bldev->pru1);
fail_shutdown_pru0:
	rproc_shutdown(bldev->pru0);
fail_free_irqs:
	free_irq(bldev->from_bl_irq_2, bldev);
fail_free_irq1:
	free_irq(bldev->from_bl_irq_1, bldev);
fail_putmem:
	if (bldev->pru0sram.va)
		pruss_release_mem_region(bldev->pruss, &bldev->pru0sram);
	pruss_rproc_put(bldev->pruss, bldev->pru1);
fail_pru0_put:
	pruss_rproc_put(bldev->pruss, bldev->pru0);
fail_pruss_put:
	pruss_put(bldev->pruss);
fail_free:
	kfree(bldev);
fail:
	return ret;
}

static int beaglelogic_remove(struct platform_device *pdev)
{
	struct beaglelogicdev *bldev = platform_get_drvdata(pdev);
	struct device *dev = bldev->miscdev.this_device;

	/* Free all buffers */
	beaglelogic_memfree(dev);

	/* Remove the sysfs attributes */
	sysfs_remove_group(&dev->kobj, &beaglelogic_attr_group);

	/* Deregister the misc device */
	misc_deregister(&bldev->miscdev);

	/* Shutdown the PRUs */
	rproc_shutdown(bldev->pru1);
	rproc_shutdown(bldev->pru0);

	/* Free IRQs */
	free_irq(bldev->from_bl_irq_2, bldev);
	free_irq(bldev->from_bl_irq_1, bldev);

	/* Release handles to PRUSS memory regions */
	pruss_release_mem_region(bldev->pruss, &bldev->pru0sram);
	pruss_rproc_put(bldev->pruss, bldev->pru1);
	pruss_rproc_put(bldev->pruss, bldev->pru0);
	pruss_put(bldev->pruss);

	/* Free up memory */
	kfree(bldev);

	/* Print a log message to announce unloading */
	printk("BeagleLogic unloaded\n");
	return 0;
}

static struct beaglelogic_private_data beaglelogic_pdata = {
	.fw_names[0] = "beaglelogic-pru0-fw",
	.fw_names[1] = "beaglelogic-pru1-fw",
};

static const struct of_device_id beaglelogic_dt_ids[] = {
	{ .compatible = "beaglelogic,beaglelogic", .data = &beaglelogic_pdata, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, beaglelogic_dt_ids);

static struct platform_driver beaglelogic_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = beaglelogic_dt_ids,
	},
	.probe = beaglelogic_probe,
	.remove = beaglelogic_remove,
};

module_platform_driver(beaglelogic_driver);

MODULE_AUTHOR("Kevin Verniers <kevin.verniers@kuleuven.be>");
MODULE_DESCRIPTION("Modified BeagleLogic Kernel Driver (by Kevin Verniers) for BeagleBone waveform generation based on BeagleLogic driver by Kumar Abhishek");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
