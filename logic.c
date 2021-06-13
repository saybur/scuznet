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
#include "config.h"
#include "debug.h"
#include "init.h"
#include "logic.h"
#include "phy.h"

/*
 * Generic NO SENSE response for REQUEST SENSE when there is nothing to report.
 * 
 * This is used to prevent having to overwrite the entire sense data array
 * whenever it is reset.
 */
const uint8_t sense_data_no_sense[] PROGMEM = {
	0xC0, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
};

/*
 * Bad (non-zero) LUN handling respones, for REQUEST SENSE and INQUIRY.
 * 
 * The sense data has ILLEGAL REQUEST, along with LOGICAL UNIT NOT SUPPORTED.
 */
const uint8_t sense_data_illegal_lun[] PROGMEM = {
	0xC0, 0x00, 0x05, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x25, 0x00, 0x00, 0x00,
	0x00, 0x00
};
const uint8_t inquiry_data_illegal_lun[] PROGMEM = {
	0x7F, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00,
	' ', 'i', 'n', 'v', 'a', 'l', 'i', 'd',
	' ', 'b', 'a', 'd', ' ', 'l', 'u', 'n',
	' ', ' ', ' ', ' ', ' ', ' ', ' ', '0'
};

// declaration of extern from header
LogicDataOp logic_data;

static LogicData devices[LOGIC_DEVICE_COUNT];
static uint8_t device_id;
static uint8_t last_message_in;
static uint8_t last_identify;

/*
 * ============================================================================
 * 
 *   START / STOP HANDLERS
 * 
 * ============================================================================
 */

uint8_t logic_start(uint8_t requested_device, uint8_t check_atn)
{
	if (requested_device < LOGIC_DEVICE_COUNT)
	{
		device_id = requested_device;
	}
	else
	{
		device_id = 0;
	}
	last_message_in = 0;
	last_identify = 0;

	// attention check if requested and the state is right for it
	// phy_is_active() will be checked inside the call
	if (check_atn && phy_is_atn_asserted())
	{
		return logic_message_out();
	}
	else
	{
		return 0;
	}
}

void logic_done(void)
{
	if (phy_is_active())
	{
		phy_phase(PHY_PHASE_BUS_FREE);
	}
}

/*
 * ============================================================================
 * 
 * INFORMATION FUNCTIONS
 * 
 * ============================================================================
 */

uint8_t logic_identify(void)
{
	return last_identify;
}

uint8_t logic_sense_valid(void)
{
	return devices[device_id].sense_valid;
}

void logic_parse_data_op(uint8_t* cmd)
{
	if (cmd[0] == 0x28 || cmd[0] == 0x2A)
	{
		if (cmd[1] & 1)
		{
			// RelAddr, so nope
			logic_data.invalid = 2;
		}
		else
		{
			logic_data.lba = ((uint32_t) cmd[2] << 24)
					| ((uint32_t) cmd[3] << 16)
					| ((uint32_t) cmd[4] << 8)
					| (uint32_t) cmd[5];
			logic_data.length = (cmd[7] << 8)
					+ cmd[8];
			logic_data.invalid = 0;
		}
	}
	else if (cmd[0] == 0x08 || cmd[0] == 0x0A)
	{
		logic_data.lba = ((uint32_t) (cmd[1] & 0x1F) << 16)
				| ((uint32_t) cmd[2] << 8)
				| (uint32_t) cmd[3];
		if (cmd[4] == 0)
		{
			logic_data.length = 256;
		}
		else
		{
			logic_data.length = cmd[4];
		}
		logic_data.invalid = 0;
	}
	else
	{
		logic_data.invalid = 1;
	}
}

/*
 * ============================================================================
 * 
 * BUS LOGICAL OPERATIONS
 * 
 * ============================================================================
 */

