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

#include <avr/pgmspace.h>
#include <util/delay.h>
#include "config.h"
#include "debug.h"
#include "logic.h"
#include "mem.h"
#include "hdd.h"

/*
 * Defines the standard response we provide when asked to give INQUIRY data.
 */
#define HDD_INQUIRY_LENGTH 36
const uint8_t inquiry_data[] PROGMEM = {
	0x00, 0x00, 0x02, 0x02,
	0x00, 0x00, 0x00, 0x00,
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

static void hdd_read_capacity(uint8_t* cmd)
{
	if (cmd[1] & 1)
	{
		// RelAdr set, we're not playing that game
		logic_cmd_illegal_arg(1);
	}
	else
	{
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

static void hdd_read(uint8_t* cmd)
{
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
		debug(DEBUG_HDD_OP_INVALID);
		hdd_error = 1;
		logic_cmd_illegal_arg(op.invalid - 1);
		return;
	}

	if (op.length > 0)
	{
		debug(DEBUG_HDD_READ_STARTING);

		if (! mem_op_start())
		{
			debug(DEBUG_HDD_MEM_CARD_BUSY);
			logic_status(LOGIC_STATUS_BUSY);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}

		/*
		 * Execute start-of-read operation on the memory card and make sure it
		 * responded OK. We will use CMD17 for single block and CMD18 (which
		 * requires termination) if more than 1 block, which appears to be
		 * required for some picky cards.
		 */
		uint8_t opcode = (op.length == 1 ? 17 : 18);
		uint8_t v = mem_op_cmd_args(opcode, op.lba);
		if (v != 0x00)
		{
			debug_dual(DEBUG_HDD_MEM_CMD_REJECTED, v);
			hdd_error = 1;
			logic_set_sense(SENSE_KEY_HARDWARE_ERROR,
					SENSE_DATA_NO_INFORMATION);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}

		/*
		 * Switch to the correct phase and begin the data reading process.
		 */
		phy_phase(PHY_PHASE_DATA_IN);
		for (uint16_t i = 0; i < op.length; i++)
		{
			debug(DEBUG_HDD_PACKET_START);
			v = mem_wait_for_data();
			if (v == MEM_DATA_TOKEN)
			{
				/*
				 * Transfer actual data, then transfer two dummy bytes for CRC,
				 * and one post-command byte to generate the 8 cycles needed
				 * for command commit.
				 * 
				 * The byte required by the below call should already be in the
				 * USART buffer per the contract with the data wait call.
				 */
				phy_data_offer_stream(&MEM_USART, 512);
				for (uint8_t j = 0; j < 2; j++)
				{
					MEM_USART.DATA = 0xFF;
					while (! (MEM_USART.STATUS & USART_RXCIF_bm));
					MEM_USART.DATA;
				}
			}
			else
			{
				// terminate reading operation, if needed
				if (opcode == 18)
				{
					while (! (MEM_USART.STATUS & USART_TXCIF_bm));
					mem_op_cmd(12);
				}
				mem_op_end();

				// indicate failure to initiator
				debug_dual(DEBUG_HDD_MEM_BAD_HEADER, v);
				hdd_error = 1;
				logic_set_sense(SENSE_KEY_MEDIUM_ERROR,
						SENSE_DATA_NO_INFORMATION);
				logic_status(LOGIC_STATUS_CHECK_CONDITION);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				return;
			}
		}

		// terminate reading operation, if needed
		if (opcode == 18)
		{
			while (! (MEM_USART.STATUS & USART_TXCIF_bm));
			mem_op_cmd(12);
		}
		mem_op_end();
	}

	debug(DEBUG_HDD_READ_OKAY);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void hdd_write(uint8_t* cmd)
{
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
		debug(DEBUG_HDD_OP_INVALID);
		hdd_error = 1;
		logic_cmd_illegal_arg(op.invalid - 1);
		return;
	}

	if (op.length > 0)
	{
		debug(DEBUG_HDD_WRITE_STARTING);

		if (! mem_op_start())
		{
			debug(DEBUG_HDD_MEM_CARD_BUSY);
			logic_status(LOGIC_STATUS_BUSY);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}

		/*
		 * Execute start-of-write operation on the memory card and make sure it
		 * responded OK. We will use CMD24 for single block and CMD25 (which
		 * requires termination) if more than 1 block, which appears to be
		 * required for some picky cards.
		 */
		uint8_t opcode = (op.length == 1 ? 24 : 25);
		uint8_t v = mem_op_cmd_args(opcode, op.lba);
		if (v != 0x00)
		{
			debug_dual(DEBUG_HDD_MEM_CMD_REJECTED, v);
			hdd_error = 1;
			logic_set_sense(SENSE_KEY_HARDWARE_ERROR,
					SENSE_DATA_NO_INFORMATION);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}
		uint8_t send_token = (op.length == 1
				? MEM_DATA_TOKEN
				: MEM_DATA_TOKEN_MULTIPLE);

		/*
		 * Switch to the correct phase and begin the data writing process.
		 */
		phy_phase(PHY_PHASE_DATA_OUT);
		for (uint16_t i = 0; i < op.length; i++)
		{
			/*
			 * Wait for the card to become ready, sending clocks the entire
			 * time. This also handles sending at least one 0xFF before the
			 * packet header.
			 */
			uint8_t response;
			do
			{
				MEM_USART.DATA = 0xFF;
				while (mem_data_not_ready());
				response = MEM_USART.DATA;
			}
			while (response != 0xFF);
			debug(DEBUG_HDD_PACKET_START);

			/*
			 * Send start token, then send 512 bytes of data. This will
			 * overflow RX.
			 */
			while (! (MEM_USART.STATUS & USART_DREIF_bm));
			MEM_USART.DATA = send_token;
			phy_data_ask_stream(&MEM_USART, 512);

			// wait for byte sending to stop so RX can be re-synced
			while (! (MEM_USART.STATUS & USART_TXCIF_bm));
			while (MEM_USART.STATUS & USART_RXCIF_bm)
			{
				MEM_USART.DATA;
			}

			/*
			 * Finish sending packet, by providing 16 bit fake CRC, the clocks
			 * for the data response, and an extra 8 clocks to commit the
			 * writing process.
			 */
			MEM_USART.DATA = 0xFF; // CRCH
			MEM_USART.DATA = 0xFF; // CRCL
			while (mem_data_not_ready());
			MEM_USART.DATA; // CRCH back
			MEM_USART.DATA = 0xFF; // data response
			while (mem_data_not_ready());
			MEM_USART.DATA; // CRCL back
			MEM_USART.DATA = 0xFF; // commit clocks
			while (mem_data_not_ready());
			response = MEM_USART.DATA; // data response back
			while (mem_data_not_ready());
			MEM_USART.DATA;

			/*
			 * Check if data response is OK.
			 */
			uint8_t okay = ((response & 0x1F) == 0x05);
			if (! okay)
			{
				/*
				 * Failure during read of some kind. Wait for card to come out
				 * of busy status (if possible), halt further operations, and
				 * indicate a MEDIUM ERROR to the initiator.
				 * 
				 * 
				 * FOLLOWING IS BAD AND WILL NOT WORK RIGHT
				 * USE THE VERSION BELOW THE LOOP
				 */
				if (opcode == 25)
				{
					while (! (MEM_USART.STATUS & USART_DREIF_bm));
					MEM_USART.DATA = MEM_STOP_TOKEN;
					while (! (MEM_USART.STATUS & USART_DREIF_bm));
					MEM_USART.DATA = 0xFF;
				}
				mem_op_end();
				debug_dual(DEBUG_HDD_MEM_BAD_HEADER, response);
				hdd_error = 1;
				logic_set_sense(SENSE_KEY_MEDIUM_ERROR,
						SENSE_DATA_NO_INFORMATION);
				logic_status(LOGIC_STATUS_CHECK_CONDITION);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				return;
			}
		}

		/*
		 * Wait for the card to become ready again before we proceed.
		 */
		uint8_t response;
		do
		{
			MEM_USART.DATA = 0xFF;
			while (mem_data_not_ready());
			response = MEM_USART.DATA;
		}
		while (response != 0xFF);

		// terminate writing operation if needed
		if (opcode == 25)
		{
			/*
			 * Send a stop token, a trailing 0xFF that we do not check to get
			 * the process started, then keep sending clocks until the card
			 * goes back to idle.
			 */
			MEM_USART.DATA = MEM_STOP_TOKEN;
			while (mem_data_not_ready());
			MEM_USART.DATA;
			MEM_USART.DATA = 0xFF;
			while (mem_data_not_ready());
			MEM_USART.DATA;
			do
			{
				MEM_USART.DATA = 0xFF;
				while (mem_data_not_ready());
				response = MEM_USART.DATA;
			}
			while (response != 0xFF);
		}
		mem_op_end();
	}

	debug(DEBUG_HDD_WRITE_OKAY);
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

void hdd_set_ready(uint32_t blocks)
{
	capacity_data[0] = (uint8_t) (blocks >> 24);
	capacity_data[1] = (uint8_t) (blocks >> 16);
	capacity_data[2] = (uint8_t) (blocks >> 8);
	capacity_data[3] = (uint8_t) blocks;
	hdd_ready = 1;
	hdd_error = 0;
}

uint8_t hdd_has_error(void)
{
	return hdd_error;
}

void hdd_main(void)
{
	if (! logic_ready()) return;
	logic_start(0, 1);

	uint8_t cmd[10];
	if (! logic_command(cmd)) return;

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
			hdd_read(cmd);
			break;
		case 0x25: // READ CAPACITY
			hdd_read_capacity(cmd);
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
			hdd_write(cmd);
			break;
		default:
			logic_cmd_illegal_op();
	}
	logic_done();
}
