/*
 * Copyright (C) 2019-2021 saybur
 * Copyright (C) 2021 superjer2000
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
#include <util/delay.h>
#include "config.h"
#include "debug.h"
#include "link.h"
#include "logic.h"
#include "net.h"

/*
 * Maximum packet transfer length for all device types.
 * This is 14 bytes of MAC header plus up to 1500 bytes of payload.
 * See https://en.wikipedia.org/wiki/Ethernet_frame#Ethernet_II
 */
#define MAXIMUM_TRANSFER_LENGTH 1514

// the response we always send to RECEIVE DIAGNOSTIC RESULTS
#define DIAGNOSTIC_RESULTS_LENGTH 32
static const __flash uint8_t diagnostic_results[] = {
	0x43, 0x21, 0x53, 0x02, 0x40, 0x00, 0x00, 0x00,
	0x08, 0x89, 0x12, 0x04, 0x43, 0x02, 0x40, 0x00,
	0x00, 0x00, 0x08, 0x89, 0x12, 0x04, 0x43, 0x02,
	0x40, 0x00, 0x00, 0x00, 0x08, 0x89, 0x12, 0x04
};

// offsets to the MAC address information in the below data
#define MAC_ROM_OFFSET        36
#define MAC_CONFIG_OFFSET     56

// Nuvolink-compatible INQUIRY header response
static const __flash uint8_t inquiry_data_n[36] = {
	0x09, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00,
	'N', 'u', 'v', 'o', 't', 'e', 'c', 'h',
	'N', 'u', 'v', 'o', 'S', 'C', 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	'1', '.', '1', 'r'
};

// Daynaport-compatible INQUIRY header response
static const __flash uint8_t inquiry_data_d[255] = {
	0x03, 0x00, 0x01, 0x00, // 4 bytes
	0x1E, 0x00, 0x00, 0x00, // 4 bytes
	// Vendor ID (8 Bytes)
	'D','a','y','n','a',' ',' ',' ',
	//'D','A','Y','N','A','T','R','N',
	// Product ID (16 Bytes)
	'S','C','S','I','/','L','i','n',
	'k',' ',' ',' ',' ',' ',' ',' ',
	// Revision Number (4 Bytes)
	'1','.','4','a',
	// Firmware Version (8 Bytes)
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	// Data
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x80,0x80,0xBA, //16 bytes
	0x00,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x81,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00 //3 bytes
};

// the last-seen identify value
static uint8_t last_identify;

// buffers and headers used during the reading operation
static uint8_t read_buffer[6];

// the incremental packet counter used during packet read operations by
// the Nuvo protocol
static uint8_t rx_packet_id = 0;

// the value we use as a flag to see if we need to ask for a reselection
static uint8_t asked_for_reselection = 0;

// the "ROM" (configuration file) and dynamically configured MAC addresses
static uint8_t mac_rom[6];
static uint8_t mac_dyn[6];

// if true, allow in AppleTalk multicast traffic
static uint8_t allow_atalk;

/*
 * ============================================================================
 *   UTILITY FUNCTIONS
 * ============================================================================
 */


static void link_send_packet(uint16_t length)
{
	if (length > MAXIMUM_TRANSFER_LENGTH)
	{
		length = MAXIMUM_TRANSFER_LENGTH;
	}

	phy_phase(PHY_PHASE_DATA_OUT);
	net_stream_write(phy_data_ask_stream, length);
}

/*
 * ============================================================================
 *   OPERATION HANDLERS
 * ============================================================================
 * 
 * Each of these gets called from the _main() function to perform a particular
 * task on either the device or the PHY.
 */