uint8_t logic_message_out(void)
{
	uint8_t message = 0;

	if (! phy_is_active()) return message;
	
	do
	{
		// following will do nothing if same phase
		phy_phase(PHY_PHASE_MESSAGE_OUT);

		// get the message byte
		message = phy_data_ask();
		if (message < 0x80)
		{
			/*
			 * Most of these messages are exceptional, and should not normally
			 * be encountered. Each will note on the debugging channel, and
			 * self-handle in various ways.
			 */
			if (message == LOGIC_MSG_ABORT)
			{
				// simply go bus free
				debug_dual(DEBUG_LOGIC_MESSAGE, LOGIC_MSG_ABORT);
				phy_phase(PHY_PHASE_BUS_FREE);
			}
			else if (message == LOGIC_MSG_BUS_DEVICE_RESET)
			{
				// execute a hard reset (MCU reset)
				debug_dual(DEBUG_LOGIC_MESSAGE, LOGIC_MSG_BUS_DEVICE_RESET);
				mcu_reset();
				// in the event the reset fails, go bus free anyway
				phy_phase(PHY_PHASE_BUS_FREE);
			}
			else if (message == LOGIC_MSG_DISCONNECT)
			{
				/*
				 * Send a DISCONNECT of our own, hang up, and track the
				 * duration to keep from reconnecting before we're allowed
				 */
				debug_dual(DEBUG_LOGIC_MESSAGE, LOGIC_MSG_DISCONNECT);
				phy_phase(PHY_PHASE_MESSAGE_IN);
				phy_data_offer(LOGIC_MSG_DISCONNECT);
				phy_phase(PHY_PHASE_BUS_FREE);
				PHY_TIMER_DISCON.CTRLFSET = TC_CMD_RESTART_gc;
				PHY_TIMER_DISCON.INTFLAGS = PHY_TIMER_DISCON_OVF;
			}
			else if (message == LOGIC_MSG_INIT_DETECT_ERROR)
			{
				/*
				 * We respond by disconnecting when this happens, instead of
				 * retrying.
				 */
				debug_dual(DEBUG_LOGIC_MESSAGE, LOGIC_MSG_INIT_DETECT_ERROR);
				phy_phase(PHY_PHASE_MESSAGE_IN);
				phy_data_offer(LOGIC_MSG_DISCONNECT);
				phy_phase(PHY_PHASE_BUS_FREE);
			}
			else if (message == LOGIC_MSG_PARITY_ERROR)
			{
				// resend the last message, then allow flow to continue
				debug_dual(DEBUG_LOGIC_MESSAGE, LOGIC_MSG_PARITY_ERROR);
				phy_phase(PHY_PHASE_MESSAGE_IN);
				phy_data_offer(last_message_in);
			}
			else if (message == LOGIC_MSG_REJECT)
			{
				/*
				 * We will never send a non-mandatory message except for
				 * DISCONNECT, so this seems very unlikely to ever happen.
				 * We respond by performing an unexpected disconnect.
				 */
				debug_dual(DEBUG_LOGIC_MESSAGE, LOGIC_MSG_REJECT);
				phy_phase(PHY_PHASE_BUS_FREE);
			}
			else if (message == LOGIC_MSG_NO_OPERATION)
			{
				// ignore this message completely
			}
			else
			{
				// message is not supported
				debug_dual(DEBUG_LOGIC_UNKNOWN_MESSAGE, message);
				logic_message_in(LOGIC_MSG_REJECT);
			}
		}
		else
		{
			/*
			 * This is an identity message. We need to verify that the message
			 * is sensible before we proceed, by checking if reserved bits are
			 * set or if the initiator is attempting to execute a target
			 * routine, which we don't have.
			 */
			if (message & 0x38)
			{
				logic_message_in(LOGIC_MSG_REJECT);
			}
			else if (last_identify != 0
					&& ((last_identify & 0x07) != (message & 0x07)))
			{
				// illegal to change this after received
				phy_phase(PHY_PHASE_BUS_FREE);
			}
			else
			{
				last_identify = message;
			}
		}
	}
	while (phy_is_active() && phy_is_atn_asserted());
	return message;
}

void logic_message_in(uint8_t message_in)
{
	if (! phy_is_active()) return;
	
	phy_phase(PHY_PHASE_MESSAGE_IN);
	last_message_in = message_in;
	phy_data_offer(message_in);
	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
}

uint8_t logic_command(uint8_t* command)
{
	if (! phy_is_active()) return 0;

	// switch to COMMAND and get opcode
	phy_phase(PHY_PHASE_COMMAND);
	command[0] = phy_data_ask();

	// check opcode group which defines length
	uint8_t cmd_count;
	if (command[0] < 0x20) // group 0
	{
		// group 0, 6 bytes
		cmd_count = 6;
	}
	else if (command[0] < 0x60) // group 1 or 2
	{
		cmd_count = 10;
	}
	else // not supported
	{
		cmd_count = 1;
	}

	// read command bytes
	for (uint8_t i = 1; i < cmd_count; i++)
	{
		command[i] = phy_data_ask();
	}

	// LUN handler code
	uint8_t lun = 0xFF;
	if (last_identify)
	{
		// if set, pull from IDENTIFY data
		lun = last_identify & 3;
	}
	else if (command[0] < 0x60)
	{
		// otherwise we pull from CDB
		lun = command[1] >> 5;
	}
	if (lun)
	{
		if (command[0] == 0x12) // INQUIRY
		{
			uint8_t alloc = command[4];
			if (alloc > 36)
				alloc = 36;
			logic_data_in_pgm(inquiry_data_illegal_lun, alloc);
			logic_status(LOGIC_STATUS_GOOD);
		}
		else if (command[0] == 0x03) // REQUEST SENSE
		{
			uint8_t alloc = command[4];
			if (alloc > 18)
				alloc = 18;
			logic_data_in_pgm(sense_data_illegal_lun, alloc);
			logic_status(LOGIC_STATUS_GOOD);
		}
		else
		{
			debug(DEBUG_LOGIC_BAD_LUN);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
		}
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		logic_done();
		return 0;
	}

	// command op out of range handler
	if (command[0] >= 0x60)
	{
		logic_cmd_illegal_op();
		logic_done();
		return 0;
	}

	// check control field for flag or link bits set, which we don't support
	if (cmd_count == 6 && command[5] & 3)
	{
		logic_cmd_illegal_arg(5);
	}
	else if (cmd_count == 10 && command[9] & 3)
	{
		logic_cmd_illegal_arg(9);
	}

	/*
	 * Sense data is cleared for everything except REQUEST SENSE. It has a
	 * system for clearing sense data in its own function.
	 */
	if (command[0] != 0x03)
	{
		devices[device_id].sense_valid = 0;
	}

	// move to MESSAGE OUT if required
	while (phy_is_atn_asserted())
	{
		logic_message_out();
	}

	return cmd_count;
}

