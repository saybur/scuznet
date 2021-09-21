/*
 * Copyright (C) 2014 Michael McMaster <michael@codesrc.com>
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

/*
 * This CD-ROM implementation is derived from the SCSI2SD project, also under
 * GPLv3. In particular the header/TOC handling was critical to making this
 * painless to implement on scuznet. Thank you Michael for your _awesome_
 * contributions to the community: SCSI2SDs are in several of my old systems
 * and they are just great. Information on SCSI2SD can be found here:
 * 
 * https://www.codesrc.com/mediawiki/index.php?title=SCSI2SD
 */

#include <stdlib.h>
#include <util/delay.h>
#include "lib/ff/ff.h"
#include "lib/ff/diskio.h"
#include "config.h"
#include "debug.h"
#include "logic.h"
#include "mode.h"
#include "cdrom.h"

/*
 * Defines the standard response we provide when asked to give INQUIRY data.
 */
static const __flash uint8_t cdrom_inquiry_data[] = {
	0x05, 0x80, 0x02, 0x02,
	0x1F, 0x00, 0x00, 0x00,
	' ', 's', 'c', 'u', 'z', 'n', 'e', 't',
	' ', 's', 'c', 'u', 'z', 'n', 'e', 't',
	' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 
	'0', '.', '1', 'a'
};

// the following TOC arrays are verbatim from SCSI2SD
static const __flash uint8_t toc_simple[] =
{
	0x00, // toc length, MSB
	0x12, // toc length, LSB
	0x01, // First track number
	0x01, // Last track number,
	// TRACK 1 Descriptor
	0x00, // reserved
	0x14, // Q sub-channel encodes current position, Digital track
	0x01, // Track 1,
	0x00, // Reserved
	0x00,0x00,0x00,0x00, // Track start sector (LBA)
	0x00, // reserved
	0x14, // Q sub-channel encodes current position, Digital track
	0xAA, // Leadout Track
	0x00, // Reserved
	0x00,0x00,0x00,0x00, // Track start sector (LBA)
};
static const __flash uint8_t toc_session[] =
{
	0x00, // toc length, MSB
	0x0A, // toc length, LSB
	0x01, // First session number
	0x01, // Last session number,
	// TRACK 1 Descriptor
	0x00, // reserved
	0x14, // Q sub-channel encodes current position, Digital track
	0x01, // First track number in last complete session
	0x00, // Reserved
	0x00,0x00,0x00,0x00 // LBA of first track in last session
};
static const uint8_t toc_full[] =
{
	0x00, // toc length, MSB
	0x44, // toc length, LSB
	0x01, // First session number
	0x01, // Last session number,
	// A0 Descriptor
	0x01, // session number
	0x14, // ADR/Control
	0x00, // TNO
	0xA0, // POINT
	0x00, // Min
	0x00, // Sec
	0x00, // Frame
	0x00, // Zero
	0x01, // First Track number.
	0x00, // Disc type 00 = Mode 1
	0x00,  // PFRAME
	// A1
	0x01, // session number
	0x14, // ADR/Control
	0x00, // TNO
	0xA1, // POINT
	0x00, // Min
	0x00, // Sec
	0x00, // Frame
	0x00, // Zero
	0x01, // Last Track number
	0x00, // PSEC
	0x00,  // PFRAME
	// A2
	0x01, // session number
	0x14, // ADR/Control
	0x00, // TNO
	0xA2, // POINT
	0x00, // Min
	0x00, // Sec
	0x00, // Frame
	0x00, // Zero
	0x79, // LEADOUT position BCD
	0x59, // leadout PSEC BCD
	0x74, // leadout PFRAME BCD
	// TRACK 1 Descriptor
	0x01, // session number
	0x14, // ADR/Control
	0x00, // TNO
	0x01, // Point
	0x00, // Min
	0x00, // Sec
	0x00, // Frame
	0x00, // Zero
	0x00, // PMIN
	0x00, // PSEC
	0x00,  // PFRAME
	// b0
	0x01, // session number
	0x54, // ADR/Control
	0x00, // TNO
	0xB1, // POINT
	0x79, // Min BCD
	0x59, // Sec BCD
	0x74, // Frame BCD
	0x00, // Zero
	0x79, // PMIN BCD
	0x59, // PSEC BCD
	0x74,  // PFRAME BCD
	// c0
	0x01, // session number
	0x54, // ADR/Control
	0x00, // TNO
	0xC0, // POINT
	0x00, // Min
	0x00, // Sec
	0x00, // Frame
	0x00, // Zero
	0x00, // PMIN
	0x00, // PSEC
	0x00  // PFRAME
};
static const __flash uint8_t header_simple[] =
{
	0x01, // 2048byte user data, L-EC in 288 byte aux field.
	0x00, // reserved
	0x00, // reserved
	0x00, // reserved
	0x00,0x00,0x00,0x00 // Track start sector (LBA or MSF)
};

