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
#include "config.h"
#include "debug.h"
#include "enc.h"
#include "init.h"
#include "hdd.h"
#include "link.h"
#include "logic.h"
#include "mem.h"
#include "net.h"
#include "phy.h"

static uint8_t hdd_mask;
static uint8_t link_mask;

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
	enc_init();
	mem_init();
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

	// read device configuration and assign values we need from it
	uint8_t device_config[CONFIG_EEPROM_LENGTH];
	config_read(device_config);
	GLOBAL_CONFIG_REGISTER = device_config[CONFIG_OFFSET_FLAGS];
	hdd_mask = 1 << device_config[CONFIG_OFFSET_ID_HDD];
	link_mask = 1 << device_config[CONFIG_OFFSET_ID_LINK];

	// print the configuration out if requested
	#ifdef DEBUGGING
	debug(DEBUG_CONFIG_START);
	for (uint8_t i = 0; i < CONFIG_EEPROM_LENGTH; i++)
	{
		debug(device_config[i]);
	}
	#endif

	phy_init(hdd_mask | link_mask);
	net_setup(device_config + CONFIG_OFFSET_MAC);
	link_init(device_config + CONFIG_OFFSET_MAC, link_mask);
	phy_init_hold();

	// initialize the memory card
	uint8_t v;
	do
	{
		v = mem_init_card();
		main_handle();
	}
	while (v < 0x80);
	debug_dual(DEBUG_MAIN_MEM_INIT_FOLLOWS, v);

	// get the card size and mark it as OK if possible
	if (v == 0xFF)
	{
		uint8_t csd[16];
		if (mem_read_csd(csd))
		{
			uint32_t size = mem_size(csd);
			hdd_set_ready(size);
		}
		else
		{
			debug(DEBUG_MAIN_BAD_CSD_REQUEST);
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
