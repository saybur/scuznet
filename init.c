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

#include <avr/interrupt.h>
#include "config.h"
#include "init.h"

/*
 * JTAG function blocks some pins we need, so it must be disabled.
 */
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

static void init_vports(void)
{
	PORTCFG.VPCTRLA = DEV_VPORT0_CFG | DEV_VPORT1_CFG;
	PORTCFG.VPCTRLB = DEV_VPORT2_CFG | DEV_VPORT3_CFG;
}

void init_mcu(void)
{
	init_disable_jtag();
	init_vports();
}

void init_clock(void)
{
	// enable the 32MHz and 32.768kHz internal oscillators and wait for
	// them to become stable
	OSC.CTRL |= OSC_RC32KEN_bm | OSC_RC32MEN_bm;
	while (! (OSC.STATUS & OSC_RC32KRDY_bm));
	while (! (OSC.STATUS & OSC_RC32MRDY_bm));

	// setup the DFLL for the 32MHz frequency, then enable it
	DFLLRC32M.COMP1 = 0x12;
	DFLLRC32M.COMP2 = 0x7A;
	DFLLRC32M.CTRL = DFLL_ENABLE_bm;

	// switch system clock to the DFLL-enhanced 32MHz oscillator.
	// this register is protected, hence the ASM for time critical code
	__asm__ __volatile__(
		"ldi r24, %0"		"\n\t"
		"out %1, r24"		"\n\t"
		"ldi r24, %2"		"\n\t"
		"sts %3, r24"		"\n\t"
		:
		: "M" (CCP_IOREG_gc), "i" (&CCP),
		  "M" (CLK_SCLKSEL_RC32M_gc), "i" (&(CLK.CTRL))
		: "r24"
		);
}

void init_debug(void)
{
	DEBUG_PORT.OUTSET |= DEBUG_PIN_TX;
	DEBUG_PORT.DIRSET |= DEBUG_PIN_TX;
	DEBUG_USART.BAUDCTRLA = 3; // 500kbps
	DEBUG_USART.CTRLB |= USART_TXEN_bm;

	LED_PORT.OUT &= ~LED_PIN;
}

void init_isr(void)
{
	PMIC.CTRL |= PMIC_HILVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_LOLVLEN_bm;
	sei();
}

void init_mem(void)
{
	MEM_PORT.OUTCLR = MEM_PIN_XCK;
	MEM_PORT.OUTSET = MEM_PIN_TX | MEM_PIN_CS;
	MEM_PORT.DIRSET = MEM_PIN_XCK | MEM_PIN_TX | MEM_PIN_CS;
	MEM_PINCTRL_RX |= PORT_OPC_PULLUP_gc;
}

void mcu_reset(void)
{
	__asm__ __volatile__(
		"cli"			"\n\t"
		"ldi r24, %0"		"\n\t"
		"out %1, r24"		"\n\t"
		"ldi r24, %2"		"\n\t"
		"sts %3, r24"		"\n\t"
		:
		: "M" (CCP_IOREG_gc), "i" (&CCP),
		  "M" (RST_SWRST_bm), "i" (&(RST.CTRL))
		: "r24"
		);
}
