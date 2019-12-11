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

#ifndef MEM_H
#define MEM_H

#include <avr/io.h>

/*
 * Abstraction layer over memory card operations.
 * 
 * Users must first initialize the device by using mem_init() and
 * mem_init_card(), following the instructions in those functions. Users may
 * then execute operations via the following sequence:
 * 
 * 1) mem_op_start(), checking the response to see if the card is busy.
 * 2) mem_op_cmd() to send a command, checking the command response.
 * 3) Perform any command-specific data transfers.
 * 4) mem_op_end() to stop.
 *
 * Here's a simple example, reading a single block (CMD17) from address 0 and
 * writing each byte of the response (including CRC) to a debug() function:
 * 
 * if (mem_op_start())
 * {
 *   uint8_t arg[4] = { 0x00, 0x00, 0x00, 0x00 };
 *   uint8_t r = mem_op_cmd_args(17, arg);
 *   if (r == 0x00)
 *   {
 *     r = mem_wait_for_data();
 *     if (r == 0xFE)
 *     {
 *       for (uint16_t i = 0; i < 514; i++)
 *       {
 *         MEM_USART.DATA = 0xFF;
 *         while (mem_data_not_ready());
 *         debug(MEM_USART.DATA);
 *       }
 *     }
 *   }
 *   mem_op_end();
 * }
 * 
 * Note the above doesn't do much error checking, beyond verifying that the
 * response codes are sensible.
 * 
 * There are also a limited number of higher-level operations available. Refer
 * to those functions for more details.
 */

/*
 * Status codes returned by mem_init_card(). Most are not terribly important,
 * and are mainly of interest for reporting during debugging, but a few are
 * defined below and may be useful for checking state.
 * 
 * Note that all values between 128 and 254 are reserved for various failures
 * that will prevent the system from coming online correctly, so that range
 * can be checked for specifically.
 */
#define MEM_ISTATE_STARTING             0
#define MEM_ISTATE_NATIVE_WAIT          1
#define MEM_ISTATE_RESET                2
#define MEM_ISTATE_SEND_COND            3
#define MEM_ISTATE_MODERN_LOOP          4
#define MEM_ISTATE_MODERN_CMD58         5
#define MEM_ISTATE_LEGACY_LOOP          6
#define MEM_ISTATE_OLDEST_LOOP          7
#define MEM_ISTATE_BLOCK_SIZE_SET       8
#define MEM_ISTATE_FINALIZING           9
#define MEM_ISTATE_ERR_NO_CARD          128
#define MEM_ISTATE_ERR_MOD_BAD_RESP     129
#define MEM_ISTATE_ERR_OLD_BAD_RESP     130
#define MEM_ISTATE_ERR_BLOCK_SIZE       131
#define MEM_ISTATE_DEVELOPER_ERR        254
#define MEM_ISTATE_SUCCESS              255

/*
 * Definitions for a few single-byte constants and masks.
 */
#define MEM_DATA_TOKEN                  0xFE
#define MEM_DATA_TOKEN_MULTIPLE         0xFC
#define MEM_STOP_TOKEN                  0xFD

/*
 * Shorthand for the most common operations.
 */
#define mem_data_not_ready()    (! (MEM_USART.STATUS & USART_RXCIF_bm))

/*
 * ============================================================================
 *  
 *   INITIALIZATION OPERATIONS
 * 
 * ============================================================================
 */

/*
 * Routine that must be called before any other functions are invoked. This
 * should be only called once, before interrupts are enabled on the device,
 * during startup.
 */
void mem_init(void);

/*
 * "Main method" for initialization to put the card into the correct mode
 * during startup. This should be called over and over again to do a small
 * piece of card init each time, advancing the init state machine value as we
 * go.
 * 
 * The normal use will be something like the following:
 * 
 * uint8_t state;
 * do
 * {
 *   state = mem_init_card();
 *   // other work needing to be done
 * }
 * while (state < 0x80);
 * 
 * After the loop ends, check that the state variable is equal to
 * MEM_ISTATE_SUCCESS, or else initialization has failed.
 * 
 * This can be called again after MEM_ISTATE_SUCCESS, which will reset the card
 * and go through initialization again. This feature is not well tested and
 * should only be used experimentally.
 */
uint8_t mem_init_card(void);

/*
 * ============================================================================
 *  
 *   LOW LEVEL OPERATIONS
 * 
 * ============================================================================
 * 
 * Each of these works as described in the header, requiring a call to start
 * and stop the operation explicitly.
 */

/*
 * Starts an operation, checking first that the card is initialized and is
 * not busy. This responds with a nonzero value if the system is ready to
 * proceed. Callers should not proceed with any operation if the result of this
 * is zero.
 */
uint8_t mem_op_start(void);

/*
 * Sends a command to the card, and waits for the command response byte,
 * providing it back. The byte given is the non-adjusted command byte, so
 * to send CMD9, for example, just supply 0x09 to this function. If a command
 * argument is needed, it can be provided in big endian order, and must be 32
 * bits long.
 * 
 * This will leave 1 byte in the USART buffer for the caller to read.
 */
uint8_t mem_op_cmd(uint8_t cmd);
uint8_t mem_op_cmd_args(uint8_t cmd, uint8_t* arg);

/*
 * Continues to cycle bytes until a non 0xFF byte is read, then returns.
 * 
 * As with the above commands, this will leave 1 byte in the buffer for the
 * caller to read.
 */
uint8_t mem_wait_for_data(void);

/*
 * Stops an operation, releasing both the card and the subsystem for other
 * users. This will wait until all USART bytes are sent, flush the receive
 * buffer, and release the lock.
 */
void mem_op_end(void);

/*
 * ============================================================================
 *  
 *   HIGH LEVEL OPERATIONS
 * 
 * ============================================================================
 * 
 * One-line operations that access the card to get something out of it. Each
 * will return 0 for failure or 1 for success.
 */

/*
 * Read either the CSD or CID into the given array. The array provided must be
 * at least 16 bytes long.
 */
uint8_t mem_read_csd(uint8_t*);
uint8_t mem_read_cid(uint8_t*);

/*
 * Provides the size of the card in 512 byte blocks when given the CSD bytes.
 * This is not well tested for some cards: refer to the definition for details.
 */
uint32_t mem_size(uint8_t*);

#endif /* MEM_H */
