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
				config_hdd[hddsel].mode = 1;
				return 1;
			}
			else if (strcmp(value, "normal") == 0)
			{
				config_hdd[hddsel].mode = 0;
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
 * Handles verification and final configuration of the hard drive volumes.
 * 
 * This will return true on success and false on failure.
 */
static uint8_t config_hdd_setup()
{
	FRESULT res;
	FILINFO fno;
	FIL* fp;

	for (uint8_t i = 0; i < HARD_DRIVE_COUNT; i++)
	{
		if (config_hdd[i].id != 255 && config_hdd[i].filename != NULL)
		{
			fp = &(config_hdd[i].fp);

			/*
			 * Verify the file exists. If it does not exist, we may have been
			 * asked to create it.
			 */
			res = f_stat(config_hdd[i].filename, &fno);
			if (res == FR_NO_FILE)
			{
				if (config_hdd[i].size > 0)
				{
					/*
					 * File does not exist and we have been asked to create it.
					 * This is done via f_expand() to maximize
					 * sequential-access performance. This will not work well
					 * if the drive is fragmented.
					 */
					config_hdd[i].size &= 0xFFF; // limit to 4GB
					config_hdd[i].size <<= 20; // MB to bytes (for now)
					res = f_open(fp, config_hdd[i].filename,
							FA_CREATE_NEW | FA_WRITE);
					if (res)
					{
						debug_dual(DEBUG_HDD_OPEN_FAILED, res);
						return 0;
					}
					// allocate the space
					res = f_expand(fp, config_hdd[i].size, 1);
					if (res)
					{
						debug_dual(DEBUG_HDD_ALLOCATE_FAILED, res);
						return 0;
					}
					// close the file, we'll be re-opening it later in
					// the normal modes
					f_close(fp);
					if (res)
					{
						debug_dual(DEBUG_HDD_CLOSE_FAILED, res);
						return 0;
					}
				}
			}
			else if (res == FR_OK)
			{
				// verify the file is the correct type to work with
				if ((fno.fattrib & AM_DIR) || (fno.fattrib & AM_RDO))
				{
					debug_dual(DEBUG_HDD_INVALID_FILE, fno.fattrib);
					return 0;
				}
			}
			else
			{
				// other error, can't open this file
				debug_dual(DEBUG_HDD_OPEN_FAILED, res);
				return 0;
			}

			/*
			 * If we flowed through to here, OK to attempt opening the file.
			 */
			res = f_open(fp, config_hdd[i].filename, FA_READ | FA_WRITE);
			if (res)
			{
				debug_dual(DEBUG_HDD_OPEN_FAILED, res);
				return 0;
			}
			config_hdd[i].size = (f_size(fp) >> 9); // store in 512 byte sectors
			if (config_hdd[i].size == 0)
			{
				debug(DEBUG_HDD_FILE_SIZE_FAILED);
				return 0;
			}

			/*
			 * We can enable much faster performance if the file is contiguous,
			 * and thus supporting low-level access that bypasses the FAT
			 * layer.
			 * 
			 * TODO: this needs better reporting for users.
			 */
			if (config_hdd[i].mode != 0)
			{
				uint8_t c;
				res = f_contiguous(fp, &c);
				if (res)
				{
					debug_dual(DEBUG_HDD_SEEK_ERROR, res);
					return 0;
				}
				if (c)
				{
					/*
					 * The file is contiguous, so it should be safe to enable
					 * low-level access. To figure out the start sector, we
					 * force a seek one byte into the first sector, which
					 * triggers a memory card read and fp->sect to be set.
					 */
					f_lseek(fp, 1);
					config_hdd[i].start = fp->sect;
					f_rewind(fp);
				}
			}
		}
	}

	// verify native volumes do not exceed the end of the memory card
	// TODO this is experimental and needs some testing
	uint32_t card_size;
	if (disk_ioctl(0, GET_SECTOR_COUNT, &card_size))
	{
		debug(DEBUG_HDD_IOCTRL_ERROR);
		return 0;
	}
	for (uint8_t i = 0; i < HARD_DRIVE_COUNT; i++)
	{
		if (config_hdd[i].id != 255 && config_hdd[i].start > 0)
		{
			uint32_t end = config_hdd[i].start + config_hdd[i].size;
			if(end > card_size)
			{
				debug_dual(DEBUG_HDD_NATIVE_VOLUME_SIZE_ERROR, i);
				return 0;
			}
		}
	}

	return 1;
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
		config_hdd[i].mode = 0;
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

	// finish configuring the hard drives
	if (! config_hdd_setup())
	{
		result = CONFIG_HDDERR;
	}

	return result;
}
