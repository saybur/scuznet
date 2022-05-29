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

void init_dma(void)
{
	// AVR1510 was extremely helpful for understanding what is going on here
	// refer there for additional details

	// activate the DMAC peripheral itself
	DMA.CTRL |= DMA_ENABLE_bm;
	
	/*
	 * Setup individual channels for writing between USARTs and memory blocks.
	 * One channel each direction is required to support the simultaneous RX/TX
	 * of the USARTS. Thus, there is a channel used for each of:
	 * 
	 * 1) Writing to the memory card,
	 * 2) Reading from the memory card.
	 * 3) Writing to the Ethernet chip,
	 * 4) Reading from the Ethernet chip.
	 * 
	 * This sets up each channel's USART-side address and configures them
	 * for the single-shot type of transfers that the USARTs generate.
	 * 
	 * Before using, these must have:
	 * 
	 * 1) Source address set (for write channels)
	 * 2) Source increment set or cleared (for write channels)
	 * 3) Destination address set (for read channels)
	 * 4) Destination increment set or cleared (for read channels)
	 * 5) Transfer length set to the number of bytes to run via DMA.
	 * 6) Interrupts activated or deactivated.
	 * 
	 * Remember that if using DMA interrupts, TRNIF must be manually cleared.
	 */

	// memory card channels
	MEM_DMA_WRITE.DESTADDR0 = (uint8_t) ((uint16_t) &MEM_USART);
	MEM_DMA_WRITE.DESTADDR1 = (uint8_t) (((uint16_t) (&MEM_USART)) >> 8);
	MEM_DMA_WRITE.DESTADDR2 = 0;
	MEM_DMA_WRITE.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
	MEM_DMA_WRITE.TRIGSRC = MEM_DMA_TX_TRIG;
	MEM_DMA_READ.SRCADDR0 = (uint8_t) ((uint16_t) &MEM_USART);
	MEM_DMA_READ.SRCADDR1 = (uint8_t) (((uint16_t) (&MEM_USART)) >> 8);
	MEM_DMA_READ.SRCADDR2 = 0;
	MEM_DMA_READ.CTRLA = DMA_CH_BURSTLEN_1BYTE_gc | DMA_CH_SINGLE_bm;
	MEM_DMA_READ.TRIGSRC = MEM_DMA_RX_TRIG;
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
