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

#ifndef HDD_H
#define HDD_H

#include <avr/io.h>

#ifdef HDD_ENABLED

/*
 * Simplistic hard drive emulator using a memory card as the data backend.
 * 
 * To use, call hdd_set_ready() when the memory card is activated. Whenever
 * the PHY targeting indicates the HDD is being accessed, call hdd_main(),
 * which will take over logical control of the bus to complete the operation.
 */

/*
 * Note: the following commands must be supported for direct-access devices:
 * 
 * FORMAT UNIT          (0x04)
 * INQUIRY              (0x12)
 * READ(6)              (0x08)
 * READ(10)             (0x28)
 * READ CAPACITY        (0x25)
 * RELEASE              (0x17)
 * REQUEST SENSE        (0x03)
 * RESERVE              (0x16)
 * SEND DIAGNOSTIC      (0x1D)
 * TEST UNIT READY      (0x00)
 * WRITE(6)             (0x0A)
 * WRITE(10)            (0x2A)
 */

/*
 * Called when the memory card has been detected and is ready to go. This
 * should be provided with the number of 512 byte blocks the card has been
 * detected as having.
 */
void hdd_set_ready(uint32_t);

/*
 * Provides whether or not the memory card has experienced a major error.
 * This is not for when the card is busy, but rather when the card outright
 * rejects a command when it really ought not to or does something else
 * ill-behaved. This may be used in the main loop to help indicate failure
 * to the user beyond just the debugging system.
 * 
 * Once set this is not unset until hdd_set_ready() is called again.
 */
uint8_t hdd_has_error(void);

/*
 * Called whenever the PHY detects that the hard drive has been selected. This
 * will proceed through the bus phases as needed.
 */
void hdd_main(void);

#endif /* HDD_ENABLED */

#endif /* HDD_H */
