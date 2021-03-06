/*
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/ 
 *  
 *  
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 * 
 * 	* Redistributions of source code must retain the above copyright 
 * 	  notice, this list of conditions and the following disclaimer.
 * 
 * 	* Redistributions in binary form must reproduce the above copyright
 * 	  notice, this list of conditions and the following disclaimer in the 
 * 	  documentation and/or other materials provided with the   
 * 	  distribution.
 * 
 * 	* Neither the name of Texas Instruments Incorporated nor the names of
 * 	  its contributors may be used to endorse or promote products derived
 * 	  from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>
#include <pru_cfg.h>
#include <rsc_types.h>
#include <pru_virtqueue.h>
#include <pru_rpmsg.h>
#include <sys_mailbox.h>
#include "resource_table_1.h"

/* The mailboxes used for RPMsg are defined in the Linux device tree
 * PRU0 uses mailboxes 0 (From ARM) and 1 (To ARM)
 * PRU1 uses mailboxes 2 (From ARM) and 3 (To ARM)
 */
#define MB_TO_ARM_HOST					3
#define MB_FROM_ARM_HOST				2

/*
 * Using the name 'rpmsg-pru' will probe the rpmsg_pru driver found
 * at linux-x.y.z/drivers/rpmsg/rpmsg_pru.c
 */
#define CHAN_NAME						"rpmsg-pru"
#define CHAN_DESC						"Channel 31"
#define CHAN_PORT						31

/*
 * Need to program the crossbar to bring the correct events into the
 * PRUSS INTC
 */
#define CTRL_CORE_PRUSS1_IRQ_58_59		*(volatile unsigned int *) 0x4A0028FC
#define MAILBOX3_IRQ_USER2				243

/* 
 * Used to make sure the Linux drivers are ready for RPMsg communication
 * Found at linux-x.y.z/include/uapi/linux/virtio_config.h
 */
#define VIRTIO_CONFIG_S_DRIVER_OK		4

uint8_t payload[RPMSG_BUF_SIZE];

/*
 * main.c
 */
void main() {
	struct pru_rpmsg_transport transport;
	uint16_t src, dst, len;
	uint32_t regValue;
	volatile uint8_t *status;

	/* allow OCP master port access by the PRU so the PRU can read external memories */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	/* need to program the CROSSBAR to map MBX3 User2 event to PRUSS INTC event 59 */
	regValue = CTRL_CORE_PRUSS1_IRQ_58_59;
	regValue &= 0xFE00FFFF;
	CTRL_CORE_PRUSS1_IRQ_58_59 = regValue | (MAILBOX3_IRQ_USER2 << 16);

	/* Make sure the Linux drivers are ready for RPMsg communication */
	status = &resourceTable.rpmsg_vdev.status;
	while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK));

	/* Initialize pru_virtqueue corresponding to vring0 (PRU to ARM Host direction) */
	pru_virtqueue_init(&transport.virtqueue0, &resourceTable.rpmsg_vring0, &MBX3.MESSAGE[MB_TO_ARM_HOST], &MBX3.MESSAGE[MB_FROM_ARM_HOST]);

	/* Initialize pru_virtqueue corresponding to vring1 (ARM Host to PRU direction) */
	pru_virtqueue_init(&transport.virtqueue1, &resourceTable.rpmsg_vring1, &MBX3.MESSAGE[MB_TO_ARM_HOST], &MBX3.MESSAGE[MB_FROM_ARM_HOST]);

	/* Create the RPMsg channel between the PRU and ARM user space using the transport structure.
	 * The name 'rpmsg-pru' corresponds to the rpmsg_pru driver found
	 * at linux-x.y.z/drivers/rpmsg/rpmsg_pru.c
	 */
	while(pru_rpmsg_channel(RPMSG_NS_CREATE, &transport, CHAN_NAME, CHAN_DESC, CHAN_PORT) != PRU_RPMSG_SUCCESS);
	while(1){
		if(MBX3.MESSAGE[MB_FROM_ARM_HOST] == 1){
			/* Receive the message */
			if(pru_rpmsg_receive(&transport, &src, &dst, payload, &len) == PRU_RPMSG_SUCCESS){
				/* Echo the message back to the same address from which we just received */
				pru_rpmsg_send(&transport, dst, src, payload, len);
			}
		}
	}
}
