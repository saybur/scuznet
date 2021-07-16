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

#ifndef CONFIG_H
#define CONFIG_H

#include <avr/io.h>
#include "lib/ff/ff.h"
#if defined(HW_V01)
	#include "hw_v01.h"
#elif defined(HW_V02)
	#include "hw_v02.h"
#else
	#error "You must define a hardware revision, like -DHW_V01"
#endif
#include "link.h"

/*
 * This file defines global built-in configuration values and the hardware
 * configuration.
 */

/*
 * ============================================================================
 *   CONFIGURATION VALUES
 * ============================================================================
 * 
 * Declares the configuration information visible to other parts of the
 * program. These should not be changed. To make modifications to the
 * configuration, edit 'scuznet.ini' on the memory card.
 */

/*
 * Defines the GPIO register used to store global device configuration flags.
 */
#define GLOBAL_CONFIG_REGISTER  GPIOR1

/*
 * Default value, and the location of status flags within the global
 * configuration register.
 */
#define GLOBAL_FLAG_PARITY               _BV(0)
#define GLOBAL_FLAG_DEBUG                _BV(1)
#define GLOBAL_FLAG_VERBOSE              _BV(2)
#define GLOBAL_FLAG_HDD_CHECKING         _BV(3)
#define GLOBAL_FLAG_HDD_CHECKED          _BV(4)
#define GLOBAL_FLAG_SELFTEST             _BV(5)

/*
 * The number of virtual hard drives that can be supported simultaneously.
 * 
 * This will break the debug-flash system if increased beyond 4.
 */
#define HARD_DRIVE_COUNT        4

/*
 * The Ethernet controller configuration information.
 */
typedef struct ENETConfig_t {
	uint8_t id;                 // disabled when set to 255
	uint8_t mask;               // the bitmask for the above ID
	LINKTYPE type;
	uint8_t mac[6];
} ENETConfig;
extern ENETConfig config_enet;

/*
 * The different options to set for the 'mode' value below.
 */
typedef enum {
	HDD_MODE_NORMAL,            // access is always through FAT
	HDD_MODE_FAST,              // low-level access if file contiguous
	HDD_MODE_FORCEFAST          // always low-level access (dangerous!)
} HDDMODE;

/*
 * The virtual hard drive configuration information.
 */
typedef struct HDDConfig_t {
	uint8_t id;                 // disabled when set to 255
	uint8_t mask;               // the bitmask for the above ID
	char* filename;             // filename for volume image
	uint32_t lba;	            // if !=0, start LBA for direct volumes
	uint32_t size;              // size of HDD in sectors
	FIL fp;
	HDDMODE mode;
} HDDConfig;
extern HDDConfig config_hdd[HARD_DRIVE_COUNT];

/*
 * ============================================================================
 *   HARDWARE CONFIGURATION
 * ============================================================================
 *
 * Different board revisions may have differnet pin assignments, which are
 * defined in separate headers added via an include into this file. A few
 * items get defined here if they are common to all -AU cores that could run
 * this firmware.
 */

/*
 * ****************************************************************************
 *   MEMORY CARD
 * ****************************************************************************
 */

/*
 * Defines how fast to run the SPI subsystem for the memory card when in
 * initialization mode and in standard mode. When initializing, this should be
 * between 100-400kbps. If the main MCU clock is changed this may need to be
 * updated to stay within specs. During normal operations, this should
 * probably be as fast as possible, hence 0.
 * 
 * Note: 39 is 400kbps @ 32MHz
 */
#define MEM_BAUDCTRL_INIT       39
#define MEM_BAUDCTRL_NORMAL     0

/*
 * Timer used to implement timeouts with the memory card inteface.
 */
#define MEM_TIMER               TCF0
#define MEM_TIMER_OVF           TC0_OVFIF_bm

/*
 * DMA 
 */
#define MEM_DMA_READ            DMA.CH0
#define MEM_DMA_WRITE           DMA.CH1
#define MEM_GPIOR               GPIORF

/*
 * ****************************************************************************
 *   ETHERNET PHY / NETWORKING
 * ****************************************************************************
 * 
 * This subsystem requires access to two DMA channels, the peripherals below,
 * additional resources in the hardware definitinos, and the CRC unit for
 * calculating hash filter values.
 */

/*
 * Tracks which bank SPI instructions to and from the PHY are using, for the
 * automatic bank tracking logic.
 */
#define ENC_BANK                GPIOR4

/*
 * DMA channels reserved for the networking subsystem, and the 16-bit address
 * of the relevant CTRLA registers (see datasheet 5.15 for offsets, and 34 for
 * the peripheral memory addresses).
 */
#define NET_DMA_WRITE           DMA.CH2
#define NET_DMA_WRITE_CTRLADDR  0x0130
#define NET_DMA_READ            DMA.CH3
#define NET_DMA_READ_CTRLADDR   0x0140
#define NET_DMA_READ_ISR        DMA_CH3_vect

