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

#include <avr/io.h>
#include <util/delay.h>
#include "led.h"
#include "phytest.h"

static void init_disable_jtag(void)
{
	__asm__ __volatile__(
		"ldi r24, %0"		"\n\t"
		"out %1, r24"		"\n\t"
		"ldi r24, %2"		"\n\t"
		"sts %3, r24"		"\n\t"
		:
		: "M" (CCP_IOREG_gc), "i" (&CCP),
		  "M" (MCU_JTAGD_bm), "i" (&(MCU.MCUCR))
		: "r24"
		);
}

#ifdef ENC_ENABLED
static void enc_init(void)
{
	ENC_PORT.OUTCLR = ENC_PIN_XCK;
	ENC_PORT.OUTSET = ENC_PIN_TX | ENC_PIN_CS | ENC_PIN_RST;
	ENC_PORT.DIRSET = ENC_PIN_XCK | ENC_PIN_TX | ENC_PIN_CS | ENC_PIN_RST;
	ENC_RX_PINCTRL |= PORT_OPC_PULLUP_gc;
	ENC_INT_PINCTRL |= PORT_INVEN_bm;

	_delay_ms(1);
	ENC_PORT.OUTCLR = ENC_PIN_RST;
	_delay_us(50);
	ENC_PORT.OUTSET = ENC_PIN_RST;
	_delay_ms(1);

	ENC_USART.BAUDCTRLA = ENC_USART_BAUDCTRL;
	ENC_USART.BAUDCTRLB = 0;
	ENC_USART.CTRLC = USART_CMODE_MSPI_gc;
	ENC_USART.CTRLB = USART_RXEN_bm | USART_TXEN_bm;
}
#endif

#ifdef HDD_ENABLED
static void mem_init(void)
{
	MEM_PORT.OUTCLR = MEM_PIN_XCK;
	MEM_PORT.OUTSET = MEM_PIN_TX | MEM_PIN_CS;
	MEM_PORT.DIRSET = MEM_PIN_XCK | MEM_PIN_TX | MEM_PIN_CS;
	MEM_PINCTRL_RX |= PORT_OPC_PULLUP_gc;
}
#endif

int main(void)
{
	// do bare bones board setup
	init_disable_jtag();
	PORTCFG.VPCTRLA = DEV_VPORT0_CFG | DEV_VPORT1_CFG;
	PORTCFG.VPCTRLB = DEV_VPORT2_CFG | DEV_VPORT3_CFG;
	LED_PORT.OUT &= ~LED_PIN;
	phy_init();
	#ifdef ENC_ENABLED
		enc_init();
	#endif
	#ifdef HDD_ENABLED
		mem_init();
	#endif

	// working board showing errors without this,
	// due to termination power rising too slowly?
	_delay_ms(100);

	phy_check();

	// all tests passed, pulse the LED
	uint8_t x = 0;
	while (1)
	{
		uint8_t y = x;
		if (y > 16) y = 32 - y;
		for (uint16_t i = 0; i < 500; i++)
		{
			led_on();
			for (uint8_t j = 0; j < y; j++) _delay_us(1);
			led_off();
			for (uint8_t j = 0; j < 16 - y; j++) _delay_us(1);
		}
		x++;
		if (x > 32) x = 0;
	}
	return 0;
}
