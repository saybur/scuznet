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

#define MAXIMUM_TRANSFER_LENGTH 1514 // This is the max length of ethernet data 1500 + MAC address data (12) plus 2 for length/type.  For the read routine it maxes out at 1518 as that includes the 4 CRC bytes which are appended by the ENC.

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

extern uint8_t mac_address[6];

static uint8_t inquiry_data[255] = {
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
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x80,0x80,0xBA, //16 bytes
	0x00,0x00,0xC0,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x81,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00,    0x00,0x00,0x00,0x00, //16 bytes
	0x00,0x00,0x00 //3 bytes
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
static uint8_t read_buffer[6];
static NetHeader net_header;


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

static void activate_appletalk(uint8_t* cmd)
{
	// Unsure what this daynaport msg is but it happens after atalk turns on, so may be message to turn on Multicast.  We essentially ignore by reading the number of bytes specified in bytes 4 and 5 of the command but not doing anything with it.  Broadcast/multicast is always on.

	uint16_t alloc = (cmd[3] << 8) + cmd[4];
	phy_phase(PHY_PHASE_DATA_OUT);
	for (uint16_t i = 0; i < alloc; i++)
	{
		phy_data_ask(); // Currently being sent to debug to see what it is. // THIS SEEMS NECESSARY TO ACTIVATE APPLETALK

	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
}

static void link_inquiry(uint8_t* cmd)
{
	
	// we ignore page code and rely on allocation length only for deciding
	// what to send, so find that first
	uint16_t alloc = ((cmd[3] & 1) << 8) + cmd[4];

	//if (cmd[1] & 1) jgk_debug('B'); // EVPD bit set  I havne't ever seen this set.
	if (alloc > 255) alloc = 255;
	
		phy_phase(PHY_PHASE_DATA_IN);
		for (uint8_t i = 0; i < alloc; i++)
		{
			phy_data_offer(inquiry_data[i]);
		}
		if (phy_is_atn_asserted())
		{
			logic_message_out();
		}
	

	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
	debug(DEBUG_LINK_INQUIRY);
	
}

static void link_change_mac(void)
{
	// This doesn't really seem to be a thing with the Daynaport as the software doesn't seem to allow a permanent MAC change so not implemented.
}



void link_set_filter(void)
{
	// The below sets the ENC filter to only allow packets that 1) have correct CRC, are directed to our MAC address, OR are broadcast OR are multicast
	enc_cmd_clear(ENC_ECON1, ENC_RXEN_bm);
	enc_cmd_write(ENC_ERXFCON, 163); //  163 = 10100011
	enc_cmd_set(ENC_ECON1, ENC_RXEN_bm);
}

static void link_send_packet(uint8_t* cmd)
{
	debug(DEBUG_LINK_TX_REQUESTED);


	// parse the packet header, limiting total length to 2047 JGK note, masking 7 with cmd3 sets the maximum value of length to 2047 (0000011111111111 = 2047) - note that this probably isn't necessary given the if statement afterwards.  I'm not sure what the significance of 2,047 is as max packet lengths seem to be 1500 bytes.
	uint16_t length = ((cmd[3]) << 8) + cmd[4]; // JGK 	uint16_t length = ((cmd[3] & 7) << 8) + cmd[4];
	if (length > MAXIMUM_TRANSFER_LENGTH) length = MAXIMUM_TRANSFER_LENGTH;

	// get devices in the right mode for a data transfer

	net_move_txpt(txbuf);
	enc_write_start();
	phy_phase(PHY_PHASE_DATA_OUT);

	// write the status byte
	while (! (ENC_USART.STATUS & USART_DREIF_bm));
	ENC_USART.DATA = 0x00;
	
	
	
	/*
	Per Anodyne spec:
	Command:  0a 00 00 LL LL XX (LLLL is data length, XX = 80 or 00)
	if XX = 00, LLLL is the packet length, and the data to be sent
	must be an image of the data packet
	. if XX = 80, LLLL is the packet length + 8, and the data to be
	sent is:
	PP PP 00 00 XX XX XX ... 00 00 00 00
	where:
	PPPP      is the actual (2-byte big-endian) packet length
	XX XX ... is the actual packet
	
	
	Note that for packet send type 0x00 the length is in position 3 and 4 for the Daynaport just as it is for the Nuvolink so the length calculation above is OK
	I have never seen an xx = 00 packet.  And for the XX=80, all I have ever seen is where LLLL = PPPP
	
	*/
	
	if (cmd[5]==0x00) // Simpler packet format I've never seen this.
	{
		phy_data_ask_stream(&ENC_USART, length);
		while (! (ENC_USART.STATUS & USART_TXCIF_bm));
		enc_data_end();
		net_transmit(txbuf, length +1); //length + 1
		txbuf = txbuf ? 0 : 1;
	}
	else if (cmd[5]==0x80)
	{
		
		phy_data_ask_stream_0x80(&ENC_USART, length+8); // Read the extra 8 bytes
		
		while (! (ENC_USART.STATUS & USART_TXCIF_bm));
		enc_data_end();
		net_transmit(txbuf, length +1); //length + 1
		txbuf = txbuf ? 0 : 1;	
	}
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
		//jgk_debug(read_buffer[i]);
	}
	net_process_header(read_buffer, &net_header);
}

static void link_read_packet(uint8_t* cmd) // JGK bringing in the cmd so we can parse out the transfer length which is the max size of transfer the driver will allow.
{
	
	uint16_t transfer_length = ((cmd[3]) << 8) + cmd[4]; 
	uint16_t data_length;
	uint8_t packet_counter;

	
		if (transfer_length == 1) 
		{
			
				logic_status(LOGIC_STATUS_GOOD);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				return;
		}
	
		if (ENC_PORT.IN & ENC_PIN_INT) // Is there a packet?
		{	
			enc_cmd_read(ENC_EPKTCNT, &packet_counter); // Read number of read packets from packet counter  APPEARS THAT THIS NEEDS TO OCCUR BEFORE THE HEADER IS READ AND THE ENC SWITCHES TO READ_START MODE.
		
			link_read_packet_header();
		
			 // JGK:  Move the length bytes into the correct position for the Dayna Port.  The length bytes for both Daynaport and Nuvolink seem to be the same - length of the payload excluding length and flag bytes, except little endian vs big endian
			read_buffer[0] = read_buffer[3];
			read_buffer[1] = read_buffer[2];
		
			data_length = (uint16_t)((read_buffer[0]) << 8) + (uint16_t)read_buffer[1];

			read_buffer[2] = 0x00;
			read_buffer[3] = 0x00;
			read_buffer[4] = 0x00;

			 // FLAGGING ANOTHER BYTE AS WAITING DOES MAKE A BIG DIFFERENCE IN TRANSFER SPEEDS (5.4mb FILE 3:03 VS 3:35).  Per the driver docs, a 0x10 means there is another packet ready to be read.  Presumably this means the driver doesn't just wait for it's polling interval to elapse before asking for another packet.
			if (packet_counter>1) //  Is there another packet waiting?
			{
				read_buffer[5] = 0x10;
			}
			else
			{
				read_buffer[5] = 0x00; 
			}
	
		
			if (data_length > 1518) data_length=1518; // 1500 packet bytes + 4 CRC bytes + 12 Address Bytes + 2 Len/Type bytes
			if (data_length > (transfer_length-6)) data_length = transfer_length-6; // Ensure data length isn't more than the amount the driver said it can read (although this always seems to be 0x05F4 which is 1524 which is 1518 + the 6 driver preamble bytes)
		
			phy_phase(PHY_PHASE_DATA_IN);
		
			// Send the header
			//jgk_debug('/');

			for (uint16_t i = 0; i < 6; i++) 
			{
				phy_data_offer(read_buffer[i]);
			
				//jgk_debug(read_buffer[i]);
			}
		
			_delay_us(100); // This pause necessary for the driver to properly read the packets. It might need to have time to parse the length out before reading the rest of it. 30us - 60us seemed to work reliably on my SE/30.  The SE did not work with 40 or 60 but did with 100.  There doesn't seem to be an significant performance penalty but there is a significant increase in compatibility.  
		
			phy_data_offer_stream(&ENC_USART,data_length);  // Call the version that doesn't check for _atn as it seems to run about 15% faster for some reason
			
			
			enc_data_end();
			net_move_rxpt(net_header.next_packet, 1);
			enc_cmd_set(ENC_ECON2, ENC_PKTDEC_bm); 
			

	
		}
		else // No packet is waiting so just send 0's for LL and Flag Fields.
		{
			read_buffer[0] = 0x00;
			read_buffer[1] = 0x00;
			read_buffer[2] = 0x00;
			read_buffer[3] = 0x00;
			read_buffer[4] = 0x00;
			read_buffer[5] = 0x00;
		
			phy_phase(PHY_PHASE_DATA_IN);
			for (uint16_t i = 0; i < 6; i++)
			{
				phy_data_offer(read_buffer[i]);
				//jgk_debug(read_buffer[i]);
			}
	
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
 * 
 *   EXTERNAL FUNCTIONS
 * 
 * ============================================================================
 */

void link_init(uint8_t target)
{
	// Used for reselection - Reselection code doesn't seem to cause any issues so not removed.
	target_mask = target;
	

}


void retrieve_statistics(void)
{
	
	// Per Anodyne spec, the 5th byte is always 0x12.  I have only ever seen CMD 0x09 called with byte 5 == 0x012 so this isn't checked and a 0x09 CMD always returns the below.

	
	phy_phase(PHY_PHASE_DATA_IN);
	
	// Send MAC address from config read at net_setup
	phy_data_offer(mac_address[0]);
	phy_data_offer(mac_address[1]);
	phy_data_offer(mac_address[2]);
	phy_data_offer(mac_address[3]);
	phy_data_offer(mac_address[4]);
	phy_data_offer(mac_address[5]);

	// Send back 3  2-byte statistics all set to 0

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

void link_request_sense(void)
{
	
	// JGK This has not been observed by me yet.  This just returns a simple sense response based on what I read of the SCSI spec at https://www.staff.uni-mainz.de/tacke/scsi/SCSI2-06.html
	phy_phase(PHY_PHASE_DATA_IN);
	
	
	phy_data_offer(0x70);
	for (uint8_t i = 0; i < 8; i++)
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





void link_main(void)
{
	
	if (! logic_ready()) return;
	
		// normal selection by initiator
		logic_start(1, 1);
		uint8_t cmd[10];
		if (! logic_command(cmd)) return;
	
		uint8_t identify = logic_identify();
		if (identify != 0) last_identify = identify;
		
		//jgk_debug('/');
	//jgk_debug(cmd[0]);
	//	jgk_debug(cmd[1]);
	//	jgk_debug(cmd[2]);
	//	jgk_debug(cmd[3]);
	//	jgk_debug(cmd[4]);
	//	jgk_debug(cmd[5]);
		
		switch (cmd[0])
		
		{
			
			case 0x03: // REQUEST SENSE
				link_request_sense();
				break;
			case 0x0A: // "Send Packet"
				link_send_packet(cmd);
				break;
			
				link_change_mac();
				break;
			case 0x09: 
				
				retrieve_statistics();
		
				break;
			case 0x08: // "Read Packet from device"
				
				link_read_packet(cmd);
				break;	
			case 0x12: // INQUIRY
				
				link_inquiry(cmd);
				break;
			case 0x0D: // Unsure what this daynaport msg is but it happens after atalk turns on, so may be message to turn on Multicast.  We essentially ignore
				activate_appletalk(cmd);
				break;
			
			case 0x00: // TEST UNIT READY
			case 0x02: // From Nuvolink - not observed with Daynaport so essentially ignored but left in as doesn't seem to cause any issue.
			case 0x0E: // Observed with Daynaport, Seems to be enable/disable interface per Anodyne spec but I always have interface enabled.
			case 0x0C: // Per Anodyne spec, this is set interface mode/change Mac.  Doesn't seem to be a permanent change so ignored.
			case 0x06: // From Nuvolink - not observed with Daynaport so essentially ignored but left in as doesn't seem to cause any issue.
			case 0x1C: // From Nuvolink - not observed with Daynaport so essentially ignored but left in as doesn't seem to cause any issue.
			case 0x1D: // From Nuvolink - not observed with Daynaport so essentially ignored but left in as doesn't seem to cause any issue.
			case 0x80: // From Nuvolink - not observed with Daynaport so essentially ignored but left in as doesn't seem to cause any issue.
				logic_status(LOGIC_STATUS_GOOD);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				break;

			default:
				logic_cmd_illegal_op();
		}

		
	logic_done();

}

#endif /* ENC_ENABLED */
