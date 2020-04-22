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

#ifndef PHY_H
#define PHY_H

#include <avr/io.h>

/*
 * Defines the low-level interface used by this device for communicating as a
 * target on a mostly compliant early generation parallel SCSI bus.
 * 
 * This part of the implementation is concerned with the physical layer, along
 * with certain timing-sensitive components of the logical layer that are
 * involved with arbitration and selection modes of the bus.
 */

/*
 * ============================================================================
 *  
 *   USAGE INFORMATION
 * 
 * ============================================================================
 * 
 * During initialization, call phy_init() with the mask of the target(s) that
 * will be managed by this device. After that, call phy_init_hold() to keep 
 * further startup actions from proceeding until a possible reset condition
 * ends. Upon return of this call, the PHY is active.
 * 
 * After phy_init_hold() ends interrupts will be enabled, with the /RST ISR
 * at high priority and the /BSY ISR at medium priority. Whenever the target
 * mask given during startup is matched this code will automatically respond
 * to selection and change the PHY_STATUS_REGISTER to indicate that the device
 * mode has changed. The main loop should check phy_is_active() frequently
 * and react accordingly when set.
 * 
 * Once in the driver seat, main code can change phases via phy_phase() to
 * proceed through the different bus phases. Refer to that function for details
 * on how phase transistions work.
 * 
 * The main ways to send and receive data in normal operations is via the
 * various _offer() and _ask() functions, which handle the /REQ and /ACK
 * interlock used during normal transfers. There are both single-byte and
 * multiple-byte versions for different uses, with the multiple-byte versions
 * generally offering better performance.
 * 
 * Once done with the bus, call phy_phase() with PHY_PHASE_BUS_FREE, which will
 * release the bus for other uses.
 * 
 * Finally, some devices may wish to actively reselect the initiator. To do
 * this, call phy_reselect(). To check for when reselection has been achieved,
 * in the main loop, check both phy_is_active() and phy_is_continued() are
 * set. For implementation details, refer to documentation in the phy.c code.
 */

/*
 * The condition flags and macros used with PHY_REGISTER_STATUS. The macro
 * approach is preferred for external code.
 * 
 * PHY_STATUS_ACTIVE_bm: when set, the device is in active control of the bus
 * as a target.
 * 
 * PHY_STATUS_CONTINUED_bm: only set when PHY_STATUS_ACTIVE_bm is set. If set,
 * this indicates that the device is active as a result of a reselection, to
 * continue a previous action.
 * 
 * PHY_STATUS_ASK_RESELECT_bm: a device has asked for reselection, which will
 * be attempted at the next opportunity.
 * 
 * PHY_STATUS_RESELECT_PARITY_bm: if set, the parity bit needs to be set when
 * the reselection routine is trying to reselect the initiator. This is not
 * used when parity is disabled.
 */
#define PHY_STATUS_ACTIVE_bm            _BV(0)
#define PHY_STATUS_CONTINUED_bm         _BV(1)
#define PHY_STATUS_ASK_RESELECT_bm      _BV(3)
#define PHY_STATUS_RESELECT_PARITY_bm   _BV(4)

#define phy_is_active()         (PHY_REGISTER_STATUS & PHY_STATUS_ACTIVE_bm)
#define phy_is_continued()      (PHY_REGISTER_STATUS & PHY_STATUS_CONTINUED_bm)

/*
 * Defines the different bus phases available for sending to the phy_phase()
 * command.
 * 
 * Note: MSB is always '1' except for BUS FREE. LSB is for /I/O, then next
 * lowest is /C/D, then /MSG.  Remaining bits are reserved. BUS_FREE must be
 * zero to facilitate fast detection of this condition.
 */
#define PHY_PHASE_BUS_FREE      0x00
#define PHY_PHASE_DATA_OUT      0x80
#define PHY_PHASE_DATA_IN       0x81
#define PHY_PHASE_COMMAND       0x82
#define PHY_PHASE_STATUS        0x83
#define PHY_PHASE_MESSAGE_OUT   0x86
#define PHY_PHASE_MESSAGE_IN    0x87

// macros for detecting some control signals
#define phy_is_bsy_asserted()   (PHY_PORT_R_BSY.IN & PHY_PIN_R_BSY)
#define phy_is_sel_asserted()   (PHY_PORT_R_SEL.IN & PHY_PIN_R_SEL)
#define phy_is_atn_asserted()   (PHY_PORT_R_ATN.IN & PHY_PIN_R_ATN)
#define phy_is_ack_asserted()   (PHY_PORT_R_ACK.IN & PHY_PIN_R_ACK)

/*
 * Initalizes the SCSI PHY, setting everything to defaults. This needs to be
 * invoked before any other calls to the SCSI PHY system.
 * 
 * This is given the masks that this code uses to detect when it is being
 * selected. Any number of bits may be set.
 */
void phy_init(uint8_t);

