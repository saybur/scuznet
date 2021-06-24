/*
 * Copyright (C) 2014-2020 ChaN
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
 * Most of functions here are derived from pfsample.zip and ffsample.zip,
 * available at the following links:
 * 
 * http://elm-chan.org/fsw/ff/00index_p.html
 * http://elm-chan.org/fsw/ff/00index_e.html
 * 
 * The original license for these is:
 * 
 * > These sample projects for FatFs module are free software and there is NO
 * > WARRANTY. You can use, modify and redistribute it for personal, non-profit
 * > or commercial use without any restriction UNDER YOUR RESPONSIBILITY.
 */

#include <avr/io.h>
#include <stdlib.h>
#include <util/atomic.h>
#include <util/delay.h>
#include "./lib/pff/pff.h"
#include "./lib/pff/diskio.h"
#include "config.h"

#define cs_assert()         (MEM_PORT.OUTCLR = MEM_PIN_CS)
#define cs_release()        (MEM_PORT.OUTSET = MEM_PIN_CS)
#define is_cs_asserted()    (! (MEM_PORT.IN & MEM_PIN_CS))
#define data_not_ready()    (! (MEM_USART.STATUS & USART_RXCIF_bm))
#define dma_not_ready()     (! (MEM_DMA_READ.CTRLB & (DMA_CH_ERRIF_bm | DMA_CH_TRNIF_bm)))

#define mem_timed_out()	    (MEM_TIMER.INTFLAGS & TC0_OVFIF_bm)

#define CMD0                (0)	      // GO_IDLE_STATE
#define CMD1                (1)       // SEND_OP_COND (MMC)
#define	ACMD41              (0x80+41) // SEND_OP_COND (SDC)
#define CMD8                (8)       // SEND_IF_COND
#define CMD9                (9)       // SEND_CSD
#define CMD10               (10)      // SEND_CID
#define CMD12               (12)      // STOP_TRANSMISSION
#define ACMD13              (0x80+13) // SD_STATUS (SDC)
#define CMD16               (16)      // SET_BLOCKLEN
#define CMD17               (17)      // READ_SINGLE_BLOCK
#define CMD18               (18)      // READ_MULTIPLE_BLOCK
#define CMD23               (23)      // SET_BLOCK_COUNT (MMC)
#define	ACMD23              (0x80+23) // SET_WR_BLK_ERASE_COUNT (SDC)
#define CMD24               (24)      // WRITE_BLOCK
#define CMD25               (25)      // WRITE_MULTIPLE_BLOCK
#define CMD32               (32)      // ERASE_ER_BLK_START
#define CMD33               (33)      // ERASE_ER_BLK_END
#define CMD38               (38)      // ERASE
#define	CMD48               (48)      // READ_EXTR_SINGLE
#define	CMD49               (49)      // WRITE_EXTR_SINGLE
#define CMD55               (55)      // APP_CMD
#define CMD58               (58)      // READ_OCR

#define CT_MMC3             0x01      // MMC ver 3
#define CT_MMC4             0x02      // MMC ver 4+
#define CT_MMC              0x03      // MMC
#define CT_SDC1             0x04      // SDv1
#define CT_SDC2             0x08      // SDv2+
#define CT_SDC              0x0C      // SD
#define CT_BLOCK            0x10      // block addressing

static volatile uint8_t card_status = STA_NOINIT;
static uint8_t card_type;

/*
 * Sends a byte to the memory card, returning the response.
 * 
 * This does not use the USART buffers and is thus slow. Use alternatives
 * for sending bulk data.
 */
static inline __attribute__((always_inline)) uint8_t mem_send(uint8_t data)
{
	while (! (MEM_USART.STATUS & USART_DREIF_bm));
	MEM_USART.DATA = data;
	while (! (MEM_USART.STATUS & USART_RXCIF_bm));
	return MEM_USART.DATA;
}

