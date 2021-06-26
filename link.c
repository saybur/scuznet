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

#ifdef DAYNAPORT_ETHERNET
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
static uint8_t dest_mac_addr[6];
static NetHeader net_header;

static uint8_t allowAppleTalk = 0;

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

static void daynaPort_setnetwork(uint8_t* cmd)
{
	/*
	The DaynaPort driver sends command 0D to tell the unit what types of packets to accept.  The length field of the 0D command indicates how many packet filters will be sent.  The accept our MAC and broadcast seems to be generally of the form 01 00 5E 00 00 01 whereas
	accept AppleTalk packets will be 09 00 07 FF FF FF.  The below loops through the packet filter information sent and if 09 (i.e. accept AppleTalk) is in any of the first byte positions of the packet filter information it allows Appletalk packets.
	Note that emulating this behaviour does not seem to be strictly necessary.  It IS necessary to read all of the bytes sent with the 0D command (otherwise Appletalk will fail to activate) but this behaviour is being emulated to match the actual system behaviour
	as closely as possible.
	
	*/
	uint16_t alloc = (cmd[3] << 8) + cmd[4];
	uint8_t paramSet=0;
	allowAppleTalk = 0;
	phy_phase(PHY_PHASE_DATA_OUT);
	for (uint16_t i = 0; i < alloc; i++)
	{
		//phy_data_ask(); // Left here in case decide to revert to prior behaviour of sending the packet filter data to oblivion and not using to turn on or off appletalk filtering.
		paramSet = phy_data_ask();
		if ((i==0 && paramSet==0x09) || (i==6 && paramSet==0x09) || (i==12 && paramSet==0x09)) allowAppleTalk =1; // I've only ever seen two packet filters sent but this allows for 3 (i.e. activate appletalk in position 1, 2 or 3)
	}
	logic_status(LOGIC_STATUS_GOOD);
	logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
	link_set_filter();
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
	
	enc_cmd_clear(ENC_ECON1, ENC_RXEN_bm);
	if (allowAppleTalk == 1)
	{ 
		enc_cmd_write(ENC_ERXFCON, 163); //  163 = 10100011  Sets the ENC filter to only allow packets that 1) have correct CRC, are directed to our MAC address, OR are broadcast OR are multicast
	}
	else
	{
		enc_cmd_write(ENC_ERXFCON, 161); //  160 = 10100001  Sets the ENC filter to only allow packets that 1) have correct CRC, are directed to our MAC address, OR are broadcast.  Multicast turned off if ATalk is turned off.
	}
	
	enc_cmd_set(ENC_ECON1, ENC_RXEN_bm);
	
}

static void link_send_packet(uint8_t* cmd)
{
	debug(DEBUG_LINK_TX_REQUESTED);


	
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
	}
	net_process_header(read_buffer, &net_header);
}


static void link_read_dest_addr(void)
{
	ENC_USART.DATA = 0xFF;

	for (uint8_t i = 0; i < 6; i++)
	{
		ENC_USART.DATA = 0xFF;
		while (! (ENC_USART.STATUS & USART_RXCIF_bm));
		dest_mac_addr[i] = ENC_USART.DATA;
	}
	
}

