/*
 * Copyright (c) 2006, Technical University of Munich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 * @(#)$$
 */

/**
 * \file
 *         Contiki 2.4 kernel for Jackdaw USB stick
 *
 * \author
 *         Simon Barner <barner@in.tum.de>
 *         David Kopf <dak664@embarqmail.com>
 */

#include <avr/pgmspace.h>
#include <avr/fuse.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>

#if RF230BB           //radio driver using contiki core mac
#include "radio/rf230bb/rf230bb.h"
#include "net/mac/frame802154.h"
#include "net/sicslowpan.h"
#include "net/uip-netif.h"
#include "net/mac/lpp.h"
#include "net/mac/sicslowmac.h"
#include "net/mac/cxmac.h"
#else                 //radio driver using Atmel/Cisco 802.15.4'ish MAC
#include <stdbool.h>
#include "mac.h"
#include "sicslowmac.h"
#include "sicslowpan.h"
#include "ieee-15-4-manager.h"
#endif /*RF230BB*/

#include "lib/mmem.h"
#include "loader/symbols-def.h"
#include "loader/symtab.h"

#include "contiki.h"
#include "contiki-net.h"
#include "contiki-lib.h"
#include "contiki-raven.h"

#include "usb_task.h"
#include "serial/cdc_task.h"
#include "rndis/rndis_task.h"
#include "storage/storage_task.h"

#if RF230BB
#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
extern int rf230_interrupt_flag;
extern uint8_t rf230processflag;
rimeaddr_t addr,macLongAddr;
#endif /* RF230BB */

/* Set ANNOUNCE to send boot messages to USB serial port */
#define ANNOUNCE 1

/* Test rtimers, also useful for pings and time stamps in simulator */
#define TESTRTIMER 0
#if TESTRTIMER
#define PINGS 0
#define STAMPS 30
uint8_t rtimerflag=1;
uint16_t rtime;
struct rtimer rt;
void rtimercycle(void) {rtimerflag=1;}
#endif /* TESTRTIMER */

/*-------------------------------------------------------------------------*/
/*----------------------Configuration of the .elf file---------------------*/
typedef struct {unsigned char B2;unsigned char B1;unsigned char B0;} __signature_t;
#define SIGNATURE __signature_t __signature __attribute__((section (".signature")))
SIGNATURE = {
/* Older AVR-GCCs may not define the SIGNATURE_n bytes so use explicit values */
  .B2 = 0x82,//SIGNATURE_2, //AT90USB128x
  .B1 = 0x97,//SIGNATURE_1, //128KB flash
  .B0 = 0x1E,//SIGNATURE_0, //Atmel
};
FUSES ={.low = 0xde, .high = 0x99, .extended = 0xff,};

/* Put default MAC address in EEPROM */
uint8_t mac_address[8] EEMEM = {0x02, 0x12, 0x13, 0xff, 0xfe, 0x14, 0x15, 0x16};
//uint8_t EEMEM mac_address[8];     //The raven webserver uses this EEMEM allocation
//uint8_t EEMEM server_name[16];
//uint8_t EEMEM domain_name[30];

/*-------------------------------------------------------------------------*/
/*-----------------------------Low level initialization--------------------*/
static void initialize(void) {

  /* Initialize hardware */
  init_lowlevel();
  
  /* Clock */
  clock_init();

/* rtimer init needed for low power protocols */
  rtimer_init();

  /* Process subsystem. */
  process_init();

  /* etimer process must be started before ctimer init */
  process_start(&etimer_process, NULL);
  
#if RF230BB
{
  ctimer_init();
  /* Start radio and radio receive process */
  /* Note this starts RF230 process, so must be done after process_init */
  NETSTACK_RADIO.init();

  /* Set addresses BEFORE starting tcpip process */

  memset(&addr, 0, sizeof(rimeaddr_t));
  AVR_ENTER_CRITICAL_REGION();
  eeprom_read_block ((void *)&addr.u8,  &mac_address, 8);
  AVR_LEAVE_CRITICAL_REGION();
  //RNDIS needs the mac address in reverse byte order
  macLongAddr.u8[0]=addr.u8[7];
  macLongAddr.u8[1]=addr.u8[6];
  macLongAddr.u8[2]=addr.u8[5];
  macLongAddr.u8[3]=addr.u8[4];
  macLongAddr.u8[4]=addr.u8[3];
  macLongAddr.u8[5]=addr.u8[2];
  macLongAddr.u8[6]=addr.u8[1];
  macLongAddr.u8[7]=addr.u8[0];
 
  memcpy(&uip_lladdr.addr, &addr.u8, 8);
  rf230_set_pan_addr(IEEE802154_PANID, 0, (uint8_t *)&addr.u8);
  rf230_set_channel(24);

  rimeaddr_set_node_addr(&addr); 
//  set_rime_addr();

  /* Initialize stack protocols */
  queuebuf_init();
  NETSTACK_RDC.init();
  NETSTACK_MAC.init();
  NETSTACK_NETWORK.init();

#if UIP_CONF_ROUTER
  rime_init(rime_udp_init(NULL));
  uip_router_register(&rimeroute);
#endif
}

#else
/* The order of starting these is important! */
  process_start(&mac_process, NULL);
  process_start(&tcpip_process, NULL);

  #endif /* RF230BB */

  /* Setup USB */
  process_start(&usb_process, NULL);
  process_start(&cdc_process, NULL);
  process_start(&rndis_process, NULL);
  process_start(&storage_process, NULL);

  //Fix MAC address
  init_net();
}

