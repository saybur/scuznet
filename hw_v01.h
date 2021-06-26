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
#define ENC_ENABLED
#define DAYNAPORT_ETHERNET // Comment out if compiling with Nuvolink

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
#define DEV_VPORT0_CFG          PORTCFG_VP02MAP_PORTA_gc
#define DEV_VPORT1_CFG          PORTCFG_VP13MAP_PORTR_gc
#define DEV_VPORT2_CFG          PORTCFG_VP02MAP_PORTC_gc
#define DEV_VPORT3_CFG          PORTCFG_VP13MAP_PORTD_gc

/*
 * ****************************************************************************
 * 
 *   DEBUGGING / REPORTING
 * 
 * ****************************************************************************
 */
#define DEBUG_USART             USARTE0
#define DEBUG_PORT              PORTE
#define DEBUG_PIN_TX            PIN3_bm
#define LED_PORT                VPORT3
#define LED_PIN                 PIN7_bm

/*
 * ****************************************************************************
 * 
 *   ETHERNET CONTROLLER
 * 
 * ****************************************************************************
 */
#define ENC_USART               USARTF0
#define ENC_USART_BAUDCTRL      0
#define ENC_PORT                PORTF
#define ENC_PIN_CS              PIN0_bm
#define ENC_PIN_XCK             PIN1_bm
#define ENC_PIN_RX              PIN2_bm
#define ENC_PIN_TX              PIN3_bm
#define ENC_RX_PINCTRL          PORTF.PIN2CTRL

#define ENC_PORT_EXT            PORTF
#define ENC_PIN_RST             PIN4_bm
#define ENC_PIN_INT             PIN5_bm
#define ENC_INT_PINCTRL         PORTF.PIN5CTRL

/*
 * ****************************************************************************
 * 
 *   MEMORY CARD
 * 
 * ****************************************************************************
 */
#define MEM_USART               USARTE1
#define MEM_PORT                PORTE
#define MEM_PIN_CS              PIN4_bm
#define MEM_PIN_XCK             PIN5_bm
#define MEM_PIN_RX              PIN6_bm
#define MEM_PIN_TX              PIN7_bm
#define MEM_PINCTRL_RX          PORTE.PIN6CTRL

/*
 * ****************************************************************************
 * 
 *   SCSI PHY
 * 
 * ****************************************************************************
 */

/*
 * See config.h for a description of these.
 */
#define PHY_PORT_DATA_IN_REVERSED
#define PHY_PORT_DATA_IN_INVERT
#define PHY_PORT_DATA_IN_CLOCK
#define PHY_PORT_DATA_IN_OE
#define PHY_PORT_DATA_IN_ACKEN

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
 * 5) Bitmasks must be set for all pins, and bit positions must be set for
 *    receiving on /ACK and transmitting on /DBP, /REQ.
 */
#define PHY_PORT_DATA_IN        PORTA
#define PHY_PORT_DATA_OUT       PORTB
#define PHY_PORT_R_RST          VPORT2
#define PHY_PORT_R_BSY          VPORT2
#define PHY_PORT_R_SEL          VPORT2
#define PHY_PORT_R_ATN          VPORT2
#define PHY_PORT_R_ACK          VPORT3
#define PHY_PORT_R_DBP          VPORT2
#define PHY_PORT_T_BSY          VPORT3
#define PHY_PORT_T_SEL          VPORT2
#define PHY_PORT_T_MSG          VPORT3
#define PHY_PORT_T_CD           VPORT3
#define PHY_PORT_T_IO           VPORT3
#define PHY_PORT_T_REQ          VPORT3
#define PHY_PORT_T_DBP          VPORT2
#define PHY_PORT_DOE            VPORT1
#define PHY_PORT_DCLK           VPORT3
#define PHY_PORT_ACKEN          VPORT2
#define PHY_PIN_R_RST           PIN6_bm
#define PHY_PIN_R_BSY           PIN4_bm
#define PHY_PIN_R_SEL           PIN3_bm
#define PHY_PIN_R_ATN           PIN5_bm
#define PHY_PIN_R_ACK           PIN2_bm
#define PHY_PIN_R_ACK_BP        PIN2_bp
#define PHY_PIN_R_DBP           PIN2_bm
#define PHY_PIN_T_BSY           PIN0_bm
#define PHY_PIN_T_SEL           PIN1_bm
#define PHY_PIN_T_MSG           PIN5_bm
#define PHY_PIN_T_CD            PIN3_bm
#define PHY_PIN_T_IO            PIN4_bm
#define PHY_PIN_T_REQ           PIN6_bm
#define PHY_PIN_T_REQ_BP        PIN6_bp
#define PHY_PIN_T_DBP           PIN0_bm
#define PHY_PIN_T_DBP_BP        PIN0_bp
#define PHY_PIN_DOE             PIN0_bm
#define PHY_PIN_DCLK            PIN1_bm
#define PHY_PIN_ACKEN           PIN7_bm
// a few need pin configs as well
#define PHY_CFG_R_SEL           PORTC.PIN1CTRL
#define PHY_CFG_R_BSY           PORTC.PIN4CTRL
#define PHY_CFG_R_RST           PORTC.PIN6CTRL
// and event channel information
#define PHY_CHMUX_RST           EVSYS_CHMUX_PORTC_PIN6_gc
#define PHY_CHMUX_BSY           EVSYS_CHMUX_PORTC_PIN4_gc

/*
 * Interrupt information for the port containing the /BSY and /SEL in lines.
 */
#define PHY_PORT_CTRL_IN        PORTC
#define PHY_CTRL_IN_INT0_vect   PORTC_INT0_vect
#define PHY_CTRL_IN_INT1_vect   PORTC_INT1_vect

#endif /* HARDWARE_H */
