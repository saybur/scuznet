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
#ifdef HW_V01
	#include "hw_v01.h"
#else
	#error "You must define a hardware revision, like -DHW_V01"
#endif

/*
 * This file defines global built-in configuration values and the hardware
 * configuration.
 */

/*
 * ============================================================================
 *  
 *   CONFIGURATION DEFAULT VALUES
 * 
 * ============================================================================
 * 
 * Most of these values should not be changed. Instead, refer to the program
 * documentation, which describes how the EEPROM may be used to change the
 * device's behavior.
 */

/*
 * Defines the GPIO register used to store global device configuration. This is
 * loaded at startup with a default value unless EEPROM configuration has been
 * adjusted, in which case the value stored here will be the one from EEPROM.
 */
#define GLOBAL_CONFIG_REGISTER  GPIOR1

/*
 * Default value, and the location of status flags within the global
 * configuration register.
 */
#define GLOBAL_FLAG_PARITY      _BV(0)
#define GLOBAL_FLAG_DEBUG       _BV(1)
#define GLOBAL_CONFIG_DEFAULTS  GLOBAL_FLAG_PARITY

/*
 * The number of bus devices that we are able to support at the same time.
 */
#define LOGIC_DEVICE_COUNT      2

/*
 * Defines the default "ROM" MAC address of the device used during startup, in 
 * MSB to LSB order, if a new one has not been written to EEPROM.
 * 
 * For the high byte, ensure that b0 is 0, and b1 is 1, to conform to the
 * standard MAC address requirements that this is not a multicast destination
 * and that this is a locally administered MAC address.
 */
#define NET_MAC_DEFAULT_ADDR_1  0x02
#define NET_MAC_DEFAULT_ADDR_2  0x00
#define NET_MAC_DEFAULT_ADDR_3  0x00
#define NET_MAC_DEFAULT_ADDR_4  0xBE
#define NET_MAC_DEFAULT_ADDR_5  0xEE
#define NET_MAC_DEFAULT_ADDR_6  0xEF

/*
 * Defines the default device IDs in the event that there is no configuration
 * data stored for this in EEPROM.
 */
#define DEVICE_ID_HDD           3
#define DEVICE_ID_LINK          4

/*
 * The EEPROM starting location, the length of the result array, and the value
 * used to determine if the EEPROM data is valid.
 */
#define CONFIG_EEPROM_ADDR      0x00
#define CONFIG_EEPROM_LENGTH    10
#define CONFIG_EEPROM_VALIDITY  0xAA

/*
 * The array offsets for EEPROM configuration information.
 */
#define CONFIG_OFFSET_VALIDITY  0
#define CONFIG_OFFSET_FLAGS     1
#define CONFIG_OFFSET_ID_HDD    2
#define CONFIG_OFFSET_ID_LINK   3
#define CONFIG_OFFSET_MAC       4

/*
 * ============================================================================
 *  
 *   HARDWARE CONFIGURATION
 * 
 * ============================================================================
 *
 * Different board revisions may have differnet pin assignments, which are
 * defined in separate headers added via an include into this file. A few
 * items get defined here if they are common to all -AU cores that could run
 * this firmware.
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
 * ****************************************************************************
 * 
 *   SCSI PHY
 * 
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
 * ============================================================================
 *  
 *   CONFIGURATION CALLS
 * 
 * ============================================================================
 */

/*
 * Called to read the EEPROM configuration into the given array. This will
 * read the data, modify it appropriately to make sure it is valid, and store
 * it in the provided array.
 */
void config_read(uint8_t*);

#endif /* CONFIG_H */
