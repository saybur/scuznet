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

/*
 * The configuration keys and values we check for, in flash to save precious
 * SRAM for other uses.
 */
static const __flash char str_dayna[] =     "dayna";
static const __flash char str_debug[] =     "debug";
static const __flash char str_driver[] =    "driver";
static const __flash char str_ethernet[] =  "ethernet";
static const __flash char str_fast[] =      "fast";
static const __flash char str_file[] =      "file";
static const __flash char str_forcefast[] = "forcefast";
static const __flash char str_hdd[] =       "hdd";
static const __flash char str_id[] =        "id";
static const __flash char str_mac[] =       "mac";
static const __flash char str_mode[] =      "mode";
static const __flash char str_normal[] =    "normal";
static const __flash char str_nuvo[] =      "nuvo";
static const __flash char str_parity[] =    "parity";
static const __flash char str_scuznet[] =   "scuznet";
static const __flash char str_selftest[] =  "selftest";
static const __flash char str_size[] =      "size";
static const __flash char str_verbose[] =   "verbose";
static const __flash char str_yes[] =       "yes";

ENETConfig config_enet = { 255, 0, LINK_NONE, { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00} };
HDDConfig config_hdd[HARD_DRIVE_COUNT];
uint8_t global_buffer[GLOBAL_BUFFER_SIZE];

/*
 * String equality checks where one string is in SRAM and the other is in
 * flash. These should hopefully operate similarly to strcmp and strncmp,
 * apart from only checking equality.
 */
static uint8_t strequ(const char* a, const __flash char* b)
{
	while (*a != '\0' && *a == *b)
	{
		a++; b++;
	}
	return *a == *b;
}
static uint8_t strnequ(const char* a, const __flash char* b, uint8_t l)
{
	while (*a != '\0' && *a == *b && l)
	{
		a++; b++; --l;
	}
	if (! l) return 1;
	return *a == *b;
}

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

	if (strequ(section, str_scuznet))
	{
		if (strequ(name, str_debug))
		{
			if (strequ(value, str_yes))
			{
				GLOBAL_CONFIG_REGISTER |= GLOBAL_FLAG_DEBUG;
			}
			return 1;
		}
		else if (strequ(name, str_verbose))
		{
			if (strequ(value, str_yes))
			{
				GLOBAL_CONFIG_REGISTER |= GLOBAL_FLAG_VERBOSE;
			}
			return 1;
		}
		else if (strequ(name, str_parity))
		{
			if (strequ(value, str_yes))
			{
				GLOBAL_CONFIG_REGISTER |= GLOBAL_FLAG_PARITY;
			}
			return 1;
		}
		else if (strequ(name, str_selftest))
		{
			if (strequ(value, str_yes))
			{
				GLOBAL_CONFIG_REGISTER |= GLOBAL_FLAG_SELFTEST;
			}
			return 1;
		}
		else
		{
			return 0;
		}
	}
	else if (strequ(section, str_ethernet))
	{
		if (strequ(name, str_id))
		{
			int i = atoi(value);
			if (i >= 0 && i <= 6)
			{
				config_enet.id = (uint8_t) i;
			}
			return 1;
		}
		else if (strequ(name, str_driver))
		{
			if (strequ(value, str_nuvo))
			{
				config_enet.type = LINK_NUVO;
				return 1;
			}
			else if (strequ(value, str_dayna))
			{
				config_enet.type = LINK_DAYNA;
				return 1;
			}
			else
			{
				return 0;
			}
		}
		else if (strequ(name, str_mac))
		{
			// must be in XX:XX:XX:XX:XX:XX format
			if (strlen(value) != 17) return 0;

			int v;
			char macbuf[3];
			macbuf[2] = '\0';
			for (uint8_t i = 0; i < 6; i++)
			{
				macbuf[0] = value[i * 3];
				macbuf[1] = value[i * 3 + 1];
				v = (int) strtol(macbuf, NULL, 16);
				if (v >= 0 && v <= 255)
				{
					config_enet.mac[i] = (uint8_t) v;
				}
			}

			// disable the multicast bit if set
			config_enet.mac[0] &= ~1;

			return 1;
		}
		else
		{
			return 0;
		}
	}
	else if (strnequ(section, str_hdd, 3)) // starts with "hdd"?
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

		if (strequ(name, str_id))
		{
			int v = atoi(value);
			if (v >= 0 && v <= 6)
			{
				config_hdd[hddsel].id = (uint8_t) v;
			}
			return 1;
		}
		else if (strequ(name, str_file))
		{
			if (strlen(value) < HDD_FILENAME_SIZE)
			{
				strncpy(config_hdd[hddsel].filename, value,
					sizeof(config_hdd[hddsel].filename));
				return 1;
			}
			else
			{
				return 0;
			}
		}
		else if (strequ(name, str_size))
		{
			config_hdd[hddsel].size = ((uint16_t) atoi(value));
			return 1;
		}
		else if (strequ(name, str_mode))
		{
			if (strequ(value, str_fast))
			{
				config_hdd[hddsel].mode = HDD_MODE_FAST;
				return 1;
			}
			if (strequ(value, str_forcefast))
			{
				config_hdd[hddsel].mode = HDD_MODE_FORCEFAST;
				return 1;
			}
			else if (strequ(value, str_normal))
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

void config_read(uint8_t* target_masks)
{
	*target_masks = 0;

	// initialize GPIO and hard drive structs
	GLOBAL_CONFIG_REGISTER = 0x00;
	for (uint8_t i = 0; i < HARD_DRIVE_COUNT; i++)
	{
		config_hdd[i].id = 255;
		config_hdd[i].lba = 0;
		config_hdd[i].filename[0] = '\0';
		config_hdd[i].size = 0;
		config_hdd[i].mode = HDD_MODE_NORMAL;
	}

	// open the file off the memory card
	FIL fil;
	FRESULT res = f_open(&fil, "SCUZNET.INI", FA_READ);
	if (res)
	{
		fatal(FATAL_CONFIG_FILE, (uint8_t) res);
	}

	// execute INIH parse using FatFs f_gets()
	int pres = ini_parse_stream((ini_reader) f_gets, &fil, config_handler, NULL);
	if (pres != 0)
	{
		if (pres < 0)
		{
			fatal(FATAL_CONFIG_LINE_READ, 0);
		}
		else
		{
			uint8_t line = (uint8_t) pres;
			if (pres > 255) line = 255;
			fatal(FATAL_CONFIG_LINE_READ, line);
		}
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
		if (config_hdd[i].id < 7)
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
}
