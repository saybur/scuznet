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

#include <avr/cpufunc.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay.h>
#include "config.h"
#include "debug.h"
#include "phy.h"

/*
 * Here are some notes on how resetting, selection, arbitration, and
 * reselection are implemented in this file. These steps are sensitive to bus
 * timing, and thus mostly occur in the interrupt context. The actual code
 * is probably a little hard to follow, so hopefully this explains things more
 * clearly.
 * 
 * Responding to /RST uses the "hard reset" option. There is a timer set to
 * increment in response to the event system, which is tied into the /RST line
 * with a digital filter to ignore very short pulses. When that timer ticks up
 * due to the event system detecting a solid assertion of /RST, the software
 * reset mechanism is invoked: this is mainly done to make sure that the bus
 * lines are tri-stated as quickly as possible. Upon restart, phy_init_hold()
 * prevents device startup from proceeding until /RST is no longer asserted.
 * 
 * Selection involves detecting the de-assertion edge of /BSY while /SEL is
 * still asserted. This only occurs during a (RE)SELECTION on the bus. The ISR
 * checks the state of things, and if everything indicates selection on the
 * owned mask bits, the ISR responds to selection and sets the activity flag.
 * 
 * Reselection requires arbitration, which can be tricky, since the timing of
 * things is quite tight. Bus arbitration proceeds through the following steps
 * in the general case, with clock notations measured in device clocks @ 32MHz:
 * 
 * 1) A device releases all signals and the bus enters the BUS FREE phase.
 * 2) After at least 800ns (~26 cycles) a device may assert /BSY and start to
 *    arbitrate. Devices cannot enter arbitration if it has been more than
 *    1000ns since they last detected BUS FREE.
 * 3) 2400ns (~76 cycles) after asserting /BSY, a device may check the bus
 *    and see if they won. The winning device may assert /SEL. Losing devices
 *    are suggested to keep /BSY set and the ID bits on the bus until /SEL goes
 *    true, then release all signals within 800ns (~26 cycles).
 * 4) The winner must wait at least 1200ns (~38 cycles) after asserting /SEL
 *    before changing any other signals. At this point, ARBITRATION is done,
 *    and the winner may proceed with SELECTION by asserting its ID, the target
 *    ID, optionally asserting I/O to make the phase RESELECTION, and by
 *    releasing /BSY.
 * 
 * We handle the above as follows:
 * 
 * Upon a request for reselection, we start a timer dedicated to monitoring
 * the period from the last /BSY rise, set to wrap every ~500us or so if we
 * don't detect a /BSY rise during that time (common either during bus idle or
 * long transactions). This timer uses the event system to reset automatically
 * on /BSY rise to avoid delays from the CPU being busy. The CCA of that timer
 * is set to fire at a reasonable delay after /BSY goes up. If conditions are
 * right, that ISR starts the arbitration process and sets up both a CCB ISR
 * for the same timer *and* a /SEL ISR for the assertion edge of that signal.
 * 
 * If the /SEL ISR fires, it is an indication that another device won
 * arbitration. We immediately release all signals and reset things to let the
 * CCA timer fire again later to try arbitrating again.
 * 
 * If not stopped by the /SEL ISR, the CCB timer fires about 2400ns later. It
 * checks things, and if we are the arbitration winner, it asserts /SEL and
 * starts the reselection process on the initiator, then releases /BSY.  A
 * secondary timer is used to check at a frequent interval to see when /BSY 
 * becomes set.
 * 
 * When that secondary timer sees /BSY set, it performs a similar set of steps
 * to the normal selection response routine in the main /BSY ISR, and sets
 * the status flags. At this point, the logic flow works basically the same
 * as for a normal selection, and life moves on.
 */

/*
 * Control pin aliases to help make the pin fiddling code in this file more
 * legible and (somewhat) more resilient to typos. This also helps implement
 * the DCLK and DOE optional code.
 */
#define bsy_assert()          PHY_PORT_T_BSY.OUT |=  PHY_PIN_T_BSY
#define bsy_release()         PHY_PORT_T_BSY.OUT &= ~PHY_PIN_T_BSY
#define sel_assert()          PHY_PORT_T_SEL.OUT |=  PHY_PIN_T_SEL
#define sel_release()         PHY_PORT_T_SEL.OUT &= ~PHY_PIN_T_SEL
#define msg_assert()          PHY_PORT_T_MSG.OUT |=  PHY_PIN_T_MSG
#define msg_release()         PHY_PORT_T_MSG.OUT &= ~PHY_PIN_T_MSG
#define cd_assert()           PHY_PORT_T_CD.OUT  |=  PHY_PIN_T_CD
#define cd_release()          PHY_PORT_T_CD.OUT  &= ~PHY_PIN_T_CD
#define io_assert()           PHY_PORT_T_IO.OUT  |=  PHY_PIN_T_IO
#define io_release()          PHY_PORT_T_IO.OUT  &= ~PHY_PIN_T_IO
#define req_assert()          PHY_PORT_T_REQ.OUT |=  PHY_PIN_T_REQ
#define req_release()         PHY_PORT_T_REQ.OUT &= ~PHY_PIN_T_REQ
#define dbp_assert()          PHY_PORT_T_DBP.OUT |=  PHY_PIN_T_DBP
#define dbp_release()         PHY_PORT_T_DBP.OUT &= ~PHY_PIN_T_DBP

#define asm_req_assert        "sbi %[req_out], %[req_bp] \n\t"
#define asm_req_release       "cbi %[req_out], %[req_bp] \n\t"
#define asm_req_def           [req_bp] "i" (PHY_PIN_T_REQ_bp), \
                              [req_out] "I" (&(PHY_PORT_T_REQ.OUT))
#define asm_dbp_assert        "sbi %[dbp_out], %[dbp_bp] \n\t"
#define asm_dbp_release       "cbi %[dbp_out], %[dbp_bp] \n\t"
#define asm_dbp_def           [dbp_bp] "i" (PHY_PIN_T_DBP_bp), \
                              [dbp_out] "I" (&(PHY_PORT_T_DBP.OUT))

#ifdef PHY_PORT_DATA_IN_OE
	#define doe_off()         PHY_PORT_DOE.OUT   |=  PHY_PIN_DOE
	#define doe_on()          PHY_PORT_DOE.OUT   &= ~PHY_PIN_DOE
	#define asm_doe_off       "sbi %[doe_out], %[doe_bp] \n\t"
	#define asm_doe_on        "cbi %[doe_out], %[doe_bp] \n\t"
	#define asm_doe_def       [doe_bp] "i" (PHY_PIN_DOE_bp), \
                              [doe_out] "I" (&(PHY_PORT_DOE.OUT))
