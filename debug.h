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
 */
#define DEBUG_MAIN_ACTIVE_NO_TARGET               0x10
#define DEBUG_CONFIG_FILE_MISSING                 0x11
#define DEBUG_CONFIG_LOAD_FAILED                  0x12
#define DEBUG_CONFIG_READ_ERROR                   0x13
#define DEBUG_CONFIG_MEMORY_ERROR                 0x14
#define DEBUG_LOGIC_BAD_LUN                       0x50
#define DEBUG_LOGIC_BAD_CMD                       0x52
#define DEBUG_LOGIC_BAD_CMD_ARGS                  0x53
#define DEBUG_LOGIC_UNKNOWN_MESSAGE               0x5E
#define DEBUG_LOGIC_LINK_UNKNOWN_MESSAGE          0x5D
#define DEBUG_LOGIC_MESSAGE                       0x5F
#define DEBUG_HDD_MODE_SENSE                      0x7B
#define DEBUG_HDD_MODE_SELECT                     0x7C
#define DEBUG_HDD_READ_BUFFER                     0x7D
#define DEBUG_HDD_WRITE_BUFFER                    0x7E
#define DEBUG_HDD_VERIFY                          0x7F
#define DEBUG_HDD_READ_STARTING                   0x80
#define DEBUG_HDD_READ_OKAY                       0x81
#define DEBUG_HDD_WRITE_STARTING                  0x82
#define DEBUG_HDD_WRITE_OKAY                      0x83
#define DEBUG_HDD_READ_SINGLE                     0x86
#define DEBUG_HDD_READ_MULTIPLE                   0x87
#define DEBUG_HDD_WRITE_SINGLE                    0x88
#define DEBUG_HDD_WRITE_MULTIPLE                  0x89
#define DEBUG_HDD_PACKET_START                    0x8A
#define DEBUG_HDD_PACKET_END                      0x8B
#define DEBUG_HDD_NOT_READY                       0x90
#define DEBUG_HDD_MEM_SEEK_ERROR                  0x91
#define DEBUG_HDD_MEM_READ_ERROR                  0x92
#define DEBUG_HDD_MEM_WRITE_ERROR                 0x93
#define DEBUG_HDD_INVALID_OPERATION               0x94
#define DEBUG_HDD_MOUNT_FAILED                    0x95
#define DEBUG_HDD_FILE_SIZE_FAILED                0x96
#define DEBUG_HDD_FOPEN_FAILED                    0x97
#define DEBUG_HDD_INVALID_FILE                    0x98
#define DEBUG_HDD_SIZE_EXCEEDED                   0x99
#define DEBUG_HDD_IOCTRL_ERROR                    0x9A
#define DEBUG_HDD_NATIVE_VOLUME_SIZE_ERROR        0x9B
#define DEBUG_LINK_TX_REQUESTED                   0xA0
#define DEBUG_LINK_SHORT_TX_START                 0xA4
#define DEBUG_LINK_SHORT_TX_DONE                  0xA5
#define DEBUG_LINK_INQUIRY                        0xA8
#define DEBUG_LINK_RX_ASKING_RESEL                0xB0
#define DEBUG_LINK_RX_SKIP                        0xB1
#define DEBUG_LINK_RX_STARTING                    0xB2
#define DEBUG_LINK_RX_PACKET_START                0xB3
#define DEBUG_LINK_RX_PACKET_DONE                 0xB4
#define DEBUG_LINK_RX_ENDING                      0xB5
#define DEBUG_LINK_RX_FILTER_UNICAST              0xBA
#define DEBUG_LINK_RX_FILTER_MULTICAST            0xBB
#define DEBUG_PHY_RESELECT_REQUESTED              0xD0
#define DEBUG_PHY_RESELECT_STARTING               0xD1
#define DEBUG_PHY_RESELECT_ARB_LOST               0xD2
#define DEBUG_PHY_RESELECT_ARB_WON                0xD3
#define DEBUG_PHY_RESELECT_ARB_INTERRUPTED        0xD4
#define DEBUG_PHY_RESELECT_FINISHED               0xD5
#define DEBUG_MEM_READ_SINGLE_FAILED              0xE0
#define DEBUG_MEM_READ_MUL_CMD_FAILED             0xE1
#define DEBUG_MEM_READ_MUL_FIRST_FAILED           0xE2
#define DEBUG_MEM_READ_MUL_TIMEOUT                0xE3
#define DEBUG_MEM_READ_MUL_FUNC_ERR               0xE4
#define DEBUG_MEM_READ_MUL_DMA_ERR                0xE5

// LED control macros
#define led_on()              LED_PORT.DIR |= LED_PIN;
#define led_off()             LED_PORT.DIR &= ~LED_PIN;

// alias for use below
#define debug_enabled()      1// (GLOBAL_CONFIG_REGISTER & GLOBAL_FLAG_DEBUG)
#define debug_verbose()      1// (GLOBAL_CONFIG_REGISTER & GLOBAL_FLAG_VERBOSE)

#ifdef DEBUGGING
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
#else
// silence the compiler when debugging is turned off
static inline __attribute__((always_inline)) void debug(
		__attribute__((unused)) uint8_t v)
{
	// do nothing
}
static inline __attribute__((always_inline)) void debug(
		__attribute__((unused)) uint8_t v, __attribute__((unused)) uint8_t p)
{
	// do nothing
}
#endif

#endif /* DEBUG_H */
