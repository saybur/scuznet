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

#include <stddef.h>
#include <avr/io.h>
#include <util/atomic.h>
#include "config.h"
#include "mem.h"
#include "debug.h"

// bitmasks for the R1 response
#define MEM_R1_PARM_ERR         _BV(6)
#define MEM_R1_ADDR_ERR         _BV(5)
#define MEM_R1_ERASE_SEQ_ERR    _BV(4)
#define MEM_R1_CMD_CRC_ERR      _BV(3)
#define MEM_R1_ILLEGAL_CMD      _BV(2)
#define MEM_R1_ERASE_RESET      _BV(1)
#define MEM_R1_IDLE             _BV(0)

/*
 * Macros for setting/releasing the /CS line, for code clarity.
 */
#define mem_card_assert() (MEM_PORT.OUTCLR = MEM_PIN_CS)
#define mem_card_release() (MEM_PORT.OUTSET = MEM_PIN_CS)

/* 
 * Common commands we send, typically during initalization.
 * Note per https://electronics.stackexchange.com/a/238217, some cards may
 * require CRC on CMD55/ACMD41, which is supplied below.
 */
const uint8_t mem_cmd0[6]  = { 0x40, 0x00, 0x00, 0x00, 0x00, 0x95 };
const uint8_t mem_cmd1[6]  = { 0x41, 0x00, 0x00, 0x00, 0x00, 0xFF };
const uint8_t mem_cmd8[6]  = { 0x48, 0x00, 0x00, 0x01, 0xAA, 0x87 };
const uint8_t mem_cmd9[6] =  { 0x49, 0x00, 0x00, 0x00, 0x00, 0xFF };
const uint8_t mem_cmd16[6] = { 0x50, 0x00, 0x00, 0x02, 0x00, 0xFF };
const uint8_t mem_cmd41[6] = { 0x69, 0x40, 0x00, 0x00, 0x00, 0x77 };
const uint8_t mem_cmd55[6] = { 0x77, 0x00, 0x00, 0x00, 0x00, 0x65 };
const uint8_t mem_cmd58[6] = { 0x7A, 0x00, 0x00, 0x00, 0x00, 0xFF };

// tracker for while we're initalizing (255 is fully initialized & ready)
// we also allocate a generic byte for tracking retries within the init code
static uint8_t mem_init_state = 0;
static uint8_t mem_init_retries = 0;

// command buffer
static uint8_t mem_cmd_buffer[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF };

/*
 * Resets the USART to initialization mode, without interrupts or reception,
 * and sends 80 XCK clocks with /CS and TX set high to put the card into
 * native mode.
 * 
 * This should only be called when the USART is idle, or strange behavior may
 * result.
 */
static void mem_card_native(void)
{
	// force /CS up just in case
	mem_card_release();
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
}

/*
 * Executes a raw command to the memory card. This does not manipulate /CS, or
 * check the lock: it just is responsible for getting the command bytes onto
 * the wire and getting the response back.
 * 
 * Important: the USART must be set up correctly, be idle, and have no bytes
 * in either the TX or RX buffer.
 * 
 * This is given an array that must contain at least 6 bytes, in this order:
 * 
 * 1) Command
 * 2) Argument b31-24
 * 3) Argument b23-16
 * 4) Argument b15-8
 * 5) Argument b7-0
 * 6) CRC
 * 
 * This will give back the command response, or 0xFF if none was detected. It
 * will also leave a byte in the USART read buffer, which may be removed
 * via mem_op_end(), which will also lift the /CS line.
 * 
 * Some additional special notes:
 * 
 * 1) If given a non-null data pointer, *and* the response indicates success,
 *    this will read in the next 4 bytes into the given pointer, for supporting
 *    commands that need short response data.
 * 2) Alternately, if given CMD12 as command, this will continue to send clocks
 *    after a response byte is given, until 0xFF is received (up to ~255
 *    bytes). The response byte will then be returned. Don't give a pointer
 *    if this is the intent.
 * 
 * To implement advise from https://stackoverflow.com/a/2375234, we will be
 * reading a byte behind: this keeps SPI flowing while we're processing, and
 * should ensure the extra 0xFF gets to the card.
 */
