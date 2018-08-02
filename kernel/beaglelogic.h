/*
 * Kevin Verniers (DRAMCO) - 1/08/2018
 *
 * Kumar's original BeagleLogic kernel code was used as the basis to create a
 * digital waveform generator via the PRU subsystem. The original BeagleLogic code 
 * supported only read operation and permissions. Now, it's converted to support the
 * writing operation.
 *
 * Adaptations in this file:
 *		- Removal of non-used sysfs attributes
 *
 *----------------------------------------------------------------------------
 *
 * Userspace/Kernelspace common API for BeagleLogic
 * ioctl commands and enumeration of states
 *
 * Copyright (C) 2014-17 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef BEAGLELOGIC_H_
#define BEAGLELOGIC_H_

enum beaglelogic_states {
	STATE_BL_DISABLED,	/* Powered off (at module start) */
	STATE_BL_INITIALIZED,	/* Powered on */
	STATE_BL_MEMALLOCD,	/* Buffers allocated */
	STATE_BL_ARMED,		/* All Buffers DMA-mapped and configuration done */
	STATE_BL_RUNNING,	/* Data being captured */
	STATE_BL_REQUEST_STOP,	/* Stop requested */
	STATE_BL_ERROR   	/* Buffer overrun */
};

/* ioctl calls that can be issued on /dev/beaglelogic */

#define IOCTL_BL_GET_VERSION        _IOR('k', 0x20, u32)

#define IOCTL_BL_GET_BUFFER_SIZE    _IOR('k', 0x26, u32)
#define IOCTL_BL_SET_BUFFER_SIZE    _IOW('k', 0x26, u32)

#define IOCTL_BL_GET_BUFUNIT_SIZE   _IOR('k', 0x27, u32)
#define IOCTL_BL_SET_BUFUNIT_SIZE   _IOW('k', 0x27, u32)

#define IOCTL_BL_START               _IO('k', 0x29)

#endif /* BEAGLELOGIC_H_ */
