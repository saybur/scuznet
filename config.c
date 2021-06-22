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
#include <stdio.h>
#include "lib/inih/ini.h"
#include "lib/pff/pff.h"
#include "config.h"
#include "debug.h"

ENETConfig config_enet = { 255, 0, { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00} };
HDDConfig config_hdd[HARD_DRIVE_COUNT];

static char* config_buffer;
static uint16_t config_position;
static uint16_t config_length;

/*
 * Function for providing to fdevopen() supporting memory card reads.
 */ 
static int config_fetch(FILE* fp)
{
	(void) fp; // silence compiler
	
	if (config_position >= config_length)
	{
		// at end of buffer, need more data if there is any left
		if (config_length != 512) return _FDEV_EOF;
		// might have more data, check if so
		if (pf_read(config_buffer, 512, &config_length)) return _FDEV_ERR;
		if (config_length == 0) return _FDEV_EOF;
		// must have more, reset to beginning of buffer
		config_position = 0;
	}

	int r = *(config_buffer + config_position);
	config_position++;
	return r;
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
		config_hdd[i].size = 0;
	}
	
	*target_masks = 0;
	CONFIG_RESULT result = CONFIG_OK;

	// open the file off the memory card
	FRESULT res = pf_open("SCUZNET.INI");
	if (res)
	{
		debug(DEBUG_CONFIG_FILE_MISSING);
		return CONFIG_NOFILE;
	}

	/*
	 * We use the nonstandard fdevopen() to handle wrapping the low-level block
	 * reader. The position/length values are arbitrary to trigger a pf_read()
	 * call during the first invocation.
	 */
	config_buffer = malloc(512);
	config_position = 512;
	config_length = 512;
	FILE* fp = fdevopen(NULL, config_fetch); // open read-only to our handler
	if (config_buffer != NULL && fp != NULL)
	{
		if (ini_parse_stream((ini_reader) fgets, fp, config_handler, NULL) < 0)
		{
			debug(DEBUG_CONFIG_FILE_MISSING);
			result = CONFIG_NOLOAD;
		}
	}
	else
	{
		debug(DEBUG_CONFIG_MEMORY_ERROR);
		result = CONFIG_NOLOAD;
	}

	/*
	 * Calculate the PHY masks requested from the configuration file, and
	 * finish configuring the hard drives based on the requested values.
	 */
	uint8_t used_masks = 0x80; // reserve ID 7 for initiator
	if (config_enet.id < 7)
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
		if (config_hdd[i].id < 7 && config_hdd[i].filename != NULL)
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

	// clean up
	fclose(fp);
	free(config_buffer);
	
	return result;
}
