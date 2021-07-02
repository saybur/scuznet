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
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay.h>
#include "config.h"
#include "enc.h"
#include "net.h"

/*
 * Defines the high byte value for end of the region where the receive buffer
 * is, starting at 0x0000 and extending through 0xXXFF, where 0xXX is this
 * value. All remaining space is allocated to the transmit buffer.
 */
#define NET_ERXNDH_VALUE        0x19

/*
 * Defines the starting point where packets to be transmitted are stored,
 * starting at 0xXX00 and extending through 0x1FFF, where 0xXX is this
 * value.
 */
#define ENC_XMIT_STARTH         (NET_ERXNDH_VALUE + 1)

/*
 * Flags within the NET_STATUS GPIOR
 */
#define NETSTAT_ERDPT_DIRTY     _BV(1)

/*
 * The headers for the most recently received packets.
 */
#define NET_HEADERS_SIZE        16 // must be a power of 2
#define NET_HEADERS_MASK        (NET_HEADERS_SIZE - 1);
static volatile NetHeader net_headers[NET_HEADERS_SIZE];

/*
 * Two arrays of equal length for the DMA units involved with reading packet
 * headers; one array is fixed for the write side (including the RBM command)
 * and the other is written into with received data. The read array is accessed
 * only from the interrupt context and is thus non-volatile.
 */
#define NET_DMA_BUFFER_LENGTH 7
static uint8_t dma_read_arr[7];
static const uint8_t dma_write_arr[] = { ENC_OP_RBM,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF // 6 bytes of status data
};

/*
 * Defines the array offsets within dma_read_arr where the various values live.
 * Note that byte 0 is the RBM response and is thus not useful.
 */
#define NET_HEAD_RXPTL          1
#define NET_HEAD_RXPTH          2
#define NET_HEAD_RXLENL         3
#define NET_HEAD_RXLENH         4
#define NET_HEAD_STATL          5
#define NET_HEAD_STATH          6

// the fixed CTRLA register contents used to start the DMA units
#define NET_DMA_CTRLA     (DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm);
#define NET_DMA_STARTCMD  0x82 // the above masks, plus ENABLE

/*
 * If there is a packet in the internal ring buffer, this advances to the
 * next packet atomically. Try to avoid doing this elsewhere, this operation
 * can be twitchy.
 */
static inline __attribute__((always_inline)) void advance_packet(void)
{
	if (NET_PACKET_SIZE) // safe, never decremented in ISR
	{
		ATOMIC_BLOCK(ATOMIC_FORCEON)
		{
			NET_PACKET_PTR = (NET_PACKET_PTR + 1) & NET_HEADERS_MASK;
			NET_PACKET_SIZE--;
		}
	}
}

/*
 * This mostly follows the steps in 7.1, amended by errata 12.
 */
static void net_transmit(uint16_t length)
{
	// per errata, reset TX subsystem to prevent possible stalled transmissions
	enc_cmd_set(ENC_ECON1, ENC_TXRST_bm);
	enc_cmd_clear(ENC_ECON1, ENC_TXRST_bm);
	enc_cmd_clear(ENC_EIR, ENC_TXERIF_bm);

	// program ETXST and ETXND
	enc_cmd_write(ENC_ETXSTL, 0x00);
	enc_cmd_write(ENC_ETXSTH, ENC_XMIT_STARTH);
	uint16_t end = (ENC_XMIT_STARTH << 8) + length - 1;
	enc_cmd_write(ENC_ETXNDL, (uint8_t) end);
	enc_cmd_write(ENC_ETXNDH, (uint8_t) (end >> 8));

	// clear EIR.TXIF and set EIE.TXIE and EIE.INTIE, which we do not do

	// set ECON1.TXRTS, which starts transmission
	enc_cmd_set(ENC_ECON1, ENC_TXRTS_bm);
}


/*
 * ============================================================================
 *   PUBLIC FUNCTIONS
 * ============================================================================
 */

/*
 * See section 6 in the datasheet for the process this code uses.
 */