/*
 * Sets up the memory timer to overflow after the given number of
 * milliseconds passes (approximately). Maximum wait time is about 2 seconds.
 * 
 * This call assumes a fixed clock speed of 32MHz, which may not be the case in
 * the future.
 */
static void mem_setup_timeout(uint16_t ms)
{
	MEM_TIMER.CTRLA = TC_CLKSEL_OFF_gc;
	MEM_TIMER.CTRLFSET = TC_CMD_RESET_gc;
	// clk/1024 is 32us/tick, * 32 = 1.024ms
	MEM_TIMER.PER = (ms << 5);
	MEM_TIMER.CTRLA = TC_CLKSEL_DIV1024_gc;
}

/*
 * Sends 0xFF to the card until it is no longer busy, or until the timeout is
 * reached. Returns true on ready, false otherwise.
 */
static uint8_t mem_wait_ready(uint16_t ms)
{
	uint8_t v;
	mem_setup_timeout(ms);
	do
	{
		v = mem_send(0xFF);
	}
	while (v != 0xFF && (! mem_timed_out()));
	return (v == 0xFF) ? 1 : 0;
}

static void mem_deselect(void)
{
	cs_release();
	mem_send(0xFF);
}

static uint8_t mem_select(void)
{
	cs_assert();
	mem_send(0xFF);
	if (mem_wait_ready(500)) return 1; // ok
	mem_deselect();
	return 0; // timeout
}

static uint8_t mem_block_read(uint8_t* buffer, uint16_t start, uint16_t count)
{
	uint8_t token;
	uint16_t i;
	uint16_t end = start + count;
	if (end > 512) end = 512;
	
	mem_setup_timeout(200);
	do
	{
		token = mem_send(0xFF);
	}
	while (token == 0xFF && (! mem_timed_out()));
	if (token != 0xFE) return 0;
	
	// first two
	MEM_USART.DATA = 0xFF;
	while (! (MEM_USART.STATUS & USART_DREIF_bm));
	MEM_USART.DATA = 0xFF;

	// toss until start
	for (i = 0; i < start; i++)
	{
		while (data_not_ready());
		MEM_USART.DATA;
		MEM_USART.DATA = 0xFF;
	}
	for ( ; i < end; i++)
	{
		while (data_not_ready());
		*buffer++ = MEM_USART.DATA;
		MEM_USART.DATA = 0xFF;
	}
	for ( ; i < 512; i++)
	{
		while (data_not_ready());
		MEM_USART.DATA;
		MEM_USART.DATA = 0xFF;
	}

	// trash CRC
	while (data_not_ready());
	MEM_USART.DATA;
	while (data_not_ready());
	MEM_USART.DATA;
	
	return 1;
}

static uint8_t mem_bulk_read(uint8_t* buffer, uint16_t count)
{
	uint8_t token;
	mem_setup_timeout(200);
	do
	{
		token = mem_send(0xFF);
	}
	while (token == 0xFF && (! mem_timed_out()));
	if (token != 0xFE) return 0;

	MEM_USART.DATA = 0xFF;
	while (! (MEM_USART.STATUS & USART_DREIF_bm));
	MEM_USART.DATA = 0xFF;
	do
	{
		while (data_not_ready());
		*buffer++ = MEM_USART.DATA;
		MEM_USART.DATA = 0xFF;
	}
	while (--count); // sends +2 than requested for CRC

	// trash CRC
	while (data_not_ready());
	MEM_USART.DATA;
	while (data_not_ready());
	MEM_USART.DATA;

	return 1;
}