static void link_read_packet(uint8_t* cmd) // JGK bringing in the cmd so we can parse out the transfer length which is the max size of transfer the driver will allow.
{
	
	uint16_t transfer_length = ((cmd[3]) << 8) + cmd[4]; 
	uint16_t data_length;
	uint8_t total_packets=0;
	uint8_t packet_counter=0;
	uint8_t found_packet;
		
		if (transfer_length == 1) 
		{
			
				logic_status(LOGIC_STATUS_GOOD);
				logic_message_in(LOGIC_MSG_COMMAND_COMPLETE);
				return;
		}
		
		enc_cmd_read(ENC_EPKTCNT, &total_packets); // Read number of read packets from packet counter  APPEARS THAT THIS NEEDS TO OCCUR BEFORE THE HEADER IS READ AND THE ENC SWITCHES TO READ_START MODE.
		
		if (total_packets == 0) // No packets in ENC buffer so send No Packets message to Daynaport driver
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
			}
		}
		else
		{

			
		
			found_packet = 0;
			for (packet_counter=total_packets;packet_counter>0;packet_counter--) // Is there a packet?
			{
				link_read_packet_header();
				link_read_dest_addr(); // Destination address is read to allow firmware to filterout all of the extraneous multicast packets that seem to be prevalent on modern networks EXCEPT for Appletalk multicast.
				if (dest_mac_addr[0] == mac_address[0] && dest_mac_addr[1] == mac_address[1] && dest_mac_addr[2] == mac_address[2] && dest_mac_addr[3] == mac_address[3] && dest_mac_addr[4] == mac_address[4] && dest_mac_addr[5] == mac_address[5]) found_packet = 1;
				if (dest_mac_addr[0] == 0xFF && dest_mac_addr[1] == 0xFF && dest_mac_addr[2] == 0xFF && dest_mac_addr[3] == 0xFF && dest_mac_addr[4] == 0xFF && dest_mac_addr[5] == 0xFF) found_packet = 1;
				if (allowAppleTalk==1) if (dest_mac_addr[0] == 0x09 && dest_mac_addr[1] == 0x00 && dest_mac_addr[2] == 0x07 && dest_mac_addr[3] == 0x00 && dest_mac_addr[4] == 0x00) found_packet = 1;
				if (allowAppleTalk==1) if (dest_mac_addr[0] == 0x09 && dest_mac_addr[1] == 0x00 && dest_mac_addr[2] == 0x07 && dest_mac_addr[3] == 0xFF && dest_mac_addr[4] == 0xFF && dest_mac_addr[5] == 0xFF) found_packet = 1;
				
				if (found_packet == 0) // Did not find a packet that matches our filters (own MAC address, ATalk Multicast or Broadcast so move to next packet
				{
					enc_data_end();
					net_move_rxpt(net_header.next_packet, 1);
					enc_cmd_set(ENC_ECON2, ENC_PKTDEC_bm);
				}
				else
				{
					break;
				}

			}

	
			if (found_packet == 0) // Looped through all packets in ENC, didn't find a filter match and so sending no packet's pending message to Daynaport driver.
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
				}
				
			}
			else // Found a packet to send
			{
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

			for (uint16_t i = 0; i < 6; i++) 
			{
				phy_data_offer(read_buffer[i]); // Send initial bytes required by the driver
			}
		
			_delay_us(100); // This pause necessary for the driver to properly read the packets. It might need to have time to parse the length out before reading the rest of it. 30us - 60us seemed to work reliably on my SE/30.  The SE did not work with 40 or 60 but did with 100.  There doesn't seem to be an significant performance penalty but there is a significant increase in compatibility.  
		
			
			for (uint16_t i = 0; i < 6; i++)
				{
					phy_data_offer(dest_mac_addr[i]); // Send Dest MAC address information already read for filter purposes
					
		
				}		
			phy_data_offer_stream(&ENC_USART,data_length-6);  // Call the version that doesn't check for _atn as it seems to run about 15% faster for some reason.  Send packet, substracting 6 from the length to account for MAC Address info already sent.	
			enc_data_end();
			net_move_rxpt(net_header.next_packet, 1);
			enc_cmd_set(ENC_ECON2, ENC_PKTDEC_bm);
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
		
		
		switch (cmd[0])
		
		{
			
			case 0x03: // REQUEST SENSE
				link_request_sense();
				break;
			case 0x0A: // "Send Packet"
				link_send_packet(cmd);
				break;
			case 0x0C: // Per Anodyne spec, this is set interface mode/change Mac.  Doesn't seem to be a permanent change so ignored.
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
			case 0x0D: // Set packet filtering
				daynaPort_setnetwork(cmd);
				break;
			
			case 0x00: // TEST UNIT READY
			case 0x02: // From Nuvolink - not observed with Daynaport so essentially ignored but left in as doesn't seem to cause any issue.
			case 0x0E: // Observed with Daynaport, Seems to be enable/disable interface per Anodyne spec but I always have interface enabled.
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
#else

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

static void link_send_packet(uint16_t length)
{
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
}

static void link_send_packet_cmd(uint8_t* cmd)
{
	debug(DEBUG_LINK_TX_REQUESTED);

	uint16_t length = ((cmd[3] & 7) << 8) + cmd[4];
	link_send_packet(length);

	// indicate OK on TX
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

static uint16_t link_message_out_post_rx(void)
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

	if (ENC_PORT_EXT.IN & ENC_PIN_INT)
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
				link_read_packet();
				debug(DEBUG_LINK_RX_PACKET_DONE);
				//_delay_us(100);
				txreq = link_message_out_post_rx();
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
				link_send_packet_cmd(cmd);
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
#endif /* DAYNAPORT_ETHERNET */
#endif /* ENC_ENABLED */
