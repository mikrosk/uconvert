/*
 * ushow: Atari ST/STE/TT/Falcon-specific bitmap viewer
 *
 * Copyright (c) 2022 Miro Kropacek <miro.kropacek@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef LINEPAL_ASM_H
#define LINEPAL_ASM_H

#include <stdint.h>

// Falcon, VGA 640x480, 8 bitplanes: a VBL handler writes the 256 colours of scanline 0
// and after every scanline the next of the 480 records (8 colours, the palette register
// of the first one); both arrays have to stay in place until asm_linepal_stop()
extern void asm_linepal_start(const uint32_t* pPalette, const uint32_t* pRecords);    // supervisor mode
extern void asm_linepal_stop(void);                                                     // supervisor mode

#endif
