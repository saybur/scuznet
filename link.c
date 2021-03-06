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
#include "enc.h"
#include "link.h"
#include "logic.h"
#include "net.h"

#ifdef ENC_ENABLED

#define MAXIMUM_TRANSFER_LENGTH 1522

// the response we always send to RECEIVE DIAGNOSTIC RESULTS
#define DIAGNOSTIC_RESULTS_LENGTH 32
const uint8_t diagnostic_results[] PROGMEM = {
	0x43, 0x21, 0x53, 0x02, 0x40, 0x00, 0x00, 0x00,
	0x08, 0x89, 0x12, 0x04, 0x43, 0x02, 0x40, 0x00,
	0x00, 0x00, 0x08, 0x89, 0x12, 0x04, 0x43, 0x02,
	0x40, 0x00, 0x00, 0x00, 0x08, 0x89, 0x12, 0x04
};

// offsets to the MAC address information in the below data
#define MAC_ROM_OFFSET        36
#define MAC_CONFIG_OFFSET     56

static uint8_t inquiry_data[96] = {
	// bytes 0-35 are the standard inquiry data
	0x09, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00,
	'N', 'u', 'v', 'o', 't', 'e', 'c', 'h',
	'N', 'u', 'v', 'o', 'S', 'C', 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	'1', '.', '1', 'r',
	// 36-95 are the extended page 2 stuff
	// ROM MAC
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 14 bytes of 0x00
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// configured MAC
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 34 bytes of 0x00
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
};

/*
 * The target mask for this device, and the IDs that will block reselection if
 * detected during reselection on the bus.
 */
static uint8_t target_mask;

// the selector for the TX buffer space
static uint8_t txbuf;

// the last-seen identify value
static uint8_t last_identify;

// buffers and headers used during the reading operation
static uint8_t read_buffer[19];
static NetHeader net_header;

// the counter used during packet read operations
static uint8_t rx_packet_id = 0;

// the value we use as a flag to see if we need to ask for a reselection
static uint8_t asked_for_reselection = 0;

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

static void link_send_diagnostic(uint8_t* cmd)
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

static void link_inquiry(uint8_t* cmd)
{
	// we ignore page code and rely on allocation length only for deciding
	// what to send, so find that first
	uint16_t alloc = ((cmd[3] & 1) << 8) + cmd[4];
	if (alloc >= 96)
	{
		phy_phase(PHY_PHASE_DATA_IN);
		for (uint8_t i = 0; i < 96; i++)
		{
			phy_data_offer(inquiry_data[i]);
		}
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
		if (phy_is_atn_asserted())
		{
			logic_message_out();
		}
	}
	else
	{
		logic_data_in(inquiry_data, (uint8_t) alloc);
	}

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
	debug(DEBUG_LINK_INQUIRY);
}

