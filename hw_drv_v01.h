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

#ifndef HARDWARE_H
#define HARDWARE_H

#include <avr/io.h>

/*
 * ============================================================================
 *  
 *   PIN AND PER-HARDWARE PERIPHERAL ASSIGNMENTS
 * 
 * ============================================================================
 * 
 * Use particular caution if changing anything in the following sections. Each
 * module that uses these definitions will generally assume that it is the only
 * user of assigned resources, with resource collisions likely causing
 * undefined behavior and/or possible hardware damage.
 */

/*
 * Set the features that should be compiled into the firmware.
 */
#define HDD_ENABLED

/*
 * ****************************************************************************
 * 
 *   VIRTUAL PORT ASSIGNMENTS
 * 
 * ****************************************************************************
 * 
 * The VPORT assignments on this device are static, and are likely just for
 * the PHY. Make sure to update later sections if these need to change.
 */
#define DEV_VPORT0_CFG          PORTCFG_VP02MAP_PORTB_gc
#define DEV_VPORT1_CFG          PORTCFG_VP13MAP_PORTD_gc
#define DEV_VPORT2_CFG          PORTCFG_VP02MAP_PORTE_gc
#define DEV_VPORT3_CFG          PORTCFG_VP13MAP_PORTR_gc

/*
 * ****************************************************************************
 * 
 *   DEBUGGING / REPORTING
 * 
 * ****************************************************************************
 */
#define DEBUG_USART             USARTD1
#define DEBUG_PORT              PORTD
#define DEBUG_PIN_TX            PIN7_bm
#define LED_PORT                VPORT1
#define LED_PIN                 PIN7_bm

/*
 * ****************************************************************************
 * 
 *   MEMORY CARD
 * 
 * ****************************************************************************
 */
#define MEM_USART               USARTD0
#define MEM_PORT                PORTD
#define MEM_PIN_CS              PIN0_bm
#define MEM_PIN_XCK             PIN1_bm
#define MEM_PIN_RX              PIN2_bm
#define MEM_PIN_TX              PIN3_bm
#define MEM_PINCTRL_RX          PORTD.PIN2CTRL

/*
 * ****************************************************************************
 * 
 *   SCSI PHY
 * 
 * ****************************************************************************
 */

/*
 * Pin and port assignments. These end up scattered across a bunch of ports
 * to get everything assigned correctly. Things can be rearranged if needed,
 * but the following needs to be placed together:
 * 
 * 1) A port must be dedicated to the data in lines.
 * 2) Another port must be dedicated to the data out lines.
 * 3) The control inputs need to be buffered through an inverting Schmitt
 *    trigger.
 * 4) The control input lines for /BSY and /SEL need to be on the same port,
 *    and that port can have no other pin interrupts associated with it.
 */
#define PHY_PORT_DATA_IN        PORTA
#define PHY_PORT_DATA_OUT       PORTC
#define PHY_PORT_R_RST          VPORT2
#define PHY_PORT_R_BSY          VPORT2
#define PHY_PORT_R_SEL          VPORT2
#define PHY_PORT_R_ATN          VPORT3
#define PHY_PORT_R_ACK          VPORT3
#define PHY_PORT_T_BSY          VPORT0
#define PHY_PORT_T_SEL          VPORT0
#define PHY_PORT_T_MSG          VPORT2
#define PHY_PORT_T_CD           VPORT1
#define PHY_PORT_T_IO           VPORT1
#define PHY_PORT_T_REQ          VPORT1
#define PHY_PORT_T_DBP          VPORT0
#define PHY_PIN_R_RST           PIN1_bm
#define PHY_PIN_R_BSY           PIN3_bm
#define PHY_PIN_R_SEL           PIN2_bm
#define PHY_PIN_R_ATN           PIN1_bm
#define PHY_PIN_R_ACK           PIN0_bm
#define PHY_PIN_T_BSY           PIN2_bm
#define PHY_PIN_T_SEL           PIN0_bm
#define PHY_PIN_T_MSG           PIN0_bm
#define PHY_PIN_T_CD            PIN6_bm
#define PHY_PIN_T_IO            PIN4_bm
#define PHY_PIN_T_REQ           PIN5_bm
#define PHY_PIN_T_DBP           PIN1_bm
// a few need pin configs as well
#define PHY_CFG_R_SEL           PORTE.PIN2CTRL
#define PHY_CFG_R_BSY           PORTE.PIN3CTRL
#define PHY_CFG_R_RST           PORTE.PIN1CTRL
// and event channel information
#define PHY_CHMUX_RST           EVSYS_CHMUX_PORTE_PIN1_gc
#define PHY_CHMUX_BSY           EVSYS_CHMUX_PORTE_PIN3_gc

/*
 * Interrupt information for the port containing the /BSY and /SEL in lines.
 */
#define PHY_PORT_CTRL_IN        PORTE
#define PHY_CTRL_IN_INT0_vect   PORTE_INT0_vect
#define PHY_CTRL_IN_INT1_vect   PORTE_INT1_vect

#endif /* HARDWARE_H */
