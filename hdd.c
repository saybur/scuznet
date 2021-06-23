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
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "lib/pff/diskio.h"
#include "lib/pff/pff.h"
#include "config.h"
#include "debug.h"
#include "logic.h"
#include "hdd.h"

/*
 * Defines the standard response we provide when asked to give INQUIRY data.
 */
#define HDD_INQUIRY_LENGTH 36
const uint8_t inquiry_data[] PROGMEM = {
	0x00, 0x00, 0x02, 0x02,
	0x1F, 0x00, 0x00, 0x00,
	' ', 's', 'c', 'u', 'z', 'n', 'e', 't',
	' ', 's', 'c', 'u', 'z', 'n', 'e', 't',
	' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 
	'0', '.', '1', 'a'
};

// READ CAPACITY information
static uint8_t capacity_data[8] = {
	0x00, 0x00, 0x00, 0x00, // size in blocks, inits to 0
	0x00, 0x00, 0x02, 0x00  // 512 byte blocks
};

static uint8_t hdd_ready;
static uint8_t hdd_error;
static uint8_t open_hdd_vol = 255;

// generic buffer for READ/WRITE BUFFER commands
#define MEMORY_BUFFER_LENGTH 68
static uint8_t mem_buffer[MEMORY_BUFFER_LENGTH] = {
	0x00, 0x00, 0x00, 0x40
};

/*
 * ============================================================================
 * 
 *   OPERATION HANDLERS
 * 
 * ============================================================================
 * 
 * Each of these gets called from the _main() function to perform a particular
 * task on either the device or the PHY.
 */

