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
 * Declares the format of the net_header struct, which is just a copy of the
 * ENC28J60 packet header. See the datasheet for what individual components
 * mean.
 * 
 * It is critical that these values not be modified by client code!
 */
typedef struct NetHeader_t {
	uint16_t next_packet;
	uint16_t length;
	uint8_t statl;
	uint8_t stath;
} NetHeader;

// if net_pending() is true, this contains the information about the packet
extern volatile NetHeader net_header;

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
	NETSTAT_OK = 0,
	NETSTAT_TRUNCATED,      // a call did not transfer all available bytes
	NETSTAT_NO_DATA         // no data are available
} NETSTAT;

/*
 * Flags within the NET_STATUS GPIOR
 * 
 * NETFLAG_PKT_PENDING: set if there is a pending packet to be read
 * NETFLAG_TXBUF: switched back and forth to support TX double-buffering
 * NETFLAG_TXUSED: set once the first packet has been sent
 */
#define NETFLAG_PKT_PENDING     _BV(1)
#define NETFLAG_TXBUF           _BV(2)
#define NETFLAG_TXREQ           _BV(3)

/*
 * If nonzero, there is a network packet pending and the values in net_header
 * are valid.
 */
#define net_pending()       (NET_FLAGS & NETFLAG_PKT_PENDING)

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
 * 
 * The provided function should return the number of bytes that *were not*
 * sent through properly: returning zero indicates success.
 * 
 * This will return NETSTAT_OK if all bytes were sent, and NETSTAT_TRUNCATED
 * if not all bytes were sent. The pending packet will be discarded in either
 * case.
 */ 
NETSTAT net_stream_read(uint16_t (*func)(USART_t*, uint16_t));

/*
 * Performs a buffer write, streaming data from the given function into the
 * Ethernet controller's free buffer. This does not actually transmit a packet:
 * for that, see net_transmit().
 * 
 * When invoked, this will begin a write operation, write the status byte,
 * then call the provided function with the given number of bytes that need
 * to be provided into the ENC28J60 USART.
 */
NETSTAT net_stream_write(void (*func)(USART_t*, uint16_t), uint16_t length);

/*
 * Transmits the packet in the current free buffer. Should be provided with the
 * length of the data to transmit.
 */
NETSTAT net_transmit(uint16_t length);

/*
 * Checks on the status of pending transactions. This should be called
 * intermittently in between normal transmissions to prevent the TX subsystem
 * from stalling.
 */
NETSTAT net_transmit_check(void);

#endif /* NET_H */
