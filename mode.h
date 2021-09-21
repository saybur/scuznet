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

#ifndef MODE_H
#define MODE_H

#include <avr/io.h>

/*
 * Defines different types of devices for responding to MODE SENSE.
 */
typedef enum {
	MODE_TYPE_HDD,
	MODE_TYPE_CDROM
} MODE_DEVICE_TYPE;

/**
 * Updates a four-byte capacity array, used in MODE SENSE and READ CAPACITY,
 * set according to the disk parameters reported by the mode sense handler
 * below.
 * 
 * This effectively performs a modulo-2MB operation on the real volume
 * capacity.
 */
void mode_update_capacity(uint32_t size, uint8_t* arr);

/*
 * Responds to a MODE SENSE command. This will provide the information for the
 * specified type of device and release the bus when done.
 */
void mode_sense(uint8_t* cmd, MODE_DEVICE_TYPE device_type, uint32_t size);

/*
 * Responds to a MODE SELECT command. This will accept data from the initiator
 * and release the bus when done.
 * 
 * This does nothing with the information provided. None of the implemented
 * devices support being changed in this fashion.
 */
void mode_select(uint8_t* cmd);

#endif
