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

#ifndef CONFIG_H
#define CONFIG_H

/*
 * ****************************************************************************
 *   MCXBOOT CONFIGURATION
 * ****************************************************************************
 * 
 * Edit the values in this file to conform to application needs.
 */

/*
 * The filename that will be looked for on the memory card. This must be all-
 * uppercase and in a 8.3 file format.
 */
#define FLASH_FILENAME          "SCUZNET.BIN"

/*
 * Defines the LED pin and port to report status information.
 */
#define LED_PORT                PORTE
#define LED_PIN                 PIN1_bm
/*
 * Macros for turning the LED on or off. These assume the LED sinks current
 * into the microcontroller; adjust appropriately if that is not the case.
 */
#define led_on()                LED_PORT.DIR |= LED_PIN;
#define led_off()               LED_PORT.DIR &= ~LED_PIN;

/*
 * Defines the pins used when communicating with the memory card.
 */
#define MEM_USART               USARTF0
#define MEM_PORT                PORTF
#define MEM_PIN_CS              PIN0_bm
#define MEM_PIN_XCK             PIN1_bm
#define MEM_PIN_RX              PIN2_bm
#define MEM_PIN_TX              PIN3_bm
#define MEM_PINCTRL_RX          PORTF.PIN2CTRL

#endif /* CONFIG_H */
