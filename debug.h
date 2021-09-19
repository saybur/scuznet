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

#ifndef DEBUG_H
#define DEBUG_H

#include <avr/io.h>

/*
 * Some constants for spitting out the current position of work, i.e. the
 * printf() of the embedded world. I should really get a proper debugging
 * rig at some point...
 * 
 * The following comment for each line is the number of trailing bytes expected
 * for each symbol.
 */
#define DEBUG_MAIN_ACTIVE_NO_TARGET               0x10 // 1
#define DEBUG_MAIN_STACK_UNUSED                   0x1D // 2
#define DEBUG_MAIN_RESET                          0x1E // 0
#define DEBUG_MAIN_READY                          0x1F // 0
#define DEBUG_LOGIC_BAD_LUN                       0x50 // 0
#define DEBUG_LOGIC_BAD_CMD                       0x52 // 1
#define DEBUG_LOGIC_BAD_CMD_ARGS                  0x53 // 0
#define DEBUG_LOGIC_SET_SENSE                     0x54 // 1
#define DEBUG_LOGIC_UNKNOWN_MESSAGE               0x5E // 1
#define DEBUG_LOGIC_MESSAGE                       0x5F // 1
#define DEBUG_CDROM_READ_STARTING                 0x60 // 0
#define DEBUG_CDROM_READ_OKAY                     0x61 // 0
#define DEBUG_CDROM_MEM_SEEK_ERROR                0x62 // 1
#define DEBUG_CDROM_MEM_READ_ERROR                0x63 // 1
#define DEBUG_CDROM_SIZE_EXCEEDED                 0x64 // 0
#define DEBUG_CDROM_INVALID_OPERATION             0x65 // 0
#define DEBUG_HDD_MODE_SENSE                      0x7B // 0
#define DEBUG_HDD_MODE_SELECT                     0x7C // 0
#define DEBUG_HDD_READ_BUFFER                     0x7D // 0
#define DEBUG_HDD_WRITE_BUFFER                    0x7E // 0
#define DEBUG_HDD_VERIFY                          0x7F // 0
#define DEBUG_HDD_READ_STARTING                   0x80 // 0
#define DEBUG_HDD_READ_OKAY                       0x81 // 0
#define DEBUG_HDD_WRITE_STARTING                  0x82 // 0
#define DEBUG_HDD_WRITE_OKAY                      0x83 // 0
#define DEBUG_HDD_SEEK                            0x8C // 0
#define DEBUG_HDD_NOT_READY                       0x90 // 0
#define DEBUG_HDD_MEM_SEEK_ERROR                  0x91 // 1
#define DEBUG_HDD_MEM_READ_ERROR                  0x92 // 1
#define DEBUG_HDD_MEM_WRITE_ERROR                 0x93 // 1
#define DEBUG_HDD_INVALID_OPERATION               0x94 // 0
#define DEBUG_HDD_SIZE_EXCEEDED                   0x95 // 0
#define DEBUG_HDD_CHECK_REJECTED                  0x96 // 1
#define DEBUG_HDD_CHECK_FAILED                    0x97 // 1
#define DEBUG_HDD_CHECK_SUCCESS                   0x98 // 1
#define DEBUG_HDD_CHECK_FORCED                    0x99 // 1
#define DEBUG_HDD_LBA                             0x9A // 4
#define DEBUG_HDD_LENGTH                          0x9B // 2
#define DEBUG_LINK_TX_REQUESTED                   0xA0 // 0
#define DEBUG_LINK_SHORT_TX_START                 0xA4 // 0
#define DEBUG_LINK_SHORT_TX_DONE                  0xA5 // 0
#define DEBUG_LINK_INQUIRY                        0xA8 // 0
#define DEBUG_LINK_DISCONNECT                     0xAB // 0
#define DEBUG_LINK_UNKNOWN_MESSAGE                0xAC // 1
#define DEBUG_LINK_UNKNOWN_EXTENDED_MESSAGE       0xAD // 1 + X
#define DEBUG_LINK_FILTER                         0xAE // 1
#define DEBUG_LINK_FILTER_UNKNOWN                 0xAF // 9
#define DEBUG_LINK_RX_ASKING_RESEL                0xB0 // 0
#define DEBUG_LINK_RX_SKIP                        0xB1 // 0
#define DEBUG_LINK_RX_NO_DATA                     0xB2 // 0
#define DEBUG_LINK_RX_STARTING                    0xB3 // 0
#define DEBUG_LINK_RX_PACKET_START                0xB4 // 0
#define DEBUG_LINK_RX_PACKET_DONE                 0xB6 // 0
#define DEBUG_LINK_RX_PACKET_TRUNCATED            0xB8 // 1
#define DEBUG_LINK_RX_ENDING                      0xBF // 0
#define DEBUG_NET_TX_TIMEOUT_RETRANSMIT           0xC0 // 0
#define DEBUG_NET_TX_ERROR_RETRANSMIT             0xC1 // 0
#define DEBUG_PHY_RESELECT_REQUESTED              0xD0 // 0
#define DEBUG_PHY_RESELECT_STARTING               0xD1 // 0
#define DEBUG_PHY_RESELECT_ARB_LOST               0xD2 // 0
#define DEBUG_PHY_RESELECT_ARB_WON                0xD3 // 0
#define DEBUG_PHY_RESELECT_ARB_INTERRUPTED        0xD4 // 0
#define DEBUG_PHY_RESELECT_FINISHED               0xD5 // 0
#define DEBUG_PHY_TIMED_OUT                       0xD6 // 0
#define DEBUG_MEM_READ_SINGLE_FAILED              0xE0 // 0
#define DEBUG_MEM_READ_MUL_CMD_FAILED             0xE1 // 1
#define DEBUG_MEM_READ_MUL_FIRST_FAILED           0xE2 // 0
#define DEBUG_MEM_READ_MUL_TIMEOUT                0xE3 // 1
#define DEBUG_MEM_READ_MUL_FUNC_ERR               0xE4 // 0
#define DEBUG_MEM_READ_MUL_DMA_ERR                0xE5 // 0
#define DEBUG_MEM_READ_SOFT_ERROR                 0xE7 // 0
#define DEBUG_MEM_DMA_UNDERFLOW                   0xE8 // 0
#define DEBUG_FATAL                               0xEF // 2