#else
	#define doe_off()
	#define doe_on()
	#define asm_doe_on
	#define asm_doe_off
	#define asm_doe_def
#endif

#ifdef PHY_PORT_DATA_IN_CLOCK
	#define dclk_rise()       PHY_PORT_DCLK.OUT  |=  PHY_PIN_DCLK
	#define dclk_fall()       PHY_PORT_DCLK.OUT  &= ~PHY_PIN_DCLK
	#define asm_dclk_rise     "sbi %[dclk_out], %[dclk_bp] \n\t"
	#define asm_dclk_fall     "cbi %[dclk_out], %[dclk_bp] \n\t"
	#define asm_dclk_def      [dclk_bp] "i" (PHY_PIN_DCLK_bp), \
                              [dclk_out] "I" (&(PHY_PORT_DCLK.OUT))
#else
	#define dclk_rise()
	#define dclk_fall()       _NOP()
	#define asm_dclk_rise
	#define asm_dclk_fall     "nop \n\t"
	#define asm_dclk_def
#endif

/*
 * Duration in cycles after /BSY goes up that arbitration start should occur.
 * This should be approximately 800ns and 2400ns respectively.
 */
#define PHY_TIMER_BSY_CCA_VAL   26
#define PHY_TIMER_BSY_CCB_VAL   76

/*
 * The frequency of checks during reselection to see if the initiator has
 * set /BSY and is ready to proceed. A value of 1024 equates to a check about
 * every 32us @ 32MHz.
 */
#define PHY_TIMER_RESEL_VAL     1024

/*
 * Lookup values needed to swap a reversed port order back to normal, or take
 * a normal value and reverse it. These are stored aligned to a 256 byte boundry
 * for the assembly code.
 */
const __flash uint8_t phy_reverse_table[256] __attribute__ ((aligned (256))) = {
	0, 128, 64, 192, 32, 160, 96, 224, 16, 144, 80, 208, 48, 176, 112, 240, 8, 
	136, 72, 200, 40, 168, 104, 232, 24, 152, 88, 216, 56, 184, 120, 248, 4, 
	132, 68, 196, 36, 164, 100, 228, 20, 148, 84, 212, 52, 180, 116, 244, 12, 
	140, 76, 204, 44, 172, 108, 236, 28, 156, 92, 220, 60, 188, 124, 252, 2, 
	130, 66, 194, 34, 162, 98, 226, 18, 146, 82, 210, 50, 178, 114, 242, 10, 
	138, 74, 202, 42, 170, 106, 234, 26, 154, 90, 218, 58, 186, 122, 250, 6, 
	134, 70, 198, 38, 166, 102, 230, 22, 150, 86, 214, 54, 182, 118, 246, 14, 
	142, 78, 206, 46, 174, 110, 238, 30, 158, 94, 222, 62, 190, 126, 254, 1, 
	129, 65, 193, 33, 161, 97, 225, 17, 145, 81, 209, 49, 177, 113, 241, 9, 
	137, 73, 201, 41, 169, 105, 233, 25, 153, 89, 217, 57, 185, 121, 249, 5, 
	133, 69, 197, 37, 165, 101, 229, 21, 149, 85, 213, 53, 181, 117, 245, 13, 
	141, 77, 205, 45, 173, 109, 237, 29, 157, 93, 221, 61, 189, 125, 253, 3, 
	131, 67, 195, 35, 163, 99, 227, 19, 147, 83, 211, 51, 179, 115, 243, 11, 
	139, 75, 203, 43, 171, 107, 235, 27, 155, 91, 219, 59, 187, 123, 251, 7, 
	135, 71, 199, 39, 167, 103, 231, 23, 151, 87, 215, 55, 183, 119, 247, 15, 
	143, 79, 207, 47, 175, 111, 239, 31, 159, 95, 223, 63, 191, 127, 255
};

/*
 * Truth table for parity calculations when outputting data, and for checking
 * the number of bits set in a byte more generally (we should probably begin
 * checking that when responding to selection). As with the above array, these
 * are stored aligned to a 256 byte boundry to allow the assembly code to
 * function correctly.
 */
const __flash uint8_t phy_bits_set[256] __attribute__ ((aligned (256))) = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 
	3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 
	3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 
	4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 
	3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 
	6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 
	4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 
	6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 
	3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 3, 
	4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 
	6, 7, 6, 7, 7, 8
};

/*
 * Aliases for code readability.
 */
#define parity_enabled()        (GLOBAL_CONFIG_REGISTER & GLOBAL_FLAG_PARITY)


/*
 * Stores the bitmask of the target currently active on the bus. This is only
 * valid if the PHY is active.
 * 
 * Note: if PHY_PORT_DATA_IN_REVERSED is defined, the value stored here will
 * also be reversed.
 */
static volatile uint8_t active_target;

/*
 * Stores the bitmask of PHY data input lines used for selection logic.
 * If a bit is set here that corresponds to a data input line, this device
 * will respond to selection requests on that line.
 * 
 * Note: if PHY_PORT_DATA_IN_REVERSED is defined, the value stored here will
 * also be reversed.
 */
static volatile uint8_t owned_masks;

/*
 * Values used during arbitration, as follows:
 * 
 * 1) The value placed on the bus during arbitration.
 * 2) The value stored to active_target upon successful arbitration.
 * 3) The mask of values considered higher priority during arbitration: the
 *    presence of any one on the bus will cause arbitration to be lost.
 * 
 * Note: if PHY_PORT_DATA_IN_REVERSED is defined, 2) and 3) above will also
 * be reversed.
 */
static volatile uint8_t arbitration_target_out;
static volatile uint8_t arbitration_target_in;
static volatile uint8_t arbitration_block_mask;

