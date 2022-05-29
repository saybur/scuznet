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
 * Possible return types for hdd_state().
 */
typedef enum {
	HDD_OK = 0,
	HDD_NOINIT,
	HDD_ERROR
} HDDSTATE;

/*
 * Called when the files on the memory card have been mounted and are ready for
 * use.
 * 
 * This will return 0 on success. If non-zero, the hard drive image number that
 * caused the failure will be in the upper eight bits, and the specific error
 * will be in the lower eight bits.
 */
uint16_t hdd_init(void);

/*
 * Checks for volume continuity among those marked for fast mode. This needs to
 * be called as part of the main loop. Each invocation, it will perform one
 * step of the check, until eventually it completes all checks, after which it
 * will begin returning immediately.
 * 
 * Should not be invoked until hdd_init() returns correctly.
 */
void hdd_contiguous_check(void);

/*
 * Provides the current state of the hard drive subsystem.
 */
HDDSTATE hdd_state(void);

/*
 * Called whenever the PHY detects that the hard drive has been selected. This
 * will proceed through the bus phases as needed.
 * 
 * Should be provided with the ID of the hard drive from the HDD configuration
 * array.
 * 
 * If this returns false, it indicates an error that did not result in hanging
 * up the bus. The caller needs to resolve that condition.
 */
uint8_t hdd_main(uint8_t);

#endif /* HDD_H */
