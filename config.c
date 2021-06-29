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

#include <stdlib.h>
#include <string.h>
#include "lib/inih/ini.h"
#include "config.h"
#include "lib/ff/diskio.h"
#include "debug.h"

ENETConfig config_enet = { 255, 0, LINK_NONE, { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00} };
HDDConfig config_hdd[HARD_DRIVE_COUNT];

/*
 * INIH callback for configuration information.
 */
static int config_handler(
	void* user,
	const char* section,
	const char* name,
	const char* value)
{
	(void) user; // silence compiler

	if (strcmp(section, "scuznet") == 0)
	{
		if (strcmp(name, "debug") == 0)
		{
			if (strcmp(value, "yes") == 0)
			{
				GLOBAL_CONFIG_REGISTER |= GLOBAL_FLAG_DEBUG;
			}
			return 1;
		}
		else if (strcmp(name, "verbose") == 0)
		{
			if (strcmp(value, "yes") == 0)
			{
				GLOBAL_CONFIG_REGISTER |= GLOBAL_FLAG_VERBOSE;
			}
			return 1;
		}
		else if (strcmp(name, "parity") == 0)
		{
			if (strcmp(value, "yes") == 0)
			{
				GLOBAL_CONFIG_REGISTER |= GLOBAL_FLAG_PARITY;
			}
			return 1;
		}
		else
		{
			return 0;
		}
	}
	else if (strcmp(section, "ethernet") == 0)
	{
		if (strcmp(name, "id") == 0)
		{
			int i = atoi(value);
			if (i >= 0 && i <= 6)
			{
				config_enet.id = (uint8_t) i;
			}
			return 1;
		}
		else if (strcmp(name, "driver") == 0)
		{
			if (strcmp(value, "nuvo") == 0)
			{
				config_enet.type = LINK_NUVO;
				return 1;
			}
			else if (strcmp(value, "dayna") == 0)
			{
				config_enet.type = LINK_DAYNA;
				return 1;
			}
			else
			{
				return 0;
			}
		}
		else if (strcmp(name, "mac") == 0)
		{
			int v;
			char* copy = strdup(value);
			char* tok = strtok(copy, ":");
			for (uint8_t i = 0; i < 6 && tok != NULL; i++)
			{
				v = (int) strtol(tok, NULL, 16);
				if (v >= 0 && v <= 255)
				{
					config_enet.mac[i] = (uint8_t) v;
				}
				tok = strtok(NULL, ":");
			}
			free(copy);

			// disable the multicast bit if set
			config_enet.mac[0] &= ~1;

			return 1;
		}
		else
		{
			return 0;
		}
	}
	else if (strncmp(section, "hdd", 3) == 0) // starts with "hdd"?
	{
		uint8_t hddsel;
		switch (strlen(section))
		{
			case 3: // special case: "hdd" is really "hdd1"
				hddsel = 0;
				break;
			case 4: // "hddX"
				hddsel = section[3] - '1';
				break;
			default:
				hddsel = 255;
		}
		if (hddsel > HARD_DRIVE_COUNT) return 0;

		if (strcmp(name, "id") == 0)
		{
			int v = atoi(value);
			if (v >= 0 && v <= 6)
			{
				config_hdd[hddsel].id = (uint8_t) v;
			}
			return 1;
		}
		else if (strcmp(name, "file") == 0)
		{
			config_hdd[hddsel].filename = strdup(value);
			return 1;
		}
		else if (strcmp(name, "raw") == 0)
		{
			uint8_t rval = 0;
			char* copy = strdup(value);
			char* tok = strtok(copy, ":");
			if (tok != NULL)
			{
				uint32_t start = (uint32_t) atol(tok);
				tok = strtok(NULL, ":");
				if (tok != NULL)
				{
					uint32_t end = (uint32_t) atol(tok);
					if (end > start)
					{
						config_hdd[hddsel].start = start;
						config_hdd[hddsel].size = end - start;
						rval = 1;
					}
				}
			}
			free(copy);
			return rval;
		}
		else if (strcmp(name, "size") == 0)
		{
			// disallow if a direct-sector volume is present
			if (config_hdd[hddsel].start == 0)
			{
				config_hdd[hddsel].size = ((uint16_t) atoi(value));
			}
			return 1;
		}
		else if (strcmp(name, "mode") == 0)
		{
			if (strcmp(value, "fast") == 0)
			{
				config_hdd[hddsel].mode = HDD_MODE_FAST;
				return 1;
			}
			if (strcmp(value, "forcefast") == 0)
			{
				config_hdd[hddsel].mode = HDD_MODE_FORCEFAST;
				return 1;
			}
			else if (strcmp(value, "normal") == 0)
			{
				config_hdd[hddsel].mode = HDD_MODE_NORMAL;
				return 1;
			}
			else
			{
				return 0;
			}
		}
		else
		{
			return 0;
		}
	}
	else
	{
		return 0;
	}
}

/*
 * ============================================================================
 *  
 *   PUBLIC FUNCTIONS
 * 
 * ============================================================================
 */

CONFIG_RESULT config_read(uint8_t* target_masks)
{
	// initialize GPIO and hard drive structs
	GLOBAL_CONFIG_REGISTER = 0x00;
	for (uint8_t i = 0; i < HARD_DRIVE_COUNT; i++)
	{
		config_hdd[i].id = 255;
		config_hdd[i].filename = NULL;
		config_hdd[i].start = 0;
		config_hdd[i].size = 0;
		config_hdd[i].mode = HDD_MODE_NORMAL;
	}
	
	*target_masks = 0;
	CONFIG_RESULT result = CONFIG_OK;

	// open the file off the memory card
	FIL fil;
	FRESULT res = f_open(&fil, "SCUZNET.INI", FA_READ);
	if (res)
	{
		debug(DEBUG_CONFIG_FILE_MISSING);
		f_close(&fil);
		return CONFIG_NOFILE;
	}

	// execute INIH parse using FatFs f_gets()
	if (ini_parse_stream((ini_reader) f_gets, &fil, config_handler, NULL) < 0)
	{
		debug(DEBUG_CONFIG_FILE_MISSING);
		result = CONFIG_NOLOAD;
	}
	f_close(&fil);

	/*
	 * Calculate the PHY masks requested from the configuration file, and
	 * disable hard drives with invalid values.
	 */
	uint8_t used_masks = 0x80; // reserve ID 7 for initiator
	if (config_enet.id < 7 && config_enet.type != LINK_NONE)
	{
		config_enet.mask = 1 << config_enet.id;
		used_masks |= config_enet.mask;
	}
	else
	{
		config_enet.id = 255;
		config_enet.mask = 0;
	}
	for (uint8_t i = 0; i < HARD_DRIVE_COUNT; i++)
	{
		if (config_hdd[i].id < 7
				&& (config_hdd[i].filename != NULL
					|| config_hdd[i].start > 0))
		{
			config_hdd[i].mask = 1 << config_hdd[i].id;
			if (! (config_hdd[i].mask & used_masks))
			{
				// mask is free
				used_masks |= config_hdd[i].mask;
			}
			else
			{
				// collision with another device, disable
				config_hdd[i].mask = 0;
				config_hdd[i].id = 255;
			}
		}
		else
		{
			config_hdd[i].mask = 0;
			config_hdd[i].id = 255;
		}
	}
	*target_masks = used_masks & 0x7F;

	return result;
}
