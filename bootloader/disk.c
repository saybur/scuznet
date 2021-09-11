/*
 * Copyright (C) 2014 ChaN
 * Copyright (C) 2019-2021 saybur
 * 
 * This file is part of mcxboot.
 * 
 * mcxboot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mcxboot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with mcxboot.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Some of functions are derived from pfsample.zip, available here:
 * 
 * http://elm-chan.org/fsw/ff/00index_p.html
 * 
 * The original license for these is:
 * 
 * > These sample projects for Petit FatFs module are free software and there is
 * > NO WARRANTY. You can use, modify and redistribute it for personal, non-profit
 * > or commercial use without any restriction UNDER YOUR RESPONSIBILITY.
 */

#include <avr/io.h>
#include <util/delay.h>
#include "./lib/pff/diskio.h"
#include "config.h"

#define cs_assert()         (MEM_PORT.OUTCLR = MEM_PIN_CS)
#define cs_release()        (MEM_PORT.OUTSET = MEM_PIN_CS)
#define is_cs_asserted()    (! (MEM_PORT.IN & MEM_PIN_CS))
#define data_not_ready()    (! (MEM_USART.STATUS & USART_RXCIF_bm))

#define CMD0                (0x40+0)  // GO_IDLE_STATE
#define CMD1                (0x40+1)  // SEND_OP_COND (MMC)
#define	ACMD41              (0xC0+41) // SEND_OP_COND (SDC)
#define CMD8                (0x40+8)  // SEND_IF_COND
#define CMD16               (0x40+16) // SET_BLOCKLEN
#define CMD17               (0x40+17) // READ_SINGLE_BLOCK
#define CMD24               (0x40+24) // WRITE_BLOCK
#define CMD55               (0x40+55) // APP_CMD
#define CMD58               (0x40+58) // READ_OCR

#define CT_MMC              0x01      // MMCv3
#define CT_SD1	            0x02      // SDv1
#define CT_SD2              0x04      // SDv2+
#define CT_BLOCK            0x08      // block addressing

static uint8_t card_type;

/*
 * Sends a byte to the memory card, returning the response.
 * 
 * This does not use the USART buffers and is thus slow. Use alternatives
 * for sending bulk data.
 */
static uint8_t mem_send(uint8_t data)
{
	while (! (MEM_USART.STATUS & USART_DREIF_bm));
	MEM_USART.DATA = data;
	while (! (MEM_USART.STATUS & USART_RXCIF_bm));
	return MEM_USART.DATA;
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
	MEM_USART.BAUDCTRLA = 4; // 200kbps @ 2MHz
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

/*
 * Send a command to the memory card. This follows the steps from the
 * sample code.
 */
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
	
	// select the card
	cs_release();
	mem_send(0xFF);
	cs_assert();
	mem_send(0xFF);
	
	// send a command packet
	mem_send(cmd);
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
 * Initialize the memory card. This is derived from the sample code; refer
 * there for more details.
 */
DSTATUS disk_initialize(void)
{
	uint8_t n, cmd, type, ocr[4];
	uint16_t tmr;
	
	#if PF_USE_WRITE
	if (card_type != 0 && is_cs_asserted()) disk_writep(0, 0);
	#endif
	
	mem_reset();
	
	type = 0;
	if (mem_cmd(CMD0, 0) == 1)
	{
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
				for (tmr = 10000; mem_cmd(ACMD41, 1UL<<30) && tmr; tmr--)
				{
					_delay_us(100);
				}
				// check CCS bit in the OCR
				if (tmr && mem_cmd(CMD58, 0) == 0)
				{
					for (n = 0; n < 4; n++)
					{
						ocr[n] = mem_send(0xFF);
					}
					// SDv2 (HC or SC)
					type = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;
				}
			}	
		}
		else
		{
			if (mem_cmd(ACMD41, 0) <= 1)
			{
				// SDv1
				type = CT_SD1;
				cmd = ACMD41;
			}
			else
			{
				// MMCv3
				type = CT_MMC;
				cmd = CMD1;
			}
			// wait to leave idle state
			for (tmr = 10000; mem_cmd(cmd, 0) && tmr; tmr--)
			{
				_delay_us(100);
			}
			// set R/W block length to 512
			if (!tmr || mem_cmd(CMD16, 512) != 0)
			{
				type = 0;
			}
		}
	}
	
	card_type = type;
	cs_release();
	mem_send(0xFF);
	
	if (type)
	{
		// card OK, we can now communicate at full speed
		MEM_USART.BAUDCTRLA = 0;
		MEM_USART.BAUDCTRLB = 0;
		return 0;
	}
	else
	{
		return STA_NOINIT;
	}
}

DRESULT disk_readp(
	BYTE *buff,     // pointer to the read buffer, or null for stream
	DWORD sector,   // LBA
	UINT offset,    // byte offset to start reading from
	UINT count      // number of bytes to read
)
{
	DRESULT res;
	BYTE rc;
	UINT bc;
	
	// convert to byte address if needed
	if (! (card_type & CT_BLOCK)) sector *= 512;
	
	res = RES_ERROR;
	if (mem_cmd(CMD17, sector) == 0)
	{
		bc = 40000;
		do
		{
			rc = mem_send(0xFF);
		}
		while (rc == 0xFF && --bc);
		if (rc == 0xFE)
		{
			// number of trailing bytes to skip
			bc = 512 + 2 - offset - count;
			
			// skip leading bytes in the sector
			while (offset--) mem_send(0xFF);
			
			if (buff)
			{
				// store to buffer
				do
				{
					*buff++ = mem_send(0xFF);
				}
				while (--count);
			}
			else
			{
				// forward (as of now does nothing)
				do
				{
					mem_send(0xFF);
				}
				while (--count);
			}
			
			// skip trailing bytes in the sector and block CRC
			do
			{
				mem_send(0xFF);
			}
			while (--bc);
			
			res = RES_OK;
		}
	}
	
	cs_release();
	mem_send(0xFF);
	return res;
}

#if PF_USE_WRITE
DRESULT disk_writep (
	const BYTE *buff,    // pointer to the bytes to be written
	DWORD sc             // number bytes to send, LBA, or zero
)
{
	DRESULT res;
	UINT bc;
	static UINT wc; // sector write counter
	
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
			// start sector write
			
			// convert to byte address if needed
			if (! (card_type & CT_BLOCK)) sc *= 512;
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
			// fill leftover bytes and CRC with zeros
			while (bc--) mem_send(0x00);
			
			// receive data rseponse and wait for end of write
			// timeout is 500ms
			if ((mem_send(0xFF) & 0x1F) == 0x05)
			{
				// wait until card is ready
				for (bc = 5000; mem_send(0xFF) != 0xFF && bc; bc--)
				{
					_delay_us(100);
				}
				if (bc) res = RES_OK;
			}
			
			cs_release();
			mem_send(0xFF);
		}
	}
	
	return res;
}
#endif