void net_setup(uint8_t* mac)
{
	/*
	 * Setup DMA channels. We use two DMA units for handling packet header
	 * reading, which would otherwise occupy significant interrupt time.
	 */
	NET_DMA_WRITE.SRCADDR0 = (uint8_t) ((uint16_t) (&dma_write_arr));
	NET_DMA_WRITE.SRCADDR1 = (uint8_t) (((uint16_t) (&dma_write_arr)) >> 8);
	NET_DMA_WRITE.SRCADDR2 = 0;
	NET_DMA_WRITE.DESTADDR0 = (uint8_t) ((uint16_t) &ENC_USART);
	NET_DMA_WRITE.DESTADDR1 = (uint8_t) (((uint16_t) (&ENC_USART)) >> 8);
	NET_DMA_WRITE.DESTADDR2 = 0;
	NET_DMA_WRITE.ADDRCTRL = DMA_CH_SRCDIR_INC_gc | DMA_CH_SRCRELOAD_TRANSACTION_gc;
	NET_DMA_WRITE.CTRLA = NET_DMA_CTRLA;
	NET_DMA_WRITE.TRIGSRC = ENC_DMA_TX_TRIG;
	NET_DMA_WRITE.TRFCNT = NET_DMA_BUFFER_LENGTH;
	NET_DMA_READ.SRCADDR0 = (uint8_t) ((uint16_t) &ENC_USART);
	NET_DMA_READ.SRCADDR1 = (uint8_t) (((uint16_t) (&ENC_USART)) >> 8);
	NET_DMA_READ.SRCADDR2 = 0;
	NET_DMA_READ.DESTADDR0 = (uint8_t) ((uint16_t) (&dma_read_arr));
	NET_DMA_READ.DESTADDR1 = (uint8_t) (((uint16_t) (&dma_read_arr)) >> 8);
	NET_DMA_READ.DESTADDR2 = 0;
	NET_DMA_READ.ADDRCTRL = DMA_CH_DESTDIR_INC_gc | DMA_CH_DESTRELOAD_TRANSACTION_gc;
	NET_DMA_READ.CTRLA = NET_DMA_CTRLA;
	NET_DMA_READ.CTRLB = DMA_CH_TRNINTLVL_LO_gc;
	NET_DMA_READ.TRIGSRC = ENC_DMA_RX_TRIG;
	NET_DMA_READ.TRFCNT = NET_DMA_BUFFER_LENGTH;

	/*
	 * 6.1: setup RX buffer.
	 * 
	 * This was helpful for understanding what is going on:
	 * 
	 * https://www.microchip.com/forums/m632378.aspx
	 * 
	 * I find it easier to think of the RX buffer space as a proper
	 * circular buffer, with ERXWRPT as the head and ERXRDPT as the
	 * tail, per the post. We will set things appropriately.
	 * 
	 * Per errata 5, 0x0000 should be used as the RX start.
	 * Per errata 14, ERXRDPT must be odd.
	 * 
	 * To summarize pointers here:
	 * 
	 * ERXST is the start of the RX buffer space
	 * ERXND is the end of the RX buffer space
	 * ERDPT is the current read pointer
	 * ERXRDPT is the barrier value, past which the hardware will not add new
	 *   bytes - this must be updated whenever we read data.
	 */
	enc_cmd_write(ENC_ERXSTL, 0x00);
	enc_cmd_write(ENC_ERXSTH, 0x00);
	enc_cmd_write(ENC_ERXNDL, 0xFF);
	enc_cmd_write(ENC_ERXNDH, NET_ERXNDH_VALUE);
	enc_cmd_write(ENC_ERXRDPTL, 0xFF);
	enc_cmd_write(ENC_ERXRDPTH, NET_ERXNDH_VALUE);
	enc_cmd_write(ENC_ERDPTL, 0x00);
	enc_cmd_write(ENC_ERDPTH, 0x00);

	/*
	 * 6.3: setup filters
	 * 
	 * Only allow packets through that have a valid CRC and are directed at
	 * our MAC, at least for now.
	 */
	enc_cmd_write(ENC_ERXFCON, ENC_UCEN_bm
			| ENC_CRCEN_bm);

	/*
	 * 6.4: wait for oscillator startup.
	 * 
	 * Note: errata 2 has a warning about SPI restarts not handling the CLKRDY
	 * field correctly. We only reset via the external pin, so should not be
	 * affected, but if that assumption changes this needs a delay added.
	 */
	uint8_t flags;
	do
	{
		enc_cmd_read(ENC_ESTAT, &flags);
	}
	while (! (flags & ENC_CLKRDY_bm));

	/*
	 * 6.5: setup the MAC
	 * 
	 * We enable the device in half-duplex mode: we do not need the speed or
	 * complexity of full duplex, and with modern switches this should
	 * generally be invisible and not cause collision issues.
	 */
	enc_cmd_write(ENC_MACON1, ENC_MARXEN_bm);
	enc_cmd_write(ENC_MACON3, ENC_PADCFG0_bm | ENC_TXCRCEN_bm);
	enc_cmd_write(ENC_MACON4, ENC_DEFER_bm);
	enc_cmd_write(ENC_MABBIPG, 0x12);
	enc_cmd_write(ENC_MAIPGL, 0x12);
	enc_cmd_write(ENC_MAIPGH, 0x0C);
	// assign initial MAC address to what the configuration specifies
	enc_cmd_write(ENC_MAADR1, mac[0]);
	enc_cmd_write(ENC_MAADR2, mac[1]);
	enc_cmd_write(ENC_MAADR3, mac[2]);
	enc_cmd_write(ENC_MAADR4, mac[3]);
	enc_cmd_write(ENC_MAADR5, mac[4]);
	enc_cmd_write(ENC_MAADR6, mac[5]);

	/*
	 * 6.6: configure the PHY correctly.
	 * 
	 * Per errata 16, LED auto-polarity detection may not be reliable. We must
	 * force half-duplex. The errata document suggests a parallel resistor on
	 * the LED, which may be missing.
	 * 
	 * We delay between calls to allow the MIIM operation time to complete
	 * without having to probe MISTAT.BUSY.
	 */
	enc_phy_write(ENC_PHY_PHCON1, 0);
	_delay_us(12);
	enc_phy_write(ENC_PHY_PHCON2, ENC_HDLDIS_bm);
	_delay_us(12);

	/*
	 * Enable driving /INT, enable driving it when packets arrive, and enable
	 * packet reception itself.
	 */
	enc_cmd_write(ENC_EIE, ENC_PKTIE_bm | ENC_INTIE_bm);
	enc_cmd_set(ENC_ECON1, ENC_RXEN_bm);

	/*
	 * Enable interrupts when /E_INT is asserted (pin was set up earlier
	 * in enc.c).
	 */
	ENC_PORT_EXT.INTCTRL = PORT_INT0LVL_LO_gc;
}

