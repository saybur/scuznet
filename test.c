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

#include <avr/cpufunc.h>
#include <util/delay.h>
#include "config.h"
#include "debug.h"
#include "enc.h"
#include "test.h"
#include "phy.h"

/*
 * Map the control signals to these values for allowing loops in testing code.
 * These should not be changed: they are set correctly for the test dongle 
 * image in the repository.
 */
#define ACK_BIT         0
#define SEL_BIT         1
#define ATN_BIT         2
#define RST_BIT         3
#define CD_BIT          4
#define IO_BIT          5
#define MSG_BIT         6
#define REQ_BIT         7
#define BSY_BIT         8
#define DBP_BIT         9

/*
 * Human-readable versions of the reporting information.
 */
const __flash char* const __flash CMD_NAMES[] = {
	(const __flash char[]) { "ACK" },
	(const __flash char[]) { "SEL" },
	(const __flash char[]) { "RST" },
	(const __flash char[]) { "C/D" },
	(const __flash char[]) { "I/O" },
	(const __flash char[]) { "MSG" },
	(const __flash char[]) { "REQ" },
	(const __flash char[]) { "BSY" },
	(const __flash char[]) { "DBP" },
};

/*
 * ============================================================================
 *   UTILITY FUNCTIONS
 * ============================================================================
 */

#ifdef PHY_PORT_DATA_IN_OE
	#define doe_off()         PHY_PORT_DOE.OUT   |=  PHY_PIN_DOE
	#define doe_on()          PHY_PORT_DOE.OUT   &= ~PHY_PIN_DOE
#else
	#define doe_off()
	#define doe_on()
#endif

#ifdef PHY_PORT_DATA_IN_CLOCK
	#define dclk_rise()       PHY_PORT_DCLK.OUT  |=  PHY_PIN_DCLK
	#define dclk_fall()       PHY_PORT_DCLK.OUT  &= ~PHY_PIN_DCLK
#else
	#define dclk_rise()
	#define dclk_fall()       _NOP()
#endif

/*
 * Flashes the LED in the given long/short/long pattern.
 */
static void led_flash(uint8_t l, uint8_t s, uint8_t x)
{
	while (1)
	{
		for (uint8_t i = 0; i < l; i++)
		{
			led_on();
			_delay_ms(LED_LONG_FLASH);
			led_off();
			_delay_ms(LED_LONG_FLASH);
		}
		_delay_ms(LED_BREAK);
		for (uint8_t i = 0; i < s; i++)
		{
			led_on();
			_delay_ms(LED_SHORT_FLASH);
			led_off();
			_delay_ms(LED_SHORT_FLASH);
		}
		_delay_ms(LED_BREAK);
		for (uint8_t i = 0; i < x; i++)
		{
			led_on();
			_delay_ms(LED_LONG_FLASH);
			led_off();
			_delay_ms(LED_LONG_FLASH);
		}
		_delay_ms((LED_BREAK * 3));
	}
}

/*
 * Returns the first set bit in the given value, LSB first, where '1' is the
 * first, '2' is the second, etc. If none is found 0 is returned.
 */
static uint8_t test_set_bits(uint8_t value)
{
	for (uint8_t i = 0; i < 8; i++)
	{
		if ((1 << i) & value)
		{
			return i + 1;
		}
	}
	return 0;
}

/*
 * ============================================================================
 *   PHY TESTING ROUTINES
 * ============================================================================
 */

/*
 * Variant on the normal phy_init() that does not start driving the lines.
 */
void test_phy_init(void)
{
	#ifdef PHY_PORT_DATA_IN_ACKEN
		PHY_PORT_ACKEN.OUT &= ~PHY_PIN_ACKEN;
		PHY_PORT_ACKEN.DIR |=  PHY_PIN_ACKEN;
	#endif
	#ifdef PHY_PORT_DATA_IN_CLOCK
		PHY_PORT_DCLK.OUT  &= ~PHY_PIN_DCLK;
		PHY_PORT_DCLK.DIR  |=  PHY_PIN_DCLK;
	#endif

	#ifdef PHY_PORT_DATA_IN_OE
		doe_off();
		PHY_PORT_DOE.DIR |= PHY_PIN_DOE;
		PHY_PORT_DATA_IN.PIN0CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN1CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN2CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN3CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN4CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN5CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN6CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN7CTRL |= PORT_OPC_PULLUP_gc;
	#endif

	#ifdef PHY_PORT_DATA_IN_INVERT
		PHY_PORT_DATA_IN.PIN0CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN1CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN2CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN3CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN4CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN5CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN6CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN7CTRL |= PORT_INVEN_bm;
	#endif
}

