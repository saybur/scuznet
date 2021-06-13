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

#ifndef ENC_H
#define ENC_H

#include <avr/io.h>

/*
 * Defines the interface for communicating with the ENC28J60 on an ATXMEGA AU
 * series MCU. This implementation defines the low-level registers of the ENC
 * device, handles handles basic communication, and tracks state enough to
 * make interfacing (slightly) more like handling an internal MCU peripheral
 * rather than an external device.
 * 
 * Important note: this implementation is not safe to use from the interrupt
 * context. It is able to handle being interrupted by simply idling the USART
 * with the /CS pin asserted, which modern silicon revisions of the ENC28J60
 * should be able to handle.
 * 
 * This code tracks which bank the device is in, so clients do not need to
 * manually switch banks. This is accomplished by tracking the state of the
 * ECON1 register in local memory whenever it gets read or written.
 * Consequently, it is important that after any PHY reset not associated with
 * a MCU reset, ECON1 be read or written *first*, before any other activity
 * occurs, to update this tracker.
 */

/*
 * Opcodes (reference from 4.2).
 */
#define ENC_OP_RCR 0x00
#define ENC_OP_RBM 0x3A
#define ENC_OP_WCR 0x40
#define ENC_OP_WBM 0x7A
#define ENC_OP_BFS 0x80
#define ENC_OP_BFC 0xA0
#define ENC_OP_SRC 0xFF

/*
 * Command registers. The organization of these is as follows:
 * 
 * Bit 7 (MSB): 0 for ETH registers, 1 for MAC and MII registers.
 * Bits 6 and 5: register bank, or 0 for non-banked ($1B-$1F registers)
 * Bit 4 through 0: register address.
 */
#define ENC_EIE 0x1B
#define ENC_EIR 0x1C
#define ENC_ESTAT 0x1D
#define ENC_ECON2 0x1E
#define ENC_ECON1 0x1F
#define ENC_ERDPTL 0x00
#define ENC_ERDPTH 0x01
#define ENC_EWRPTL 0x02
#define ENC_EWRPTH 0x03
#define ENC_ETXSTL 0x04
#define ENC_ETXSTH 0x05
#define ENC_ETXNDL 0x06
#define ENC_ETXNDH 0x07
#define ENC_ERXSTL 0x08
#define ENC_ERXSTH 0x09
#define ENC_ERXNDL 0x0A
#define ENC_ERXNDH 0x0B
#define ENC_ERXRDPTL 0x0C
#define ENC_ERXRDPTH 0x0D
#define ENC_ERXWRPTL 0x0E
#define ENC_ERXWRPTH 0x0F
#define ENC_EDMASTL 0x10
#define ENC_EDMASTH 0x11
#define ENC_EDMANDL 0x12
#define ENC_EDMANDH 0x13
#define ENC_EDMADSTL 0x14
#define ENC_EDMADSTH 0x15
#define ENC_EDMACSL 0x16
#define ENC_EDMACSH 0x17
#define ENC_EHT0 0x20
#define ENC_EHT1 0x21
#define ENC_EHT2 0x22
#define ENC_EHT3 0x23
#define ENC_EHT4 0x24
#define ENC_EHT5 0x25
#define ENC_EHT6 0x26
#define ENC_EHT7 0x27
#define ENC_EPMM0 0x28
#define ENC_EPMM1 0x29
#define ENC_EPMM2 0x2A
#define ENC_EPMM3 0x2B
#define ENC_EPMM4 0x2C
#define ENC_EPMM5 0x2D
#define ENC_EPMM6 0x2E
#define ENC_EPMM7 0x2F
#define ENC_EPMCSL 0x30
#define ENC_EPMCSH 0x31
#define ENC_EPMOL 0x34
#define ENC_EPMOH 0x35
#define ENC_ERXFCON 0x38
#define ENC_EPKTCNT 0x39
#define ENC_MACON1 0xC0
#define ENC_MACON3 0xC2
#define ENC_MACON4 0xC3
#define ENC_MABBIPG 0xC4
#define ENC_MAIPGL 0xC6
#define ENC_MAIPGH 0xC7
#define ENC_MACLCON1 0xC8
#define ENC_MACLCON2 0xC9
#define ENC_MAMXFLL 0xCA
#define ENC_MAMXFLH 0xCB
#define ENC_MICMD 0xD2
#define ENC_MIREGADR 0xD4
#define ENC_MIWRL 0xD6
#define ENC_MIWRH 0xD7
#define ENC_MIRDL 0xD8
#define ENC_MIRDH 0xD9
#define ENC_MAADR5 0xE0
#define ENC_MAADR6 0xE1
#define ENC_MAADR3 0xE2
#define ENC_MAADR4 0xE3
#define ENC_MAADR1 0xE4
#define ENC_MAADR2 0xE5
#define ENC_EBSTSD 0x66
#define ENC_EBSTCON 0x67
#define ENC_EBSTCSL 0x68
#define ENC_EBSTCSH 0x69
#define ENC_MISTAT 0xEA
#define ENC_EREVID 0x72
#define ENC_ECOCON 0x75
#define ENC_EFLOCON 0x77
#define ENC_EPAUSL 0x78
#define ENC_EPAUSH 0x79