static uint8_t mem_bulk_write(const uint8_t* buffer, uint8_t token, uint16_t count)
{
	if (! mem_wait_ready(500)) return 0;
	mem_send(token);
	if (token != 0xFD)
	{
		// send requested count
		MEM_USART.DATA = *buffer++;
		while (! (MEM_USART.STATUS & USART_DREIF_bm));
		MEM_USART.DATA = *buffer++;
		count -= 2;
		do
		{
			while (data_not_ready());
			MEM_USART.DATA;
			MEM_USART.DATA = *buffer++;
		}
		while (--count);
		
		// trash CRC
		while (data_not_ready());
		MEM_USART.DATA;
		MEM_USART.DATA = 0xFF;
		while (data_not_ready());
		MEM_USART.DATA;
		MEM_USART.DATA = 0xFF;
		
		// flush CRC responses and send final byte to get status
		while (data_not_ready());
		MEM_USART.DATA;
		MEM_USART.DATA = 0xFF;
		while (data_not_ready());
		MEM_USART.DATA;
		
		// get status response 
		while (data_not_ready());
		uint8_t response = MEM_USART.DATA;
		if ((response & 0x1F) != 0x05) return 0;
	}
	
	return 1;
}

/*
 * Resets the USART to initialization mode, without interrupts or reception,
 * and sends 80 XCK clocks with /CS and TX set high to put the card into
 * native mode.
 * 
 * This should probably only be called when the USART is idle, or strange
 * behavior may result.
 */
static void mem_reset(void)
{
	cs_release();
	// disable the USART
	MEM_USART.CTRLB = 0;
	MEM_USART.CTRLC = USART_CMODE_MSPI_gc; // SPI mode 0,0
	MEM_USART.CTRLA = 0;
	// set the baudrate to the initialization defaults
	MEM_USART.BAUDCTRLA = MEM_BAUDCTRL_INIT;
	MEM_USART.BAUDCTRLB = 0;
	// (re)enable the USART again, in TX mode only
	MEM_USART.CTRLB = USART_TXEN_bm;

	/*
	 * Send at least 74 clocks (we send 80) with /CS and TX high to put the
	 * card into native mode and wait for bytes to finish sending before
	 * returning.
	 */
	MEM_USART.DATA = 0xFF;
	for (uint8_t i = 0; i < 9; i++)
	{
		while (! (MEM_USART.STATUS & USART_DREIF_bm));
		MEM_USART.DATA = 0xFF;
	}
	while (! (MEM_USART.STATUS & USART_TXCIF_bm));
	MEM_USART.STATUS = USART_TXCIF_bm;
	
	// enable the receiver
	MEM_USART.CTRLB |= USART_RXEN_bm;
}

static uint8_t mem_cmd(uint8_t cmd, uint32_t arg)
{
	uint8_t n, res;
	
	// ACMD<n> is the command sequense of CMD55-CMD<n>
	if (cmd & 0x80)
	{
		cmd &= 0x7F;
		uint8_t res = mem_cmd(CMD55, 0);
		if (res > 1) return res;
	}
	
	// select the card, unless stopping a multiple block read
	if (cmd != CMD12)
	{
		mem_deselect();
		if (! mem_select()) return 0xFF;
	}

	// send a command packet
	mem_send(cmd | 0x40);
	mem_send((uint8_t) (arg >> 24));
	mem_send((uint8_t) (arg >> 16));
	mem_send((uint8_t) (arg >> 8));
	mem_send((uint8_t) arg);
	switch(cmd)
	{
		case CMD0:
			n = 0x95;
			break;
		case CMD8:
			n = 0x87;
			break;
		default:
			n = 0x01; // dummy CRC + stop
	}
	mem_send(n);
	
	// skip stuff byte when stopping a multiple block read
	if (cmd == CMD12) mem_send(0xFF);
	
	// wait for response
	n = 10;
	do
	{
		res = mem_send(0xFF);
	}
	while ((res & 0x80) && --n);
	return res;
}

/*
 * ============================================================================
 *   Public Functions
 * ============================================================================
 */

