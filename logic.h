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

#ifndef LOGIC_H
#define LOGIC_H

#include <avr/io.h>
#include "config.h"
#include "phy.h"

/*
 * Defines common logical operations used by devices that talk on the bus.
 * 
 * The overall usage pattern is as follows:
 * 
 * 1) wait until logic_ready() indicates that we're ready.
 * 2) call logic_start() to setup.
 * 3) call remaining functions as needed (described below).
 * 4) call logic_done() to stop.
 * 
 * Device specific code can be found in the individual device compilation
 * units.
 * 
 * This will track the result of any IDENTIFY messages to keep track of the
 * LUN being addressed.
 */

/*
 * *** Important note regarding multiple-initiator systems ***
 * 
 * This implementation does not properly support systems where there is more
 * than one initiator. The RESERVE / RELEASE commands will act like they have
 * worked, but will not do anything!
 */

/*
 * Defines the sense data types supported as a response to a REQUEST SENSE
 * command. This is a small subset of the full sense data supported by the
 * standard, which is much more comprehensive (and confusing).
 * 
 * A single 32-bit value is stored alongside the sense data for reporting.
 * This value means different things depending on the sense state.
 * 
 * SENSE_OK: no sense data to report (device OK).
 * SENSE_INVALID_CDB_OPCODE: the command opcode is unsupported; provide the
 *     opcode that triggered the issue in the low 8 bits of the value.
 * SENSE_INVALID_CDB_ARGUMENT: one or more of the command arguments are
 *     invalid; provide index of the first invalid argument.
 * SENSE_INVALID_PARAMETER: one or more of the parameters sent after the
 *     command were invalid; provide the index of the first invalid argument
 *     sent during DATA OUT.
 * SENSE_ILLEGAL_LBA: a logical block address was invalid; provide the first
 *     invalid LBA.
 * SENSE_MEDIUM_ERROR: an unspecified medium error; can provide anything.
 * SENSE_MEDIUM_ERROR: an unspecified hardware error; can provide anything.
 * SENSE_BECOMING_READY: device not yet ready; can provide anything.
 */
typedef enum {
	SENSE_OK,
	SENSE_INVALID_CDB_OPCODE,
	SENSE_INVALID_CDB_ARGUMENT,
	SENSE_INVALID_PARAMETER,
	SENSE_ILLEGAL_LBA,
	SENSE_MEDIUM_ERROR,
	SENSE_HARDWARE_ERROR,
	SENSE_BECOMING_READY
} SENSEDATA;

/*
 * Stores 32 bit LBA and transfer length from a READ(6), READ(10), WRITE(6),
 * or WRITE(10) command.
 */
typedef struct LogicDataOp_t {
	uint32_t lba;
	uint16_t length;
} LogicDataOp;

/*
 * The number of devices this must support. Values at or above this will
 * cause an error if supplied to logic_start().
 */
#define LOGIC_DEVICE_COUNT              (HARD_DRIVE_COUNT + 1)

/*
 * The MESSAGE types that are of interest by this device, in addition to 
 * IDENTIFY messages in the 0x80-0xFF range. For specific messages supported
 * during MESSAGE OUT, refer to that section of the logic code.
 */
#define LOGIC_MSG_ABORT                 0x06
#define LOGIC_MSG_BUS_DEVICE_RESET      0x0C
#define LOGIC_MSG_COMMAND_COMPLETE      0x00
#define LOGIC_MSG_DISCONNECT            0x04
#define LOGIC_MSG_INIT_DETECT_ERROR     0x05
#define LOGIC_MSG_PARITY_ERROR          0x09
#define LOGIC_MSG_REJECT                0x07
#define LOGIC_MSG_NO_OPERATION          0x08

/*
 * Common codes for the STATUS phase.
 */
#define LOGIC_STATUS_GOOD               0x00
#define LOGIC_STATUS_CHECK_CONDITION    0x02
#define LOGIC_STATUS_BUSY               0x08

