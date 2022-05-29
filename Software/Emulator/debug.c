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

#include <util/delay.h>
#include "config.h"
#include "debug.h"

extern uint8_t _end;
extern uint8_t __stack;

/*
 * Fills the stack with 0xC5 to assist with stack usage diagnostics. Both this
 * and the subsequent debug_stack_check() are (very slightly) modified from the
 * public-domain code here:
 * 
 * https://www.avrfreaks.net/forum/soft-c-avrgcc-monitoring-stack-usage
 */
void stack_paint(void) __attribute__ ((naked)) __attribute__ ((section (".init1")));
void stack_paint(void)
{
	__asm__ __volatile__(
		"ldi r30, lo8(_end)"		"\n\t"
		"ldi r31, hi8(_end)"		"\n\t"
		"ldi r24, lo8(0xC5)"		"\n\t"
		"ldi r25, hi8(__stack)"		"\n\t"
		".paint_loop:"				"\n\t"
		"st Z+,r24"					"\n\t"
		".paint_cmp:"				"\n\t"
		"cpi r30, lo8(__stack)"		"\n\t"
		"cpc r31, r25"				"\n\t"
		"brlo .paint_loop"			"\n\t"
		"breq .paint_loop"			"\n\t"
		);
}

void debug_init(void)
{
	DEBUG_PORT.OUTSET |= DEBUG_PIN_TX;
	DEBUG_PORT.DIRSET |= DEBUG_PIN_TX;
	DEBUG_USART.BAUDCTRLA = 3; // 500kbps
	DEBUG_USART.CTRLB |= USART_TXEN_bm;

#if defined(LED_POW_PORT) && defined(LED_POW_PIN)
	LED_POW_PORT.DIRSET = LED_POW_PIN;
#endif

	LED_PORT.OUT &= ~LED_PIN;
}

uint16_t debug_stack_unused(void)
{
	const uint8_t *p = &_end;
	uint16_t c = 0;
	while (*p == 0xC5 && p <= &__stack)
	{
		p++; c++;
	}
	return c;
}

void fatal(uint8_t lflash, uint8_t sflash)
{
	// disable all but high-level (/RST) interrupts
	PMIC.CTRL = PMIC_HILVLEN_bm;
	// disable the watchdog timer
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

	// report to the debugger
	debug(DEBUG_FATAL);
	debug_dual(lflash, sflash);

	// begin flash pattern
	led_off();
	_delay_ms(LED_BREAK);
	while (1)
	{
		for (uint8_t i = 0; i < lflash; i++)
		{
			led_on();
			_delay_ms(LED_LONG_FLASH);
			led_off();
			_delay_ms(LED_LONG_FLASH);
		}
		_delay_ms(LED_BREAK);
		for (uint8_t i = 0; i < sflash; i++)
		{
			led_on();
			_delay_ms(LED_SHORT_FLASH);
			led_off();
			_delay_ms(LED_SHORT_FLASH);
		}
		_delay_ms(LED_BREAK);
	}
}
