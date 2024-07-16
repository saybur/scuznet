/*
 * Copyright (C) 2024 saybur
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

#include <string.h>
#include "config.h"
#include "logic.h"
#include "toolbox.h"

#ifdef USE_TOOLBOX

static const __flash char str_directory[] = TOOLBOX_FOLDER;

typedef enum {
	LIST_COUNT = 0,
	LIST_INDEX,
	LIST_FIND
} LSMODE;

static FIL fp;
static uint8_t fp_index = 255;
static uint32_t fp_pos;
static uint32_t fp_size;

/*
 * Generic listing call for the shared file directory. This is integrated to
 * simplify file iteration/filtering. It operates in different modes:
 *
 * 1) in count mode, just counts the valid files and returns the number found.
 * 2) in name mode, it builds the file name return data in 40 byte chunks and
 *    pipes it out over the SCSI bus, returning 0 on error and the number of
 *    files found on success; make sure the bus is in the right mode before
 *    calling this.
 * 3) in find mode, it locates a file at fp_index and updates the file
 *    pointer, returning 0 on error and 1 on success; make sure the file is
 *    closed prior to invoking.
 */
static uint8_t toolbox_ls(LSMODE mode)
{
	DIR dir;
	FILINFO finfo;
	FRESULT res;
	uint8_t fcount = 0;
	char* fname;

	// save a bit of SRAM via the global buffer used later anyway
	for (uint8_t i = 0; i < sizeof(str_directory); i++)
	{
		global_buffer[i] = str_directory[i];
	}

	res = f_opendir(&dir, (char*) global_buffer);
	if (res == FR_OK)
	{
		res = f_readdir(&dir, &finfo);
		while (res == FR_OK
				&& finfo.fname[0] != 0
				&& fcount < TOOLBOX_MAX_FILES)
		{
			if (finfo.fname[0] == '.') continue;
			if (finfo.fattrib & AM_DIR) continue;

#if _USE_LFN
			fname = *finfo.lfname ? finfo.lfname : finfo.fname;
#else
			fname = finfo.fname;
#endif

			if (mode == LIST_INDEX)
			{
				uint16_t len = strlen(fname);
				if (len > 32) len = 32;
				memset(global_buffer, 0, 40);
				global_buffer[0] = fcount;
				global_buffer[1] = 1;
				memcpy((char*) &(global_buffer[2]), fname, len);
				global_buffer[36] = ((finfo.fsize) >> 24) & 0xFF;
				global_buffer[37] = ((finfo.fsize) >> 16) & 0xFF;
				global_buffer[38] = ((finfo.fsize) >> 8) & 0xFF;
				global_buffer[39] = (finfo.fsize) & 0xFF;

				if (phy_data_offer_bulk(global_buffer, 40) != 40)
				{
					return 0;
				}
			}
			else if (mode == LIST_FIND)
			{
				if (fcount == fp_index)
				{
					res = f_chdir((char*) global_buffer);
					if (res) return 0;
					fp_size = finfo.fsize;
					res = f_open(&fp, fname, FA_READ | FA_OPEN_EXISTING);
					return (res == FR_OK ? 1 : 0);
				}
			}

			fcount++;
			res = f_readdir(&dir, &finfo);
		}
	}

	if (mode == LIST_FIND)
	{
		return 0;
	}
	else
	{
		return fcount;
	}
}

static void toolbox_index(void)
{
	phy_phase(PHY_PHASE_DATA_IN);
	toolbox_ls(LIST_INDEX);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static uint16_t toolbox_offer_size;
static uint8_t toolbox_offer_block(uint8_t* data)
{
	uint16_t size = 512;
	if (toolbox_offer_size < 512)
	{
		size = toolbox_offer_size;
	}
	toolbox_offer_size -= size;

	if (phy_data_offer_bulk(data, size) == size)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

static void toolbox_read(uint8_t* cmd)
{
	FRESULT res;
	uint8_t index = cmd[1];
	uint32_t pos = ((uint32_t) cmd[2] << 24)
		| ((uint32_t) cmd[3] << 16)
		| ((uint32_t) cmd[4] << 8)
		| ((uint32_t) cmd[5]);
	pos <<= 12;

	// open file if not already at the one they're asking for
	if (index != fp_index)
	{
		if (fp_index != 255)
		{
			f_close(&fp);
		}

		fp_index = index;

		if (! toolbox_ls(LIST_FIND))
		{
			fp_index = 255;
			logic_set_sense(SENSE_INVALID_CDB_ARGUMENT, 1);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}
		else
		{
			fp_pos = 0;
			res = f_lseek(&fp, 0);
			if (res)
			{
				logic_set_sense(SENSE_INVALID_CDB_ARGUMENT, 5);
				logic_status(LOGIC_STATUS_CHECK_CONDITION);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				return;
			}
		}
	}

	// move to the new index if not already there
	if (pos != fp_pos)
	{
		res = f_lseek(&fp, pos);
		if (! res)
		{
			logic_set_sense(SENSE_INVALID_CDB_ARGUMENT, 2);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}
	}

	/*
	 * 64A3U has insufficient SRAM to load the full 4K block for reading,
	 * but the custom f_mread() lets us cheat by piping the output directly
	 * to the bus; requires using the 512-byte block helper above.
	 */
	uint16_t blocks = 8;
	uint32_t rem = fp_size - pos;
	if (rem < 4096)
	{
		blocks = (rem >> 9); // div by 512
		if (rem % 512) blocks++; // add a block if there is partial data
		toolbox_offer_size = rem;
		fp_pos = fp_size;
	}
	else
	{
		toolbox_offer_size = 4096;
		fp_pos = pos + 4096;
	}

	phy_phase(PHY_PHASE_DATA_IN);
	uint16_t act_len;
	res = f_mread(&fp, toolbox_offer_block, blocks, &act_len, 1);
	if (res)
	{
		logic_set_sense(SENSE_MEDIUM_ERROR, 0);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void toolbox_count()
{
	uint8_t files = toolbox_ls(LIST_COUNT);
	phy_phase(PHY_PHASE_DATA_IN);
	phy_data_offer(files);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

uint8_t toolbox_main(uint8_t* cmd)
{
	switch (cmd[0])
	{
		case 0xD0:
			toolbox_index();
			break;
		case 0xD1:
			toolbox_read(cmd);
			break;
		case 0xD2:
			toolbox_count();
			break;
		default:
			return 0;
	}
	return 1;
}

#endif /* USE_TOOLBOX */
