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
#include <avr/cpufunc.h>
#include <util/atomic.h>
#include <util/delay.h>
#include "config.h"
#include "debug.h"
#include "enc.h"
#include "net.h"

/*
 * Defines the high byte value for end of the region where the receive buffer
 * is, starting at 0x0000 and extending through 0xXXFF, where 0xXX is this
 * value. All remaining space is allocated to the transmit buffer.
 */
#define NET_ERXNDH_VALUE        0x13

/*
 * Defines the starting point where packets to be transmitted are stored.
 * There are two regions, each 1536 bytes in size reserved for this purpose.
 * They can be switched between in the transmit functions based on the provided
 * buffer selection value.
 */
#define NET_XMIT_BUF1           0x14
#define NET_XMIT_BUF2           0x1A

/*
 * Header for the received packet.
 */
volatile NetHeader net_header;

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
#define NET_DMA_CTRLA           (DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm);
#define NET_DMA_STARTCMD        0x84 // the above masks, plus ENABLE

// threshold after which the packet is considered to be late, in milliseconds
#define NET_TIMER_TX_LIMIT      250

// the eight hash table bytes
static uint8_t hash_table[8];

/*
 * This (re-)enables the /E_INT interrupt routine. That ISR is auto-disabled at
 * the start of each packet reception event. This should only be used when
 * the packet header buffer is empty.
 * 
 * This will clear the ISR flag before enabling the interrupt to avoid
 * spurrious triggers. /E_INT is level triggered when active, so the flag will
 * immediately become set again if /E_INT is still being driven.
 * 
 * For more details, see the ISR definition.
 */
static inline __attribute__((always_inline)) void net_enable_isr(void)
{
	ENC_PORT_EXT.INTFLAGS = PORT_INT0IF_bm;
	ENC_PORT_EXT.INTCTRL = PORT_INT0LVL_LO_gc;
}

/*
 * Resets the packet transmission timer. This assumes a fixed clock speed of
 * 32MHz, which may not be the case in the future.
 */
static inline __attribute__((always_inline)) void net_timer_reset(void)
{
	NET_TIMER.CTRLA = TC_CLKSEL_OFF_gc;
	NET_TIMER.CTRLFSET = TC_CMD_RESET_gc;
	// clk/1024 is 32us/tick, * 32 = 1.024ms
	NET_TIMER.PER = (NET_TIMER_TX_LIMIT << 5);
	NET_TIMER.CTRLA = TC_CLKSEL_DIV1024_gc;
}

/*
 * Used by net.h calls that may be invoked when the /E_INT interrupt is live,
 * and thus will be vulnerable to having their communications trashed. This
 * turns off that interrupt and waits for a pending DMA transaction to end.
 */
static void net_lock(void)
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
}

/*
 * Complement to the above call. This restarts interrupts and should be
 * invoked in all function exits where net_lock() was used.
 */
static void net_unlock(void)
{
	if (! net_pending())
	{
		net_enable_isr();
	}
}

/*
 * Moves ERXRDPT (and optionally ERDPT) to the given address, following
 * the requirements of erratas 5 and 14.
 */
static void net_move_rxpt(uint16_t next, uint8_t move_erdpt)
{
	if (move_erdpt)
	{
		enc_cmd_write(ENC_ERDPTL, (uint8_t) next);
		enc_cmd_write(ENC_ERDPTH, (uint8_t) (next >> 8));
	}
	if(next == 0)
	{
		enc_cmd_write(ENC_ERXRDPTL, 0xFF);
		enc_cmd_write(ENC_ERXRDPTH, NET_ERXNDH_VALUE);
	}
	else
	{
		next--;
		enc_cmd_write(ENC_ERXRDPTL, (uint8_t) next);
		enc_cmd_write(ENC_ERXRDPTH, (uint8_t) (next >> 8));
	}
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
	enc_cmd_write(ENC_MAMXFLL, 0xEE);
	enc_cmd_write(ENC_MAMXFLH, 0x05); // MTU == 1518 (0x5EE)
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
	 * Clear the hash table bytes in case there are any set.
	 */
	net_hash_filter_reset();

	/*
	 * Enable interrupts when /E_INT is asserted (pin was set up earlier
	 * in enc.c).
	 */
	net_enable_isr();
}

void net_hash_filter_add(uint8_t* mac)
{
	// setup the CRC unit
	CRC.CTRL = CRC_RESET_RESET1_gc;
	_NOP();
	CRC.CTRL = CRC_CRC32_bm | CRC_SOURCE_IO_gc;
	_NOP();
	// load the MAC
	for (uint8_t i = 0; i < 6; i++)
	{
		CRC.DATAIN = *(mac + i);
	}
	// mark complete
	CRC.STATUS |= CRC_BUSY_bm;

	/*
	 * The CRC needs to be reversed and complemented. We just pull out the
	 * six bits we need from 28:23.
	 */
	uint8_t h = ~(CRC.CHECKSUM0); // 24:31
	uint8_t l = ~(CRC.CHECKSUM1); // 16:23
	uint8_t ptr = 0;
	if (h & 0x08) ptr |= 0x20;
	if (h & 0x10) ptr |= 0x10;
	if (h & 0x20) ptr |= 0x08;
	if (h & 0x40) ptr |= 0x04;
	if (h & 0x80) ptr |= 0x02;
	if (l & 0x01) ptr |= 0x01;

	// set the correct bit
	uint8_t idx = ptr >> 3;
	uint8_t bit = 1 << (ptr & 0x07);
	hash_table[idx] |= bit;
}