/*
 * ============================================================================
 *   LOW-LEVEL INPUT/OUTPUT CALLS
 * ============================================================================
 * 
 * These implement low-level reading and writing on the bus. The first two are
 * used to read or write a snapshot of the data bus, regardless of logic. The
 * next four handle bulk data transfers, both for internal memory buffers and
 * USART-based streaming.
 * 
 * For the bulk data transfer functions:
 * 
 * 1) The T flag is used to indicate the operation should be stopped early.
 *    This is set asynchronously to the function call via interrupts.
 * 2) Each takes an 8-bit count value, which is the number of bytes to
 *    transfer, with 0 indicating 256 bytes.
 * 3) All values will be transferred unless the T flag is set. The byte counter
 *    may not be reliable, so it is not returned: use the T flag as an
 *    indication of failure.
 * 4) The inline assembly is quite ugly but is needed to support the T flag
 *    and aligned memory access of the lookup tables. If there is a better
 *    approach please let the author know :)
 * 
 * These are reliant on the parity table and reverse table being byte-aligned,
 * which allows the low byte of the relevant pointer to directly look into the
 * table.
 */

#define asm_data_in_def       [data_in] "p" (&(PHY_PORT_DATA_IN.IN))
#define asm_data_out_def      [data_out] "e" (&(PHY_PORT_DATA_OUT.OUT))

// skip next instruction (usually RJMP) if condition true
#define asm_skip_if_ack_asrt  "sbis %[ack_in], %[ack_bp] \n\t"
#define asm_skip_if_ack_rel   "sbic %[ack_in], %[ack_bp] \n\t"
#define asm_ack_def           [ack_bp] "i" (PHY_PIN_R_ACK_bp), \
                              [ack_in] "I" (&(PHY_PORT_R_ACK.IN))

/*
 * Performs a raw read of the data bus and returns the result. This is the
 * preferred approach outside of an ISR.
 * 
 * Note: if PHY_PORT_DATA_IN_REVERSED is defined, the value returned will also
 * be reversed.
 */
static inline __attribute__((always_inline)) uint8_t phy_data_get(void)
{
	uint8_t raw;

	#if defined(PHY_PORT_DATA_IN_OE) || defined(PHY_PORT_DATA_IN_CLOCK)
		ATOMIC_BLOCK(ATOMIC_FORCEON)
		{
			dclk_rise();
			doe_on();
			dclk_fall();
			raw = PHY_PORT_DATA_IN.IN;
			doe_off();
		}
	#else
		raw = PHY_PORT_DATA_IN.IN;
	#endif

	return raw;
}

/*
 * Performs a raw set of the data bus, including the parity line.
 */
static inline __attribute__((always_inline)) void phy_data_set(uint8_t data)
{
	if (GLOBAL_CONFIG_REGISTER & GLOBAL_FLAG_PARITY)
	{
		dbp_release();
		if (! (phy_bits_set[data] & 1))
		{
			dbp_assert();
		}
	}
	PHY_PORT_DATA_OUT.OUT = data;
}

/*
 * Reads X bytes from the data bus into memory at the given location.
 */
static inline __attribute__((always_inline)) void phy_get(
		uint8_t* data, uint8_t count)
{
	__asm__ __volatile__(
		// wait for /ACK to release, then drive /REQ to ask for a byte
		"wait_ack_first%=:"             "\n\t"
		"brts end_%="                   "\n\t"
		asm_skip_if_ack_rel
		"rjmp wait_ack_first%="         "\n\t"
		asm_req_assert

		// nothing to store yet, so jump past the first part of the loop
		"rjmp wait_ack_assert_%="       "\n\t"

		// start of operation except for 1st pass (see above)
	    "loop_%=:"                      "\n\t"

		// wait for /ACK to release, then drive /REQ to ask for a byte
	    "wait_ack_release_%=:"          "\n\t"
		"brts end_%="                   "\n\t"
		asm_skip_if_ack_rel
		"rjmp wait_ack_release_%="      "\n\t"
		asm_req_assert

		/*
		 * Before we wait for the remote system to respond, store the results
		 * from the previous loop iteration, reversing the data if needed. This
		 * step is also performed after the loop, to catch the last value from
		 * the bus after the loop concludes.
		 */
#ifdef PHY_PORT_DATA_IN_REVERSED
		"lpm __tmp_reg__, %a[rev]"      "\n\t"
#else
		"mov __tmp_reg__, %A[rev]"      "\n\t"
#endif
		"st %a[ptr]+, __tmp_reg__"      "\n\t"

		// wait for /ACK to assert
		"wait_ack_assert_%=:"           "\n\t"
		"brts end_%="                   "\n\t"
		asm_skip_if_ack_asrt
		"rjmp wait_ack_assert_%="       "\n\t"

		/*
		 * Read the byte on the wire, functionally the same as phy_data_get().
		 * This value is read into ZL to allow for the reverse-lookup to occur
		 * afterward.
		 */
		asm_dclk_rise
		asm_doe_on
		asm_dclk_fall
		"lds %A[rev], %[data_in]"       "\n\t"
		asm_doe_off

		// release /REQ
		asm_req_release

		// loop if not done
		"dec %[cnt]"                    "\n\t"
		"brne loop_%="                  "\n\t"

		// store the pending byte, same routine as earlier
#ifdef PHY_PORT_DATA_IN_REVERSED
		"lpm __tmp_reg__, %a[rev]"      "\n\t"
#else
		"mov __tmp_reg__, %A[rev]"      "\n\t"
#endif
		"st %a[ptr]+, __tmp_reg__"      "\n\t"

		// clean-up
		"end_%=:"                       "\n\t"
		"clr %A[rev]"                   "\n\t" // input operand reset
		: [ptr] "+e" (data), [cnt] "+r" (count)
		: [rev] "z" (phy_reverse_table),
		  asm_data_in_def, asm_dclk_def, asm_doe_def, asm_req_def, asm_ack_def
	);
}

/*
 * Write X bytes from memory at the given location to the data bus. There are
 * two versions of this call:
 * 
 * _set() will set without transmission parity, and
 * _setp() will set with transmission each time.
 * 
 * The versions are otherwise identical, including the behavior where they will
 * read one more byte than asked out of the given memory buffer. This should
 * not normally be a problem unless at the extreme end of the stack, which
 * is prevented (hopefully) by the memory checking code in main().
 */
