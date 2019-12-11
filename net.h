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
 * Helper functions for manipulating the Ethernet device. This abstract out
 * some of the more common operations, though calling the functions in enc.c
 * will still be needed to use the chip.
 */

/*
 * Defines the high byte value for end of the region where the receive buffer
 * is, starting at 0x000 and extending through 0xXXFF, where 0xXX is this
 * value. All remaining space is allocated to the transmit buffer.
 */
#define NET_ERXNDH_VALUE    0x13

/*
 * Defines the starting point where packets to be transmitted are stored.
 * There are two regions, each 1536 bytes in size reserved for this purpose.
 * They can be switched between in the transmit functions based on the provided
 * buffer selection value.
 */
#define NET_XMIT_BUF1       0x14
#define NET_XMIT_BUF2       0x1A

/*
 * Defines the array offsets within received data where the various data
 * structures are supposed to live, indexed from zero.
 */
#define NET_HEAD_RXPTL      0
#define NET_HEAD_RXPTH      1
#define NET_HEAD_RXLENL     2
#define NET_HEAD_RXLENH     3
#define NET_HEAD_STATL      4
#define NET_HEAD_STATH      5

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
} NetHeader;

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
 * Fills the given NetHeader with the information contained in the given set
 * of bytes read from the controller chip. This will read the first 6 bytes
 * to generate the header information.
 * 
 * This uses the byte offsets declared earlier. Since the first byte read
 * via enc_data_read() is often invalid, be careful about the pointer passed
 * into this function or invalid data may be stored.
 */
void net_process_header(uint8_t*, NetHeader*);

/*
 * Moves the read pointer(s) to the given location, with adjustments as
 * needed for the device per errata.
 * 
 * This should always be called after packet reading is finished to set buffer
 * pointers to the correct location. This should be given the address of the
 * next read pointer position that was stored in the receive status bytes of
 * the previous packet.
 * 
 * To be particular, this will set ERXRDPT to be one less than the given value,
 * per errata 14.  This will optionally set ERDPT to the given value if the
 * second parameter is true, for use in instances when that pointer was not
 * already incremented to the correct location (such as when only part of a
 * packet was read into local memory).
 */
void net_move_rxpt(uint16_t, uint8_t);

/*
 * Moves the write pointer into the correct location to write data. The
 * parameter chooses the transmission buffer to select. This should be invoked
 * prior to calling net_tx_write().
 */
void net_move_txpt(uint8_t);

/*
 * Instructs the device to transmit the packet in the given buffer of the given
 * length. Before invoking this, be sure that the data in the packet buffer is
 * ready to be sent, and that the device isn't currently trying to transmit a
 * packet.
 */
void net_transmit(uint8_t, uint16_t);

#endif /* NET_H */