NETSTAT net_start(void)
{
	/*
	 * Disable /E_INT reception interrupts to avoid that mucking with
	 * transactions the client wants to execute. This disables all port
	 * interrupts, per the configuration contract.
	 */
	ENC_PORT_EXT.INTCTRL = 0;

	/*
	 * Wait until the DMA units spin down. After that happens, they fire an
	 * ISR, clean up, and wait for /E_INT to go again; it never will due to
	 * above line disabling it, so after this clears we are safe to give
	 * control to clients.
	 */
	while (NET_DMA_READ.CTRLA & DMA_CH_ENABLE_bm);

	// ok to continue
	return NET_OK;
}

NETSTAT net_end(void)
{
	// update ERDPT
	if (NET_STATUS & NETSTAT_ERDPT_DIRTY)
	{
		// restore ERDPT to where the next inbound packet will be
		enc_cmd_write(ENC_ERDPTL, NET_ERDPTL_NEXT);
		enc_cmd_write(ENC_ERDPTH, NET_ERDPTH_NEXT);
	}

	// re-enable /E_INT packet reception interrupts
	// (only do this if we have space to store)
	if (NET_PACKET_SIZE < NET_HEADERS_SIZE - 1)
	{
		ENC_PORT_EXT.INTCTRL = PORT_INT0LVL_LO_gc;
	}

	// done
	return NET_OK;
}

NETSTAT net_get(volatile NetHeader* header)
{
	(void) header; // silence compiler
	if (NET_PACKET_SIZE > 0)
	{
		header = &(net_headers[NET_PACKET_PTR]);
		return NET_OK;
	}
	else
	{
		header = NULL;
		return NET_NO_DATA;
	}
}