static inline __attribute__((always_inline)) void phy_set(
		uint8_t* data, uint8_t count)
{
	__asm__ __volatile__(
		// fetch value for the first iteration
		"ld __tmp_reg__, %a[ptr]+"       "\n\t"

		// start of loop
	    "loop_%=:"                      "\n\t"

		// wait for /ACK to release
	    "wait_ack_release_%=:"          "\n\t"
		"brts end_%="                   "\n\t"
		asm_skip_if_ack_rel
		"rjmp wait_ack_release_%="      "\n\t"

		// put the value on the bus
		"st %a[data_out], __tmp_reg__"  "\n\t"
		"nop"                           "\n\t" // propagation delay

		// drive /REQ, informing byte is ready
		asm_req_assert

		// fetch value for next iteration
		"ld __tmp_reg__, %a[ptr]+"       "\n\t"

		// wait for /ACK to assert
		"wait_ack_assert_%=:"           "\n\t"
		"brts end_%="                   "\n\t"
		asm_skip_if_ack_asrt
		"rjmp wait_ack_assert_%="       "\n\t"

		// release /REQ
		asm_req_release

		// loop if not done
		"dec %[cnt]"                    "\n\t"
		"brne loop_%="                  "\n\t"

		// clean-up
		"end_%=:"                       "\n\t"
		: [ptr] "+e" (data), [cnt] "+r" (count)
		: asm_data_out_def, asm_dbp_def, asm_req_def, asm_ack_def
	);
}
static inline __attribute__((always_inline)) void phy_setp(
		uint8_t* data, uint8_t count)
{
	__asm__ __volatile__(
		// fetch value for the first iteration
		"ld %A[bits], %a[ptr]+"         "\n\t"

		// start of loop
	    "loop_%=:"                      "\n\t"

		// wait for /ACK to release
	    "wait_ack_release_%=:"          "\n\t"
		"brts end_%="                   "\n\t"
		asm_skip_if_ack_rel
		"rjmp wait_ack_release_%="      "\n\t"

		// put the value on the bus with parity
		"st %a[data_out], %A[bits]"     "\n\t" // set data bus to value
		asm_dbp_release
		"lpm %A[bits], %a[bits]"        "\n\t" // get # bits set into ZL
		"andi %A[bits], 1"              "\n\t" // is # bits set odd?
		"brne parity_end_%="            "\n\t"
		asm_dbp_assert                         // set if not already odd
		"nop"                           "\n\t" // propagation delay
		"parity_end_%=:"                "\n\t"

		// drive /REQ, informing byte is ready
		asm_req_assert

		// fetch value for next iteration
		"ld %A[bits], %a[ptr]+"         "\n\t"

		// wait for /ACK to assert
		"wait_ack_assert_%=:"           "\n\t"
		"brts end_%="                   "\n\t"
		asm_skip_if_ack_asrt
		"rjmp wait_ack_assert_%="       "\n\t"

		// release /REQ
		asm_req_release

		// loop if not done
		"dec %[cnt]"                    "\n\t"
		"brne loop_%="                  "\n\t"

		// clean-up
		"end_%=:"                       "\n\t"
		"clr %A[bits]"                  "\n\t" // input operand reset
		: [ptr] "+e" (data), [cnt] "+r" (count)
		: [bits] "z" (phy_bits_set),
		  asm_data_out_def, asm_dbp_def, asm_req_def, asm_ack_def
	);
}

/*
 * Releases any data bits and the parity line (/DB0-7, /DBP).
 */
static inline __attribute__((always_inline)) void phy_data_clear(void)
{
	PHY_PORT_DATA_OUT.OUT = 0;
	dbp_release();
}

/*
 * ============================================================================
 *  SETUP / UTILITY
 * ============================================================================
 */

/*
 * Handles starting and stopping the PHY watchdog timer.
 */
static inline __attribute__((always_inline)) void phy_watchdog_start(void)
{
	PHY_TIMER_WATCHDOG.CTRLFSET = TC_CMD_RESTART_gc;
	PHY_TIMER_WATCHDOG.CTRLA = TC_CLKSEL_DIV1024_gc;
}
static inline __attribute__((always_inline)) void phy_watchdog_stop(void)
{
	PHY_TIMER_WATCHDOG.CTRLA = TC_CLKSEL_OFF_gc;
	asm volatile("clt"); // clear T flag
}

