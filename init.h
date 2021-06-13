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

#ifndef INIT_H
#define INIT_H

/*
 * Performs a few initial setup items:
 * 
 * 1) Disables the JTAG interface to free up the pins, and
 * 2) Sets up the VPORTs.
 * 
 * This should only be called once, from main(), during initial MCU startup.
 */
void init_mcu(void);

/*
 * Sets up the internal oscillator, calibrated with the ~32kHz internal
 * oscillator via the DFLL, and switches the system clock over to it.
 * 
 * This should only be called once, from main(), during initial MCU startup.
 */
void init_clock(void);

/*
 * Initializes the USART that sends debugging information and sets up the
 * output status LED.
 * 
 * This should only be called once, from main(), during initial MCU startup.
 */
void init_debug(void);

/*
 * Sets up the PMIC for all interrupt levels and activates interrupts.
 */
void init_isr(void);

/*
 * Sets up the memory card interface pins.
 */
void init_mem(void);

/*
 * Executes an MCU reset via the protected registers. This also disables
 * interrupts as part of the operation.
 */
void mcu_reset(void);

#endif /* INIT_H */
