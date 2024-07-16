/*
 * Copyright (C) 2024 saybur
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

#ifndef TOOLBOX_H
#define TOOLBOX_H

#ifdef USE_TOOLBOX

#define TOOLBOX_FOLDER          "/shared"
#define TOOLBOX_MAX_FILES       64

uint8_t toolbox_main(uint8_t *cmd);

#endif /* USE_TOOLBOX */

#endif /* TOOLBOX_H */