void phy_init(uint8_t mask)
{
	PHY_REGISTER_PHASE = 0;
	#ifdef PHY_PORT_DATA_IN_REVERSED
		owned_masks = phy_reverse_table[mask];
	#else
		owned_masks = mask;
	#endif

	/*
	 * Important: DCLK can get into a port conflict state that can damage the
	 * MCU if ACKEN is not kept low. ACKEN has a pull-down, but for extra
	 * insurance we drive it low as well.
	 * 
	 * The whole ACKEN/DCLK thing was a bad idea and is just left disabled on
	 * the board. Future revisions will switch the hardware up to fix this
	 * problem.
	 */
	#ifdef PHY_PORT_DATA_IN_ACKEN
		PHY_PORT_ACKEN.OUT &= ~PHY_PIN_ACKEN;
		PHY_PORT_ACKEN.DIR |=  PHY_PIN_ACKEN;
	#endif
	#ifdef PHY_PORT_DATA_IN_CLOCK
		PHY_PORT_DCLK.OUT  &= ~PHY_PIN_DCLK;
		PHY_PORT_DCLK.DIR  |=  PHY_PIN_DCLK;
	#endif

	/*
	 * If present, we keep the read buffer off when not in use to mitigate
	 * issues with slow CMOS inputs: LVTH logic has a 10ns/V restriction that
	 * applies only (apparently?) when outputs are enabled. To keep the read
	 * pins in a consistent state when the buffer is off, we enable the data
	 * in pull-ups.
	 * 
	 * This is not necessary if the inputs are Schmitt triggers.
	 */
	#ifdef PHY_PORT_DATA_IN_OE
		doe_off();
		PHY_PORT_DOE.DIR |= PHY_PIN_DOE;
		PHY_PORT_DATA_IN.PIN0CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN1CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN2CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN3CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN4CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN5CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN6CTRL |= PORT_OPC_PULLUP_gc;
		PHY_PORT_DATA_IN.PIN7CTRL |= PORT_OPC_PULLUP_gc;
	#endif

	/*
	 * If not handled by external hardware like it is on the control lines,
	 * we must invert the data lines.
	 */
	#ifdef PHY_PORT_DATA_IN_INVERT
		PHY_PORT_DATA_IN.PIN0CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN1CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN2CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN3CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN4CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN5CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN6CTRL |= PORT_INVEN_bm;
		PHY_PORT_DATA_IN.PIN7CTRL |= PORT_INVEN_bm;
	#endif

	/*
	 * The data output lines all have pull-downs to keep the output lines 
	 * floating, but we drive them low anyway now that we're in control.
	 * 
	 * Note: even through the output drivers are in the LVTH family, the bus
	 * hold circuitry is only on the inputs, not the OE pins (which is sad).
	 */
	PHY_PORT_DATA_OUT.OUT = 0x00;
	PHY_PORT_DATA_OUT.DIR = 0xFF;

	/*
	 * Init output control lines. Each has a pull-down to keep the driver
	 * output floating, and we output to re-enforce this default behavior.
	 */
	bsy_release();
	sel_release();
	msg_release();
	cd_release();
	io_release();
	req_release();
	dbp_release();
	PHY_PORT_T_BSY.DIR |= PHY_PIN_T_BSY;
	PHY_PORT_T_SEL.DIR |= PHY_PIN_T_SEL;
	PHY_PORT_T_MSG.DIR |= PHY_PIN_T_MSG;
	PHY_PORT_T_CD.DIR  |= PHY_PIN_T_CD;
	PHY_PORT_T_IO.DIR  |= PHY_PIN_T_IO;
	PHY_PORT_T_REQ.DIR |= PHY_PIN_T_REQ;
	PHY_PORT_T_DBP.DIR |= PHY_PIN_T_DBP;

	/*
	 * Do the initial setup for the /RST timer, but do not start it yet.
	 * This timer will use the event system to listen for an assertion pulse
	 * on /RST, which ticks up the timer, which then fires the interrupt. Once
	 * in the ISR code the system will execute a software reset which should
	 * immediately release all signals.
	 */
	PHY_TIMER_RST.CCA = 1;
	PHY_TIMER_RST.INTCTRLB = TC_CCAINTLVL_HI_gc;
	PHY_TIMER_RST_CHCTRL = EVSYS_DIGFILT_8SAMPLES_gc;

	/*
	 * Setup ISR edge sense. We need a few different things:
	 * 
	 * 1) Any time /RST gets asserted,
	 * 2) Limited times /SEL starts being asserted, and
	 * 2) The end of /BSY being asserted (by default).
	 * 
	 * We set these up, but we don't enable them until later when they are
	 * needed.
	 */
	PHY_CFG_R_RST |= PORT_ISC_LEVEL_gc;
	PHY_CFG_R_BSY |= PORT_ISC_FALLING_gc;
	PHY_CFG_R_SEL |= PORT_ISC_RISING_gc;
	PHY_PORT_CTRL_IN.INT0MASK = PHY_PIN_R_SEL;
	PHY_PORT_CTRL_IN.INT1MASK = PHY_PIN_R_BSY;

	/*
	 * Setup timer that monitors the time elapsed since a DISCONNECT message
	 * was received from the initiator. This will run continuously during
	 * operation and be reset (along with the OVF flag) when DISCONNECT
	 * occurs.
	 */
	PHY_TIMER_DISCON.PER = PHY_TIMER_DISCON_DELAY;
	PHY_TIMER_DISCON.CTRLA = TC_CLKSEL_DIV64_gc;

	/*
	 * Setup timer used to monitor REQ/ACK deadlocks and similar situations
	 * when data flow on the bus has stopped.
	 */
	PHY_TIMER_WATCHDOG.INTCTRLA = TC_OVFINTLVL_LO_gc;

	/*
	 * Verify correct alignment of the tables. These must have 0x00 in their
	 * low byte or errors will occur later in the assembly routines.
	 */
	if ((((uint16_t) phy_reverse_table) & 0xFF) != 0x00
			|| (((uint16_t) phy_bits_set) & 0xFF) != 0x00)
	{
		fatal(FATAL_GENERAL, FATAL_MISALIGNED);
	}
}

void phy_init_hold(void)
{
	/*
	 * Wait until /RST is no longer asserted, then enable the timer that
	 * enables the reset-on-/RST command. 
	 */
	while (PHY_PORT_R_RST.IN & PHY_PIN_R_RST);
	PHY_TIMER_RST_CHMUX = PHY_CHMUX_RST;
	PHY_TIMER_RST.CTRLA = PHY_TIMER_RST_CLKSEL;

	/*
	 * Then enable the /BSY interrupt to start accepting transactions.
	 */
	PHY_PORT_R_BSY.INTFLAGS  = PORT_INT1IF_bm;
	PHY_PORT_CTRL_IN.INTCTRL = PORT_INT1LVL_MED_gc;
}

uint8_t phy_get_target(void)
{
	#ifdef PHY_PORT_DATA_IN_REVERSED
		return phy_reverse_table[active_target];
	#else
		return active_target;
	#endif
}

/*
 * ============================================================================
 *  DATA TRANSFER
 * ============================================================================
 */

void phy_data_offer(uint8_t data)
{
	if (! (PHY_REGISTER_PHASE & 0x01)) return;
	if (! phy_is_active()) return;
	phy_watchdog_start();

	// TODO add T flag monitoring
	while (phy_is_ack_asserted());
	phy_data_set(data);
	req_assert();
	while (! phy_is_ack_asserted());
	req_release();

	phy_watchdog_stop();
}

uint8_t phy_data_offer_block(uint8_t* data)
{
	if (! (PHY_REGISTER_PHASE & 0x01)) return 0;
	if (! phy_is_active()) return 0;
	phy_watchdog_start();

	if (parity_enabled())
	{
		phy_setp(data, 0);
		phy_setp(data + 256, 0);
	}
	else
	{
		phy_set(data, 0);
		phy_set(data + 256, 0);
	}

	phy_watchdog_stop();
	return 1;
}

uint16_t phy_data_offer_bulk(uint8_t* data, uint16_t len)
{
	if (! (PHY_REGISTER_PHASE & 0x01)) return 0;
	if (! phy_is_active()) return 0;
	phy_watchdog_start();

	for (uint16_t i = 0; i < len; i++)
	{
		while (phy_is_ack_asserted());
		phy_data_set(data[i]);
		req_assert();
		while (! phy_is_ack_asserted());
		req_release();
	}

	phy_watchdog_stop();
	return len;
}