/*
 * Used to manage state within the networking code.
 */
#define NET_FLAGS               GPIO5

/*
 * Used by the networking ISR to stash register values.
 */
#define NET_SCRATCH             GPIO6
#define NET_SCRATCH_IOADDR      0x06

/*
 * Timer to track the duration from the last time we requested a packet to be
 * sent. This helps inform the decision to cancel a packet transmission and
 * try it again.
 */
#define NET_TIMER               TCD0
#define NET_TIMER_OVF           TC0_OVFIF_bm

/*
 * ****************************************************************************
 *   SCSI PHY
 * ****************************************************************************
 */

/*
 * The following define the data input configuration of the hardware.
 * 
 * PHY_PORT_DATA_IN_REVERSED: if defined, the bit order on the input lines is
 * swapped, so that pin 0 has /DB7, pin 1 has /DB6, etc.
 * 
 * PHY_PORT_DATA_IN_INVERT: if defined, PORT_INVEN_bm will be set on the data
 * input lines. This is needed if the external hardware does not have built-in
 * logic inversion.
 * 
 * PHY_PORT_DATA_IN_CLOCK: if defined, this cycles PHY_PIN_DCLK from
 * low-to-high and from high-to-low prior to reading. If not defined, this
 * process is skipped.
 * 
 * PHY_PORT_DATA_IN_OE: if defined, this sets PHY_PIN_DOE low before reading,
 * and returns it to high after reading. If not defined, this process is
 * skipped.
 * 
 * PHY_PORT_DATA_IN_ACKEN: if defined, this indicates to the init logic that
 * the board has ACKEN logic that needs to be disabled. If not defined, this
 * process is skipped.
 * 
 * Define the relevant entries in the hardware header to ensure correct reading
 * operation.
 */

/*
 * GPIO registers where the condition of the PHY is tracked.
 * 
 * For details on what these do, refer to the PHY documentation.
 */
#define PHY_REGISTER_STATUS     GPIOR2
#define PHY_REGISTER_PHASE      GPIOR3

/*
 * The timer used for tracking the duration of time since /BSY was last seen
 * rising, along with the event channel information that resets the timer.
 */
#define PHY_TIMER_BSY           TCC0
#define PHY_TIMER_BSY_CHMUX     EVSYS.CH7MUX
#define PHY_TIMER_BSY_EVSEL     TC_EVSEL_CH7_gc
#define PHY_TIMER_BSY_CCA_vect  TCC0_CCA_vect
#define PHY_TIMER_BSY_CCB_vect  TCC0_CCB_vect

/*
 * Timer used for probing if /BSY has become asserted while we're waiting for
 * the initiator to respond to reselection.
 */
#define PHY_TIMER_RESEL         TCC1
#define PHY_TIMER_RESEL_vect    TCC1_OVF_vect

/*
 * The timer used to consume /RST events and trigger a interrupt that will
 * reset the MCU. The timer will be set up to trigger CCA, so the relevant
 * CCA ISR vector must be the one defined below.
 */
#define PHY_TIMER_RST           TCD1
#define PHY_ISR_RST_vect        TCD1_CCA_vect
#define PHY_TIMER_RST_CLKSEL    TC_CLKSEL_EVCH6_gc
#define PHY_TIMER_RST_CHMUX     EVSYS.CH6MUX
#define PHY_TIMER_RST_CHCTRL    EVSYS.CH6CTRL

/*
 * Timer used to monitor how long it has been since a DISCONNECT message was
 * received from the initiator, to implement the disconnection delay.
 */
#define PHY_TIMER_DISCON        TCE0
#define PHY_TIMER_DISCON_OVF    TC0_OVFIF_bm

/*
 * After a DISCONNECT message is received, wait this many Fclk/64 clocks before
 * attempting to reselect the initiator. This disconnects for 5ms, which is
 * significantly longer than the 200us required.
 */
#define PHY_TIMER_DISCON_DELAY  2480

/*
 * Timer used to track bus deadlock conditions and respond to situations where
 * the REQ/ACK interlock has failed.
 */
#define PHY_TIMER_WATCHDOG      TCE1
#define PHY_TIMER_WATCHDOG_vect TCE1_OVF_vect

/*
 * ============================================================================
 *  
 *   CONFIGURATION CALLS
 * 
 * ============================================================================
 */

/*
 * Reads SCUZNET.INI and inserts the configuration values into the global
 * variables. This returns the logical OR of the target masks in the provided
 * pointer.
 * 
 * If there is a problem reading the configuration, this will directly invoke
 * fatal() with appropriate messages. The volume must be mounted before this is
 * invoked!
 */
void config_read(uint8_t*);

#endif /* CONFIG_H */