/*
 * Hardware check for if the bus is in a state where we are in full control
 * and can order the initiator around.
 */
#define logic_ready()   (phy_is_active() && (! phy_is_sel_asserted()))

/*
 * ============================================================================
 * 
 *   START / STOP HANDLERS
 * 
 * ============================================================================
 */

/*
 * Resets the state of the logic for a new operation. Should only be called
 * once logic_ready() is set.
 * 
 * This should be provided with the logic identifier for the device to pull out
 * of the internal state tracking array, starting from 0. Each device should
 * get a number for this and not change it.
 * 
 * The second parameter instructs the function to observe /ATN if asserted
 * at the beginning of the operation. If it is true, then /ATN will be
 * respected, otherwise it will be ignored.
 * 
 * This will return 0 if the attention check is disabled, or if /ATN was not
 * asserted. It will return the result of logic_message_out() otherwise.
 */
uint8_t logic_start(uint8_t, uint8_t);

/*
 * Releases the bus at the end of a full logical operation.
 * 
 * This should always be called at the end of a logic handle function to
 * release the bus as a fail-safe operation. Ensure this function is called or
 * unpredictable behavior may result.
 */
void logic_done(void);

/*
 * ============================================================================
 * 
 * INFORMATION FUNCTIONS
 * 
 * ============================================================================
 */

/*
 * Provides the last accepted IDENTIFY mask, as received, or 0 if none has
 * been accepted yet (MSB will always be set once received).
 */
uint8_t logic_identify(void);

/*
 * Provides whether or not the sense data for this device has been set.
 */
uint8_t logic_sense_valid(void);

/*
 * Parses the LBA and transfer length from a READ(6), READ(10), WRITE(6), or
 * WRITE(10) command using the given CDB array and stores the result in the
 * given struct.
 * 
 * This will return nonzero on success and zero on failure. On failure, sense
 * data will already be set.
 */
uint8_t logic_parse_data_op(uint8_t* cmd, LogicDataOp* op);

/*
 * ============================================================================
 * 
 *   BUS LOGICAL OPERATIONS
 * 
 * ============================================================================
 */

/*
 * Unconditionally moves to the MESSAGE OUT phase and gets a message from the
 * initiator.
 * 
 * Most messages are handled within this function, either by sending messages
 * back and forth, by going BUS FREE, or executing a MCU reset (for BUS DEVICE
 * RESET).
 * 
 * This is called automatically (sometimes repeatedly) at the end of each other
 * phase handler defined here if /ATN is asserted, but is available for other
 * code to invoke if manual logic handling for a phase requires it.
 * 
 * This supports only a limited number of messages from the initiator:
 * 
 * ABORT                    (0x06)
 * BUS DEVICE RESET         (0x0C)
 * DISCONNECT               (0x04)
 * INITIATOR DETECTED ERROR (0x05)
 * MESSAGE PARITY ERROR     (0x09)
 * MESSAGE REJECT           (0x07)
 * NO OPERATION             (0x08)
 * IDENTIFY                 (0x80-0xFF)
 * 
 * This will update the last seen IDENTIFY byte if such a byte is received.
 * Once set to non-zero, further changes to this byte will not be allowed
 * (except disconnect priviledge).
 * 
 * This will return the last message received, which may or may not be useful.
 * Zero is returned if no message was received, or if 0x00 was received, which
 * should not be a valid message for MESSAGE OUT anyway.
 */
uint8_t logic_message_out(void);

/*
 * Moves to the MESSAGE IN phase and sends the given message to the initiator.
 * Common message codes are defined elsewhere in this header.
 */
void logic_message_in(uint8_t);