DSTATUS disk_initialize(void)
{
	uint8_t n, cmd, type, ocr[4];

	mem_reset();
	
	type = 0;
	if (mem_cmd(CMD0, 0) == 1) // go to SPI mode
	{
		// limit total init time to ~1 second
		mem_setup_timeout(1000);
		
		if (mem_cmd(CMD8, 0x1AA))
		{
			// SDv2
			for (n = 0; n < 4; n++)
			{
				ocr[n] = mem_send(0xFF);
			}
			if (ocr[2] == 0x01 && ocr[3] == 0xAA)
			{
				// wait to leave idle state (ACMD41 with HCS bit)
				while ((! mem_timed_out()) && mem_cmd(ACMD41, 1UL<<30));

				// check CCS bit in the OCR
				if ((! mem_timed_out()) && mem_cmd(CMD58, 0) == 0)
				{
					for (n = 0; n < 4; n++)
					{
						ocr[n] = mem_send(0xFF);
					}
					// SDv2 (HC or SC)
					type = (ocr[0] & 0x40) ? CT_SDC2 | CT_BLOCK : CT_SDC2;
				}
			}	
		}
		else
		{
			if (mem_cmd(ACMD41, 0) <= 1)
			{
				// SDv1
				type = CT_SDC1;
				cmd = ACMD41;
			}
			else
			{
				// MMCv3
				type = CT_MMC;
				cmd = CMD1;
			}
			// wait to leave idle state
			while ((! mem_timed_out()) && mem_cmd(cmd, 0));

			// set R/W block length to 512
			if (mem_timed_out() || mem_cmd(CMD16, 512) != 0)
			{
				type = 0;
			}
		}
	}
	
	card_type = type;
	mem_deselect();
	
	if (type)
	{
		card_status &= ~STA_NOINIT;
		MEM_USART.BAUDCTRLA = MEM_BAUDCTRL_NORMAL;
		MEM_USART.BAUDCTRLB = 0;
	}
	
	return card_status;
}

DRESULT disk_readp(
	BYTE *buff,     // pointer to the read buffer, or null for stream
	DWORD sector,   // LBA
	UINT offset,    // byte offset to start reading from
	UINT count      // number of bytes to read
)
{
	if (card_status & STA_NOINIT) return RES_NOTRDY;
	if (! count) return RES_PARERR;
	
	if (! (card_type & CT_BLOCK)) sector *= 512;
	DRESULT res = RES_ERROR;
	if (mem_cmd(CMD17, sector) == 0)
	{
		if (mem_block_read(buff, offset, count)) res = RES_OK;
	}
	mem_deselect();
	return res;
}

#include "debug.h"