static void link_cmd_send_diagnostic(uint8_t* cmd)
{
	// we basically ignore this command
	uint16_t alloc = (cmd[3] << 8) + cmd[4];
	phy_phase(PHY_PHASE_DATA_OUT);
	for (uint16_t i = 0; i < alloc; i++)
	{
		phy_data_ask();
	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_cmd_inquiry(uint8_t* cmd)
{
	/*
	 * We ignore page code and rely on allocation length only for deciding
	 * what to send, so find that first.
	 */
	uint16_t alloc = ((cmd[3] & 1) << 8) + cmd[4];
	if (debug_enabled())
	{
		debug(DEBUG_LINK_INQUIRY);
		if (debug_verbose())
		{
			debug_dual(alloc >> 8, alloc);
		}
	}

	// protect against 0-length allocation
	if (alloc == 0)
	{
		// allow flow-through to completion
	}
	// otherwise switch behavior based on device type
	else if (config_enet.type == LINK_NUVO)
	{
		phy_phase(PHY_PHASE_DATA_IN);

		// do first 36 bytes of pre-programmed INQUIRY
		uint8_t limit = 36;
		if (alloc < ((uint16_t) limit)) limit = (uint8_t) alloc;
		for (uint8_t i = 0; i < limit; i++)
		{
			phy_data_offer(inquiry_data_n[i]);
		}

		// next 60 bytes
		if (alloc >= 96)
		{
			// 6 bytes of ROM MAC
			for (uint8_t i = 0; i < 6; i++)
			{
				phy_data_offer(mac_rom[i]);
			}
			// 14 bytes of 0x00
			for (uint8_t i = 0; i < 14; i++)
			{
				phy_data_offer(0x00);
			}
			// 6 bytes of dynamic MAC
			for (uint8_t i = 0; i < 6; i++)
			{
				phy_data_offer(mac_dyn[i]);
			}
			// 34 bytes of 0x00
			for (uint8_t i = 0; i < 34; i++)
			{
				phy_data_offer(0x00);
			}
		}

		// next 196
		if (alloc >= 292)
		{
			// bus statistics
			phy_data_offer(0x04);
			phy_data_offer(0xD2);
			for (uint8_t i = 0; i < 86; i++)
			{
				phy_data_offer(0x00);
			}
			// bus errors
			phy_data_offer(0x09);
			phy_data_offer(0x29);
			for (uint8_t i = 0; i < 58; i++)
			{
				phy_data_offer(0x00);
			}
			// network statistics
			phy_data_offer(0x0D);
			phy_data_offer(0x80);
			for (uint8_t i = 0; i < 14; i++)
			{
				phy_data_offer(0x00);
			}
			// network errors
			phy_data_offer(0x11);
			phy_data_offer(0xD7);
			for (uint8_t i = 0; i < 30; i++)
			{
				phy_data_offer(0x00);
			}
		}
	}
	else if (config_enet.type == LINK_DAYNA)
	{
		phy_phase(PHY_PHASE_DATA_IN);

		if (alloc > 255) alloc = 255;
		for (uint16_t i = 0; i < alloc; i++)
		{
			phy_data_offer(inquiry_data_d[i]);
		}
	}

	// wrap up command regardless
	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

// TODO actually implement
static void link_cmd_change_mac(uint8_t* cmd)
{
	// fetch requested MAC address
	uint8_t alloc = 0;
	if (config_enet.type == LINK_NUVO)
	{
		alloc = cmd[4];
	}
	else if (config_enet.type == LINK_DAYNA)
	{
		alloc = 6;
	}
	if (alloc > 0)
	{
		phy_phase(PHY_PHASE_DATA_OUT);
		for (uint8_t i = 0; i < alloc; i++)
		{
			phy_data_ask();
		}
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_cmd_dayna_statistics(uint8_t* cmd)
{
	(void) cmd; // silence compiler

	phy_phase(PHY_PHASE_DATA_IN);

	// send MAC, then 3x DWORD 0x0 values
	phy_data_offer_bulk(mac_dyn, 6);
	for (uint8_t i = 0; i < 12; i++)
	{
		phy_data_offer(0x00);
	}

	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_cmd_nuvo_filter(uint8_t* cmd)
{
	uint8_t data[8] = {0}; // init to 0x00 on all
	uint8_t alloc = cmd[4];
	if (alloc > 8) alloc = 8;

	// get the hash bytes
	phy_phase(PHY_PHASE_DATA_OUT);
	for (uint8_t i = 0; i < alloc; i++)
	{
		data[i] = phy_data_ask();
	}

	/*
	 * We get 8 bytes, which I believe are targeted at the 8390's MAR0-7
	 * registers. The only patterns I've ever seen are all zeroes with the MSB
	 * of the last byte set (for MAR7, FB63, I think), or just all zeroes. This
	 * doesn't make much sense to me for multicast traffic. As a result, for
	 * now, we use that MSB bit as a flag to turn the ENC28J60 multicast filter
	 * on or off. At some point this should be revisited to cut down on traffic
	 * accepted by the device.
	 */
	if (data[7] & 0x80)
	{
		net_set_filter(NET_FILTER_MULTICAST);
		debug(DEBUG_LINK_FILTER_MULTICAST);
	}
	else
	{
		net_set_filter(NET_FILTER_BROADCAST);
		debug(DEBUG_LINK_FILTER_UNICAST);
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_cmd_dayna_filter(uint8_t* cmd)
{
	/*
	 * The DaynaPort driver sends command 0D to tell the unit what types of
	 * packets to accept. The length field of the 0D command indicates how
	 * many packet filters will be sent. The accept our MAC and broadcast
	 * seems to be generally of the form 01 00 5E 00 00 01 whereas accept
	 * AppleTalk packets will be 09 00 07 FF FF FF. The below loops through
	 * the packet filter information sent and if 09 (i.e. accept AppleTalk)
	 * is in any of the first byte positions of the packet filter
	 * information it allows Appletalk packets.
	 * 
	 * Note that emulating this behaviour does not seem to be strictly
	 * necessary. It IS necessary to read all of the bytes sent with the 0D
	 * command (otherwise Appletalk will fail to activate) but this behaviour
	 * is being emulated to match the actual system behaviour as closely as
	 * possible.
	 */

	uint16_t alloc = (cmd[3] << 8) + cmd[4];
	uint8_t param_set = 0;
	allow_atalk = 0;

	phy_phase(PHY_PHASE_DATA_OUT);
	for (uint16_t i = 0; i < alloc; i++)
	{
		// I've only ever seen two packet filters sent but this allows for 3
		// (i.e. activate Appletalk in position 1, 2 or 3)
		param_set = phy_data_ask();
		if ((i == 0 && param_set == 0x09)
				|| (i == 6 && param_set == 0x09)
				|| (i == 12 && param_set == 0x09))
		{
			allow_atalk = 1;
		}
	}

	if (allow_atalk)
	{
		net_set_filter(NET_FILTER_MULTICAST);
	}
	else
	{
		net_set_filter(NET_FILTER_BROADCAST);
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_cmd_nuvo_send(uint8_t* cmd)
{
	debug(DEBUG_LINK_TX_REQUESTED);

	uint16_t length = ((cmd[3] & 7) << 8) + cmd[4];
	link_send_packet(length);

	// indicate OK on TX
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_cmd_dayna_send(uint8_t* cmd)
{
	debug(DEBUG_LINK_TX_REQUESTED);

	uint16_t length = ((cmd[3]) << 8) + cmd[4];

	if (cmd[5] == 0x80)
	{
		// for the XX=80, all I have ever seen is where LLLL = PPPP
		phy_phase(PHY_PHASE_DATA_OUT);
		for (uint8_t i = 0; i < 4; i++) phy_data_ask();
		length -= 8;
	}

	link_send_packet(length);

	if (cmd[5] == 0x80)
	{
		// trash trailing 4x 0x00
		phy_phase(PHY_PHASE_DATA_OUT);
		for (uint8_t i = 0; i < 4; i++) phy_data_ask();
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_nuvo_read_packet(void)
{
	NetHeader* net_header = NULL;
	if (net_get(net_header))
	{
		// TODO ERROR HANDLE
	}

	/*
	 * Construct the flag byte and packet counter in the buffer at the correct
	 * position for sending over the PHY. Length is already in position 2 and 3
	 * from the ENC read.
	 */
	if ((net_header->stath) & 3)
	{
		// broadcast/multicast set
		read_buffer[0] = 0x21;
	}
	else
	{
		read_buffer[0] = 0x01;
	}
	read_buffer[1] = rx_packet_id++;
	read_buffer[2] = (uint8_t) (net_header->length);
	read_buffer[3] = (uint8_t) ((net_header->length) >> 8);

	phy_phase(PHY_PHASE_DATA_IN);
	phy_data_offer_bulk(read_buffer, 4);
	net_stream_read(phy_data_offer_stream_atn);
}

static uint16_t link_nuvo_message_out_post_rx(void)
{
	if (! phy_is_active()) return 0;

	phy_phase(PHY_PHASE_MESSAGE_OUT);

	uint8_t message = phy_data_ask();
	if (message == LOGIC_MSG_NO_OPERATION)
	{
		// normal post-RX response, no action needed
		return 0;
	}
	else if (message == 0x01)
	{
		// extended message
		uint8_t ext_len = phy_data_ask();
		if (ext_len == 3)
		{
			uint8_t ext_cmd = phy_data_ask();
			if (ext_cmd == 0xFF)
			{
				// short TX command, fetch the length
				uint16_t replay_len = phy_data_ask() << 8;
				replay_len += phy_data_ask();
				return replay_len;
			}
			else
			{
				// not a known extended message, debug and abort
				debug_dual(DEBUG_LOGIC_LINK_UNKNOWN_MESSAGE, 0x01);
				debug(0x03);
				debug(phy_data_ask());
				debug(phy_data_ask());
			}
		}
		else
		{
			// not a known extended message, debug and abort
			uint16_t ext_real_len = ext_len;
			if (ext_len == 0) ext_real_len = 256;
			debug_dual(DEBUG_LOGIC_LINK_UNKNOWN_MESSAGE, message);
			debug(ext_len);
			for (uint16_t i = 0; i < ext_real_len; i++)
			{
				debug(phy_data_ask());
			}
		}

		/*
		 * We go unexpectedly bus free if we didn't get the one supported
		 * extended message format.
		 */
		phy_phase(PHY_PHASE_BUS_FREE);
		return 0;
	}
	else
	{
		// message is not supported, just go bus free unexpectedly
		debug_dual(DEBUG_LOGIC_LINK_UNKNOWN_MESSAGE, message);
		phy_phase(PHY_PHASE_BUS_FREE);
		return 0;
	}
}

static void link_cmd_dayna_read(uint8_t* cmd)
{
	uint16_t allocation = ((cmd[3]) << 8) + cmd[4];
	if (allocation == 1)
	{
		debug(DEBUG_LINK_RX_SKIP);
		logic_status(LOGIC_STATUS_GOOD);
		logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
		return;
	}

	if (net_size() == 0)
	{
		// send "No Packets" message
		debug(DEBUG_LINK_RX_NO_DATA);
		phy_phase(PHY_PHASE_DATA_IN);
		for (uint8_t i = 0; i < 6; i++)
		{
			phy_data_offer(0x00);
		}
	}
	else
	{
		debug(DEBUG_LINK_RX_STARTING);

		NetHeader* net_header = NULL;
		if (net_get(net_header))
		{
			// TODO ERROR HANDLE
		}

		/*
		 * Move the length bytes into the correct position. The length
		 * bytes for both Daynaport and Nuvolink seem to be the same -
		 * length of the payload excluding length and flag bytes, except
		 * little endian vs big endian.
		 */
		read_buffer[0] = (uint8_t) ((net_header->length) >> 8);
		read_buffer[1] = (uint8_t) (net_header->length);
		read_buffer[2] = 0x00;
		read_buffer[3] = 0x00;
		read_buffer[4] = 0x00;

		/*
		 * Flagging another byte as waiting does make a big difference in
		 * transfer speeds (5.4mb FILE 3:03 VS 3:35). Per the driver docs,
		 * a 0x10 means there is another packet ready to be read.
		 * Presumably this means the driver doesn't just wait for it's
		 * polling interval to elapse before asking for another packet.
		 */
		if (net_size() > 1)
		{
			read_buffer[5] = 0x10;
		}
		else
		{
			read_buffer[5] = 0x00;
		}

		/*
		 * Ensure data length isn't more than the amount the driver
		 * said it can read (although this always seems to be 0x05F4
		 * which is 1524 which is 1518 + the 6 driver preamble bytes)
		 * 
		 * TODO HANDLE THIS CONDITION
		 */
/*		uint16_t 
		if (net_header.length > allocation - 6)
		{
			net_header.length = allocation - 6;
		}
*/

		phy_phase(PHY_PHASE_DATA_IN);
		// send the header
		for (uint16_t i = 0; i < 6; i++)
		{
			phy_data_offer(read_buffer[i]);
		}

		/*
		 * This pause necessary for the driver to properly read the
		 * packets. It might need to have time to parse the length out
		 * before reading the rest of it. 30us - 60us seemed to work
		 * reliably on my SE/30.  The SE did not work with 40 or 60 but
		 * did with 100.  There doesn't seem to be an significant
		 * performance penalty but there is a significant increase in
		 * compatibility.
		 */
		_delay_us(100);

		// send data
		net_stream_read(phy_data_offer_stream);
	}

	// Close out transaction
	if (phy_is_atn_asserted())
	{
		logic_message_out();
	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

/*
 * ============================================================================
 *   EXTERNAL FUNCTIONS
 * ============================================================================
 */

void link_init()
{
	if (config_enet.id < 7 && (config_enet.type == LINK_NUVO ||
			config_enet.type == LINK_DAYNA))
	{
		for (uint8_t i = 0; i < 6; i++)
		{
			mac_rom[i] = config_enet.mac[i];
			mac_dyn[i] = config_enet.mac[i];
		}
	}
	else
	{
		config_enet.id = 255;
		config_enet.type = LINK_NONE;
	}
}

void link_check_rx(void)
{
	// only the Nuvo protocol needs this
	if (config_enet.type != LINK_NUVO) return;

	// abort if we have not hit the disconnection delay
	if (! (PHY_TIMER_DISCON.INTFLAGS & PHY_TIMER_DISCON_OVF))
	{
		return;
	}

	if (ENC_PORT_EXT.IN & ENC_PIN_INT)
	{
		if (last_identify & 0x40)
		{
			if (! asked_for_reselection)
			{
				debug(DEBUG_LINK_RX_ASKING_RESEL);
				phy_reselect(config_enet.mask);
				asked_for_reselection = 1;
			}
		}
		else
		{
			// discard packet
			debug(DEBUG_LINK_RX_SKIP);
			net_skip();
		}
	}
}

uint8_t link_main(void)
{
	if (! logic_ready()) return 0;
	if (config_enet.id == 255) return 0;
	if (phy_is_continued())
	{
		// reselection is only supported on the Nuvo device
		if (config_enet.type != LINK_NUVO) return 0;

		/*
		 * Note for below: the driver appears to be picky about timing, so we
		 * insert various waits to make sure we don't get too far ahead of it.
		 */
		debug(DEBUG_LINK_RX_STARTING);
		logic_start(0, 0);

		/*
		 * We have reselected the initiator. First step is MESSAGE OUT. This
		 * will disconnect us automatically if we get the DISCONNECT message.
		 */
		//_delay_us(65);
		logic_message_out();

		/*
		 * Then we loop, sending packets to the initiator. This will continue
		 * as long as we have packets to send (or transmit), until we get
		 * disconnected. Once done, we clear our reselection flag and then
		 * disconnect ourselves.
		 */
		uint16_t txreq = 0;
		while (phy_is_active() && ((ENC_PORT_EXT.IN & ENC_PIN_INT) || txreq))
		{
			if (txreq)
			{
				// initiator wants to send a packet
				debug(DEBUG_LINK_SHORT_TX_START);
				link_send_packet(txreq);

				/*
				 * I've not been able to catch this behavior on the real
				 * device. The driver appears to dislike us doing anything
				 * but disconnecting at this point.
				 */
				logic_message_in(LOGIC_MSG_DISCONNECT);
				phy_phase(PHY_PHASE_BUS_FREE);
				debug(DEBUG_LINK_SHORT_TX_DONE);
			}
			else
			{
				// nothing to transmit, just send the pending packet
				debug(DEBUG_LINK_RX_PACKET_START);
				//_delay_us(150);
				link_nuvo_read_packet();
				debug(DEBUG_LINK_RX_PACKET_DONE);
				//_delay_us(100);
				txreq = link_nuvo_message_out_post_rx();
			}
		}
		asked_for_reselection = 0;
		if (phy_is_active())
		{
			//_delay_us(150);
			logic_message_in(LOGIC_MSG_DISCONNECT);
		}
		debug(DEBUG_LINK_RX_ENDING);
	}
	else
	{
		// normal selection by initiator
		logic_start(0, 1);
		uint8_t cmd[10];
		if (! logic_command(cmd)) return 1;

		uint8_t identify = logic_identify();
		if (identify != 0) last_identify = identify;

		if (config_enet.type == LINK_NUVO)
		{
			switch (cmd[0])
			{
				case 0x02: // "Reset Stats"
					// at the moment, we have no stats to reset
					logic_status(LOGIC_STATUS_GOOD);
					logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
					break;
				case 0x03: // REQUEST SENSE
					logic_request_sense(cmd);
					break;
				case 0x05: // "Send Packet"
					link_cmd_nuvo_send(cmd);
					break;
				case 0x06: // "Change MAC"
					link_cmd_change_mac(cmd);
					break;
				case 0x09: // "Set Filter"
					link_cmd_nuvo_filter(cmd);
					break;
				case 0x12: // INQUIRY
					link_cmd_inquiry(cmd);
					break;
				case 0x1C: // RECEIVE DIAGNOSTIC
					logic_data_in_pgm(diagnostic_results, DIAGNOSTIC_RESULTS_LENGTH);
					logic_status(LOGIC_STATUS_GOOD);
					logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
					break;
				case 0x1D: // SEND DIAGNOSTIC
					link_cmd_send_diagnostic(cmd);
					break;
				case 0x00: // TEST UNIT READY
				case 0x08: // GET MESSAGE(6)
				case 0x0A: // SEND MESSAGE(6)
				case 0x0C: // "Medium Sense"
					logic_status(LOGIC_STATUS_GOOD);
					logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
					break;
				default:
					logic_cmd_illegal_op(cmd[0]);
			}
		}
		else if (config_enet.type == LINK_DAYNA)
		{
			switch (cmd[0])
			{
				case 0x03: // REQUEST SENSE
					// per device description, alloc always mis-set to 0
					// and the device always responds with 9 bytes
					cmd[4] = 9;
					logic_request_sense(cmd);
					break;
				case 0x08: // GET MESSAGE(6)
					link_cmd_dayna_read(cmd);
					break;
				case 0x09: // "Retrieve Statistics"
					link_cmd_dayna_statistics(cmd);
					break;
				case 0x0A: // SEND MESSAGE(6)
					link_cmd_dayna_send(cmd);
					break;
				case 0x0C:
					if (cmd[5] == 0x40)  // "Change MAC"
					{
						link_cmd_change_mac(cmd);
					}
					else  // "Set Interface Mode"
					{
						// broadcast always on for us
						logic_status(LOGIC_STATUS_GOOD);
						logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
					}
					break;
				case 0x0D: // "Set multicast filtering (?)"
					link_cmd_dayna_filter(cmd);
					break;
				case 0x0E: // "Enable/Disable Interface"
					// leave on all the time
					logic_status(LOGIC_STATUS_GOOD);
					logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
					break;
				case 0x12: // INQUIRY
					link_cmd_inquiry(cmd);
					break;
				case 0x00: // TEST UNIT READY
					logic_status(LOGIC_STATUS_GOOD);
					logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
					break;
				default:
					logic_cmd_illegal_op(cmd[0]);
			}
		}
	}

	logic_done();
	return 1;
}