void logic_status(uint8_t status)
{
	if (! phy_is_active()) return;

	phy_phase(PHY_PHASE_STATUS);
	phy_data_offer(status);
	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
}

uint8_t logic_data_out(uint8_t* data, uint8_t len)
{
	if (! phy_is_active()) return 0;

	phy_phase(PHY_PHASE_DATA_OUT);
	uint8_t i;
	for (i = 0; i < len; i++)
	{
		data[i] = phy_data_ask();
	}
	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
	return i;
}

void logic_data_out_dummy(uint8_t len)
{
	if (! phy_is_active()) return;

	phy_phase(PHY_PHASE_DATA_OUT);
	for (uint8_t i = 0; i < len; i++)
	{
		phy_data_ask();
	}
	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
}

void logic_data_in(uint8_t* data, uint8_t len)
{
	if (! phy_is_active()) return;

	phy_phase(PHY_PHASE_DATA_IN);
	for (uint8_t i = 0; i < len; i++)
	{
		phy_data_offer(data[i]);
	}
	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
}

void logic_data_in_pgm(const uint8_t* data, uint8_t len)
{
	if (! phy_is_active()) return;

	phy_phase(PHY_PHASE_DATA_IN);
	for (uint8_t i = 0; i < len; i++)
	{
		phy_data_offer(pgm_read_byte(&(data[i])));
	}
	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
}

/*
 * ============================================================================
 * 
 *   SENSE KEY / ERROR REPORTING FUNCTIONS
 * 
 * ============================================================================
 */

void logic_cmd_illegal_op(void)
{
	debug(DEBUG_LOGIC_BAD_CMD);

	// update sense data
	devices[device_id].sense_data[0] = 0x80;
	for (uint8_t i = 1; i < 18; i++)
	{
		devices[device_id].sense_data[i] = 0x00;
	}
	devices[device_id].sense_data[2] = SENSE_KEY_ILLEGAL_REQUEST;
	devices[device_id].sense_data[12] = 0x20;
	devices[device_id].sense_valid = 1;

	// terminate rest of command
	logic_status(LOGIC_STATUS_CHECK_CONDITION);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

void logic_cmd_illegal_arg(uint8_t position)
{
	debug(DEBUG_LOGIC_BAD_CMD_ARGS);

	// update sense data
	devices[device_id].sense_data[0] = 0x80;
	for (uint8_t i = 1; i < 18; i++)
	{
		devices[device_id].sense_data[i] = 0x00;
	}
	devices[device_id].sense_data[2] = SENSE_KEY_ILLEGAL_REQUEST;
	devices[device_id].sense_data[12] = 0x24;
	devices[device_id].sense_data[15] = 0xC0;
	devices[device_id].sense_data[17] = position;
	devices[device_id].sense_valid = 1;

	// terminate rest of command
	logic_status(LOGIC_STATUS_CHECK_CONDITION);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

void logic_set_sense(uint8_t sense, uint16_t asc)
{
	devices[device_id].sense_data[0] = 0x80;
	for (uint8_t i = 1; i < 18; i++)
	{
		devices[device_id].sense_data[i] = 0x00;
	}
	devices[device_id].sense_data[2] = sense;
	devices[device_id].sense_data[12] = (uint8_t) (asc >> 8);
	devices[device_id].sense_data[13] = (uint8_t) asc;
	devices[device_id].sense_valid = 1;
}

void logic_set_sense_pointer(uint8_t sense, uint16_t asc,
		uint8_t sksv, uint16_t ptr)
{
	logic_set_sense(sense, asc);
	devices[device_id].sense_data[15] = sksv;
	devices[device_id].sense_data[16] = (uint8_t) (ptr >> 8);
	devices[device_id].sense_data[17] = (uint8_t) ptr;
}

/*
 * ============================================================================
 * 
 *   COMMON OPERATION HANDLERS
 * 
 * ============================================================================
 */

void logic_request_sense(uint8_t* cmd)
{
	uint8_t alloc = cmd[4];
	if (alloc > 18)
		alloc = 18;
	
	if (devices[device_id].sense_valid)
	{
		logic_data_in(devices[device_id].sense_data, alloc);
		devices[device_id].sense_valid = 0;
	}
	else
	{
		logic_data_in_pgm(sense_data_no_sense, alloc);
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

void logic_send_diagnostic(uint8_t* cmd)
{
	uint16_t pl = (cmd[3] << 8) + cmd[4];
	if (pl > 0)
	{
		phy_phase(PHY_PHASE_DATA_OUT);
		for (uint16_t i = 0; i < pl; i++)
		{
			phy_data_ask();
		}
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}