DRESULT disk_read_multi (
	BYTE (*func)(BYTE*),
	DWORD sector,
	UINT count
)
{
	DRESULT res = RES_OK;

	if (card_status & STA_NOINIT) return RES_NOTRDY;
	if (! count) return RES_PARERR;
	
	if (! (card_type & CT_BLOCK)) sector *= 512;
	if (count == 1)
	{
		// we treat single-sector reads like a normal FIFO call
		void* buff = malloc(512);
		if (buff != NULL
				&& mem_cmd(CMD17, sector) == 0
				&& mem_block_read(buff, 0, 512)
				&& func(buff))
		{
			count = 0;
		}
		else
		{
			debug(DEBUG_MEM_READ_SINGLE_FAILED);
			res = RES_ERROR;
		}
		free(buff);
	}
	else
	{
		void* buff_a = malloc(514);
		void* buff_b = malloc(514);
		uint8_t cmdres = mem_cmd(CMD18, sector);
		if (buff_a != NULL && buff_b != NULL && cmdres == 0)
		{
			uint8_t bufsel = 0;
			uint8_t* cbuf = buff_a;
			uint8_t* dbuf = buff_b;
			MEM_GPIOR = 0xFF;
			
			// setup the parts of DMA that are consistent throughout
			ATOMIC_BLOCK(ATOMIC_FORCEON)
			{
				MEM_DMA_WRITE.SRCADDR0 = (uint8_t) ((uint16_t) &MEM_GPIOR);
				MEM_DMA_WRITE.SRCADDR1 = (uint8_t) (((uint16_t) (&MEM_GPIOR)) >> 8);
				MEM_DMA_WRITE.SRCADDR2 = 0;
			}
			MEM_DMA_WRITE.TRFCNT = 514;
			MEM_DMA_WRITE.ADDRCTRL = 0;
			MEM_DMA_READ.TRFCNT = 514;
			MEM_DMA_READ.ADDRCTRL = DMA_CH_DESTDIR_INC_gc;
			
			// directly read the first block
			if (! mem_block_read(buff_a, 0, 512))
			{
				debug(DEBUG_MEM_READ_MUL_FIRST_FAILED);
				res = RES_ERROR;
			}
			
			// cycle (count - 1) total times; we do +1 transfer at end
			while ((! res) && --count)
			{
				// swap between buffers
				if (bufsel)
				{
					cbuf = buff_a;
					dbuf = buff_b;
				}
				else
				{
					cbuf = buff_b;
					dbuf = buff_a;
				}
				bufsel = !bufsel;
				
				// setup DMA on the empty current buffer
				ATOMIC_BLOCK(ATOMIC_FORCEON)
				{
					MEM_DMA_READ.DESTADDR0 = (uint8_t) ((uint16_t) cbuf);
					MEM_DMA_READ.DESTADDR1 = ((uint16_t) cbuf) >> 8;
					MEM_DMA_READ.DESTADDR2 = 0;
				}
				
				// wait for the card to become ready
				uint8_t token;
				mem_setup_timeout(200);
				do
				{
					token = mem_send(0xFF);
				}
				while (token == 0xFF && (! mem_timed_out()));
				if (token != 0xFE)
				{
					debug_dual(DEBUG_MEM_READ_MUL_TIMEOUT, token);
					res = RES_ERROR;
					break;
				}
				
				// execute the DMA operation
				MEM_DMA_READ.CTRLA |= DMA_CH_ENABLE_bm;
				MEM_DMA_WRITE.CTRLA |= DMA_CH_ENABLE_bm;

				// send the data buffer to the computer
				if (! func(dbuf))
				{
					debug(DEBUG_MEM_READ_MUL_FUNC_ERR);
					res = RES_ERROR;
				}
				
				// wait for the DMA transaction to finish
				while (MEM_DMA_READ.CTRLA & DMA_CH_ENABLE_bm);
				if (MEM_DMA_READ.CTRLB & DMA_CH_ERRIF_bm)
				{
					debug(DEBUG_MEM_READ_MUL_DMA_ERR);
					res = RES_ERROR;
					break;
				}
			}

			// terminate operation regardless, and ignore response
			mem_cmd(CMD12, 0);
			
			// send the last sector to the computer if valid
			if (! res)
			{
				if (! func(cbuf))
				{
					debug(DEBUG_MEM_READ_MUL_FUNC_ERR);
					res = RES_ERROR;
				}
			}
		}
		else
		{
			debug(DEBUG_MEM_READ_MUL_CMD_FAILED);
			debug(cmdres);
			res = RES_ERROR;
		}

		// clean up
		free(buff_a);
		free(buff_b);
	}
	mem_deselect();

	return res;
}

#if PF_USE_WRITE

static UINT wc; // sector write counter

