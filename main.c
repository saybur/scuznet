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

#include <stdlib.h>
#include <avr/io.h>
#include <util/delay.h>
#include "lib/pff/pff.h"
#include "config.h"
#include "debug.h"
#include "enc.h"
#include "init.h"
#include "hdd.h"
#include "link.h"
#include "logic.h"
#include "net.h"
#include "phy.h"

static FATFS fs;

static void main_handle(void)
{
	if (logic_ready())
	{
		uint8_t searching = 1;

		led_on();
		uint8_t target = phy_get_target();
		if (target == config_enet.mask)
		{
			searching = 0;
			link_main();
		}
		for (uint8_t i = 0; i < HARD_DRIVE_COUNT && searching; i++)
		{
			if (target == config_hdd[i].mask)
			{
				searching = 0;
				if (! hdd_main(i))
				{
					searching = 1;
				}
			}
		}

		if (searching)
		{
			debug_dual(DEBUG_MAIN_ACTIVE_NO_TARGET, phy_get_target());
			logic_done();
		}

		led_off();
	}

	link_check_rx();
}

int main(void)
{
	// configure basic peripherals and get ISRs going
	init_mcu();
	init_clock();
	init_debug();
	led_on();
	init_dma();
	enc_init();
	init_mem();
	init_isr();

	// fail here if there was a brown-out, so we can easily tell if the
	// PSU is having problems
	uint8_t rst_stat = RST.STATUS;
	RST.STATUS = 0xFF; // clear all flags for next reboot (?)
	if (rst_stat & RST_BORF_bm)
	{
		while (1)
		{
			led_on();
			_delay_ms(500);
			led_off();
			_delay_ms(500);
		}
	}

	// mount the memory card
	uint8_t res = pf_mount(&fs);
	if (res)
	{
		while (1)
		{
			led_on();
			_delay_ms(1000);
			led_off();
			_delay_ms(1000);
		}
	}

	// attempt to read the main configuration file off the card
	uint8_t target_masks;
	CONFIG_RESULT cres = config_read(&target_masks);
	if (cres)
	{
		while (1)
		{
			led_on();
			_delay_ms(100);
			led_off();
			_delay_ms(100);
		}
	}

	// complete setup
	phy_init(target_masks);
	if (config_enet.id != 255)
	{
		// only enable networking if asked
		net_setup(config_enet.mac);
		link_init(config_enet.mac, config_enet.mask);
	}
	if (! hdd_init())
	{
		while (1)
		{
			led_on();
			_delay_ms(250);
			led_off();
			_delay_ms(250);
		}
	}
	phy_init_hold();
	
	led_off();

	// and continue main handler function
	while (1)
	{
		main_handle();
	}
	return 0;
}