static uint8_t mem_card_cmd(const uint8_t* cmd, uint8_t* data)
{
	/*
	 * Send 6 command bytes, double-stacking on the first insertion per
	 * above, and using RXCIF as a hint that a transaction has finished and
	 * we're safe to insert a new byte. This is the "standard" approach for
	 * using the USART throughout this code.
	 */
	MEM_USART.DATA = cmd[0];
	for (uint8_t i = 1; i < 6; i++)
	{
		MEM_USART.DATA = cmd[i];
		while (mem_data_not_ready());
		MEM_USART.DATA;
	}

	// send first wait byte, and junk the last response to the command
	MEM_USART.DATA = 0xFF;
	while (! (MEM_USART.STATUS & USART_RXCIF_bm));
	MEM_USART.DATA;

	/*
	 * We junk 1 additional byte if CMD12 to avoid seeing the stuff byte as a
	 * command data response.
	 */
	if (cmd[0] == 0x4C)
	{
		MEM_USART.DATA = 0xFF;
		while (! (MEM_USART.STATUS & USART_RXCIF_bm));
		MEM_USART.DATA;
	}

	// send 1-8 additional wait bytes until we get a response
	uint8_t rx = 0xFF;
	for (uint8_t i = 0; i < 8 && rx == 0xFF; i++)
	{
		MEM_USART.DATA = 0xFF;
		while (mem_data_not_ready());
		rx = MEM_USART.DATA;
	}

	/*
	 * We read the next 4 bytes if provided a pointer into memory for it, and
	 * if the response is either 0x00 (non-idle OK) or 0x01 (idle OK). This
	 * supports R3/R7 responses.
	 */
	if (data && (rx & 0xFE) == 0)
	{
		for (uint8_t i = 0; i < 4; i++)
		{
			MEM_USART.DATA = 0xFF;
			while (! (MEM_USART.STATUS & USART_RXCIF_bm));
			data[i] = MEM_USART.DATA;
		}
	}

	// return while there is still a pending byte
	return rx;
}

/*
 * Note: this just sets up the pins. Actual USART init is done via the card
 * setup routine.
 */
void mem_init(void)
{
	/*
	 * Setup pins. Per ENC init code, on XMEGAs USART pin direction has to be
	 * set manually. Also, the schematic has a pull-down on MISO, which is
	 * wrong: it should be a pull-up, so the external resistor on that line
	 * must be removed and we'll use the internal pull-up instead.
	 */
	MEM_PORT.OUTCLR = MEM_PIN_XCK;
	MEM_PORT.OUTSET = MEM_PIN_TX | MEM_PIN_CS;
	MEM_PORT.DIRSET = MEM_PIN_XCK | MEM_PIN_TX | MEM_PIN_CS;
	MEM_PINCTRL_RX |= PORT_OPC_PULLUP_gc;
}

/*
 * Per header docs, this flows through the serpentine initialization strategy
 * required to get a memory card initialized for further operations.
 */
