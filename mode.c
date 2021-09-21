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

#include "config.h"
#include "debug.h"
#include "logic.h"
#include "mode.h"

void mode_update_capacity(uint32_t size, uint8_t* arr)
{
	/*
	 * We strip off the low 12 bits to conform with the sizing reported by the
	 * rigid disk geometry page in MODE SENSE, then subtract 1 to get the last
	 * readable block for the command.
	 */
	uint32_t last = (size & 0xFFFFF000) - 1;
	arr[0] = (uint8_t) (last >> 24);
	arr[1] = (uint8_t) (last >> 16);
	arr[2] = (uint8_t) (last >> 8);
	arr[3] = (uint8_t) last;
}

void mode_sense(uint8_t* cmd, MODE_DEVICE_TYPE device_type, uint32_t size)
{
	debug(DEBUG_MODE_SENSE);

	// extract basic command values
	uint8_t cmd_dbd = cmd[1] & 0x8;
	uint8_t cmd_pc = (cmd[2] & 0xC0) >> 6;
	uint8_t cmd_page = cmd[2] & 0x3F;

	// reset result length values
	uint8_t* buffer = global_buffer;
	buffer[0] = 0;
	buffer[1] = 0;

	// get allocation length and set the block descriptor basics,
	// which vary between the (6) and (10) command variants.
	uint8_t cmd_alloc;
	uint8_t mode_pos;
	if (cmd[0] == 0x5A)
	{
		// allocation length, limit to 8 bits (we never will use more)
		if (cmd[7] > 0)
		{
			cmd_alloc = 255;
		}
		else
		{
			cmd_alloc = cmd[8];
		}

		// header values
		mode_pos = 2;
		switch (device_type)
		{
		case MODE_TYPE_HDD:
			buffer[mode_pos++] = 0x00; // default medium
			buffer[mode_pos++] = 0x00; // not write protected
			break;
		case MODE_TYPE_CDROM:
			buffer[mode_pos++] = 0x01; // 120mm CD-ROM data only
			buffer[mode_pos++] = 0x80; // write protected
			break;
		default:
			debug_dual(DEBUG_MODE_SENSE_UNKNOWN_TYPE, device_type);
		}

		// reserved
		buffer[mode_pos++] = 0;
		buffer[mode_pos++] = 0;

		// include block descriptor?
		buffer[mode_pos++] = 0;
		if (cmd_dbd)
		{
			buffer[mode_pos++] = 0x00;
		}
		else
		{
			buffer[mode_pos++] = 0x08;
		}
	}
	else
	{
		// allocation length
		cmd_alloc = cmd[4];

		// header values
		mode_pos = 1;
		buffer[mode_pos++] = 0x00; // default medium
		buffer[mode_pos++] = 0x00; // not write protected

		// include block descriptor?
		if (cmd_dbd)
		{
			buffer[mode_pos++] = 0x00;
		}
		else
		{
			buffer[mode_pos++] = 0x08;
		}
	}

	// append block descriptors, if allowed
	if (! cmd_dbd)
	{
		switch (device_type)
		{
		case MODE_TYPE_HDD:
			buffer[mode_pos++] = 0x00; // default density
			break;
		case MODE_TYPE_CDROM:
			buffer[mode_pos++] = 0x01; // 2048 bytes/physical sector
			break;
		default:
			debug_dual(DEBUG_MODE_SENSE_UNKNOWN_TYPE, device_type);
		}
		buffer[mode_pos++] = 0x00; // blocks MSB
		buffer[mode_pos++] = 0x00;
		buffer[mode_pos++] = 0x00;
		buffer[mode_pos++] = 0x00; // reserved
		buffer[mode_pos++] = 0x00; // block length MSB
		buffer[mode_pos++] = 0x02;
		buffer[mode_pos++] = 0x00;
	}

	/*
	 * Append pages in descending order as we get to them.
	 */
	uint8_t page_found = 0;

	// R/W error recovery page
	if (cmd_page == 0x01 || cmd_page == 0x3F)
	{
		page_found = 1;

		buffer[mode_pos++] = 0x01;
		buffer[mode_pos++] = 0x0A;
		for (uint8_t i = 0; i < 0x0A; i++)
		{
			buffer[mode_pos++] = 0x00;
		}
	}

	// disconnect/reconnect page
	if (cmd_page == 0x02 || cmd_page == 0x3F)
	{
		page_found = 1;

		buffer[mode_pos++] = 0x02;
		buffer[mode_pos++] = 0x0E;
		for (uint8_t i = 0; i < 0x0E; i++)
		{
			buffer[mode_pos++] = 0x00;
		}
	}

	// format page
	if (device_type == MODE_TYPE_HDD &&
			(cmd_page == 0x03 || cmd_page == 0x3F))
	{
		page_found = 1;

		buffer[mode_pos++] = 0x03;
		buffer[mode_pos++] = 0x16;
		for (uint8_t i = 0; i < 8; i++)
		{
			buffer[mode_pos++] = 0x00;
		}

		// sectors per track, fixed @ 32
		buffer[mode_pos++] = 0x00;
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 32;
		}
		else
		{
			buffer[mode_pos++] = 0;
		}

		// bytes per sector, fixed @ 512
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 0x02;
		}
		else
		{
			buffer[mode_pos++] = 0x00;
		}
		buffer[mode_pos++] = 0x00;

		// interleave, fixed @ 1
		buffer[mode_pos++] = 0x00;
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 0x01;
		}
		else
		{
			buffer[mode_pos++] = 0x00;
		}

		// track skew, cyl skew
		for (uint8_t i = 0; i < 4; i++)
		{
			buffer[mode_pos++] = 0x00;
		}

		// flags in byte 20
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 0x40; // hard sectors only
		}
		else
		{
			buffer[mode_pos++] = 0x00;
		}

		// remaining reserved bytes
		for (uint8_t i = 0; i < 3; i++)
		{
			buffer[mode_pos++] = 0x00;
		}
	}

	// rigid disk geometry page
	uint8_t cap[4];
	uint8_t cyl[3];
	if (device_type == MODE_TYPE_HDD &&
			(cmd_page == 0x04 || cmd_page == 0x3F))
	{
		page_found = 1;

		/*
		 * We always report 128 heads and 32 sectors per track, so only
		 * cylinder data needs to be reported as a variable based on the
		 * volume capacity. With a fixed 512 byte sector size, this allows
		 * incrementing in 4096 block steps, or 2MB each.
		 */
		mode_update_capacity(size, cap);
		cyl[0] = cap[0] >> 4;
		cyl[1] = (cap[0] << 4) | (cap[1] >> 4);
		cyl[2] = (cap[1] << 4) | (cap[2] >> 4);

		buffer[mode_pos++] = 0x04;
		buffer[mode_pos++] = 0x16;

		// cylinders
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = cyl[0];
			buffer[mode_pos++] = cyl[1];
			buffer[mode_pos++] = cyl[2];
		}
		else
		{
			buffer[mode_pos++] = 0x00;
			buffer[mode_pos++] = 0x00;
			buffer[mode_pos++] = 0x00;
		}

		// heads, fixed at 64
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 0x40;
		}
		else
		{
			buffer[mode_pos++] = 0x00;
		}

		// disable the next fields by setting to max cyl
		for (uint8_t j = 0; j < 2; j++)
		{
			for (uint8_t i = 0; i < 3; i++)
			{
				if (cmd_pc != 0x01)
				{
					buffer[mode_pos++] = cyl[i];
				}
				else
				{
					buffer[mode_pos++] = 0x00;
				}
			}
		}

		// step rate
		buffer[mode_pos++] = 0x00;
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 0x01;
		}
		else
		{
			buffer[mode_pos++] = 0x00;
		}

		// defaults for the next values
		for (uint8_t i = 0; i < 6; i++)
		{
			buffer[mode_pos++] = 0x00;
		}

		// medium rotation rate... say, maybe 10,000 RPM?
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 0x27;
			buffer[mode_pos++] = 0x10;
		}
		else
		{
			buffer[mode_pos++] = 0x00;
			buffer[mode_pos++] = 0x00;
		}

		// defaults for the next values
		for (uint8_t i = 0; i < 2; i++)
		{
			buffer[mode_pos++] = 0x00;
		}
	}

	// cache page
	if (cmd_page == 0x08 || cmd_page == 0x3F)
	{
		page_found = 1;

		buffer[mode_pos++] = 0x08;
		buffer[mode_pos++] = 0x0A;

		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 0x01; // only RCD set, no read cache
		}
		else
		{
			buffer[mode_pos++] = 0x00;
		}

		for (uint8_t i = 1; i < 0x0A; i++)
		{
			buffer[mode_pos++] = 0x00;
		}
	}

	// finally, either send or error out, depending on if any page matched.
	if (page_found)
	{
		if (mode_pos > cmd_alloc)
			mode_pos = cmd_alloc;
		if (cmd[0] == 0x5A)
		{
			buffer[1] = mode_pos - 2;
		}
		else
		{
			buffer[0] = mode_pos - 1;
		}

		logic_data_in(buffer, mode_pos);
		logic_status(LOGIC_STATUS_GOOD);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
	}
	else
	{
		logic_cmd_illegal_arg(2);
	}
}

void mode_select(uint8_t* cmd)
{
	debug(DEBUG_MODE_SELECT);
	uint8_t length = cmd[4];
	if (length > 0)
	{
		logic_data_out_dummy(length);
	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}
