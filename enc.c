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

#include <util/atomic.h>
#include <util/delay.h>
#include "config.h"
#include "enc.h"

#define ENC_ECON1_ARGUMENT 0x1F

/*
 * Writes the given byte to the given register. This is a low-level
 * operation that performs no checks before executing the command.
 */
static uint8_t enc_exchange_byte(uint8_t op, uint8_t send)
{
	ENC_PORT.OUTCLR = ENC_PIN_CS;
	ENC_USART.DATA = op;
	ENC_USART.DATA = send;

	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	ENC_USART.DATA; // corresponds to command, ignored
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	uint8_t received = ENC_USART.DATA;

	ENC_PORT.OUTSET = ENC_PIN_CS;

	return received;
}

/*
 * Special purpose, read-only version of the above function for
 * handling RCR ops that reply with a dummy byte included. This
 * includes all MAC and MII registers.
 */
static uint8_t enc_exchange_special(uint8_t op)
{
	ENC_PORT.OUTCLR = ENC_PIN_CS;
	ENC_USART.DATA = op;
	ENC_USART.DATA = 0; // dummy byte

	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	ENC_USART.DATA; // corresponds to command, ignored
	ENC_USART.DATA = 0; // clocks for data response
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	ENC_USART.DATA; // corresponds to dummy, ignored
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	uint8_t received = ENC_USART.DATA; // actual data, finally

	ENC_PORT.OUTSET = ENC_PIN_CS;

	return received;
}

/*
 * Sets the PHY SPI bank to the given value, and updates the internal
 * bank tracking variable appropriately.
 */
static inline __attribute__((always_inline)) void enc_bank(uint8_t op_bank)
{
	// sanitize input
	op_bank = op_bank & 0x03;

	// and reset if needed
	if (ENC_BANK != op_bank)
	{
		uint8_t reg = enc_exchange_byte(
				(ENC_ECON1 & ENC_REG_MASK) | ENC_OP_RCR,
				0);
		reg = (reg & 0xFC) | op_bank;
		enc_exchange_byte(
				(ENC_ECON1 & ENC_REG_MASK) | ENC_OP_WCR,
				reg);
		ENC_BANK = op_bank;
	}
}

/*
 * Initialization routine that must be called before any other functions
 * are invoked. This should be only called once, before interrupts are
 * enabled on the device, during startup.
 * 
 * This does the following:
 * 
 * 1) Sets up the dedicated pins,
 * 2) Executes an asynchronous reset of the Ethernet controller,
 * 3) Sets up the USART peripheral in MSPI mode for talking to the controller
 */
void enc_init(void)
{
	/*
	 * On XMEGAs, USART pin direction has to be set manually: the unit
	 * does not override the port. Additionally, setup the select and
	 * reset lines. All should be high except clock, per 4.1.
	 */
	ENC_PORT.OUTCLR = ENC_PIN_XCK;
	ENC_PORT.OUTSET = ENC_PIN_TX | ENC_PIN_CS;
	ENC_PORT_EXT.OUTSET = ENC_PIN_RST;
	ENC_PORT.DIRSET = ENC_PIN_XCK | ENC_PIN_TX | ENC_PIN_CS;
	ENC_PORT_EXT.DIRSET = ENC_PIN_RST;
	/*
	 * Hardware does not have a pull-up on RX, unfortunately. This pin is
	 * supposed to be driven by the ENC, but will be tri-stated during some
	 * operations. To keep it from floating, we try to use the weak internal
	 * pull-up. This problem should be fixed in hardware in the next revision.
	 */
	ENC_RX_PINCTRL |= PORT_OPC_PULLUP_gc;
	/*
	 * Invert the read state of the /INT pin to make the logic that checks it
	 * a little simpler to implement. To manually check, perform the following:
	 * 
	 * if (ENC_PORT_EXT.IN & ENC_PIN_INT) { };
	 * 
	 * This also causes the rising edge to be the assertion side for
	 * interrupts. Set it up for INT1.
	 */
	ENC_INT_PINCTRL |= PORT_INVEN_bm | PORT_ISC_RISING_gc;
	ENC_PORT_EXT.INT1MASK = ENC_PIN_INT;

	// wait before we do anything with the reset line
	_delay_ms(1);
	// drive the /RESET line low for 50us (min 400ns)
	ENC_PORT_EXT.OUTCLR = ENC_PIN_RST;
	_delay_us(50);
	// then raise it, and wait again (min ~50us per 11.2)
	ENC_PORT_EXT.OUTSET = ENC_PIN_RST;
	_delay_ms(1);

	// setup speed
	ENC_USART.BAUDCTRLA = ENC_USART_BAUDCTRL;
	ENC_USART.BAUDCTRLB = 0;
	// SPI mode 0,0 to match PHY requirements
	ENC_USART.CTRLC = USART_CMODE_MSPI_gc;
	// start unit
	ENC_USART.CTRLB = USART_RXEN_bm | USART_TXEN_bm;
}