static void hdd_test_unit_ready(void)
{
	if (hdd_ready)
	{
		logic_status(LOGIC_STATUS_GOOD);
	}
	else
	{
		logic_set_sense(SENSE_KEY_NOT_READY, SENSE_DATA_LUN_BECOMING_RDY);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
	}
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void hdd_inquiry(uint8_t* cmd)
{
	uint8_t alloc = cmd[4];
	if (alloc > HDD_INQUIRY_LENGTH)
		alloc = HDD_INQUIRY_LENGTH;

	logic_data_in_pgm(inquiry_data, alloc);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void hdd_set_capacity(uint8_t hdd_id)
{
	/*
	 * We strip off the low 12 bits to conform with the sizing reported by the
	 * rigid disk geometry page in MODE SENSE, then subtract 1 to get the last
	 * readable block for the command.
	 */
	uint32_t last = (config_hdd[hdd_id].size & 0xFFFFF000) - 1;
	capacity_data[0] = (uint8_t) (last >> 24);
	capacity_data[1] = (uint8_t) (last >> 16);
	capacity_data[2] = (uint8_t) (last >> 8);
	capacity_data[3] = (uint8_t) last;
}

static void hdd_read_capacity(uint8_t hdd_id, uint8_t* cmd)
{
	if (cmd[1] & 1)
	{
		// RelAdr set, we're not playing that game
		logic_cmd_illegal_arg(1);
	}
	else
	{
		hdd_set_capacity(hdd_id);
		logic_data_in(capacity_data, 8);
		logic_status(LOGIC_STATUS_GOOD);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
	}
}

/*
 * Minimalistic implementation of the FORMAT UNIT command, supporting only
 * no-arg defect lists.
 * 
 * The flash card handles all this internally so this is likely useless to
 * support anyway.
 */
static void hdd_format(uint8_t* cmd)
{
	if (hdd_ready)
	{
		uint8_t fmt = cmd[1];
		if (fmt == 0x00)
		{
			logic_status(LOGIC_STATUS_GOOD);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		}
		else if (fmt == 0x10
				|| fmt == 0x18)
		{
			// read the defect list header
			uint8_t parms[4];
			uint8_t len = logic_data_out(parms, 4);
			if (len != 4)
			{
				phy_phase(PHY_PHASE_BUS_FREE);
				return;
			}

			// we only support empty lists
			// TODO: should be bother checking the flags?
			if (parms[2] == 0x00 && parms[3] == 0x00)
			{
				logic_status(LOGIC_STATUS_GOOD);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			}
			else
			{
				logic_set_sense_pointer(SENSE_KEY_ILLEGAL_REQUEST,
						SENSE_DATA_INVALID_CDB_PARAM,
						0xC0, 0x02);
				logic_status(LOGIC_STATUS_CHECK_CONDITION);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			}
		}
		else
		{
			logic_cmd_illegal_arg(1);
		}
	}
	else
	{
		logic_set_sense(SENSE_KEY_NOT_READY, SENSE_DATA_LUN_BECOMING_RDY);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
	}
}

static void hdd_read(uint8_t hdd_id, uint8_t* cmd)
{
	uint16_t act_len;
	
	if (! hdd_ready)
	{
		debug(DEBUG_HDD_NOT_READY);
		logic_set_sense(SENSE_KEY_NOT_READY, SENSE_DATA_LUN_BECOMING_RDY);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}

	logic_parse_data_op(cmd);
	LogicDataOp op = logic_data;
	if (op.invalid)
	{
		debug(DEBUG_HDD_INVALID_OPERATION);
		hdd_error = 1;
		logic_cmd_illegal_arg(op.invalid - 1);
		return;
	}
	if (op.lba + op.length >= config_hdd[hdd_id].size)
	{
		debug(DEBUG_HDD_SIZE_EXCEEDED);
		/*
		 * TODO: this should be ILLEGAL ARGUMENT with LBA in information. I
		 * should redo the entire REQUEST SENSE handling system in the logic
		 * subsystem, it does not adhere to the way things are supposed to be
		 * done.
		 */
		logic_cmd_illegal_arg(op.invalid - 1);
		return;
	}

	if (op.length > 0)
	{
		if (debug_enabled())
		{
			debug(DEBUG_HDD_READ_STARTING);
			if (debug_verbose())
			{
				debug_dual(
					(uint8_t) (op.length >> 8),
					(uint8_t) op.length);
			}
		}
		phy_phase(PHY_PHASE_DATA_IN);

		uint8_t res;
		if (config_hdd[hdd_id].filename != NULL) // filesystem virtual HDD
		{
			// if virtual HDD is not already open, do so now
			if (open_hdd_vol != hdd_id)
			{
				res = pf_open(config_hdd[hdd_id].filename);
				if (res)
				{
					debug(DEBUG_HDD_FOPEN_FAILED);
					hdd_error = 1;
					open_hdd_vol = 255;
					logic_set_sense(SENSE_KEY_MEDIUM_ERROR,
							SENSE_DATA_NO_INFORMATION);
					logic_status(LOGIC_STATUS_CHECK_CONDITION);
					logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
					return;
				}
				else
				{
					open_hdd_vol = hdd_id;
				}
			}

			// move to correct sector
			res = pf_lseek(op.lba * 512);
			if (res)
			{
				debug_dual(DEBUG_HDD_MEM_SEEK_ERROR, res);
				hdd_error = 1;
				logic_set_sense(SENSE_KEY_MEDIUM_ERROR,
						SENSE_DATA_NO_INFORMATION);
				logic_status(LOGIC_STATUS_CHECK_CONDITION);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				return;
			}

			// read from card
			res = pf_mread(phy_data_offer_bulk, op.length, &act_len);
		}
		else if (config_hdd[hdd_id].size > 0) // native card access
		{
			uint32_t offset = config_hdd[hdd_id].size + op.lba;
			res = disk_read_multi(phy_data_offer_bulk, offset, op.length);
			if (! res) act_len = op.length;
		}
		else
		{
			// shouldn't be able to get here, this is probably a programming error
			debug(DEBUG_HDD_INVALID_FILE);
			logic_set_sense(SENSE_KEY_NOT_READY, SENSE_DATA_LUN_BECOMING_RDY);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}

		if (res || act_len != op.length)
		{
			if (debug_enabled())
			{
				debug_dual(DEBUG_HDD_MEM_READ_ERROR, res);
				if (debug_verbose())
				{
					debug_dual(
						(uint8_t) (act_len >> 8),
						(uint8_t) act_len);
				}
			}
			hdd_error = 1;
			logic_set_sense(SENSE_KEY_MEDIUM_ERROR,
					SENSE_DATA_NO_INFORMATION);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}
	}

	debug(DEBUG_HDD_READ_OKAY);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void hdd_write(uint8_t hdd_id, uint8_t* cmd)
{
	uint16_t act_len;
	
	if (! hdd_ready)
	{
		debug(DEBUG_HDD_NOT_READY);
		logic_set_sense(SENSE_KEY_NOT_READY, SENSE_DATA_LUN_BECOMING_RDY);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}

	logic_parse_data_op(cmd);
	LogicDataOp op = logic_data;
	if (op.invalid)
	{
		debug(DEBUG_HDD_INVALID_OPERATION);
		hdd_error = 1;
		logic_cmd_illegal_arg(op.invalid - 1);
		return;
	}
	if (op.lba + op.length >= config_hdd[hdd_id].size)
	{
		debug(DEBUG_HDD_SIZE_EXCEEDED);
		// TODO: per comment in _read()
		logic_cmd_illegal_arg(op.invalid - 1);
		return;
	}

	if (op.length > 0)
	{
		if (debug_enabled())
		{
			debug(DEBUG_HDD_WRITE_STARTING);
			if (debug_verbose())
			{
				debug_dual(
					(uint8_t) (op.length >> 8),
					(uint8_t) op.length);
			}
		}
		phy_phase(PHY_PHASE_DATA_OUT);

		uint8_t res;
		if (config_hdd[hdd_id].filename != NULL) // filesystem virtual HDD
		{
			// if virtual HDD is not already open, do so now
			if (open_hdd_vol != hdd_id)
			{
				res = pf_open(config_hdd[hdd_id].filename);
				if (res)
				{
					debug(DEBUG_HDD_FOPEN_FAILED);
					hdd_error = 1;
					open_hdd_vol = 255;
					logic_set_sense(SENSE_KEY_MEDIUM_ERROR,
							SENSE_DATA_NO_INFORMATION);
					logic_status(LOGIC_STATUS_CHECK_CONDITION);
					logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
					return;
				}
				else
				{
					open_hdd_vol = hdd_id;
				}
			}

			// move to correct sector
			res = pf_lseek(op.lba * 512);
			if (res)
			{
				debug_dual(DEBUG_HDD_MEM_SEEK_ERROR, res);
				hdd_error = 1;
				logic_set_sense(SENSE_KEY_MEDIUM_ERROR,
						SENSE_DATA_NO_INFORMATION);
				logic_status(LOGIC_STATUS_CHECK_CONDITION);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				return;
			}

			// write to card
			res = pf_mwrite(phy_data_ask_bulk, op.length, &act_len);
		}
		else if (config_hdd[hdd_id].size > 0) // native card access
		{
			uint32_t offset = config_hdd[hdd_id].size + op.lba;
			res = disk_write_multi(phy_data_ask_bulk, offset, op.length);
			if (! res) act_len = op.length;
		}
		else
		{
			// shouldn't be able to get here, this is probably a programming error
			debug(DEBUG_HDD_INVALID_FILE);
			logic_set_sense(SENSE_KEY_NOT_READY, SENSE_DATA_LUN_BECOMING_RDY);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}

		if (res || act_len != op.length)
		{
			if (debug_enabled())
			{
				debug_dual(DEBUG_HDD_MEM_WRITE_ERROR, res);
				if (debug_verbose())
				{
					debug_dual(
						(uint8_t) (act_len >> 8),
						(uint8_t) act_len);
				}
			}
			hdd_error = 1;
			logic_set_sense(SENSE_KEY_MEDIUM_ERROR,
					SENSE_DATA_NO_INFORMATION);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}
	}

	debug(DEBUG_HDD_WRITE_OKAY);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void hdd_mode_sense(uint8_t hdd_id, uint8_t* cmd)
{
	if (! hdd_ready)
	{
		debug(DEBUG_HDD_NOT_READY);
		logic_set_sense(SENSE_KEY_NOT_READY, SENSE_DATA_LUN_BECOMING_RDY);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}

	debug(DEBUG_HDD_MODE_SENSE);

	// extract basic command values
	uint8_t cmd_dbd = cmd[1] & 0x8;
	uint8_t cmd_pc = (cmd[2] & 0xC0) >> 6;
	uint8_t cmd_page = cmd[2] & 0x3F;

	// reset result length values
	uint8_t buffer[128];
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
		buffer[mode_pos++] = 0x00; // default medium
		buffer[mode_pos++] = 0x00; // not write protected

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
		buffer[mode_pos++] = 0x00; // density
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
		buffer[mode_pos++] = 0x02;
		buffer[mode_pos++] = 0x0E;
		for (uint8_t i = 0; i < 0x0E; i++)
		{
			buffer[mode_pos++] = 0x00;
		}
	}

	// format page
	if (cmd_page == 0x03 || cmd_page == 0x3F)
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
	uint8_t cyl[3];
	if (cmd_page == 0x04 || cmd_page == 0x3F)
	{
		page_found = 1;

		/*
		 * We always report 128 heads and 32 sectors per track, so only
		 * cylinder data needs to be reported as a variable based on the
		 * volume capacity. With a fixed 512 byte sector size, this allows
		 * incrementing in 4096 block steps, or 2MB each.
		 */
		hdd_set_capacity(hdd_id);
		cyl[0] = capacity_data[0] >> 4;
		cyl[1] = (capacity_data[0] << 4) | (capacity_data[1] >> 4);
		cyl[2] = (capacity_data[1] << 4) | (capacity_data[2] >> 4);

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

		// heads
		if (cmd_pc != 0x01)
		{
			buffer[mode_pos++] = 0x80;
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

static void hdd_verify(uint8_t* cmd)
{
	debug(DEBUG_HDD_VERIFY);
	if (! hdd_ready)
	{
		debug(DEBUG_HDD_NOT_READY);
		logic_set_sense(SENSE_KEY_NOT_READY, SENSE_DATA_LUN_BECOMING_RDY);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}

	if (cmd[1] & 1)
	{
		// RelAdr set
		logic_cmd_illegal_arg(1);
		return;
	}
	if (cmd[1] & 2)
	{
		/*
		 * This is a dummy operation: we just pretend to care about what the
		 * initiator is asking for, and we don't verify anything: just get
		 * the DATA IN length we need to do and report that everything is OK.
		 */
		phy_phase(PHY_PHASE_DATA_IN);
		uint16_t len = (cmd[7] << 8) | cmd[8];
		for (uint16_t i = 0; i < len; i++)
		{
			for (uint16_t j = 0; j < 512; j++)
			{
				// this will be glacial, but this should be an uncommon
				// operation anyway
				phy_data_ask();
			}
		}
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void hdd_read_buffer(uint8_t* cmd)
{
	debug(DEBUG_HDD_READ_BUFFER);
	uint8_t cmd_mode = cmd[1] & 0x7;
	// we only support mode 0
	if (cmd_mode)
	{
		logic_cmd_illegal_arg(1);
		return;
	}

	// figure how long the READ BUFFER needs to be
	uint8_t length;
	if (cmd[6] > 0 || cmd[7] > 0)
	{
		length = 255;
	}
	else
	{
		length = cmd[8];
	}
	if (length > MEMORY_BUFFER_LENGTH)
	{
		length = MEMORY_BUFFER_LENGTH;
	}

	// send the data
	logic_data_in(mem_buffer, length);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void hdd_write_buffer(uint8_t* cmd)
{
	debug(DEBUG_HDD_WRITE_BUFFER);
	uint8_t cmd_mode = cmd[1] & 0x7;
	// we only support mode 0
	if (cmd_mode)
	{
		logic_cmd_illegal_arg(1);
		return;
	}

	uint8_t length = cmd[8];
	if (cmd[6] > 0
			|| cmd[7] > 0
			|| length > MEMORY_BUFFER_LENGTH - 4)
	{
		// exceeded buffer capacity
		logic_cmd_illegal_arg(6);
		return;
	}
	if (length < 4)
	{
		// too short?
		logic_status(LOGIC_STATUS_GOOD);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}

	phy_phase(PHY_PHASE_DATA_OUT);
	for (uint8_t i = 0; i < 4; i++)
	{
		phy_data_ask();
	}
	logic_data_out(mem_buffer + 4, length);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

// hacky version that just accepts without complaint anything
static void hdd_mode_select(uint8_t* cmd)
{
	debug(DEBUG_HDD_MODE_SELECT);
	uint8_t length = cmd[4];
	if (length > 0)
	{
		logic_data_out_dummy(length);
	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

/*
 * ============================================================================
 * 
 *   EXTERNAL FUNCTIONS
 * 
 * ============================================================================
 */

uint8_t hdd_init(void)
{
	FRESULT res;
	
	// TODO: this is currently unused, which is bad
	hdd_error = 0;

	for (uint8_t i = 0; i < HARD_DRIVE_COUNT; i++)
	{
		if (config_hdd[i].id != 255 && config_hdd[i].filename != NULL)
		{
			res = pf_open(config_hdd[i].filename);
			if (res)
			{
				debug(DEBUG_HDD_MOUNT_FAILED);
				return 0;
			}
			
			uint32_t size;
			res = pf_size(&size);
			if (res)
			{
				debug(DEBUG_HDD_FILE_SIZE_FAILED);
				return 0;
			}
			size >>= 9; // in 512 byte sectors
			if (size > 0)
			{
				config_hdd[i].size = size;
			}
		}
	}

	// verify native volumes do not exceed the end of the memory card
	// TODO this is experimental and needs some testing
	uint32_t card_size;
	if (disk_ioctl(GET_SECTOR_COUNT, &card_size))
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

	hdd_ready = 1;
	return 1;
}

uint8_t hdd_has_error(void)
{
	return hdd_error;
}

uint8_t hdd_main(uint8_t hdd_id)
{
	if (! logic_ready()) return 0;
	if (hdd_id >= HARD_DRIVE_COUNT) return 0;
	if (config_hdd[hdd_id].id == 255) return 0;
	
	logic_start(hdd_id + 1, 1); // logic ID 0 for the link device, hence +1

	uint8_t cmd[10];
	if (! logic_command(cmd)) return 1; // this disconnects on failure

	switch (cmd[0])
	{
		case 0x04: // FORMAT UNIT
			hdd_format(cmd);
			break;
		case 0x12: // INQUIRY
			hdd_inquiry(cmd);
			break;
		case 0x08: // READ(6)
		case 0x28: // READ(10)
			hdd_read(hdd_id, cmd);
			break;
		case 0x25: // READ CAPACITY
			hdd_read_capacity(hdd_id, cmd);
			break;
		case 0x17: // RELEASE
			logic_status(LOGIC_STATUS_GOOD);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			break;
		case 0x03: // REQUEST SENSE
			logic_request_sense(cmd);
			break;
		case 0x16: // RESERVE
			logic_status(LOGIC_STATUS_GOOD);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			break;
		case 0x1D: // SEND DIAGNOSTIC
			logic_send_diagnostic(cmd);
			break;
		case 0x00: // TEST UNIT READY
			hdd_test_unit_ready();
			break;
		case 0x0A: // WRITE(6)
		case 0x2A: // WRITE(10)
			hdd_write(hdd_id, cmd);
			break;
		case 0x1A: // MODE SENSE(6)
		case 0x5A: // MODE SENSE(10)
			hdd_mode_sense(hdd_id, cmd);
			break;
		case 0x15: // MODE SELECT(6)
			hdd_mode_select(cmd);
			break;
		case 0x2F: // VERIFY
			hdd_verify(cmd);
			break;
		case 0x3C: // READ BUFFER
			hdd_read_buffer(cmd);
			break;
		case 0x3B: // WRITE BUFFER
			hdd_write_buffer(cmd);
			break;
		default:
			logic_cmd_illegal_op(cmd[0]);
	}
	logic_done();
	return 1;
}