static void link_change_mac(uint8_t* cmd)
{
	// TODO actually implement

	uint8_t alloc = cmd[4];
	phy_phase(PHY_PHASE_DATA_OUT);
	for (uint8_t i = 0; i < alloc; i++)
	{
		phy_data_ask();
	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_set_filter(uint8_t* cmd)
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
	 * 
	 * Per 7.2.1, we should disable packet reception, reset ERXFCON, then
	 * re-enable packet reception.
	 */
	enc_cmd_clear(ENC_ECON1, ENC_RXEN_bm);
	if (data[7] & 0x80)
	{
		// accept unicast and multicast
	//	enc_cmd_write(ENC_ERXFCON, ENC_UCEN_bm
	//		| ENC_CRCEN_bm
	//		| ENC_MCEN_bm);
	
		// DEBUG: try taking everything
		enc_cmd_write(ENC_ERXFCON, 0);
		debug(DEBUG_LINK_RX_FILTER_MULTICAST);
	}
	else
	{
		// just accept unicast
		enc_cmd_write(ENC_ERXFCON, ENC_UCEN_bm
			| ENC_CRCEN_bm);
		debug(DEBUG_LINK_RX_FILTER_UNICAST);
	}
	enc_cmd_set(ENC_ECON1, ENC_RXEN_bm);

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_send_packet(uint8_t* cmd)
{
	debug(DEBUG_LINK_TX_REQUESTED);

	// parse the packet header, limiting total length to 2047
	uint16_t length = ((cmd[3] & 7) << 8) + cmd[4];
	if (length > MAXIMUM_TRANSFER_LENGTH) length = MAXIMUM_TRANSFER_LENGTH;

	// get devices in the right mode for a data transfer
	phy_phase(PHY_PHASE_DATA_OUT);
	net_move_txpt(txbuf);
	enc_write_start();

	// write the status byte
	while (! (ENC_USART.STATUS & USART_DREIF_bm));
	ENC_USART.DATA = 0x00;

	// transfer raw data, which happens to match the needed format (yay)
	phy_data_ask_stream(&ENC_USART, length);

	// instruct network chip to send the packet
	while (! (ENC_USART.STATUS & USART_TXCIF_bm));
	enc_data_end();
	net_transmit(txbuf, length + 1);
	txbuf = txbuf ? 0 : 1;

	// indicate OK on RX
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_read_packet_header(void)
{
	enc_read_start();
	ENC_USART.DATA = 0xFF;
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	ENC_USART.DATA; // junk RBM response
	for (uint8_t i = 0; i < 6; i++)
	{
		ENC_USART.DATA = 0xFF;
		while (! (ENC_USART.STATUS & USART_RXCIF_bm));
		read_buffer[i] = ENC_USART.DATA;
	}
	net_process_header(read_buffer, &net_header);
}

static void link_read_packet(void)
{
	link_read_packet_header();

	/*
	 * Construct the flag byte and packet counter in the buffer at the correct
	 * position for sending over the PHY. Length is already in position 2 and 3
	 * from the ENC read.
	 */
	if (net_header.stath & 3)
	{
		// broadcast/multicast set
		read_buffer[0] = 0x21;
	}
	else
	{
		read_buffer[0] = 0x01;
	}
	read_buffer[1] = rx_packet_id++;

	/*
	 * Read in another 14 bytes. The driver seems to require 19 bytes quickly,
	 * then waits for 67us, then gets more data. We'll provide the 19 bytes via
	 * SRAM, then pass off to the USART handler for the rest of the data.
	 */
	for (uint8_t i = 4; i < 18; i++)
	{
		ENC_USART.DATA = 0xFF;
		while (! (ENC_USART.STATUS & USART_RXCIF_bm));
		read_buffer[i] = ENC_USART.DATA;
	}

	// send initial data
	uint16_t usart_len = net_header.length - 14;
	phy_phase(PHY_PHASE_DATA_IN);
	_delay_us(6);
	phy_data_offer_bulk(read_buffer, 18);

	/*
	 * Per above note, this is when the driver pauses. Change to direct 
	 * USART / PHY control. Byte is already waiting in the USART per PHY 
	 * contract. We call the version that checks /ATN, and end early if we
	 * see it, discarding the rest of the packet.
	 */
	phy_data_offer_stream_atn(&ENC_USART, usart_len);
	enc_data_end();

	// decrement the packet counter and make sure the read pointer is set right
	net_move_rxpt(net_header.next_packet, 1);
	enc_cmd_set(ENC_ECON2, ENC_PKTDEC_bm);
}

static void link_skip_packet(void)
{
	link_read_packet_header();
	enc_data_end();

	// then skip it without reading
	net_move_rxpt(net_header.next_packet, 1);
	enc_cmd_set(ENC_ECON2, ENC_PKTDEC_bm);
}

/*
 * ============================================================================
 * 
 *   EXTERNAL FUNCTIONS
 * 
 * ============================================================================
 */

void link_init(uint8_t* mac, uint8_t target)
{
	target_mask = target;

	// assign MAC address information into the INQUIRY data.
	uint8_t idx = MAC_ROM_OFFSET;
	for (uint8_t i = 0; i < 6; i++)
	{
		inquiry_data[idx++] = mac[i];
	}
	idx = MAC_CONFIG_OFFSET;
	for (uint8_t i = 0; i < 6; i++)
	{
		inquiry_data[idx++] = mac[i];
	}
}

void link_check_rx(void)
{
	// abort if we have not hit the disconnection delay
	if (! (PHY_TIMER_DISCON.INTFLAGS & PHY_TIMER_DISCON_OVF))
	{
		return;
	}

	if (ENC_PORT.IN & ENC_PIN_INT)
	{
		if (last_identify & 0x40)
		{
			if (! asked_for_reselection)
			{
				debug(DEBUG_LINK_RX_ASKING_RESEL);
				phy_reselect(target_mask);
				asked_for_reselection = 1;
			}
		}
		else
		{
			debug(DEBUG_LINK_RX_SKIP);
			link_skip_packet();
		}
	}
}

void link_main(void)
{
	if (! logic_ready()) return;
	if (phy_is_continued())
	{
		/*
		 * Note for below: the driver appears to be picky about timing, so we
		 * insert various waits to make sure we don't get too far ahead of it.
		 */
		debug(DEBUG_LINK_RX_STARTING);
		logic_start(1, 0);

		/*
		 * We have reselected the initiator. First step is MESSAGE OUT. This
		 * will disconnect us automatically if we get the DISCONNECT message.
		 */
		//_delay_us(65);
		logic_message_out();

		/*
		 * Then we loop as long as we have packets to send and we haven't
		 * been disconnected from the initiator. Once done, we clear our
		 * reselection flag and then disconnect ourselves.
		 */
		while (phy_is_active() && (ENC_PORT.IN & ENC_PIN_INT))
		{
			debug(DEBUG_LINK_RX_PACKET_START);
			//_delay_us(150);
			link_read_packet();
			debug(DEBUG_LINK_RX_PACKET_DONE);
			//_delay_us(100);
			logic_message_out();
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
		logic_start(1, 1);
		uint8_t cmd[10];
		if (! logic_command(cmd)) return;

		uint8_t identify = logic_identify();
		if (identify != 0) last_identify = identify;

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
				link_send_packet(cmd);
				break;
			case 0x06: // "Change MAC"
				link_change_mac(cmd);
				break;
			case 0x09: // "Set Filter"
				link_set_filter(cmd);
				break;
			case 0x12: // INQUIRY
				link_inquiry(cmd);
				break;
			case 0x1C: // RECEIVE DIAGNOSTIC
				logic_data_in_pgm(diagnostic_results, DIAGNOSTIC_RESULTS_LENGTH);
				logic_status(LOGIC_STATUS_GOOD);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				break;
			case 0x1D: // SEND DIAGNOSTIC
				link_send_diagnostic(cmd);
				break;
			case 0x00: // TEST UNIT READY
			case 0x08: // GET MESSAGE(6)
			case 0x0A: // SEND MESSAGE(6)
			case 0x0C: // "Medium Sense"
				logic_status(LOGIC_STATUS_GOOD);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				break;
			default:
				logic_cmd_illegal_op();
		}
	}

	logic_done();
}

#endif /* ENC_ENABLED */
