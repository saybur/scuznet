/*
 * Copyright (C) 2019-2021 saybur
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

#ifndef CDROM_H
#define CDROM_H

#include <avr/io.h>

/*
 * Simplistic CD-ROM drive emulator using a memory card as the data
 * backend.
 * 
 * This subsystem is an add-on to the hard drive emulator, which must be
 * initialized and ready before this one is used.
 */

/*
 * Note: the following commands should be supported for CD-ROM devices:
 * 
 * INQUIRY              (0x12)
 * READ(10)             (0x28)
 * READ CAPACITY        (0x25) [the CD-ROM variant]
 * READ HEADER          (0x44)
 * READ TOC             (0x43)
 * RELEASE              (0x17)
 * REQUEST SENSE        (0x03)
 * RESERVE              (0x16)
 * SEND DIAGNOSTIC      (0x1D)
 * TEST UNIT READY      (0x00)
 */

/*
 * Called whenever the PHY detects that a hard drive with the HDD_MODE_CDROM
 * option has been selected. This will proceed through the bus phases as
 * needed.
 * 
 * Should be provided with the ID of the "hard drive" from the HDD
 * configuration array.
 * 
 * If this returns false, it indicates an error that did not result in hanging
 * up the bus. The caller needs to resolve that condition.
 */
uint8_t cdrom_main(uint8_t);

#endif /* CDROM_H */