NETSTAT net_set_filter(NETFILTER ftype)
{
	/*
	 * Per 7.2.1, we should disable packet reception, reset ERXFCON, then
	 * re-enable packet reception.
	 */
	enc_cmd_clear(ENC_ECON1, ENC_RXEN_bm);
	switch (ftype)
	{
		case NET_FILTER_MULTICAST:
			enc_cmd_write(ENC_ERXFCON, ENC_UCEN_bm
				| ENC_CRCEN_bm
				| ENC_MCEN_bm);
			break;
		case NET_FILTER_BROADCAST:
			enc_cmd_write(ENC_ERXFCON, ENC_UCEN_bm
				| ENC_BCEN_bm
				| ENC_CRCEN_bm);
			break;
		default:
			enc_cmd_write(ENC_ERXFCON, ENC_UCEN_bm
				| ENC_CRCEN_bm);
	}
	enc_cmd_set(ENC_ECON1, ENC_RXEN_bm);

	return NET_OK;
}

NETSTAT net_skip(void)
{
	if (net_size() > 0)
	{
		uint16_t erxrdpt = net_headers[NET_PACKET_PTR].next_packet - 1;
		enc_cmd_write(ENC_ERXRDPTL, (uint8_t) erxrdpt);
		enc_cmd_write(ENC_ERXRDPTH, (uint8_t) (erxrdpt >> 8));
		advance_packet();
	}
	return NET_OK;
}

NETSTAT net_stream_read(void (*func)(USART_t*, uint16_t))
{
	NETSTAT res;

	// get the current packet data
	NetHeader* header = NULL;
	res = net_get(header);
	if (res) return res;

	// start the read at the packet data
	NET_STATUS |= NETSTAT_ERDPT_DIRTY;
	enc_cmd_write(ENC_ERDPTL, header->packet_data);
	enc_cmd_write(ENC_ERDPTH, (header->packet_data) >> 8);
	enc_read_start();
	func(&ENC_USART, header->length);
	enc_data_end();

	// packet data is no longer useful, allow chip to overwrite it
	uint16_t erxrdpt = header->next_packet - 1;
	enc_cmd_write(ENC_ERXRDPTL, (uint8_t) erxrdpt);
	enc_cmd_write(ENC_ERXRDPTH, (uint8_t) (erxrdpt >> 8));

	// decrement packet counter
	advance_packet();

	// then we're done
	return NET_OK;
}

NETSTAT net_stream_write(void (*func)(USART_t*, uint16_t), uint16_t length)
{
	// TODO verify that no packet is waiting for transmission?

	// move TX pointer
	enc_cmd_write(ENC_EWRPTL, 0x00);
	enc_cmd_write(ENC_EWRPTH, ENC_XMIT_STARTH);
	// setup write
	enc_write_start();
	// write the status byte
	enc_swap(0x00);
	// transfer raw data
	func(&ENC_USART, length);
	enc_data_end();
	// instruct network chip to send the packet
	net_transmit(length + 1);

	return NET_OK;
}

/*
 * ============================================================================
 *   /INT HANDLER
 * ============================================================================
 */

// needed for getting raw addresses/values in the basic asm below
// https://gcc.gnu.org/onlinedocs/gcc-4.8.5/cpp/Stringification.html
#define XSTRINGIFY(s)   #s
#define STRINGIFY(s)    XSTRINGIFY(s)

/*
 * Triggered on the falling edge of /E_INT, which occurs when there are packets
 * waiting on the device to be read. This part of the interrupt just triggers
 * the DMA unit.
 * 
 * Packet reception happens quite a bit and requires communicating with the
 * ENC28J60 chip via SPI. This takes nontrivial amounts of time, during which
 * other activites are halted. Several things are done to ensure the interrupt
 * runs as fast as possible:
 * 
 * 1) /E_INT is only driven by packet reception. This removes the need to read
 *    EPKTCNT: if /E_INT is low, there is always a packet waiting.
 * 2) net_end() updates ERDPT to the position where the next packet will be
 *    before re-enabling this interrupt, removing the need to update those
 *    registers here: the RBM command will always pull from the right address.
 * 3) The DMA channels are preconfigured to use the same memory buffers and
 *    reload their initial state on completion. All we need to do is start
 *    them to get the transaction rolling.
 * 
 * This allows the ISR to be a simple sequence of the following:
 * 
 * 1) Drive /E_CS low to activate the chip.
 * 2) Start the writing DMA unit, which has a pre-baked RBM command to fetch
 *    the packet header.
 * 3) Start the reading DMA unit to get the data that will be arriving on the
 *    SPI bus.
 * 4) Stop further /E_INT interrupts until we're ready to handle them again.
 * 
 * This is easy enough that the ISR is done "naked," to avoid GCC generating
 * the usual preamble/postamble. All these instructions leave SREG alone, thus
 * it does not need to be saved.
 */