// mask to get just the register out of the above
#define ENC_REG_MASK 0x1F

/*
 * Bit flags for the above registers.
 */
 
// EIE
#define ENC_INTIE_bm _BV(7)
#define ENC_PKTIE_bm _BV(6)
#define ENC_DMAIE_bm _BV(5)
#define ENC_LINKIE_bm _BV(4)
#define ENC_TXIE_bm _BV(3)
#define ENC_TXERIE_bm _BV(1)
#define ENC_RXERIE_bm _BV(0)

// EIR
#define ENC_PKTIF_bm _BV(6)
#define ENC_DMAIF_bm _BV(5)
#define ENC_LINKIF_bm _BV(4)
#define ENC_TXIF_bm _BV(3)
#define ENC_TXERIF_bm _BV(1)
#define ENC_RXERIF_bm _BV(0)

// ESTAT
#define ENC_INT_bm _BV(7)
#define ENC_BUFER_bm _BV(6)
#define ENC_LATECOL_bm _BV(4)
#define ENC_RXBUSY_bm _BV(2)
#define ENC_TXABRT_bm _BV(1)
#define ENC_CLKRDY_bm _BV(0)

// ECON2
#define ENC_AUTOINC_bm _BV(7)
#define ENC_PKTDEC_bm _BV(6)
#define ENC_PWRSV_bm _BV(5)
#define ENC_VRPS_bm _BV(3)

// ECON1
#define ENC_TXRST_bm _BV(7)
#define ENC_RXRST_bm _BV(6)
#define ENC_DMAST_bm _BV(5)
#define ENC_CSUMEN_bm _BV(4)
#define ENC_TXRTS_bm _BV(3)
#define ENC_RXEN_bm _BV(2)
#define ENC_BSEL1_bm _BV(1)
#define ENC_BSEL0_bm _BV(0)

// ERXFCON
#define ENC_UCEN_bm _BV(7)
#define ENC_ANDOR_bm _BV(6)
#define ENC_CRCEN_bm _BV(5)
#define ENC_PMEN_bm _BV(4)
#define ENC_MPEN_bm _BV(3)
#define ENC_HTEN_bm _BV(2)
#define ENC_MCEN_bm _BV(1)
#define ENC_BCEN_bm _BV(0)

// MACON1
#define ENC_TXPAUS_bm _BV(3)
#define ENC_RXPAUS_bm _BV(2)
#define ENC_PASSALL_bm _BV(1)
#define ENC_MARXEN_bm _BV(0)

// MACON3
#define ENC_PADCFG2_bm _BV(7)
#define ENC_PADCFG1_bm _BV(6)
#define ENC_PADCFG0_bm _BV(5)
#define ENC_TXCRCEN_bm _BV(4)
#define ENC_PHDREN_bm _BV(3)
#define ENC_HFRMEN_bm _BV(2)
#define ENC_FRMLNEN_bm _BV(1)
#define ENC_FULDPX_bm _BV(0)

// MACON4
#define ENC_DEFER_bm _BV(6)
#define ENC_BPEN_bm _BV(5)
#define ENC_NOBKOFF_bm _BV(4)

// EBSTCON
#define ENC_PSV2_bm _BV(7)
#define ENC_PSV1_bm _BV(6)
#define ENC_PSV0_bm _BV(5)
#define ENC_PSEL_bm _BV(4)
#define ENC_TMSEL1_bm _BV(3)
#define ENC_TMSEL0_bm _BV(2)
#define ENC_TME_bm _BV(1)
#define ENC_BISTST_bm _BV(0)

// MICMD
#define ENC_MIISCAN_bm _BV(1)
#define ENC_MIIRD_bm _BV(0)

// MISTAT
#define ENC_NVALID_bm _BV(2)
#define ENC_SCAN_bm _BV(1)
#define ENC_BUSY_bm _BV(0)

// ECOCON
#define ENC_COCON2_bm _BV(2)
#define ENC_COCON1_bm _BV(1)
#define ENC_COCON0_bm _BV(0)

// EFLOCON
#define ENC_FULDPXS_bm _BV(2)
#define ENC_FCEN1_bm _BV(1)
#define ENC_FCEN0_bm _BV(0)

/*
 * PHY registers. These require a special sequence for reading or
 * writing, which is handled by the PHY functions: do not use the
 * normal commands for these registers.
 */
