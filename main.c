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
#include "lib/ff/ff.h"
#include "config.h"
#include "debug.h"
#include "enc.h"
#include "init.h"
#include "hdd.h"
#include "cdrom.h"
#include "link.h"
#include "logic.h"
#include "net.h"
#include "phy.h"
#include "test.h"

static FATFS fs;
static uint8_t exec_count = 0;
static uint16_t stack_unused = 0xFFFF;

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
			if (! link_main())
			{
				searching = 1;
			}
		}
		for (uint8_t i = 0; i < HARD_DRIVE_COUNT && searching; i++)
		{
			if (target == config_hdd[i].mask)
			{
				searching = 0;
				if (config_hdd[i].mode == HDD_MODE_CDROM)
				{
					if (! cdrom_main(i))
					{
						searching = 1;
					}
				}
				else
				{
					if (! hdd_main(i))
					{
						searching = 1;
					}
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
	else if (exec_count == 0)
	{
		uint16_t stackp = debug_stack_unused();
		if (stackp < 4)
		{
			fatal(FATAL_GENERAL, FATAL_STACK_CORRUPTED);
		}
		else if (stackp < stack_unused)
		{
			stack_unused = stackp;
			if (debug_verbose())
			{
				debug(DEBUG_MAIN_STACK_UNUSED);
				debug_dual(stackp >> 8, stackp);
			}
		}
	}

	link_check_rx();
	net_transmit_check();
	hdd_contiguous_check();
	exec_count++;
}

int main(void)
{
	// configure basic peripherals and get ISRs going
	init_mcu();
	init_clock();
	debug_init();
	led_on();
	init_dma();
	enc_init();
	init_mem();
	init_isr();
	debug(DEBUG_MAIN_RESET);

	// fail here if there was a brown-out, so we can easily tell if the
	// PSU is having problems
	uint8_t rst_stat = RST.STATUS;
	RST.STATUS = 0xFF; // clear all flags for next reboot (?)
	if (rst_stat & RST_BORF_bm)
	{
		fatal(FATAL_GENERAL, FATAL_BROWNOUT);
	}

	// mount the memory card
	uint8_t res = f_mount(&fs, "", 0);
	if (res)
	{
		fatal(FATAL_MEM_MOUNT_FAILED, res);
	}

	// read the main configuration file off the card
	uint8_t target_masks;
	config_read(&target_masks);

	// if we're executing a self-test, branch off the usual startup here
	if (GLOBAL_CONFIG_REGISTER & GLOBAL_FLAG_SELFTEST)
	{
		test_check();
		// the above call should never return, but in case it does
		// we should prevent further program execution
		while (1) {};
		return 0;
	}

	// complete setup
	phy_init(target_masks);
	if (config_enet.id != 255)
	{
		// only enable networking if asked
		net_setup(config_enet.mac);
		link_init();
	}
	uint16_t hdd_init_res = hdd_init();
	if (hdd_init_res)
	{
		fatal(hdd_init_res >> 8, hdd_init_res);
	}
	phy_init_hold();
	
	led_off();

	// and continue main handler function
	debug(DEBUG_MAIN_READY);
	while (1)
	{
		main_handle();
	}
	return 0;
}