uint16_t phy_data_offer_stream(USART_t* usart, uint16_t len)
{
	uint8_t v;

	if (! (PHY_REGISTER_PHASE & 0x01)) return len;
	if (! phy_is_active()) return len;
	if (len == 0) return len;
	phy_watchdog_start();

	// queue first byte
	while (! (usart->STATUS & USART_DREIF_bm));
	usart->DATA = 0xFF;

	while (--len)
	{
		// queue next byte, and wait for reply from previous
		while (! (usart->STATUS & USART_DREIF_bm));
		usart->DATA = 0xFF;
		while (! (usart->STATUS & USART_RXCIF_bm));
		v = usart->DATA;

		// share results with initiator
		while (phy_is_ack_asserted());
		phy_data_set(v);
		req_assert();
		while (! phy_is_ack_asserted());
		req_release();
	}

	// fetch the last byte we sent
	while (! (usart->STATUS & USART_RXCIF_bm));
	v = usart->DATA;

	// and share that with the initiator
	while (phy_is_ack_asserted());
	phy_data_set(v);
	req_assert();
	while (! phy_is_ack_asserted());
	req_release();

	phy_watchdog_stop();
	return len;
}

uint16_t phy_data_offer_stream_atn(USART_t* usart, uint16_t len)
{
	uint8_t v;

	if (! (PHY_REGISTER_PHASE & 0x01)) return len;
	if (! phy_is_active()) return len;
	if (len == 0) return len;
	phy_watchdog_start();

	// queue first byte
	while (! (usart->STATUS & USART_DREIF_bm));
	usart->DATA = 0xFF;

	while (--len && ! phy_is_atn_asserted())
	{
		// queue next byte, and wait for reply from previous
		while (! (usart->STATUS & USART_DREIF_bm));
		usart->DATA = 0xFF;
		while (! (usart->STATUS & USART_RXCIF_bm));
		v = usart->DATA;

		// share results with initiator
		while (phy_is_ack_asserted());
		phy_data_set(v);
		req_assert();
		while ((! phy_is_atn_asserted()) && (! phy_is_ack_asserted()));
		req_release();
	}

	// fetch the last byte we sent
	while (! (usart->STATUS & USART_RXCIF_bm));
	v = usart->DATA;

	// and share that with the initiator
	while (phy_is_ack_asserted());
	phy_data_set(v);
	req_assert();
	while ((! phy_is_atn_asserted()) && (! phy_is_ack_asserted()));
	req_release();

	phy_watchdog_stop();
	return len;
}

uint8_t phy_data_ask(void)
{
	if (! phy_is_active()) return 0;
	phy_watchdog_start();

	// TODO add T flag monitoring

	// wait for initiator to be ready
	while (phy_is_ack_asserted());

	// ask for a byte of data
	req_assert();

	// wait for initiator to give us the data
	while (! phy_is_ack_asserted());

	// get the data
	uint8_t data = phy_data_get();
	#ifdef PHY_PORT_DATA_IN_REVERSED
		data = phy_reverse_table[data];
	#endif

	// release /REQ, then we're done
	req_release();
	phy_watchdog_stop();
	return data;
}

uint8_t phy_data_ask_block(uint8_t* data)
{
	if (! phy_is_active()) return 0;
	phy_watchdog_start();

	phy_get(data, 0);
	phy_get(data + 256, 0);

	phy_watchdog_stop();
	return 1;
}

uint16_t phy_data_ask_bulk(uint8_t* data, uint16_t len)
{
	uint8_t v;

	if (! phy_is_active()) return 0;
	phy_watchdog_start();

	for (uint16_t i = 0; i < len; i++)
	{
		while (phy_is_ack_asserted());
		req_assert();
		while (! (phy_is_ack_asserted()));
		v = phy_data_get();
		req_release();
		#ifdef PHY_PORT_DATA_IN_REVERSED
			data[i] = phy_reverse_table[v];
		#else
			data[i] = v;
		#endif
	}

	phy_watchdog_stop();
	return len;
}

void phy_data_ask_stream(USART_t* usart, uint16_t len)
{
	uint8_t v;

	// guard against calling when not in control
	// note that ISR has the opposite guard
	if (! phy_is_active()) return;
	if (len == 0) return;
	phy_watchdog_start();

	uint8_t not_first = 0;
	do
	{
		// verify the initiator has released /ACK
		while (phy_is_ack_asserted());
		// ask for a byte of data
		req_assert();
		// wait for initiator to give us the data
		while (! (phy_is_ack_asserted()));
		// read data from the bus
		v = phy_data_get();
		// let the initiator know we got the information
		req_release();
		// get the true data value, if needed
		#ifdef PHY_PORT_DATA_IN_REVERSED
			v = phy_reverse_table[v];
		#endif

		// write to the USART once it is ready
		while (! (usart->STATUS & USART_DREIF_bm));
		usart->DATA = v;
		// wait for the previous byte to arrive, then pitch it
		if (not_first)
		{
			while (! (usart->STATUS & USART_RXCIF_bm));
			usart->DATA;
		}
		else
		{
			not_first = 1;
		}
	}
	while (--len);

	// get the response to the previous byte
	while (! (usart->STATUS & USART_RXCIF_bm));
	usart->DATA;

	phy_watchdog_stop();
}

/*
 * ============================================================================
 *  BUS CONTROL
 * ============================================================================
 */

void phy_phase(uint8_t new_phase)
{
	if (! phy_is_active()) return;
	
	// do nothing if the phase is the same
	if (PHY_REGISTER_PHASE == new_phase) return;

	// make sure REQ/ACK and any data lines have been released
	phy_data_clear();
	req_release();
	while (phy_is_ack_asserted());

	// wait at least 400ns for the bus to settle
	_delay_us(0.4);

	// change phase
	PHY_REGISTER_PHASE = new_phase;
	if (PHY_REGISTER_PHASE)
	{
		// I/O
		if (PHY_REGISTER_PHASE & 0x01)
		{
			io_assert();
		}
		else
		{
			io_release();
		}
		// C/D
		if (PHY_REGISTER_PHASE & 0x02)
		{
			cd_assert();
		}
		else
		{
			cd_release();
		}
		// MSG
		if (PHY_REGISTER_PHASE & 0x04)
		{
			msg_assert();
		}
		else
		{
			msg_release();
		}

		// wait another 400ns for the bus to settle
		_delay_us(0.4);
	}
	else
	{
		// clear the GPIO indicating we're selected
		PHY_REGISTER_STATUS &= ~(PHY_STATUS_ACTIVE_bm | PHY_STATUS_CONTINUED_bm);

		// release any set data lines
		phy_data_clear();

		// release phase control signals
		msg_release();
		cd_release();
		io_release();

		// and finally release /BSY to go bus free
		bsy_release();
	}
}

