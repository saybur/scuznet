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
#include "led.h"

void led_flash(uint8_t l, uint8_t s, uint8_t x)
{
	while (1)
	{
		for (uint8_t i = 0; i < l; i++)
		{
			led_on();
			_delay_ms(500);
			led_off();
			_delay_ms(500);
		}
		_delay_ms(1500);
		for (uint8_t i = 0; i < s; i++)
		{
			led_on();
			_delay_ms(200);
			led_off();
			_delay_ms(200);
		}
		_delay_ms(1500);
		for (uint8_t i = 0; i < x; i++)
		{
			led_on();
			_delay_ms(500);
			led_off();
			_delay_ms(500);
		}
		_delay_ms(4000);
	}
}