/*
 * Fatal error codes. Codes 1-4 are reserved for the hard drive devices. This
 * first batch are the long flash codes.
 */
#define FATAL_CONFIG_FILE                         5
#define FATAL_CONFIG_LINE_READ                    6
#define FATAL_GENERAL                             7
#define FATAL_MEM_MOUNT_FAILED                    8
// short codes
#define FATAL_BROWNOUT                            2
#define FATAL_STACK_CORRUPTED                     3
#define FATAL_MISALIGNED                          4

// the LED on and off time for each flash, in milliseconds
#define LED_LONG_FLASH                            500
#define LED_SHORT_FLASH                           200
#define LED_BREAK                                 1000

// LED control macros
#define led_on()              LED_PORT.DIR |= LED_PIN;
#define led_off()             LED_PORT.DIR &= ~LED_PIN;

// alias for use below
#define debug_enabled()       (GLOBAL_CONFIG_REGISTER & GLOBAL_FLAG_DEBUG)
#define debug_verbose()       (GLOBAL_CONFIG_REGISTER & GLOBAL_FLAG_VERBOSE)

// calls for sending bytes to the print-style debugging system
static inline __attribute__((always_inline)) void debug(uint8_t v)
{
	if (debug_enabled())
	{
		while (! (DEBUG_USART.STATUS & USART_DREIF_bm));
		DEBUG_USART.DATA = v;
	}
}
static inline __attribute__((always_inline)) void debug_dual(
		uint8_t v, uint8_t p)
{
	if (debug_enabled())
	{
		while (! (DEBUG_USART.STATUS & USART_DREIF_bm));
		DEBUG_USART.DATA = v;
		while (! (DEBUG_USART.STATUS & USART_DREIF_bm));
		DEBUG_USART.DATA = p;
	}
}

/*
 * Sets up the debugging system, including the USART and the reporting LEDs.
 * 
 * This should only be called once, from main(), during initial MCU startup.
 */
void debug_init(void);

/*
 * Calculates the amount of stack space not yet used, using the "painting" done
 * during startup. This method is not foolproof but should give a good idea of
 * how much stack is being used during program execution.
 */
uint16_t debug_stack_unused();

/*
 * Called to halt processing when a fatal condition is detected.
 * 
 * When invoked, this will disable interrupts and enter an infinite loop.
 * During the loop, this will show a series of long flashes followed by a
 * series of short flashes, per the given parameters.
 */
void fatal(uint8_t lflash, uint8_t sflash);

#endif /* DEBUG_H */