DRESULT disk_writep (
	const BYTE *buff,    // pointer to the bytes to be written
	DWORD sc             // number bytes to send, LBA, or zero
)
{
	if (card_status & STA_NOINIT) return RES_NOTRDY;

	DRESULT res;
	UINT bc;
	
	res = RES_ERROR;
	if (buff)
	{
		bc = sc;
		while (bc && wc)
		{
			mem_send(*buff++);
			wc--;
			bc--;
		}
		res = RES_OK;
	}
	else
	{
		if (sc)
		{
			// convert to byte address if needed
			if (! (card_type & CT_BLOCK)) sc *= 512;
			// start sector write
			if (mem_cmd(CMD24, sc) == 0)
			{
				mem_send(0xFF);
				mem_send(0xFE);
				wc = 512;
				res = RES_OK;
			}
		}
		else
		{
			bc = wc + 2;
			wc = 0;

			// fill leftover bytes and CRC with zeros
			while (bc--) mem_send(0x00);
			
			// receive data response and wait for end of write
			if ((mem_send(0xFF) & 0x1F) == 0x05)
			{
				// wait until card goes back to ready
				mem_setup_timeout(500);
				uint8_t v;
				do
				{
					v = mem_send(0xFF);
				}
				while (v != 0xFF && (! mem_timed_out()));
				if (v == 0xFF) res = RES_OK;
			}
			mem_deselect();
		}
	}
	
	return res;
}

DRESULT disk_write_multi (
	BYTE (*func)(BYTE*),
	DWORD sector,
	UINT count
)
{
	if (card_status & STA_NOINIT) return RES_NOTRDY;
	if (! count) return RES_PARERR;
	
	if (! (card_type & CT_BLOCK)) sector *= 512;
	if (count == 1)
	{
		// we treat single-sector writes like a normal FIFO call
		void* buff = malloc(512);
		if (buff != NULL)
		{
			if (func(buff))
			{
				if ((mem_cmd(CMD24, sector) == 0) && mem_bulk_write(buff, 0xFE, 512))
				{
					count = 0;
				}
			}
			else
			{
				count = 1;
			}
		}
		free(buff);
	}
	else
	{
		// multiple sector writes use DMA
		if (card_type & CT_SDC) mem_cmd(ACMD23, count);
		if (mem_cmd(CMD25, sector) == 0)
		{
			// http://elm-chan.org/docs/mmc/mmc_e.html#dataxfer
			// diagram indicates need to have at least 1 byte before data
			mem_send(0xFF);
						
			// TODO verify malloc() does not fail
			uint8_t bufsel = 1;
			uint8_t func_res;
			uint8_t* buff_a = (uint8_t*) malloc(516);
			*buff_a = 0xFC;
			*(buff_a + 513) = 0xFF;
			*(buff_a + 514) = 0xFF;
			*(buff_a + 515) = 0xFF;
			uint8_t* buff_b = (uint8_t*) malloc(516);
			*buff_b = 0xFC;
			*(buff_b + 513) = 0xFF;
			*(buff_b + 514) = 0xFF;
			*(buff_b + 515) = 0xFF;
			uint8_t* cbuf;
			MEM_GPIOR = 0x05; // allow first itr to pass

			// setup the parts of DMA that are consistent throughout
			MEM_DMA_WRITE.ADDRCTRL = DMA_CH_SRCDIR_INC_gc;
			MEM_DMA_WRITE.TRFCNT = 516;
			ATOMIC_BLOCK(ATOMIC_FORCEON)
			{
				MEM_DMA_READ.DESTADDR0 = (uint8_t) ((uint16_t) &MEM_GPIOR);
				MEM_DMA_READ.DESTADDR1 = (uint8_t) (((uint16_t) (&MEM_GPIOR)) >> 8);
				MEM_DMA_READ.DESTADDR2 = 0;
			}
			MEM_DMA_READ.TRFCNT = 516;
			MEM_DMA_READ.ADDRCTRL = 0;

			do
			{
				// swap between buffers
				if (bufsel)
				{
					cbuf = buff_a;
				}
				else
				{
					cbuf = buff_b;
				}
				bufsel = !bufsel;
				
				// fetch fresh data
				func_res = func(cbuf + 1);
				if (func_res)
				{
					// wait for the last DMA transaction to finish
					while (MEM_DMA_READ.CTRLA & DMA_CH_ENABLE_bm);
					if (MEM_DMA_READ.CTRLB & DMA_CH_ERRIF_bm) break;

					// check result of last transaction
					uint8_t response = MEM_GPIOR;
					if ((response & 0x1F) != 0x05) break;

					// setup channel for the fresh data
					ATOMIC_BLOCK(ATOMIC_FORCEON)
					{
						MEM_DMA_WRITE.SRCADDR0 = (uint8_t) ((uint16_t) cbuf);
						MEM_DMA_WRITE.SRCADDR1 = ((uint16_t) cbuf) >> 8;
						MEM_DMA_WRITE.SRCADDR2 = 0;
					}

					// wait for the card to become ready
					if (! mem_wait_ready(500)) break;

					// execute the DMA operation
					MEM_DMA_READ.CTRLA |= DMA_CH_ENABLE_bm;
					MEM_DMA_WRITE.CTRLA |= DMA_CH_ENABLE_bm;
				}
			}
			while (--count && func_res);

			// wait for the last DMA transaction to finish
			while (MEM_DMA_READ.CTRLA & DMA_CH_ENABLE_bm);
			if (MEM_DMA_READ.CTRLB & DMA_CH_ERRIF_bm) count = 1;

			// check result of last transaction
			uint8_t response = MEM_GPIOR;
			if ((response & 0x1F) != 0x05) count = 1;

			// then send finalization and clean up
			if (! mem_bulk_write(NULL, 0xFD, 0)) count = 1;
			free(buff_a);
			free(buff_b);
		}
	}
	mem_deselect();

	return count ? RES_ERROR : RES_OK;
}