uint8_t enc_swap(uint8_t tx)
{
	while (! (ENC_USART.STATUS & USART_DREIF_bm));
	ENC_USART.DATA = tx;
	while (! (ENC_USART.STATUS & USART_RXCIF_bm));
	return ENC_USART.DATA;
}

ENCSTAT enc_cmd_read(uint8_t reg, uint8_t* response)
{
	uint8_t arg = reg & 0x1F;
	if (arg == 0x1A)
	{
		return ENC_ILLEGAL_OP;
	}
	else if (arg < 0x1A)
	{
		enc_bank((reg >> 5) & 0x03);
	}

	if (reg & 0x80)
	{
		// MAC/MII, so we have to handle the dummy byte
		*response = enc_exchange_special(arg);
	}
	else
	{
		*response = enc_exchange_byte(arg, 0);
		if (arg == ENC_ECON1_ARGUMENT)
		{
			ENC_BANK = ((*response) & 0x03);
		}
	}

	return ENC_OK;
}

ENCSTAT enc_cmd_write(uint8_t reg, uint8_t value)
{
	uint8_t arg = reg & 0x1F;
	if (arg == 0x1A)
	{
		return ENC_ILLEGAL_OP;
	}
	else if (arg < 0x1A)
	{
		enc_bank((reg >> 5) & 0x03);
	}

	enc_exchange_byte(arg | ENC_OP_WCR, value);
	if (arg == ENC_ECON1_ARGUMENT)
	{
		ENC_BANK = (value & 0x03);
	}

	return ENC_OK;
}

ENCSTAT enc_cmd_set(uint8_t reg, uint8_t mask)
{
	if (reg & 0x80)
	{
		return ENC_ILLEGAL_OP;
	}

	uint8_t arg = reg & 0x1F;
	if (arg == 0x1A)
	{
		return ENC_ILLEGAL_OP;
	}
	else if (arg < 0x1A)
	{
		enc_bank((reg >> 5) & 0x03);
	}

	enc_exchange_byte(arg | ENC_OP_BFS, mask);

	if (arg == ENC_ECON1_ARGUMENT)
	{
		ENC_BANK |= (mask & 0x03);
	}

	return ENC_OK;
}

ENCSTAT enc_cmd_clear(uint8_t reg, uint8_t mask)
{
	if (reg & 0x80)
	{
		return ENC_ILLEGAL_OP;
	}

	uint8_t arg = reg & 0x1F;
	if (arg == 0x1A)
	{
		return ENC_ILLEGAL_OP;
	}
	else if (arg < 0x1A)
	{
		enc_bank((reg >> 5) & 0x03);
	}

	enc_exchange_byte(arg | ENC_OP_BFC, mask);

	if (arg == ENC_ECON1_ARGUMENT)
	{
		ENC_BANK &= ~(mask & 0x03);
	}

	return ENC_OK;
}

/*
 * Reads a value out of the PHY. This (mostly) follows the steps in
 * 3.3.1, and blocks while the operation is in progress, so this
 * method takes a while to complete.
 * 
 * Overall, this method is expensive to call, so if the information is
 * needed more than once, it is probably a better idea to use the
 * scanning system.
 */
ENCSTAT enc_phy_read(uint8_t phy_register, uint16_t* response)
{
	enc_bank(3);
	uint8_t r = enc_exchange_special(
			(ENC_MISTAT & ENC_REG_MASK) | ENC_OP_RCR);
	if (r & ENC_BUSY_bm)
	{
		return ENC_PHYBSY;
	}
	if (r & ENC_SCAN_bm)
	{
		return ENC_PHYSCAN;
	}

	// TODO verify PHY address is valid??

	// fast switch from bank 3 to bank 2
	enc_exchange_byte(
			(ENC_ECON1 & ENC_REG_MASK) | ENC_OP_BFC,
			ENC_BSEL0_bm);
	// write PHY address
	enc_exchange_byte(
			(ENC_MIREGADR & ENC_REG_MASK) | ENC_OP_WCR,
			phy_register);
	// set MICMD.MIIRD
	enc_exchange_byte(
			(ENC_MICMD & ENC_REG_MASK) | ENC_OP_WCR,
			ENC_MIIRD_bm);

	/* 
	 * Wait at least the 10.24us.
	 * 
	 * This is 384 clock cycles, and may be even longer if the code gets
	 * interrupted. This could probably be done with a timer instead,
	 * which the MCU has plenty of.
	 */
	_delay_us(12);

	// clear MICMD.MIIRD
	enc_exchange_byte(
			(ENC_MICMD & ENC_REG_MASK) | ENC_OP_WCR,
			0);
	// read MIRDL, then MIRDH into the response
	*response = enc_exchange_special(
			(ENC_MIRDL & ENC_REG_MASK) | ENC_OP_RCR);
	r = enc_exchange_special(
			(ENC_MIRDH & ENC_REG_MASK) | ENC_OP_RCR);
	*response += (r << 8);

	// bank variable must end up in bank 2 where we left it
	ENC_BANK = 2;
	return ENC_OK;
}