#define ENC_PHY_PHCON1 0x00
#define ENC_PHY_PHSTAT1 0x01
#define ENC_PHY_PHID1 0x02
#define ENC_PHY_PHID2 0x03
#define ENC_PHY_PHCON2 0x10
#define ENC_PHY_PHSTAT2 0x11
#define ENC_PHY_PHIE 0x12
#define ENC_PHY_PHIR 0x13
#define ENC_PHY_PHLCON 0x14

/*
 * PHY bit flags for the above registers.
 */

// PHCON1
#define ENC_PRST_bm _BV(15)
#define ENC_PLOOPBK_bm _BV(14)
#define ENC_PPWRSV_bm _BV(11)
#define ENC_PDPXMD_bm _BV(8)

// PHSTAT1
#define ENC_PFDPX_bm _BV(12)
#define ENC_PHDPX_bm _BV(11)
#define ENC_LLSTAT_bm _BV(2)
#define ENC_JBSTAT_bm _BV(1)

// PHCON2
#define ENC_FRCLNK_bm _BV(14)
#define ENC_TXDIS_bm _BV(13)
#define ENC_JABBER_bm _BV(10)
#define ENC_HDLDIS_bm _BV(8)

// PHSTAT2
#define ENC_TXSTAT_bm _BV(13)
#define ENC_RXSTAT_bm _BV(12)
#define ENC_COLSTAT_bm _BV(11)
#define ENC_LSTAT_bm _BV(10)
#define ENC_DPXSTAT_bm _BV(9)
#define ENC_PLRITY_bm _BV(5)

// PHIE
#define ENC_PLNKIE_bm _BV(4)
#define ENC_PGEIE_bm _BV(1)

// PHIR
#define ENC_PLNKIF_bm _BV(4)
#define ENC_PGIF_bm _BV(2)

// PHLCON
#define ENC_LACFG3_bm _BV(11)
#define ENC_LACFG2_bm _BV(10)
#define ENC_LACFG1_bm _BV(9)
#define ENC_LACFG0_bm _BV(8)
#define ENC_LBCFG3_bm _BV(7)
#define ENC_LBCFG2_bm _BV(6)
#define ENC_LBCFG1_bm _BV(5)
#define ENC_LBCFG0_bm _BV(4)
#define ENC_LFRQ1_bm _BV(3)
#define ENC_LFRQ0_bm _BV(2)
#define ENC_STRCH_bm _BV(1)

/*
 * Response codes given in some functions:
 * 
 * * ENC_ERR_PHYBSY if MISTAT.BUSY set when a PHY function is called.
 * * ENC_ERR_PHYSCANNING if MISTAT.SCAN set when a PHY function is
 *   called.
 */
#define ENC_ERR_PHYBSY        4
#define ENC_ERR_PHYSCANNING   5

/*
 * Initalization calls, for use during MCU startup, before interrupts are
 * enabled. For details, see the function definitions.
 */
void enc_init(void);

/*
 * Register operations, as defined in 4.2. Most accept a data value to send to
 * the device, except for reading, which returns information in the given
 * pointer.
 * 
 * Each of these correctly handles the different types of registers that the
 * underlying command supports, more or less. Note that BFS and BFC work only
 * on ETH registers and will fail silently if given non-ETH registers.
 */
void enc_cmd_read(uint8_t, uint8_t*);
void enc_cmd_write(uint8_t, uint8_t);
void enc_cmd_set(uint8_t, uint8_t);
void enc_cmd_clear(uint8_t, uint8_t);

/*
 * PHY operations, documented in 3.3. They work similarly to the above
 * functions, except that they operate on the PHY registers and use the PHY
 * access procedure described in the datasheet. They will return errors if the
 * PHY subsystem is not available.
 * 
 * The scan function enables scanning the given PHY register. See the
 * definition in the .c file for details.
 */
uint8_t enc_phy_read(uint8_t, uint16_t*);
uint8_t enc_phy_write(uint8_t, uint16_t);
uint8_t enc_phy_scan(uint8_t);

/*
 * Operations that start a read buffer or write buffer operation.
 * 
 * Usage is as follows:
 * 
 * 1) Call either enc_read_start() or enc_write_start(). It will start the
 *    relevant transaction and return. While this is pending, no other
 *    operations should be performed in this system except as below.
 * 2) Use ENC_USART.DATA to send bytes. If writing, be aware that the RX logic
 *    of the USART is disabled.
 * 3) When all bytes are fully sent or received, call enc_data_end() to
 *    finish the transaction and restore the system to normal mode.
 * 
 * Note these calls assume ECON2.AUTOINC is set. If it is not, these will not
 * work correctly.
 */
void enc_read_start(void);
void enc_write_start(void);
void enc_data_end(void);

#endif /* ENC_H */
