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

static uint8_t hdd_mask;
static uint8_t link_mask;
static FATFS fs;

static void main_handle(void)
{
	if (phy_is_active())
	{
		led_on();
		uint8_t target = phy_get_target();
		if (target == hdd_mask)
		{
			hdd_main();
		}
		else if (target == link_mask)
		{
			link_main();
		}
		else
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
	CONFIG_RESULT cres = config_read();
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

	// set the masks appropriately
	if (config_enet.id < 7)
	{
		link_mask = 1 << config_enet.id;
	}
	if (config_hdd.id != config_enet.id
			&& config_hdd.id < 7
			&& config_hdd.filename != NULL)
	{
		hdd_mask = 1 << config_hdd.id;
	}

	// complete setup
	phy_init(hdd_mask | link_mask);
	net_setup(config_enet.mac);
	link_init(config_enet.mac, link_mask);
	phy_init_hold();

	// mount hard drive volume
	if (hdd_mask != 0)
	{
		res = pf_open(config_hdd.filename);
		if (res)
		{
			debug(DEBUG_HDD_MOUNT_FAILED);
			while (1)
			{
				led_on();
				_delay_ms(250);
				led_off();
				_delay_ms(250);
			}
		}
		
		uint32_t size;
		res = pf_size(&size);
		if (res)
		{
			debug(DEBUG_HDD_SIZE_FAILED);
			while (1)
			{
				led_on();
				_delay_ms(250);
				led_off();
				_delay_ms(250);
			}
		}
		size >>= 9; // in 512 byte sectors
		if (size > 0)
		{
			hdd_set_ready(size);
		}
	}
	
	
	led_off();

	// and continue main handler function
	while (1)
	{
		main_handle();
	}
	return 0;
}