static uint8_t phy_read(void)
{
	uint8_t raw;

	#if defined(PHY_PORT_DATA_IN_OE) || defined(PHY_PORT_DATA_IN_CLOCK)
		dclk_rise();
		doe_on();
		dclk_fall();
		raw = PHY_PORT_DATA_IN.IN;
		doe_off();
	#else
		raw = PHY_PORT_DATA_IN.IN;
	#endif

	#ifdef PHY_PORT_DATA_IN_REVERSED
		return phy_reverse_table[raw];
	#else
		return raw;
	#endif
}

static uint16_t phy_read_ctrl(void)
{
	uint16_t v = 0;

	// perform reading on the input lines tied to the inverter
	if (PHY_PORT_R_SEL.IN & PHY_PIN_R_SEL) v |= _BV(SEL_BIT);
	if (PHY_PORT_R_ATN.IN & PHY_PIN_R_ATN) v |= _BV(ATN_BIT);
	if (PHY_PORT_R_RST.IN & PHY_PIN_R_RST) v |= _BV(RST_BIT);
	if (PHY_PORT_R_ACK.IN & PHY_PIN_R_ACK) v |= _BV(ACK_BIT);
	if (PHY_PORT_R_BSY.IN & PHY_PIN_R_BSY) v |= _BV(BSY_BIT);
	if (PHY_PORT_R_DBP.IN & PHY_PIN_R_DBP) v |= _BV(DBP_BIT);

	// then do the lines crossed-over to the data pin reader
	uint8_t d = phy_read();
	if (d & _BV(REQ_BIT)) v |= _BV(REQ_BIT);
	if (d & _BV(MSG_BIT)) v |= _BV(MSG_BIT);
	if (d & _BV(IO_BIT)) v |= _BV(IO_BIT);
	if (d & _BV(CD_BIT)) v |= _BV(CD_BIT);

	return v;
}

static void test_phy_ctrl(VPORT_t* port, uint8_t bitmask, uint8_t bitpos)
{
	// drive lines and let them stabilize
	port->OUT |= bitmask;
	port->DIR |= bitmask;
	_delay_us(0.2);

	// read information
	uint16_t cmask = phy_read_ctrl();

	// release lines
	port->DIR &= ~bitmask;
	port->OUT &= ~bitmask;

	// check the pin that should be driven to '1'
	if (! (cmask & _BV(bitpos))) led_flash(4, bitpos + 1, 2);

	// then make sure all other pins are '0'
	cmask &= ~_BV(bitpos);
	if (cmask)
	{
		uint16_t m = 1;
		for (uint8_t i = 1; i <= 10; i++)
		{
			if (cmask & m) led_flash(5, bitpos + 1, i);
			m = m << 1;
		}
	}
}

/*
 * As above, but for the lines that are looped-back.
 */
static void test_phy_ctrl2(PORT_t* port, uint8_t bitmask, uint8_t bitpos)
{
	// drive lines and let them stabilize
	port->OUT |= bitmask;
	port->DIR |= bitmask;
	_delay_us(0.2);

	// read information
	uint16_t cmask = phy_read_ctrl();

	// release lines
	port->DIR &= ~bitmask;
	port->OUT &= ~bitmask;

	// check the pin that should be driven to '1'
	if (! (cmask & _BV(bitpos))) led_flash(4, bitpos + 1, 2);

	// then make sure all other pins are '0'
	cmask &= ~_BV(bitpos);
	if (cmask)
	{
		uint16_t m = 1;
		for (uint8_t i = 1; i <= 10; i++)
		{
			if (cmask & m) led_flash(5, bitpos + 1, i);
			m = m << 1;
		}
	}
}