/*
 * ============================================================================
 *   UTILITY FUNCTIONS
 * ============================================================================
 */

/*
 * Converts a LBA address to MSF format, needed when the MSF bit is set to 1 in
 * certain commands. This is copied from the SCSI2SD implementation.
 */
static void lba2msf(uint32_t lba, uint8_t* msf)
{
	msf[0] = 0; // reserved
	msf[3] = lba % 75; // M
	uint32_t rem = lba / 75;
	msf[2] = rem % 60; // S
	msf[1] = rem / 60;
}

// copied from the SCSI2SD implementation
static uint8_t from_bcd(uint8_t val)
{
	return ((val >> 4) * 10) + (val & 0xF);
}

/*
 * ============================================================================
 *   OPERATION HANDLERS
 * ============================================================================
 * 
 * Each of these gets called from the _main() function to perform a particular
 * task on either the device or the PHY.
 */

static void cdrom_cmd_test_unit_ready()
{
	// no test currently performed, always assume good
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void cdrom_cmd_inquiry(uint8_t id, uint8_t* cmd)
{
	(void) id; // silence compiler warning

	// limit size to the requested inquiry length
	uint8_t alloc = cmd[4];
	if (alloc > sizeof(cdrom_inquiry_data))
		alloc = sizeof(cdrom_inquiry_data);

	logic_data_in_pgm(cdrom_inquiry_data, alloc);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void cdrom_cmd_read(uint8_t id, uint8_t* cmd)
{
	LogicDataOp op;
	if (! logic_parse_data_op(cmd, &op))
	{
		debug(DEBUG_CDROM_INVALID_OPERATION);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}
	if (op.lba + op.length >= config_hdd[id].size)
	{
		debug(DEBUG_CDROM_SIZE_EXCEEDED);
		logic_set_sense(SENSE_ILLEGAL_LBA, config_hdd[id].size);
		logic_status(LOGIC_STATUS_CHECK_CONDITION);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}

	if (op.length > 0)
	{
		if (debug_enabled())
		{
			debug(DEBUG_CDROM_READ_STARTING);
			if (debug_verbose())
			{
				debug(DEBUG_HDD_LBA);
				debug(op.lba >> 24);
				debug(op.lba >> 16);
				debug(op.lba >> 8);
				debug(op.lba);
				debug(DEBUG_HDD_LENGTH);
				debug_dual(
					(uint8_t) (op.length >> 8),
					(uint8_t) op.length);
			}
		}
		phy_phase(PHY_PHASE_DATA_IN);

		// move to correct sector
		FRESULT res = f_lseek(&(config_hdd[id].fp), op.lba * 2048);
		if (res)
		{
			debug_dual(DEBUG_CDROM_MEM_SEEK_ERROR, res);
			logic_set_sense(SENSE_MEDIUM_ERROR, 0);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}

		// read from the card
		uint16_t act_len = 0;
		res = f_mread(&(config_hdd[id].fp), phy_data_offer_block,
				op.length * 4, &act_len);
		if (res || act_len != op.length * 4)
		{
			if (debug_enabled())
			{
				debug_dual(DEBUG_CDROM_MEM_READ_ERROR, res);
				if (debug_verbose())
				{
					debug(DEBUG_HDD_LENGTH);
					debug_dual(
						(uint8_t) (act_len >> 8),
						(uint8_t) act_len);
				}
			}
			logic_set_sense(SENSE_MEDIUM_ERROR, 0);
			logic_status(LOGIC_STATUS_CHECK_CONDITION);
			logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
			return;
		}
	}

	debug(DEBUG_CDROM_READ_OKAY);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void cdrom_cmd_read_capacity(uint8_t id, uint8_t* cmd)
{
	uint8_t resp[8];

	if (cmd[1] & 1)
	{
		// RelAdr set, we're not playing that game
		logic_cmd_illegal_arg(1);
	}
	else
	{
		// set number of sectors
		uint32_t last = config_hdd[id].size - 1;
		resp[0] = (uint8_t) (last >> 24);
		resp[1] = (uint8_t) (last >> 16);
		resp[2] = (uint8_t) (last >> 8);
		resp[3] = (uint8_t) last;

		// sectors fixed at 2048 bytes
		resp[4] = 0x00;
		resp[5] = 0x00;
		resp[6] = 0x08;
		resp[7] = 0x00;

		logic_data_in(resp, 8);
		logic_status(LOGIC_STATUS_GOOD);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
	}
}

static void cdrom_cmd_read_header(uint8_t id, uint8_t* cmd)
{
	(void) id; // silence compiler warning

	uint16_t alloc = (cmd[7] << 8) | cmd[8];
	if (alloc > sizeof(header_simple))
		alloc = sizeof(header_simple);

	logic_data_in_pgm(header_simple, alloc);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void cdrom_cmd_read_toc(uint8_t id, uint8_t* cmd)
{
	uint8_t msf = cmd[1] & 0x02 ? 1 : 0;
	uint8_t track = cmd[6];
	uint16_t alloc = (cmd[7] << 8) | cmd[8];
	uint8_t format = cmd[2] & 0x0F;

	// allocate enough memory for any possible response
	uint8_t resp[sizeof(toc_full)];
	uint8_t len = 0;

	// fill array or fail out
	switch (format)
	{
	case 0: // short TOC
		if (track > 1)
		{
			logic_cmd_illegal_arg(6);
			return;
		}

		for (uint8_t i = 0; i < sizeof(toc_simple); i++)
			resp[i] = toc_simple[i];
		len = sizeof(toc_simple);

		// replace start of leadout track
		uint32_t last = config_hdd[id].size - 1;
		if (msf)
		{
			lba2msf(last, resp + 16);
		}
		else
		{
			resp[16] = (uint8_t) (last >> 24);
			resp[17] = (uint8_t) (last >> 16);
			resp[18] = (uint8_t) (last >> 8);
			resp[19] = (uint8_t) last;
		}

		break;
	case 1: // session data
		for (uint8_t i = 0; i < sizeof(toc_session); i++)
			resp[i] = toc_session[i];
		len = sizeof(toc_session);

		break;
	case 2: // long TOC
	case 3: // long TOC w/ BCD
		if (track > 1)
		{
			logic_cmd_illegal_arg(6);
			return;
		}

		for (uint8_t i = 0; i < sizeof(toc_full); i++)
			resp[i] = toc_full[i];
		len = sizeof(toc_full);

		if (format == 3) // convert BCD
		{
			uint8_t desc = 4;
			while (desc < len)
			{
				for (uint8_t i = 0; i < 7; i++)
				{
					resp[desc + i] = from_bcd(resp[desc + 4 + i]);
				}
				desc += 11;
			}
		}

		break;
	default:
		logic_cmd_illegal_arg(2);
		return;
	}

	// provide data to the initiator
	if (len > alloc) len = alloc;
	logic_data_in(resp, len);
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

/*
 * ============================================================================
 *   EXTERNAL FUNCTIONS
 * ============================================================================
 */

uint8_t cdrom_main(uint8_t id)
{
	if (! logic_ready()) return 0;
	if (id >= HARD_DRIVE_COUNT) return 0;
	if (config_hdd[id].id == 255) return 0;
	if (config_hdd[id].mode != HDD_MODE_CDROM) return 0;

	uint8_t cmd[10];
	logic_start(id + 1, 1); // logic ID 0 for the link device, hence +1
	if (! logic_command(cmd)) return 1; // takes care of disconnection on fail

	switch (cmd[0])
	{
		case 0x12: // INQUIRY
			cdrom_cmd_inquiry(id, cmd);
			break;
		case 0x1A: // MODE SENSE(6)
		case 0x5A: // MODE SENSE(10)
			mode_sense(cmd, MODE_TYPE_CDROM, config_hdd[id].size);
			break;
		case 0x15: // MODE SELECT(6)
			mode_select(cmd);
			break;
		case 0x08: // READ(6) [not in spec, but we'll allow it]
		case 0x28: // READ(10)
			cdrom_cmd_read(id, cmd);
			break;
		case 0x25: // READ CAPACITY
			cdrom_cmd_read_capacity(id, cmd);
			break;
		case 0x44: // READ HEADER
			cdrom_cmd_read_header(id, cmd);
			break;
		case 0x43: // READ TOC
			cdrom_cmd_read_toc(id, cmd);
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
			cdrom_cmd_test_unit_ready(id, cmd);
			break;
		default:
			logic_cmd_illegal_op(cmd[0]);
	}
	logic_done();
	return 1;
}