uint8_t mem_init_card(void)
{
	/*
	 * If we were able to get initialized originally, the card should still
	 * be in SPI mode. Just set things up for a CMD0 execution.
	 */
	if (mem_init_state == MEM_ISTATE_SUCCESS)
	{
		MEM_USART.CTRLB &= ~USART_RXEN_bm;
		MEM_USART.BAUDCTRLA = MEM_BAUDCTRL_INIT;
		MEM_USART.BAUDCTRLB = 0;
		mem_init_state = MEM_ISTATE_RESET;
	}

	/*
	 * Native card initialization section.
	 * 
	 * We send the ~74 0xFF cycles, wait for that to be done, then send the
	 * reset command. We use that response to detect if a card is present or
	 * not, since support of it should be universal.
	 */
	if (mem_init_state == MEM_ISTATE_STARTING)
	{
		mem_card_native();
		mem_init_retries = 0;
		mem_init_state = MEM_ISTATE_RESET;
	}
	else if (mem_init_state == MEM_ISTATE_RESET)
	{
		MEM_USART.CTRLB |= USART_RXEN_bm;
		mem_card_assert();
		uint8_t v = mem_card_cmd(mem_cmd0, NULL);
		mem_op_end();

		if (v == 0x01)
		{
			mem_init_retries = 0;
			mem_init_state = MEM_ISTATE_SEND_COND;
		}
		else
		{
			/*
			 * Per https://electronics.stackexchange.com/a/238217, this may
			 * just be the card not responding. We allow a large number of
			 * retries on this command.
			 */
			mem_init_retries++;
			if (mem_init_retries > 250)
			{
				mem_init_state = MEM_ISTATE_ERR_NO_CARD;
			}
		}
	}

	/*
	 * Check if this is a modern or legacy card and respond appropriately.
	 */
	else if (mem_init_state == MEM_ISTATE_SEND_COND)
	{
		uint8_t response[4] = { 0x00, 0x00, 0x00, 0x00 };
		mem_card_assert();
		uint8_t v = mem_card_cmd(mem_cmd8, response);
		mem_op_end();

		if (v == 0x01)
		{
			if (response[0] == 0x00
					&& response[1] == 0x00
					&& response[2] == 0x01
					&& response[3] == 0xAA)
			{
				mem_init_state = MEM_ISTATE_MODERN_LOOP;
			}
			else
			{
				mem_init_state = MEM_ISTATE_ERR_MOD_BAD_RESP;
			}
		}
		else
		{
			mem_init_state = MEM_ISTATE_LEGACY_LOOP;
		}
	}

	/*
	 * Initialization block for modern cards that replied correctly for CMD8.
	 * If we get anything unusual at all, treat it as an error.
	 * 
	 * Keep sending ACMD41 until card is no longer idle. Then, read OCR, and
	 * see what kind of mode the card works in.
	 */
	else if (mem_init_state == MEM_ISTATE_MODERN_LOOP)
	{
		mem_card_assert();
		uint8_t v = mem_card_cmd(mem_cmd55, NULL);
		mem_op_end();

		if (v == 0x01)
		{
			mem_card_assert();
			v = mem_card_cmd(mem_cmd41, NULL);
			mem_op_end();
			if (v == 0x01)
			{
				// still not initialized, keep waiting
			}
			else if (v == 0x00)
			{
				// initialized now, proceed
				mem_init_state = MEM_ISTATE_MODERN_CMD58;
			}
			else
			{
				mem_init_state = MEM_ISTATE_ERR_MOD_BAD_RESP;
			}
		}
		else
		{
			mem_init_state = MEM_ISTATE_ERR_MOD_BAD_RESP;
		}
	}
	else if (mem_init_state == MEM_ISTATE_MODERN_CMD58)
	{
		uint8_t ocr[4] = { 0x00, 0x00, 0x00, 0x00 };
		mem_card_assert();
		uint8_t v = mem_card_cmd(mem_cmd58, ocr);
		mem_op_end();

		if (v == 0x00)
		{
			if (ocr[0] & 0x40)
			{
				// SDHC/SDXC, already in LBA mode
				// okay to proceed to final steps
				mem_init_state = MEM_ISTATE_FINALIZING;
			}
			else
			{
				// card OK, but still need to set block size
				mem_init_state = MEM_ISTATE_BLOCK_SIZE_SET;
			}
		}
		else
		{
			mem_init_state = MEM_ISTATE_ERR_MOD_BAD_RESP;
		}
	}

	/*
	 * Initialization block for older cards that did not reply correctly for
	 * CMD8. If we get anything unusual at all, drop to CMD1 init mode.
	 */
	else if (mem_init_state == MEM_ISTATE_LEGACY_LOOP)
	{
		mem_card_assert();
		uint8_t v = mem_card_cmd(mem_cmd55, NULL);
		mem_op_end();

		if (v == 0x01)
		{
			mem_card_assert();
			v = mem_card_cmd(mem_cmd41, NULL);
			mem_op_end();
			if (v == 0x01)
			{
				// keep waiting
			}
			else if (v == 0x00)
			{
				mem_init_state = MEM_ISTATE_BLOCK_SIZE_SET;
			}
			else
			{
				mem_init_state = MEM_ISTATE_OLDEST_LOOP;
			}
		}
		else
		{
			mem_init_state = MEM_ISTATE_OLDEST_LOOP;
		}
	}

	/*
	 * Oldest cards that need CMD1 to get going.
	 */
	else if (mem_init_state == MEM_ISTATE_OLDEST_LOOP)
	{
		mem_card_assert();
		uint8_t v = mem_card_cmd(mem_cmd1, NULL);
		mem_op_end();

		if (v == 0x01)
		{
			// keep waiting
		}
		else if (v == 0x00)
		{
			// okay now, move to block size change
			mem_init_state = MEM_ISTATE_BLOCK_SIZE_SET;
		}
		else
		{
			mem_init_state = MEM_ISTATE_ERR_OLD_BAD_RESP;
		}
	}

	/*
	 * Adjust block size to 512 bytes.
	 */
	else if (mem_init_state == MEM_ISTATE_BLOCK_SIZE_SET)
	{
		mem_card_assert();
		uint8_t v = mem_card_cmd(mem_cmd16, NULL);
		mem_op_end();

		if (v == 0x00)
		{
			mem_init_state = MEM_ISTATE_FINALIZING;
		}
		else
		{
			mem_init_state = MEM_ISTATE_ERR_BLOCK_SIZE;
		}
	}

	/*
	 * Final setup step before card is flagged as OK to use.
	 */
	else if (mem_init_state == MEM_ISTATE_FINALIZING)
	{
		MEM_USART.BAUDCTRLA = MEM_BAUDCTRL_NORMAL;
		MEM_USART.BAUDCTRLB = 0;
		mem_init_state = MEM_ISTATE_SUCCESS;
	}

	else
	{
		// this should never happen, we obviously missed a state somewhere
		mem_init_state = MEM_ISTATE_DEVELOPER_ERR;
	}
	return mem_init_state;
}

