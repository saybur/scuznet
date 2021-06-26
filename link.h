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

#ifndef LINK_H
#define LINK_H

#include <avr/io.h>

/*
 * Nuvolink emulator using the ENC28J60 and associated peripherals to support
 * network connections.
 * 
 * To use, call link_init() during startup. The main loop should call
 * link_check_rx() frequently to check if there are pending packets when not
 * busy with other work. Whenever the PHY targeting indicates the link device
 * is being accessed, either due to SELECTION or RESELECTION, call link_main(),
 * which will take over logical control of the bus to complete the operation.
 */

/*
 * The Ethernet device must support the following commands:
 * 
 * TEST UNIT READY      (0x00)
 * "Reset Stats"        (0x02)
 * REQUEST SENSE        (0x03)
 * "Send Packet"        (0x05)
 * "Change MAC"         (0x06)
 * GET MESSAGE(6)       (0x08)
 * "Set Filter"         (0x09)
 * SEND MESSAGE(6)      (0x0A)
 * "Medium Sense"       (0x0C)
 * INQUIRY              (0x12)
 * RECIEVE DIAGNOSTIC   (0x1C)
 * SEND DIAGNOSTIC      (0x1D)
 */

/*
 * Defines the supported link emulation types.
 */
typedef enum {
	LINK_NONE = 0,
	LINK_NUVO,
	LINK_DAYNA
} LINKTYPE;

/*
 * Initializes the emulated link device. This should be given a six-byte array
 * with the MAC address, as well as a mask with only 1 bit set for the target
 * that this device will obey. This function should only be called once at
 * startup.
 */
void link_init(uint8_t*, uint8_t);

/*
 * Checks the network device for pending packets.
 * 
 * If there is a pending packet, behavior depends on whether or not the
 * initiator has indicated that we are allowed to send packets.  If we are,
 * this checks if we are already requesting a reconnection, and if we are,
 * this just returns, under the assumption that the main handler will get
 * reconnected eventually. If there is not a pending reconnection, this
 * queues one and returns.
 * 
 * If the initiator is not allowing us to send packets, then we drop the
 * packet to free space in the RX buffer.
 */
void link_check_rx(void);

/*
 * Called whenever the PHY detects that the link device has been selected, or
 * has managed to make a reconnection to the initiator. This will proceed
 * through the bus phases as needed.
 */
void link_main(void);

#endif /* LINK_H */