/*
 * Writes the given value into the given PHY register. This will only
 * be written if the MIIM is not busy. After writing this value into
 * the appropriate registers, this immediately returns and does not
 * wait for the 10.24us - it is the responsibility of the caller to
 * ensure enough time passes before subsequent use of the MIIM.
 */
ENCSTAT enc_phy_write(uint8_t phy_register, uint16_t value)
{
	enc_bank(3);
	uint8_t r = enc_exchange_special(
			(ENC_MISTAT & ENC_REG_MASK) | ENC_OP_RCR);
	if (r & ENC_BUSY_bm)
	{
		return ENC_PHYBSY;
	}
	if (r & ENC_SCAN_bm)
	{
		return ENC_PHYSCAN;
	}

	// TODO verify PHY address is valid??

	// fast switch from bank 3 to bank 2
	enc_exchange_byte(
			(ENC_ECON1 & ENC_REG_MASK) | ENC_OP_BFC,
			ENC_BSEL0_bm);
	// write PHY address
	enc_exchange_byte(
			(ENC_MIREGADR & ENC_REG_MASK) | ENC_OP_WCR,
			phy_register);
	// write low 8 bits
	enc_exchange_byte(
			(ENC_MIWRL & ENC_REG_MASK) | ENC_OP_WCR,
			((uint8_t) value));
	// write high 8 bits, which triggers MIIM write
	enc_exchange_byte(
			(ENC_MIWRH & ENC_REG_MASK) | ENC_OP_WCR,
			((uint8_t) (value >> 8)));

	// bank variable must end up in bank 2 where we left it
	ENC_BANK = 2;
	return ENC_OK;
}

/*
 * Starts a scanning operation on the PHY and then returns. This will
 * only start scanning if the MIIM is not busy.
 * 
 * After starting this, perform the steps indicated in 3.3.3, where you
 * check if MISTAT.NVALID is set, then read MIRDL and/or MIRDH. To stop
 * scanning, clear MICMD.MIISCAN manually.
 */
ENCSTAT enc_phy_scan(uint8_t phy_register)
{
	enc_bank(ENC_MISTAT); // always switches us to bank 3
	uint8_t r = enc_exchange_special(
			(ENC_MISTAT & ENC_REG_MASK) | ENC_OP_RCR);
	if (r & ENC_BUSY_bm)
	{
		return ENC_PHYBSY;
	}
	if (r & ENC_SCAN_bm)
	{
		return ENC_PHYSCAN;
	}

	// TODO verify PHY address is valid??

	// fast switch from bank 3 to bank 2
	enc_exchange_byte(
			(ENC_ECON1 & ENC_REG_MASK) | ENC_OP_BFC,
			ENC_BSEL0_bm);
	// write PHY address
	enc_exchange_byte(
			(ENC_MIREGADR & ENC_REG_MASK) | ENC_OP_WCR,
			phy_register);
	// write MICMD.MIISCAN
	enc_exchange_byte(
			(ENC_MICMD & ENC_REG_MASK) | ENC_OP_WCR,
			ENC_MIISCAN_bm);

	// bank variable must end up in bank 2 where we left it
	ENC_BANK = 2;
	return ENC_OK;
}

ENCSTAT enc_read_start(void)
{
	ENC_PORT.OUTCLR = ENC_PIN_CS;
	enc_swap(ENC_OP_RBM);
	return ENC_OK;
}

ENCSTAT enc_write_start(void)
{
	ENC_PORT.OUTCLR = ENC_PIN_CS;
	enc_swap(ENC_OP_WBM);
	return ENC_OK;
}

ENCSTAT enc_data_end(void)
{
	// wait in case there are remaining values in progress
	while (! (ENC_USART.STATUS & USART_TXCIF_bm));
	while (ENC_USART.STATUS & USART_RXCIF_bm)
	{
		ENC_USART.DATA;
	}
	ENC_PORT.OUTSET = ENC_PIN_CS;
	return ENC_OK;
}