ISR(ENC_INT_ISR, ISR_NAKED)
{
	__asm__(
		// save contents of r16 to our scratch GPIO (1 cycle)
		"out " STRINGIFY(NET_SCRATCH_IOADDR) ", r16 \n\t"
		// load the bitmask for the /E_CS pin (1 cycle)
		"ldi r16, " STRINGIFY(ENC_PIN_CS) " \n\t"
		// drive /CS low (2 cycles)
		"sts " STRINGIFY(ENC_PORT_OUTCLR_ADDR) ", r16 \n\t"
		// load the DMA CTRLA data (1 cycle)
		"ldi r16, " STRINGIFY(NET_DMA_STARTCMD) " \n\t"
		// write CTRLA for the write channel, then the read channel (4 cycles)
		"sts " STRINGIFY(NET_DMA_WRITE_CTRLADDR) ", r16 \n\t"
		"sts " STRINGIFY(NET_DMA_READ_CTRLADDR) ", r16 \n\t"
		// stop further interrupt triggers for /E_INT (3 cycles)
		"ldi r16, 0x00 \n\t"
		"sts " STRINGIFY(ENC_PORT_EXT_ICTRL_ADDR) ", r16 \n\t"
		// restore r16 before returning (1 cycle)
		"in r16, " STRINGIFY(NET_SCRATCH_IOADDR) " \n\t"
		// return from the interrupt (4 cycles)
		"reti \n\t"
	);
}

/*
 * Invoked when the read DMA unit completes a transaction, meaning the array
 * has a packet header needing to be parsed.
 */
ISR(NET_DMA_READ_ISR)
{
	/*
	 * Decrement PKTDEC in ECON2. Done directly in ISR to bypass the call
	 * overhead to the enc.c library, as well as the irrelevant bank switch
	 * logic (ECON2 visible in all banks).
	 */
	ENC_PORT.OUTCLR = ENC_PIN_CS;
	ENC_USART.DATA = ENC_OP_BFS | ENC_ECON2;
	ENC_USART.DATA = 0;

	// while that's transmitting, decode the packet pointer and store results
	uint8_t pkt = (NET_PACKET_PTR + NET_PACKET_SIZE) & NET_HEADERS_MASK;
	volatile NetHeader* header = &(net_headers[pkt]);
	// first do the data offset for the current packet
	// see 7.2.3, though this simplifies by knowing ERXST == 0
	uint16_t pkt_data = (NET_ERDPTH_NEXT << 8) + NET_ERDPTL_NEXT;
	pkt_data += 6;
	if (((pkt_data & 0xFF00) >> 8) > (NET_ERXNDH_VALUE + 1))
	{
		pkt_data &= 0xFF;
	}
	header->packet_data = pkt_data;
	// next packet values
	NET_ERDPTH_NEXT = dma_read_arr[NET_HEAD_RXPTH];
	NET_ERDPTL_NEXT = dma_read_arr[NET_HEAD_RXPTL];
	header->next_packet =
			(dma_read_arr[NET_HEAD_RXPTH] << 8)
			+ dma_read_arr[NET_HEAD_RXPTL];
	// then remaining status values
	header->length =
			(dma_read_arr[NET_HEAD_RXLENH] << 8)
			+ dma_read_arr[NET_HEAD_RXLENL];
	header->statl = dma_read_arr[NET_HEAD_STATL];
	header->stath = dma_read_arr[NET_HEAD_STATH];

	// clear received USART bytes (garbage response)
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	ENC_USART.DATA;
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	ENC_USART.DATA;
	ENC_PORT.OUTSET = ENC_PIN_CS;

	// TODO NET_STATUS |= NETSTAT_ERDPT_DIRTY;????
	// maybe this whole thing isn't such a good idea...

	// increment the packet counter
	NET_PACKET_SIZE++;
	// if we still have header storage, re-enable packet reception ISR
	if (NET_PACKET_SIZE < NET_HEADERS_SIZE - 1)
	{
		ENC_PORT_EXT.INTCTRL = PORT_INT0LVL_LO_gc;
	}
}
