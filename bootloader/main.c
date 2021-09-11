/*
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

#include <avr/io.h>
#include <util/delay.h>
#include "lib/pff/pff.h"
#include "sp_driver.h"
#include "config.h"

// flash-code error conditions
#define FLASH_OK                 3
#define ERR_FLASH_VERIFY         4
#define ERR_MEM_CARD_READ        5

#ifndef FLASH_PAGE_SIZE
	#error "FLASH_PAGE_SIZE must be defined"
#elif (512 % FLASH_PAGE_SIZE != 0)
	#error "FLASH_PAGE_SIZE must be a multiple of 512"
#endif

static FATFS fs;

/*
 * Executes a software reboot.
 * 
 * The bootloader checks the reset status flags when starting, and will jump
 * to the application section immediately if not a power-on / PDI reset.
 */
static void sw_rst(void)
{
	while (1)
	{
		__asm__ __volatile__(
			"ldi r24, %0"       "\n\t"
			"out %1, r24"       "\n\t"
			"ldi r24, %2"       "\n\t"
			"sts %3, r24"       "\n\t"
			:
			: "M" (CCP_IOREG_gc), "i" (&CCP),
			  "M" (RST_SWRST_bm), "i" (&(RST.CTRL))
			: "r24"
			);
	};
}

int main(void)
{
	// skip bootloader if not a power-on reset
	uint8_t rst_stat = RST.STATUS & (RST_PORF_bm | RST_PDIRF_bm);
	if (! (rst_stat))
	{
		// jump to the application at address 0x0
		EIND = 0;
		__asm__ __volatile__(
			"clr ZL"            "\n\t"
			"clr ZH"            "\n\t"
			"ijmp"              "\n\t"
			);
	}
	// clear flags to prevent a bootloader boot-loop
	RST.STATUS = rst_stat;

	// initialize the memory card interface
	MEM_PORT.OUTCLR = MEM_PIN_XCK;
	MEM_PORT.OUTSET = MEM_PIN_TX | MEM_PIN_CS;
	MEM_PORT.DIRSET = MEM_PIN_XCK | MEM_PIN_TX | MEM_PIN_CS;
	MEM_PINCTRL_RX |= PORT_OPC_PULLUP_gc;

	// mount the memory card
	uint8_t res = pf_mount(&fs);
	if (res)
	{
		// no SD card, allow the application to handle
		sw_rst();
	}
	// open the programming file
	res = pf_open(FLASH_FILENAME);
	if (res)
	{
		// not unexpected, the file will frequently be missing
		sw_rst();
	}

	// erase the application
	SP_EraseApplicationSection();
	SP_WaitForSPM();

	// write the file to flash, starting at 0x0
	uint8_t cnt = 0;
	uint32_t addr = 0;
	uint8_t end = 0;
	uint8_t buf[FLASH_PAGE_SIZE];
	uint8_t flash[FLASH_PAGE_SIZE];
	uint16_t br;
	do
	{
		// cycle the LED periodically to show status
		if (! (cnt++ % 16)) LED_PORT.DIRTGL = LED_PIN;

		// read memory card contents
		res = pf_read(buf, FLASH_PAGE_SIZE, &br);
		if (res)
		{
			end = ERR_MEM_CARD_READ;
			break;
		}
		// note end if encountered
		if (br != FLASH_PAGE_SIZE) end = FLASH_OK;

		// write data to flash
		led_off();
		SP_LoadFlashPage(buf);
		SP_WriteApplicationPage(addr);
		SP_WaitForSPM();

		// read back to verify
		SP_ReadFlashPage(flash, addr);
		for (uint16_t i = 0; i < br; i++)
		{
			if (buf[i] != flash[i]) end = ERR_FLASH_VERIFY;
		}

		// increment flash for next iteration
		addr += FLASH_PAGE_SIZE;
	}
	while (! end);

	// programming complete
	while (1)
	{
		for (uint8_t i = 0; i < end; i++)
		{
			led_on();
			_delay_ms(750);
			led_off();
			_delay_ms(750);
		}
		_delay_ms(1500);
	}
	return 0;
}