uint8_t mem_op_start(void)
{
	if (mem_init_state != MEM_ISTATE_SUCCESS) return 0;

	mem_card_assert();
	// RX should go high unless card is busy w/ internal op
	if (MEM_PORT.IN & MEM_PIN_RX)
	{
		return 1;
	}
	else
	{
		mem_card_release();
		return 0;
	}
}

uint8_t mem_op_cmd(uint8_t cmd)
{
	mem_cmd_buffer[0] = 0x40 + cmd;
	for (uint8_t i = 0; i < 4; i++)
	{
		mem_cmd_buffer[i + 1] = 0x00;
	}
	return mem_card_cmd(mem_cmd_buffer, NULL);
}

uint8_t mem_op_cmd_args(uint8_t cmd, uint8_t* arg)
{
	mem_cmd_buffer[0] = 0x40 + cmd;
	for (uint8_t i = 0; i < 4; i++)
	{
		mem_cmd_buffer[i + 1] = arg[i];
	}
	return mem_card_cmd(mem_cmd_buffer, NULL);
}

uint8_t mem_wait_for_data(void)
{
	uint8_t v;
	do
	{
		MEM_USART.DATA = 0xFF;
		while (mem_data_not_ready());
		v = MEM_USART.DATA;
	}
	while (v == 0xFF);
	return v;
}

void mem_op_end(void)
{
	/*
	 * Wait for the operation to complete, then clear completion flag and put
	 * /CS back to high to allow the card to execute the command.
	 */
	while (! (MEM_USART.STATUS & USART_TXCIF_bm));
	MEM_USART.STATUS = USART_TXCIF_bm;
	mem_card_release();

	/*
	 * Drain residual information to prevent stuck bytes from corrupting
	 * subsequent commands.
	 */
	while (MEM_USART.STATUS & USART_RXCIF_bm)
	{
		MEM_USART.DATA;
	}
}

/*
 * ============================================================================
 *  
 *   HIGH LEVEL OPERATIONS
 * 
 * ============================================================================
 */

static uint8_t mem_read_cxd(uint8_t opcode, uint8_t* data)
{
	if (! mem_op_start())
	{
		debug(DEBUG_MEM_NOT_READY);
		return 0;
	}

	uint8_t v = mem_op_cmd(opcode);
	if (v != 0x00)
	{
		debug(DEBUG_MEM_CMD_REJECTED);
		debug(v);
		return 0;
	}

	v = mem_wait_for_data();
	if (v == MEM_DATA_TOKEN)
	{
		// get 16 bytes of CxD data
		for (uint8_t i = 0; i < 16; i++)
		{
			MEM_USART.DATA = 0xFF;
			while (! (MEM_USART.STATUS & USART_RXCIF_bm));
			data[i] = MEM_USART.DATA;
		}
		// get CRC bytes
		for (uint8_t i = 0; i < 2; i++)
		{
			MEM_USART.DATA = 0xFF;
			while (! (MEM_USART.STATUS & USART_RXCIF_bm));
			MEM_USART.DATA;
		}
	}
	else
	{
		debug(DEBUG_MEM_BAD_DATA_TOKEN);
		debug(v);
		return 0;
	}

	mem_op_end();
	return 1;
}

uint8_t mem_read_cid(uint8_t* data)
{
	return mem_read_cxd(10, data);
}

uint8_t mem_read_csd(uint8_t* data)
{
	return mem_read_cxd(9, data);
}

/*
 * This is an obnoxious problem due to the different versions of the CSD,
 * specifically the strange layout of the first version. See
 * https://en.wikipedia.org/wiki/SD_card for some idea of what is going on in
 * this function. This information has been gleaned from lots and lots of
 * forum posts and thus may not be 100% accurate.
 * 
 * Note that this is calculating the number of 512 byte *blocks*, not the
 * absolute size, and thus should work on cards up to 2TB in size (though
 * at the time of this writing those don't exist... yet).
 */
uint32_t mem_size(uint8_t* csd)
{
	if (csd[0] & 0xC0)
	{
		// version 2+
		uint32_t size = (((uint32_t) (csd[7] & 0x3F)) << 16)
				| (csd[8] << 8) | csd[9];
		return size << 10;
	}
	else
	{
		// version 1 routine; this looks messy but has been tested to work
		uint8_t c_size_h = ((csd[6] & 3) << 2) | (csd[7] >> 6);
		uint8_t c_size_l = (csd[7] << 2) | (csd[8] >> 6);
		uint16_t c_size = (c_size_h << 8) | c_size_l;
		uint8_t c_size_mult = ((csd[9] & 3) << 1) | (csd[10] >> 7);
		uint8_t read_bl_len = csd[5] & 0x0F;
		uint8_t scalar = c_size_mult + read_bl_len - 7;
		return ((uint32_t) c_size + 1) << scalar;
	}
}