void net_hash_filter_set(uint8_t idx, uint8_t value)
{
	hash_table[idx & 0x07] = value;
}

void net_hash_filter_reset(void)
{
	for (uint8_t i = 0; i < 8; i++) hash_table[i] = 0;
}

NETSTAT net_set_filter(uint8_t ftype)
{
	uint8_t v = ENC_CRCEN_bm;
	if (ftype & NET_FILTER_UNICAST) v |= ENC_UCEN_bm;
	if (ftype & NET_FILTER_BROADCAST) v |= ENC_BCEN_bm;
	if (ftype & NET_FILTER_MULTICAST) v |= ENC_MCEN_bm;
	if (ftype & NET_FILTER_HASH) v |= ENC_HTEN_bm;

	/*
	 * Per 7.2.1, we should disable packet reception, reset ERXFCON, then
	 * re-enable packet reception.
	 */
	net_lock();
	enc_cmd_clear(ENC_ECON1, ENC_RXEN_bm);
	if (ftype & NET_FILTER_HASH)
	{
		enc_cmd_write(ENC_EHT0, hash_table[0]);
		enc_cmd_write(ENC_EHT1, hash_table[1]);
		enc_cmd_write(ENC_EHT2, hash_table[2]);
		enc_cmd_write(ENC_EHT3, hash_table[3]);
		enc_cmd_write(ENC_EHT4, hash_table[4]);
		enc_cmd_write(ENC_EHT5, hash_table[5]);
		enc_cmd_write(ENC_EHT6, hash_table[6]);
		enc_cmd_write(ENC_EHT7, hash_table[7]);
	}
	enc_cmd_write(ENC_ERXFCON, v);
	enc_cmd_set(ENC_ECON1, ENC_RXEN_bm);
	net_unlock();
	return NETSTAT_OK;
}

NETSTAT net_skip(void)
{
	if (! net_pending())
	{
		return NETSTAT_NO_DATA;
	}

	net_move_rxpt(net_header.next_packet, 1);
	NET_FLAGS &= ~NETFLAG_PKT_PENDING;
	net_enable_isr();
	return NETSTAT_OK;
}

NETSTAT net_stream_read(uint16_t (*func)(USART_t*, uint16_t))
{
	if (! net_pending())
	{
		return NETSTAT_NO_DATA;
	}

	// ERDPT is already at the start of packet data, just start reading
	enc_read_start();
	uint16_t rxlen = net_header.length;
	uint16_t remaining = func(&ENC_USART, rxlen);
	enc_data_end();

	/*
	 * Move to the next packet. If the packet length was even, and we sent all
	 * bytes requested, there is no need to move ERDPT.
	 */
	if ((! (rxlen & 1)) && remaining == 0)
	{
		net_move_rxpt(net_header.next_packet, 0);
	}
	else
	{
		net_move_rxpt(net_header.next_packet, 1);
	}

	// no longer have a packet pending
	NET_FLAGS &= ~NETFLAG_PKT_PENDING;
	net_enable_isr();

	// report result of operation
	if (remaining)
	{
		return NETSTAT_TRUNCATED;
	}
	else
	{
		return NETSTAT_OK;
	}
}

NETSTAT net_stream_write(void (*func)(USART_t*, uint16_t), uint16_t length)
{
	net_lock();

	// shift the write pointer to the free buffer
	uint8_t txsel = (NET_FLAGS & NETFLAG_TXBUF);
	uint8_t txh = txsel ? NET_XMIT_BUF1 : NET_XMIT_BUF2;
	enc_cmd_write(ENC_EWRPTL, 0x00);
	enc_cmd_write(ENC_EWRPTH, txh);
	// setup write
	enc_write_start();
	// write the status byte
	enc_swap(0x00);
	// transfer raw data
	func(&ENC_USART, length);
	enc_data_end();

	// done
	net_unlock();
	return NETSTAT_OK;
}

/*
 * This mostly follows the steps in 7.1, amended by errata 12.
 */