/*-------------------------------------------------------------------------*/
/*---------------------------------Main Routine----------------------------*/
int
main(void)
{
#if ANNOUNCE
  uint16_t firsttime=1;
#endif
  /* GCC depends on register r1 set to 0 (?) */
  asm volatile ("clr r1");
  
  /* Initialize in a subroutine to maximize stack space */
  initialize();

  while(1) {
    process_run();

#if TESTRTIMER
    if (rtimerflag) {  //8 seconds is maximum interval, my jackdaw 4% slow
      rtimer_set(&rt, RTIMER_NOW()+ RTIMER_ARCH_SECOND*1UL, 1,(void *) rtimercycle, NULL);
      rtimerflag=0;
#if STAMPS
      if ((rtime%STAMPS)==0) {
        printf("%us ",rtime);
      }
#endif
      rtime+=1;
#if PINGS
      if ((rtime%PINGS)==0) {
        PRINTF("**Ping\n");
        pingsomebody();
      }
#endif
    }
#endif /* TESTRTIMER */

//Use with RF230BB DEBUGFLOW to show path through driver
//Warning, Jackdaw doesn't handle simultaneous radio and USB interrupts very well.
#if RF230BB&&0
extern uint8_t debugflowsize,debugflow[];
  if (debugflowsize) {
    debugflow[debugflowsize]=0;
    printf("%s",debugflow);
    debugflowsize=0;
   }
#endif

  /* Allow USB CDC to keep up with printfs */
#if ANNOUNCE
  if (firsttime) {
    firsttime++;
    if (firsttime==36000){
       printf_P(PSTR("\n\n\n********BOOTING CONTIKI*********\n\r"));
#if RF230BB
    } else if (firsttime==44000) {
       printf_P(PSTR("MAC address %x:%x:%x:%x:%x:%x:%x:%x\n\r"),addr.u8[0],addr.u8[1],addr.u8[2],addr.u8[3],addr.u8[4],addr.u8[5],addr.u8[6],addr.u8[7]);

    } else if (firsttime==52000) {
       printf_P(PSTR("%s %s, channel %u"),NETSTACK_MAC.name, NETSTACK_RDC.name,rf230_get_channel());
       if (NETSTACK_RDC.channel_check_interval) {//function pointer is zero for sicslowmac
         unsigned short tmp;
         tmp=CLOCK_SECOND / (NETSTACK_RDC.channel_check_interval == 0 ? 1:\
                                   NETSTACK_RDC.channel_check_interval());
         if (tmp<65535) printf_P(PSTR(", check rate %u Hz"),tmp);
       }
       printf_P(PSTR("\n\r"));
#endif /* RF230BB */
    } else if (firsttime==60000) {
      printf_P(PSTR("System online.\n\r"));
    }
  }
#endif /* ANNOUNCE */

#if RF230BB&&0
  if (rf230processflag) {
    printf("**RF230 process flag %u\n\r",rf230processflag);
    rf230processflag=0;
  }
  if (rf230_interrupt_flag) {
//  if (rf230_interrupt_flag!=11) {
      printf("**RF230 Interrupt %u\n\r",rf230_interrupt_flag);
 // }
    rf230_interrupt_flag=0;
  }
#endif
  }

  return 0;
}
