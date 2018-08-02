/*
 * Kevin Verniers (DRAMCO) - 1/08/2018
 *
 * Original BeagleLogic code was slightly adapted here in order to operate
 * as a waveform generator. PRU1 has no explicit configuration anymore, only
 * its firmware version is now checked. In the main loop, PRU1 is started
 * before executing the PRU0 assembler code.
 *
 *
 * Copyright (C) 2014-17 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 *
 * This file is a part of the BeagleLogic project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* We are compiling for PRU0 */
#define PRU0

#include <stdint.h>
#include <stdio.h>
#include <pru_cfg.h>
#include <pru_intc.h>
#include <rsc_types.h>

#include "pru_defs.h"
#include "resource_table_0.h"

/*
 * Define firmware version
 * This is version 0.1
 */
#define MAJORVER	0
#define MINORVER	1

/* Maximum number of SG entries; each entry is 8 bytes */
#define MAX_BUFLIST_ENTRIES	128

/* Commands */
#define CMD_GET_VERSION	1   /* Firmware version */
#define CMD_GET_MAX_SG	2   /* Get the max number of bufferlist entries */
#define CMD_SET_CONFIG 	3   /* Get the context pointer */
#define CMD_START	4   /* Arm the LA (start sampling) */

/* Define magic bytes for the structure. This "looks like" BEAGLELO */
#define FW_MAGIC	0xBEA61E10

/* Structure describing the start and end buffer addresses */
typedef struct buflist {
	uint32_t dma_start_addr;
	uint32_t dma_end_addr;
} bufferlist;

/* Structure describing the core context.
 * Compiler attributes pin it at 0x0000 */
struct capture_context {
	uint32_t magic;         // Magic bytes, should be 0xBEA61E10

	uint32_t cmd;           // Command from Linux host to us
	uint32_t resp;          // Response code

	bufferlist list[MAX_BUFLIST_ENTRIES];
} cxt __attribute__((location(0))) = {0};

uint16_t state_run = 0;

static inline void resume_other_pru(void) {
	uint32_t i;

	i = (uint16_t)PCTRL_OTHER(0x0000);
	i |= (((uint16_t)PCTRL_OTHER(0x0004) + 1) << 16) | CONTROL_ENABLE;
	i &= ~CONTROL_SOFT_RST_N;
	PCTRL_OTHER(0x0000) = i;
}

static inline int wait_other_pru_timeout(uint32_t timeout) {
	do {
		if ((PCTRL_OTHER(0x0000) & CONTROL_RUNSTATE) == 0)
			return 0;
	} while (timeout--);
	return -1;
}

// This function now merely checks if the version of PRU1 is valid 
int configure_capture() {
	/* Verify if PRU1 is indeed halted and waiting for us */
	if (wait_other_pru_timeout(200))
		return -1;

	/* Verify magic bytes */
	if (pru_other_read_reg(0) != FW_MAGIC)
		return -1;
	
	/* Resume over the HALT instruction, give it some time to configure */
	resume_other_pru();
	__delay_cycles(10);
	if (wait_other_pru_timeout(200))
		return -1;

	/* Now the other PRU should be ready to take instructions */
	return 0;
 }

static int handle_command(uint32_t cmd) {
	switch (cmd) {
		case CMD_GET_VERSION:
			return (MINORVER | (MAJORVER << 8));

		case CMD_GET_MAX_SG:
			return MAX_BUFLIST_ENTRIES;

		case CMD_SET_CONFIG:
			return configure_capture();

		case CMD_START:
			state_run = 1;
			return 0;
	}
	return -1;
}

extern void run(struct capture_context *ctx);

int main(void) {
	/* Enable OCP Master Port */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;
	cxt.magic = FW_MAGIC;

	/* Clear all interrupts */
	CT_INTC.SECR0 = 0xFFFFFFFF;

	while (1) {
		/* Process received command */
		if (cxt.cmd != 0)
		{
			cxt.resp = handle_command(cxt.cmd);
			cxt.cmd = 0;
		}

		/* Run triggered */
		if (state_run == 1) {
			CT_INTC.SECR0 = 0xFFFFFFFF;

			resume_other_pru();
			run(&cxt);

			// Reset PRU1 (correct timing is governed in assembler code)
			PCTRL_OTHER(0x0000) &= (uint16_t)~CONTROL_SOFT_RST_N;
			state_run = 0;
		}
	}
}