/*
 * Halts the initialization process from proceeding until /RST has gone low.
 * This is intended to stop the MCU startup in instances when /RST has been
 * asserted long enough that the previous hard reset has occurred, the MCU
 * has restarted, and is now ready to start participating again, except the
 * bus is still in reset.
 * 
 * This should be called immediately before normal bus operations are to
 * proceed, during startup. This must be called with interrupts enabled. The
 * /RST and /BSY interrupts will be activated during this call.
 */
void phy_init_hold(void);

/*
 * Provides the target, if the device is active. This is mainly useful in
 * multiple-target configurations to figure out which device is active. In
 * single target implementations this can be ignored.
 * 
 * The result will only be valid if the device is active.
 */
uint8_t phy_get_target(void);

/*
 * ============================================================================
 *  
 *   DATA TRANSFER OPERATIONS
 * 
 * ============================================================================
 * 
 * These operations perform a /REQ / /ACK handshake to transfer data between
 * the initiator and the target.
 * 
 * For safety, each of these calls will have no effect if the bus is not
 * active, unless otherwise noted. Any call that drives the data lines will
 * also have no effect unless the phase indicates that the /I/O line is being
 * asserted.
 * 
 * Operations that drive the data lines will leave them asserted after
 * returning. This is normally OK, and will be reset during a phase change.
 * 
 * "Offers" in this context are transfers from the target to the initiator
 * (DATA IN and others) and "asks" are transfers from the initiator to the
 * target (DATA OUT and others).
 * 
 * TODO: each of these functions is vulnerable to busy-wait locks when the
 * initiator fails to respond to a /REQ assertion, which should be corrected
 * in a future version of the software.
 */

/*
 * Offers the initiator a single byte of data, waits for the initiator to be
 * ready to accept it, completes the transaction, and returns.
 */
void phy_data_offer(uint8_t);

/*
 * Offers the initiator the given number bytes from the given array. Apart
 * from working on a series of bytes, this is identical to the above function.
 */
void phy_data_offer_bulk(uint8_t*, uint16_t);

/*
 * Used during DATA IN to take data from the given USART and send it to the
 * initiator.
 * 
 * The connected USART device needs to be in a form of an auto-increment mode
 * pointing at the correct location, with exactly 1 byte waiting in the
 * reception queue from that location. The length given should be for the total
 * length, including that 1 byte.
 * 
 * This will push the given number of bytes into the device, which will end up
 * leaving one additional byte in the RX queue.
 */
void phy_data_offer_stream(USART_t*, uint16_t);

/*
 * As above, but for fixed lengths of 512 bytes.
 */
void phy_data_offer_stream_block(USART_t*);

/*
 * Specialized version of the above call, for use with the link device. This
 * version does two things differently:
 * 
 * 1) It will not push an additional byte into the USART at the end of the
 *    transmission, and
 * 2) It will eagerly abort if /ATN becomes asserted.
 */
void phy_data_offer_stream_atn(USART_t*, uint16_t);

/*
 * Asks the initiator for a byte of data, waits until it is available, then
 * reads it and provides it back.
 */
uint8_t phy_data_ask(void);

/*
 * Asks the initiator for the given number of bytes and stores them in the
 * given array. Apart from working on a series of bytes, this is identical to
 * the above function.
 */
void phy_data_ask_bulk(uint8_t*, uint16_t);

/*
 * Used during DATA OUT to take data from the initiator and send it to the
 * given USART.
 * 
 * The connected USART device needs to be in a form of an auto-increment mode
 * pointing at the correct location. Calling this with a length of more than
 * one or two bytes will likely cause the USART RX buffer to overflow, which
 * is not checked here.
 */
void phy_data_ask_stream(USART_t*, uint16_t);

/*
 * ============================================================================
 *  
 *   BUS CONTROL OPERATIONS
 * 
 * ============================================================================
 */

/*
 * Moves the bus to the given phase by changing /MSG, /CD, and /IO to match
 * the given phase as defined in the header. This call includes delays required
 * to guard the phase change, such as restoring data lines to idle, and will
 * force up /REQ as part of the call in the event that it has somehow become
 * asserted.
 * 
 * This will not do anything if PHY_REGISTER_STATUS indicates that we are not
 * active, and should not be called if that is the case. If given BUS FREE as
 * the phase, this will reset PHY_REGISTER_STATUS as part of this operation.
 */
void phy_phase(uint8_t);

/*
 * Starts the process of reselecting the initiator. In our implementation,
 * we assume there is only one initiator and it is always at ID 7.
 * 
 * This should be given the target mask that will be driven onto the lines
 * during the reselection process, of which only 1 bit should be set.
 * 
 * This will return 1 if the request to reselect was accepted, or 0 if not, due
 * to an existing reselection request that has not yet been cleared.
 */
uint8_t phy_reselect(uint8_t);

#endif /* PHY_H */
