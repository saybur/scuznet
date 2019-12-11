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

#include <avr/eeprom.h>
#include "config.h"
#include "debug.h"

void config_read(uint8_t* data)
{
	// perform read of data into given array
	eeprom_read_block((void*) data,
			(const void*) CONFIG_EEPROM_ADDR,
			CONFIG_EEPROM_LENGTH);

	// verify information contained is valid, or force-set defaults
	if (data[CONFIG_OFFSET_VALIDITY] == CONFIG_EEPROM_VALIDITY)
	{
		debug(DEBUG_CONFIG_FOUND);
		// data is at least theoretically OK, sanity check some items

		// check if device IDS are between 0 and 6
		if (data[CONFIG_OFFSET_ID_HDD] > 6)
		{
			data[CONFIG_OFFSET_ID_HDD] = DEVICE_ID_HDD;
		}
		if (data[CONFIG_OFFSET_ID_LINK] > 6)
		{
			data[CONFIG_OFFSET_ID_LINK] = DEVICE_ID_LINK;
		}

		// check that the device IDs are not colliding
		if (data[CONFIG_OFFSET_ID_HDD] == data[CONFIG_OFFSET_ID_LINK])
		{
			data[CONFIG_OFFSET_ID_HDD] = DEVICE_ID_HDD;
			data[CONFIG_OFFSET_ID_LINK] = DEVICE_ID_LINK;
		}

		// verify that MAC MSB has b0 cleared to avoid being multicast
		data[CONFIG_OFFSET_MAC] &= ~_BV(0);
	}
	else
	{
		debug(DEBUG_CONFIG_NOT_FOUND);
		// EEPROM data is not set, we must handle everything ourselves
		data[CONFIG_OFFSET_FLAGS] = GLOBAL_CONFIG_DEFAULTS;
		data[CONFIG_OFFSET_ID_HDD] = DEVICE_ID_HDD;
		data[CONFIG_OFFSET_ID_LINK] = DEVICE_ID_LINK;
		data[CONFIG_OFFSET_MAC] = NET_MAC_DEFAULT_ADDR_1;
		data[CONFIG_OFFSET_MAC + 1] = NET_MAC_DEFAULT_ADDR_2;
		data[CONFIG_OFFSET_MAC + 2] = NET_MAC_DEFAULT_ADDR_3;
		data[CONFIG_OFFSET_MAC + 3] = NET_MAC_DEFAULT_ADDR_4;
		data[CONFIG_OFFSET_MAC + 4] = NET_MAC_DEFAULT_ADDR_5;
		data[CONFIG_OFFSET_MAC + 5] = NET_MAC_DEFAULT_ADDR_6;
	}
}