uint8_t phy_reselect(uint8_t target_mask)
{
	if (PHY_REGISTER_STATUS & PHY_STATUS_ASK_RESELECT_bm)
	{
		// request already pending
		return 0;
	}

	/* 
	 * Construct the mask of higher-priority IDs that will block successful
	 * reselection if detected on the bus.
	 */
	uint8_t block_mask = 0x00;
	uint8_t m = 1;
	for (uint8_t i = 0; i < 8; i++)
	{
		if (m > target_mask)
			block_mask |= m;
		m = m << 1;
	}

	// store the values we will need to use for arbitration/reselection
	debug(DEBUG_PHY_RESELECT_REQUESTED);
	PHY_REGISTER_STATUS |= PHY_STATUS_ASK_RESELECT_bm;
	arbitration_target_out = target_mask;
	#ifdef PHY_PORT_DATA_IN_REVERSED
		arbitration_target_in = phy_reverse_table[target_mask];
		arbitration_block_mask = phy_reverse_table[block_mask];
	#else
		arbitration_target_in = target_mask;
		arbitration_block_mask = block_mask;
	#endif

	// calculate the parity we'll need when /DB7 and our mask is set
	if (phy_bits_set[(target_mask | 0x80)] & 1)
	{
		PHY_REGISTER_STATUS &= ~PHY_STATUS_RESELECT_PARITY_bm;
	}
	else
	{
		PHY_REGISTER_STATUS |= PHY_STATUS_RESELECT_PARITY_bm;
	}

	/*
	 * Setup the /BSY ISR timer, used to detect the minimum safe time from /BSY
	 * rise to when arbitration could start. This is reset both when it hits
	 * TOP (about every 512us) and when /BSY rises (via the event system).
	 * 
	 * CCA is timed to fire about 800ns after the start of the timer.
	 * CCB is re-timed in the CCA ISR depending on call time.
	 */
	PHY_TIMER_BSY.PER = 0x3FFF;
	PHY_TIMER_BSY.CCA = PHY_TIMER_BSY_CCA_VAL;
	PHY_TIMER_BSY.CTRLD = TC_EVACT_RESTART_gc | PHY_TIMER_BSY_EVSEL;
	PHY_TIMER_BSY.INTCTRLB = TC_CCAINTLVL_MED_gc;
	PHY_TIMER_BSY.CTRLA = TC_CLKSEL_DIV1_gc;
	// and reset waveform on /BSY free
	PHY_TIMER_BSY_CHMUX = PHY_CHMUX_BSY;

	return 1;
}

/*
 * ============================================================================
 *   INTERRUPT DEFINITIONS
 * ============================================================================
 * 
 * A description of these is included in this file at the beginning. Refer
 * there for details.
 */

/*
 * Fired while a reselection request is pending about 800ns after /BSY has gone
 * from asserted to deasserted. If conditions are correct, this will start the
 * arbitration process.
 */
ISR(PHY_TIMER_BSY_CCA_vect)
{
	/*
	 * For our implementation, we will not start arbitrating unless we are the
	 * first entity to do so: our timer detects from the start of BUS FREE,
	 * so if another device is already set /BSY we would not know how long that
	 * has been going on, so it likely isn't safe to join in with them.
	 */
	if (phy_is_bsy_asserted()) return;
	if (phy_is_sel_asserted()) return;

	/*
	 * Still free, so we should be OK to start the arbitration process.
	 * 
	 * First, set /BSY, and put our ID on the bus (without parity).
	 */
	bsy_assert();
	PHY_PORT_DATA_OUT.OUT = arbitration_target_out;

	/*
	 * If another device detects that it won arbitration, it will assert /SEL,
	 * and we need to clear the bus quickly. Set that up now.
	 * 
	 * Note: this will disable the /BSY ISR to prevent possible wired-OR
	 * glitches from causing weird behavior.
	 */
	PHY_PORT_CTRL_IN.INTFLAGS = PORT_INT0IF_bm; // clear /SEL ISR
	PHY_PORT_CTRL_IN.INTCTRL  = PORT_INT0LVL_MED_gc; // /SEL on, /BSY off

	/*
	 * Note start of arbitration if enabled.
	 */
	debug(DEBUG_PHY_RESELECT_STARTING);

	/*
	 * Then we need to wait ~2400ns and check if we won. If we lose, /SEL goes
	 * low, which will handle disabling what we enable.
	 */
	PHY_TIMER_BSY.CCB = PHY_TIMER_BSY.CNT + PHY_TIMER_BSY_CCB_VAL;
	PHY_TIMER_BSY.INTFLAGS = TC0_CCBIF_bm; // clear last CCB match
	PHY_TIMER_BSY.INTCTRLB = TC_CCBINTLVL_MED_gc; // CCA off, CCB on
}

/*
 * Fired during a reselection request if we are trying to arbitrate, and only
 * if /SEL hasn't gone low while we've been waiting.
 */
ISR(PHY_TIMER_BSY_CCB_vect)
{
	/*
	 * Make sure /SEL didn't become asserted while the ISR was executing. If it
	 * did, it is an indication another device belives they won arbitration. We
	 * just return, and let the /SEL ISR should immediately fire and handle the
	 * condition.
	 */
	if (phy_is_sel_asserted()) return;

	/*
	 * Check if we are the highest priority on the bus.
	 */
	dclk_rise();
	doe_on();
	dclk_fall();
	uint8_t raw = PHY_PORT_DATA_IN.IN;
	doe_off();
	if (raw & arbitration_block_mask)
	{
		/*
		 * Lost arbitration, so put things back the way they were before the
		 * CCA ISR changed things.
		 */
		PHY_TIMER_BSY.INTFLAGS = TC0_CCAIF_bm; // clear any CCA
		PHY_TIMER_BSY.INTCTRLB = TC_CCAINTLVL_MED_gc; // CCB off, CCA on
		PHY_PORT_CTRL_IN.INTFLAGS = PORT_INT1IF_bm; // clear /BSY flag
		PHY_PORT_CTRL_IN.INTCTRL = PORT_INT1LVL_MED_gc; // /SEL off, /BSY on
		PHY_PORT_DATA_OUT.OUT = 0;
		bsy_release();
		debug(DEBUG_PHY_RESELECT_ARB_LOST);
	}
	else
	{
		// arbitration won, so set signals accordingly
		sel_assert();
		io_assert();
		if ((GLOBAL_CONFIG_REGISTER & GLOBAL_FLAG_PARITY)
			&& (PHY_REGISTER_STATUS & PHY_STATUS_RESELECT_PARITY_bm))
		{
			dbp_assert();
		}
		PHY_PORT_DATA_OUT.OUT = arbitration_target_out | 0x80; // assert /DB7

		// stop and disable the timer for use next time
		PHY_TIMER_BSY.CTRLA = TC_CLKSEL_OFF_gc;
		PHY_TIMER_BSY.CTRLGSET = TC_CMD_RESET_gc;
		PHY_TIMER_BSY_CHMUX = EVSYS_CHMUX_OFF_gc;

		// disallow /SEL or /BSY ISRs to prevent wired-OR glitch issues
		PHY_PORT_CTRL_IN.INTCTRL = 0;

		// setup and start the reselection response detect timer
		PHY_TIMER_RESEL.PER = PHY_TIMER_RESEL_VAL;
		PHY_TIMER_RESEL.INTCTRLA = TC_OVFINTLVL_MED_gc;
		PHY_TIMER_RESEL.CTRLA = TC_CLKSEL_DIV1_gc;

		// release /BSY and start waiting for the initiator
		debug(DEBUG_PHY_RESELECT_ARB_WON);
		bsy_release();
	}
}