/*
 * Moves to the COMMAND phase and accepts a command from the initiator,
 * returning the result in the given array with the length given in the
 * returned integer.
 * 
 * This places the opcode in the first byte of the provided array. This
 * function only supports 6 or 10 byte commands in groups 0, 1, or 2, and so
 * all returned opcodes will be 0x5F or less.
 * 
 * This has several bits of error handling baked-in:
 * 
 * *** LUNS: This will detect the selected LUN and report an error to the
 * initiator if non-zero. This handles everything related to sense data for
 * non-zero LUNs, so callers do not need to handle LUNs at all. The target
 * platform for this code has no support for them anyway (AFAIK).
 * 
 * *** Bad Opcodes: Sense data will be set appropriately and the function will
 * terminate the operation with CHECK CONDITION.
 * 
 * *** Control Field: if either link or flag bits are set, this will report an
 * error.
 * 
 * If the return length is zero, no further processing needs to be done, as the
 * system will already be in BUS FREE.
 */
uint8_t logic_command(uint8_t*);

/*
 * Moves to the STATUS phase and sends the given status code to the initiator.
 * Common status codes are defined elsewhere in this header.
 */
void logic_status(uint8_t);

/*
 * Moves to the DATA OUT phase and accepts a array of data from the initiator
 * equal to the number of bytes given.
 * 
 * This should generally be reserved for small chunks of data: for big amounts,
 * see the underlying methods in the PHY code, which should offer better
 * performance.
 * 
 * This will return the number of bytes read, which if not equal to the number
 * given indicates there was an error.
 */
uint8_t logic_data_out(uint8_t*, uint8_t);

/*
 * Version of the above that consigns data from the initiator to oblivion.
 * Useful for when we want to ignore dumb instructions. Provide with the number
 * of bytes we should ask for.
 */
void logic_data_out_dummy(uint8_t);

/*
 * Moves to the DATA IN phase and sends an array of data in memory to the
 * initiator.
 * 
 * This should generally be reserved for small chunks of data: for big amounts,
 * see the underlying methods in the PHY code, which should offer better
 * performance.
 */
void logic_data_in(const uint8_t*, uint8_t);

/*
 * As above, but sends data in flash memory to the initiator.
 */
void logic_data_in_pgm(const __flash uint8_t*, uint8_t);

/*
 * ============================================================================
 * 
 *   SENSE KEY / ERROR REPORTING FUNCTIONS
 * 
 * ============================================================================
 */

/*
 * Used when a target has detected an illegal command opcode. The opcode should
 * be given to the call for reporting.
 * 
 * This will update the given sense bytes, and if the PHY is still active,
 * will send CHECK CONDITION and COMMAND COMPLETE as well.
 */
void logic_cmd_illegal_op(uint8_t);

/*
 * Used when a target has detected an illegal command argument in the CDB.
 * 
 * This will update the given sense bytes, and if the PHY is still active,
 * will send CHECK CONDITION and COMMAND COMPLETE as well.
 * 
 * Provide this with the byte offset from the front of the CDB that caused the
 * problem.
 */
void logic_cmd_illegal_arg(uint8_t);

/*
 * Sets the sense data as given. The 32-bit value is dependent on the type of
 * sense data being given; refer to the enum for details.
 * 
 * This just makes the sense data valid, and does *not* send CHECK CONDITION /
 * COMMAND COMPLETE as the above functions do.
 */
void logic_set_sense(SENSEDATA data, uint32_t value);

/*
 * ============================================================================
 * 
 *   COMMON OPERATION HANDLERS
 * 
 * ============================================================================
 * 
 * These functions take care of responding to simple operations. Each takes the
 * command data bytes from the command handler.
 */

/*
 * Responds to a REQUEST SENSE command. This will provide the information for
 * the current device, then clear that information, and release the bus when
 * done.
 */
void logic_request_sense(uint8_t*);

/*
 * Handles responding to a SEND DIAGNOSTIC call. Specifically, this will:
 * 
 * 1) Accept whatever bytes the target wants to send, trashing each one.
 * 2) Send status GOOD,
 * 3) Send the COMMAND COMPLETE message.
 * 
 * Obviously this undermines the point of SEND DIAGNOSTIC, so don't use this
 * if you actually want to perform diagnostic activities.
 */
void logic_send_diagnostic(uint8_t*);

#endif
