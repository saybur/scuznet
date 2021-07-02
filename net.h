/*
 * Copyright (C) 2019 saybur
 * 
 * This file is part of scuznet.
 * 
 * scuznet is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * scuznet is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with scuznet.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef NET_H
#define NET_H

#include <avr/io.h>

/*
 * Defines the return format of information from the header processor
 * function. These are simply copied from the array 
 * 
 * -> The next packet value is the pointer into the receive buffer
 *    memory where the next packet lives, which is always valid.
 * -> The destination is the least-significant MAC byte, which
 *    indicates the LLAP target. This is zero if the remainder of the
 *    packet should be ignored.
 * -> The length is the *valid length* of the packet, as indicated by
 *    the packet itself (and checked against received data)
 * -> The source is the MAC of the source.
 * 
 * See the header processing code for more details.
 */
typedef struct NetHeader_t {
	uint16_t next_packet;
	uint16_t length;
	uint8_t statl;
	uint8_t stath;
	uint8_t dest[6];
	uint16_t packet_data;
} NetHeader;

/*
 * Options for providing to net_set_filter.
 */
typedef enum {
	NET_FILTER_UNICAST,     // unicast only
	NET_FILTER_BROADCAST,   // unicast, plus broadcast
	NET_FILTER_MULTICAST    // unicast, broadcast, and multicast
} NETFILTER;

/*
 * Return values for the functions.
 */
typedef enum {
	NET_OK = 0,
	NET_LOCKED,             // Ethernet subsystem locked in other context
	NET_BUSY,               // device is temporarily unable to accept request
	NET_NO_DATA             // no data are available
} NETSTAT;

/*
 * Provides the current number of packets in the headers array.
 */
#define net_size()          NET_PACKET_SIZE

/*
 * Initalizes the Ethernet controller by writing appropriate values to its
 * registers. This should be done immediately after a controller reset to
 * restore normal function. Before calling this, MCU peripherals must be
 * correctly configured (i.e. enc_init() must have been called).
 * 
 * This needs to be given the MAC address to configure as the built-in ROM
 * address, in LSB to MSB order.
 */
void net_setup(uint8_t*);

/*
 * These calls manage access for the net subsystem.
 * 
 * Whenever the network chip gets a packet, interrupts handle reading
 * components of the data. When this is happening, client code cannot use the
 * chip. To manage access, whenever a user needs to read or write a packet,
 * net_start() should be called to stop the periodic packet reception code
 * until net_end() is called to release the subsystem.
 * 
 * net_start() will block until the networking subsystem is ready to be used.
 */
NETSTAT net_start(void);
NETSTAT net_end(void);

/*
 * Provides the current packet of interest in the given pointer, or NULL if
 * there is none.
 */
NETSTAT net_get(volatile NetHeader* header);

/*
 * Updates the filtering system to match packets of the given type.
 */
NETSTAT net_set_filter(NETFILTER ftype);

/*
 * Skips past the current packet, adjusting pointers as needed.
 */
NETSTAT net_skip(void);

/*
 * Performs a read action, streaming packet data into the given function from
 * the Ethernet controller.
 * 
 * When invoked, this will start a read operation against the network packet
 * pointed to by NET_PACKET_PTR, then call the given function with the number
 * of bytes that should be read from the given USART. Once that returns, this
 * will move the read pointers past the packet.
 */ 
NETSTAT net_stream_read(void (*func)(USART_t*, uint16_t));

/*
 * Performs a write action, streaming data from the given function into the
 * Ethernet controller.
 * 
 * When invoked, this will begin a write operation, write the status byte,
 * then call the provided function with the given number of bytes that need
 * to be provided into the ENC28J60 USART. Once that function returns, the
 * packet will be finalized and queued for transmission.
 */
NETSTAT net_stream_write(void (*func)(USART_t*, uint16_t), uint16_t length);

/*
 * Checks if the device is ready to accept a packet of the given size. This
 * will provide NET_OK if ready, and NET_BUSY if not.
 */
NETSTAT net_write_ready(uint16_t size);

#endif /* NET_H */