/*
 * Called frequently during attempted reselection to see if the initiator has
 * responded to reselection.
 */
ISR(PHY_TIMER_RESEL_vect)
{
	if (phy_is_bsy_asserted())
	{
		/*
		 * Reselection has been successful. Match the current phase to the
		 * lines asserted and return to normal bus transactions with us
		 * active on the system. We also reconfigure the /BSY interrupt
		 * to normal mode at this point.
		 * 
		 * This disables any active request for reselection (since we're
		 * doing that now) by hard-setting the status register.
		 */
		bsy_assert();
		sel_release();
		phy_data_clear();

		// halt the overflow timer
		PHY_TIMER_RESEL.CTRLA = TC_CLKSEL_OFF_gc;
		PHY_TIMER_RESEL.CTRLGSET = TC_CMD_RESET_gc;

		// restore usual /BSY monitoring
		PHY_PORT_CTRL_IN.INTFLAGS = PORT_INT1IF_bm; // clear /BSY flag
		PHY_PORT_CTRL_IN.INTCTRL = PORT_INT1LVL_MED_gc; // /SEL off, /BSY on

		// indicate via status that we're now reconnected to the initiator
		active_target = arbitration_target_in;
		PHY_REGISTER_PHASE = PHY_PHASE_DATA_IN;
		PHY_REGISTER_STATUS = PHY_STATUS_ACTIVE_bm | PHY_STATUS_CONTINUED_bm;
		debug(DEBUG_PHY_RESELECT_FINISHED);
	}
}

/*
 * Handles /SEL becoming asserted during ARBITRATION.
 * 
 * If /SEL becomes asserted during this time and we didn't do it, it is an
 * indication that arbitration has been lost and we need to clear the bus
 * quickly. This will do that, and then reset things for the CCA logic again.
 */
ISR(PHY_CTRL_IN_INT0_vect)
{
	// release signals
	phy_data_clear();
	bsy_release();

	// set things back to let CCA hit again, if possible
	PHY_TIMER_BSY.INTFLAGS = TC0_CCAIF_bm; // clear any CCA
	PHY_TIMER_BSY.INTCTRLB = TC_CCAINTLVL_MED_gc; // CCB off, CCA on
	PHY_PORT_CTRL_IN.INTFLAGS = PORT_INT1IF_bm; // clear /BSY flag
	PHY_PORT_CTRL_IN.INTCTRL = PORT_INT1LVL_MED_gc; // /SEL off, /BSY on

	// note /SEL assertion
	debug(DEBUG_PHY_RESELECT_ARB_INTERRUPTED);
}

/*
 * Responds to /BSY releasing with /SEL asserted during a SELECTION phase. We
 * check the data bits on the line, and if we are being selected, we respond to
 * selection by asserting /BSY and becoming active on the bus.
 */
ISR(PHY_CTRL_IN_INT1_vect)
{
	if (phy_is_bsy_asserted()) return;
	if (phy_is_active()) return;
	if (phy_is_sel_asserted())
	{
		/*
		 * Pull the raw wire values, and check them against the prestored
		 * masks to see if we should treat this as a selection.
		 * 
		 * No ISRs are allowed to be higher than this one, except the /RST
		 * hard reset condition, so there is no need to make the following
		 * read atomic.
		 * 
		 * /DB7 is ignored in this situation, as it is implicit in a
		 * selection that can contain these devices thanks to our initiator
		 * limitations.
		 */
		dclk_rise();
		doe_on();
		dclk_fall();
		uint8_t raw = PHY_PORT_DATA_IN.IN;
		doe_off();
		raw = raw & 0xFE; // note: owned masks is pre-reversed as needed
		if (raw & owned_masks)
		{
			/*
			 * Respond to selection by asserting /BSY and storing the
			 * target details. We use DATA OUT as the phase to match
			 * /MSG, /I/O, /C/D all being released.
			 * 
			 * Note: we do not check to make sure that only our own mask
			 * is present on the bus, which is probably not a great idea.
			 */
			bsy_assert();
			active_target = raw;
			PHY_REGISTER_PHASE = PHY_PHASE_DATA_OUT;
			PHY_REGISTER_STATUS |= PHY_STATUS_ACTIVE_bm;
		}
	}
}

/*
 * When invoked, /RST is set, so we perform a hard reset.
 */
ISR(PHY_ISR_RST_vect)
{
	// following register is protected, hence ASM for time critical code
	__asm__ __volatile__(
		"ldi r24, %0"		"\n\t"
		"out %1, r24"		"\n\t"
		"ldi r24, %2"		"\n\t"
		"sts %3, r24"		"\n\t"
		:
		: "M" (CCP_IOREG_gc), "i" (&CCP),
		  "M" (RST_SWRST_bm), "i" (&(RST.CTRL))
		: "r24"
		);
}

/*
 * Invoked after an extended period of time where there is no data transfer
 * activity on the bus.
 */
ISR(PHY_TIMER_WATCHDOG_vect, ISR_NAKED)
{
	__asm__ __volatile__("set");
}