#endif

DRESULT disk_ioctl (
	BYTE cmd,
	void* buff
)
{
	DRESULT result;
	BYTE n, csd[16];
	DWORD csize;

	if (card_status & STA_NOINIT) return RES_NOTRDY;

	result = RES_ERROR;
	switch (cmd)
	{
		case CTRL_SYNC:
			if (mem_select()) result = RES_OK;
			mem_deselect();
			break;

		case GET_SECTOR_COUNT: // get number of sectors on disk
			if ((mem_cmd(CMD9, 0) == 0) && mem_bulk_read(csd, 16))
			{
				if ((csd[0] >> 6) == 1) // SDv2
				{
					csize = csd[9] + ((WORD)csd[8] << 8)
							+ ((DWORD)(csd[7] & 63) << 16) + 1;
					*(DWORD*) buff = csize << 10;
				}
				else // SDv1 or MMC
				{
					n = (csd[5] & 15) + ((csd[10] & 128) >> 7)
							+ ((csd[9] & 3) << 1) + 2;
					csize = (csd[8] >> 6) + ((WORD) csd[7] << 2)
							+ ((WORD) (csd[6] & 3) << 10) + 1;
					*(DWORD*) buff = csize << (n - 9);
				}
				result = RES_OK;
			}
			mem_deselect();
			break;

		case GET_BLOCK_SIZE: // set erase block size in sectors
			if (card_type & CT_SDC2) // SDv2
			{
				if (mem_cmd(ACMD13, 0) == 0)
				{
					mem_send(0xFF);
					if (mem_bulk_read(csd, 16))
					{
						for (n = 64 - 16; n; n--) mem_send(0xFF);
						*(DWORD*) buff = 16UL << (csd[10] >> 4);
						result = RES_OK;
					}
				}
			}
			else // SDv1 or MMCv3
			{
				if ((mem_cmd(CMD9, 0) == 0) && mem_bulk_read(csd, 16))
				{
					if (card_type & CT_SDC1)
					{
						*(DWORD*) buff = (((csd[10] & 63) << 1)
								+ ((WORD) (csd[11] & 128) >> 7) + 1)
										<< ((csd[13] >> 6) - 1);
					}
					else
					{
						*(DWORD*)buff = ((WORD) ((csd[10] & 124) >> 2) + 1)
								* (((csd[11] & 3) << 3)
								+ ((csd[11] & 224) >> 5) + 1);
					}
				}
			}
			mem_deselect();
			break;

		default:
			result = RES_PARERR;
	}

	return result;
}