NETSTAT net_transmit(uint16_t length)
{
	/*
	 * Ensure the previous transmission completed. Per errata 13, when we are
	 * operating in half-duplex mode there are false/late collision issues that
	 * need to be worked around. This is handled in the below call.
	 */
	while (NET_FLAGS & NETFLAG_TXREQ)
	{
		net_transmit_check();
	}

	// reserve
	net_lock();

	// verify we're pointing to the free buffer
	uint8_t txsel = (NET_FLAGS & NETFLAG_TXBUF);
	uint8_t txh = txsel ? NET_XMIT_BUF1 : NET_XMIT_BUF2;

	// per errata 12, reset TX to prevent stalled transmissions
	enc_cmd_set(ENC_ECON1, ENC_TXRST_bm);
	enc_cmd_clear(ENC_ECON1, ENC_TXRST_bm);
	enc_cmd_clear(ENC_EIR, ENC_TXIF_bm | ENC_TXERIF_bm);

	/*
	 * Program ETXST and ETXND for the correct data buffer.
	 * 
	 * The given length is the total length of the packet. With the extra
	 * status byte on the front of the buffer, this is OK to use as the
	 * addition for the ending pointer.
	 */
	enc_cmd_write(ENC_ETXSTL, 0x00);
	enc_cmd_write(ENC_ETXSTH, txh);
	uint16_t end = (txh << 8) + length;
	enc_cmd_write(ENC_ETXNDL, (uint8_t) end);
	enc_cmd_write(ENC_ETXNDH, (uint8_t) (end >> 8));

	// set ECON1.TXRTS, which starts transmission
	enc_cmd_set(ENC_ECON1, ENC_TXRTS_bm);
	NET_FLAGS |= NETFLAG_TXREQ;

	// reset the information for next time
	txsel ? (NET_FLAGS &= ~NETFLAG_TXBUF) : (NET_FLAGS |= NETFLAG_TXBUF);
	net_timer_reset();

	// done
	net_unlock();
	return NETSTAT_OK;
}

NETSTAT net_transmit_check(void)
{
	uint8_t reset = 0;
	uint8_t rd;

	if (NET_FLAGS & NETFLAG_TXREQ)
	{
		net_lock();
		enc_cmd_read(ENC_EIR, &rd);
		if (rd & ENC_TXERIF_bm)
		{
			// packet transmission failed due to error, re-transmit
			debug(DEBUG_NET_TX_ERROR_RETRANSMIT);
			reset = 1;
		}
		else if (rd & ENC_TXIF_bm)
		{
			// packet transmission OK!
			NET_FLAGS &= ~NETFLAG_TXREQ;
		}
		else
		{
			/*
			 * Packet has yet to transmit. While this isn't mentioned in the
			 * errata, I've had issues where this condition continues
			 * indefinitely, so we have a grace period timer running that helps
			 * keep track of when the system should be reset to have another
			 * go at it.
			 */
			if (NET_TIMER.INTFLAGS & NET_TIMER_OVF)
			{
				// queue up a reset
				debug(DEBUG_NET_TX_TIMEOUT_RETRANSMIT);
				reset = 1;
			}
		}

		// if needed, reset the transmission system for another try
		if (reset)
		{
			// reset TX subsystem
			enc_cmd_set(ENC_ECON1, ENC_TXRST_bm);
			enc_cmd_clear(ENC_ECON1, ENC_TXRST_bm);
			enc_cmd_clear(ENC_EIR, ENC_TXIF_bm | ENC_TXERIF_bm);
			// set ECON1.TXRTS again
			enc_cmd_set(ENC_ECON1, ENC_TXRTS_bm);
			// and reset the monitor timer
			net_timer_reset();
		}

		net_unlock();
	}

	return NETSTAT_OK;
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
 * 
 * TODO: this isn't terribly efficient with the memory copying between the
 * array and the struct; this might be better to just do with the array.
 */
ISR(NET_DMA_READ_ISR)
{
	// drive /CS high from the ongoing read that just finished, and wait at
	// least 50ns before driving it low again for the next operation.
	ENC_PORT.OUTSET = ENC_PIN_CS;
	_NOP();
	_NOP();

	/*
	 * Decrement PKTDEC in ECON2. Done directly in ISR to bypass the call
	 * overhead to the enc.c library, as well as the irrelevant bank switch
	 * logic (ECON2 visible in all banks).
	 */
	ENC_PORT.OUTCLR = ENC_PIN_CS;
	ENC_USART.DATA = ENC_OP_BFS | ENC_ECON2;
	ENC_USART.DATA = ENC_PKTDEC_bm;

	/*
	 * While the USART is cooking we can do some work.
	 */

	// per 5.14.2, flag triggering this ISR is not auto-cleared
	// we don't use the error flags, so ignore those
	NET_DMA_READ.CTRLB |= DMA_CH_TRNIF_bm;

	// decode the packet pointer and store
	net_header.next_packet =
			(dma_read_arr[NET_HEAD_RXPTH] << 8)
			+ dma_read_arr[NET_HEAD_RXPTL];
	net_header.length =
			(dma_read_arr[NET_HEAD_RXLENH] << 8)
			+ dma_read_arr[NET_HEAD_RXLENL];
	net_header.statl = dma_read_arr[NET_HEAD_STATL];
	net_header.stath = dma_read_arr[NET_HEAD_STATH];

	// clear received USART bytes (garbage response) and wrap up command
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	ENC_USART.DATA;
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	ENC_USART.DATA;
	ENC_PORT.OUTSET = ENC_PIN_CS;

	// flag that we have a packet waiting
	NET_FLAGS |= NETFLAG_PKT_PENDING;
}
