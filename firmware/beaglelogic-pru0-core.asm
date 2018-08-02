;* Kevin Verniers (DRAMCO) - 1/08/2018
;*
;* Original PRU0 code from BeagleLogic project was adapted to reverse the 
;* data flow. 
;*
;* Copyright (C) 2014 Kumar Abhishek <abhishek@theembeddedkitchen.net>
;*
;* This file is a part of the BeagleLogic project
;*
;* This program is free software; you can redistribute it and/or modify
;* it under the terms of the GNU General Public License version 2 as
;* published by the Free Software Foundation.

;* Import all symbols from the C file
	.cdecls "beaglelogic-pru0.c"
	.include "beaglelogic-pru-defs.inc"

;* C declaration:
;* void run(struct capture_context *ctx)
	.clink
	.global run
run:
	XOUT	11, &R0, 120									; Save all registers (R0:29) onto scratchpad's bank 1
	LDI	R0, SYSEV_PRU1_TO_PRU0								; Necessary to reset PRU1's interrupt
	ADD	R1, R14, 12											; Load scatter/gather list entries
	LBBO	&R2, R1, 0, 8									; Load first DMA addresses, if they are 0 = exit	
	QBEQ	$run$exit, R2, 0
	LBBO	&R13, R2, 0, 64									; Load data and place onto scratchpad
	XOUT	10, &R13, 64
	ADD	R2, R2, 64
	QBLT	$run$start, R3, R2								; Check if more data is available in buffer
	ADD	R1, R1, 8											; If not, check if there is a second buffer
	LBBO	&R2, R1, 0, 8
	QBEQ	$run$oneChunk, R2, 0							; If no second buffer, start sending and wait until PRU1 finishes this only chunk

$run$start:
	LDI	R31, PRU0_PRU1_INTERRUPT + 16						; Start PRU1 and wait until it ends its operation

$run$0:
	WBS	R31, 30												; Wait until PRU1 has completely processed the last data block
	SBCO	&R0, C0, 0x24, 4
	LBBO	&R13, R2, 0, 64
	XOUT	10, &R13, 64
	ADD	R2, R2, 64
	QBLT	$run$0, R3, R2
	ADD	R1, R1, 8
	LBBO	&R2, R1, 0, 8
	QBNE	$run$0, R2, 0
	JMP	$run$exit

$run$oneChunk:
	LDI	R31, PRU0_PRU1_INTERRUPT + 16						
	WBS	R31,30												
	SBCO	&R0, C0, 0x24, 4
	
$run$exit:
	LDI	R31, 32 | (SYSEV_PRU0_TO_ARM_A - 16)				; Notify ARM that process is done
	XIN	11, &R0, 120										; Restore the original register values via scratchpad's bank 1
	LDI	R14, 0												; Return succesful operation
	JMP	R3.w2