static void test_phy(void)
{
	/*
	 * Verify all data output pins are reading logical low. If not, this might
	 * indicate that something is shorted, or the external pull-downs are not
	 * present.
	 */
	uint8_t v = PHY_PORT_DATA_OUT.IN;
	if (v)
	{
		uint8_t m = 1;
		for (uint8_t i = 1; i <= 8; i++)
		{
			if (v & m) led_flash(1, i, 0);
			m = m << 1;
		}
	}

	/*
	 * Iterate through the data lines looking for shorts. We'll do them one at
	 * a time, triggering an error if asserting one line causes any other data
	 * line to become asserted.
	 */
	PHY_PORT_DATA_OUT.DIR = 0x00;
	PHY_PORT_DATA_OUT.OUT = 0xFF;
	uint8_t dmask = 1;
	for (uint8_t i = 1; i <= 8; i++)
	{
		// drive the line, and wait a moment for it to stabilize
		PHY_PORT_DATA_OUT.DIR = dmask;
		_delay_us(1);

		// test the output port: any T_DBx neighbors suddenly high?
		uint8_t read = PHY_PORT_DATA_OUT.IN;
		if (read != dmask)
		{
			PHY_PORT_DATA_OUT.DIR = 0x00;
			read &= ~dmask;
			led_flash(2, i, test_set_bits(read));
		}

		// now the reading port
		read = phy_read();
		if (read != dmask)
		{
			PHY_PORT_DATA_OUT.DIR = 0x00;
			read &= ~dmask;
			led_flash(3, i, test_set_bits(read));
		}

		PHY_PORT_DATA_OUT.DIR = 0x00;
		dmask = dmask << 1;
	}

	// check for bad control lines when nothing is asserted
	uint16_t cmask = phy_read_ctrl();
	if (cmask)
	{
		uint16_t m = 1;
		for (uint16_t i = 1; i <= 10; i++)
		{
			if (cmask & m) led_flash(4, i, 1);
			m = m << 1;
		}
	}

	// check control lines with drivers
	test_phy_ctrl(&PHY_PORT_T_BSY, PHY_PIN_T_BSY, BSY_BIT);
	test_phy_ctrl(&PHY_PORT_T_DBP, PHY_PIN_T_DBP, DBP_BIT);
	test_phy_ctrl(&PHY_PORT_T_SEL, PHY_PIN_T_SEL, SEL_BIT);
	test_phy_ctrl(&PHY_PORT_T_REQ, PHY_PIN_T_REQ, REQ_BIT);
	test_phy_ctrl(&PHY_PORT_T_IO, PHY_PIN_T_IO, IO_BIT);
	test_phy_ctrl(&PHY_PORT_T_CD, PHY_PIN_T_CD, CD_BIT);
	test_phy_ctrl(&PHY_PORT_T_MSG, PHY_PIN_T_MSG, MSG_BIT);

	// then check control lines without drivers via the data pin drivers
	test_phy_ctrl2(&PHY_PORT_DATA_OUT, _BV(ATN_BIT), ATN_BIT);
	test_phy_ctrl2(&PHY_PORT_DATA_OUT, _BV(ACK_BIT), ACK_BIT);
	test_phy_ctrl2(&PHY_PORT_DATA_OUT, _BV(RST_BIT), RST_BIT);
}

/*
 * ============================================================================
 *   ETHERNET TESTING ROUTINES
 * ============================================================================
 */

static void test_enc(void)
{
	uint8_t v;
	enc_cmd_read(ENC_ERDPTL, &v);
	if (v != 0xFA) led_flash(6, 1, 0);
	v = 0xAA;
	enc_cmd_write(ENC_ERDPTL, v);
	enc_cmd_read(ENC_ERDPTL, &v);
	if (v != 0xAA) led_flash(6, 2, 0);
	uint16_t vl;
	v = enc_phy_read(ENC_PHY_PHID1, &vl);
	if (v) led_flash(6, 3, 0);
	if (vl != 0x0083) led_flash(6, 4, 0);
}

/*
 * ============================================================================
 *   PUBLIC FUNCTIONS
 * ============================================================================
 */

void test_check(void)
{
	// disable all interrupts
	PMIC.CTRL = 0;
	// disable the watchdog timer if it happens to be running
	__asm__ __volatile__(
		"ldi r24, %0"		"\n\t"
		"out %1, r24"		"\n\t"
		"ldi r24, %2"		"\n\t"
		"sts %3, r24"		"\n\t"
		:
		: "M" (CCP_IOREG_gc), "i" (&CCP),
		  "M" (WDT_CEN_bm), "i" (&(WDT.CTRL))
		: "r24"
		);

	// init the PHY to avoid floating signals unnecessarily
	test_phy_init();

	// give a 5 second warning that we're about to do a self-test
	// I'm open to suggestions about better ways to do this
	for (uint8_t i = 0; i < 25; i++)
	{
		// 5x/sec
		led_on();
		_delay_ms(200);
		led_off();
		_delay_ms(200);
	}
	// wait two more seconds before executing tests
	_delay_ms(2000);

	// perform tests sequentially
	test_phy();
	test_enc();

	// all tests passed, pulse the LED forever
	uint8_t x = 0;
	while (1)
	{
		uint8_t y = x;
		if (y > 16) y = 32 - y;
		for (uint16_t i = 0; i < 1000; i++)
		{
			led_on();
			for (uint8_t j = 0; j < y; j++) _delay_us(1);
			led_off();
			for (uint8_t j = 0; j < 16 - y; j++) _delay_us(1);
		}
		x++;
		if (x > 32) x = 0;
	}
}
