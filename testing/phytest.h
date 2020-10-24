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

#ifndef PHYTEST_H
#define PHYTEST_H

#include <avr/io.h>

#define ACK_BIT 1
#define SEL_BIT 2
#define ATN_BIT 3
#define RST_BIT 4
#define CD_BIT  5
#define IO_BIT  6
#define MSG_BIT 7
#define REQ_BIT 8
#define BSY_BIT 9
#define DBP_BIT 10

void phy_init(void);
void phy_check(void);

#endif /* PHYTEST_H */
