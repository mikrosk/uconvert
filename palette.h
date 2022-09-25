/*
 * uconvert: bitmap converter into Atari ST/STE/TT/Falcon-specific format
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

#ifndef PALETTE_H
#define PALETTE_H

#include <cstdint>

#include "bitfield.h"

// RRRRRRrr GGGGGGgg 00000000 BBBBBBbb
BEGIN_BITFIELD_TYPE(FalconPaletteEntry, uint32_t)
    ADD_BITFIELD_MEMBER(r765432, 26, 6)
    ADD_BITFIELD_MEMBER(r10,     24, 2)
    ADD_BITFIELD_MEMBER(g765432, 18, 6)
    ADD_BITFIELD_MEMBER(g10,     16, 2)
    ADD_BITFIELD_MEMBER(unused,   8, 8)
    ADD_BITFIELD_MEMBER(b765432,  2, 6)
    ADD_BITFIELD_MEMBER(b10,      0, 2)
END_BITFIELD_TYPE()

// 0000 RRRR GGGG BBBB
BEGIN_BITFIELD_TYPE(TtPaletteEntry, uint16_t)
    ADD_BITFIELD_MEMBER(unused, 12, 4)
    ADD_BITFIELD_MEMBER(r3210,   8, 4)
    ADD_BITFIELD_MEMBER(g3210,   4, 4)
    ADD_BITFIELD_MEMBER(b3210,   0, 4)
END_BITFIELD_TYPE()

// 0000 rRRR gGGG bBBB
BEGIN_BITFIELD_TYPE(StePaletteEntry, uint16_t)
    ADD_BITFIELD_MEMBER(unused, 12, 4)
    ADD_BITFIELD_MEMBER(r0,     11, 1)
    ADD_BITFIELD_MEMBER(r321,    8, 3)
    ADD_BITFIELD_MEMBER(g0,      7 ,1)
    ADD_BITFIELD_MEMBER(g321,    4, 3)
    ADD_BITFIELD_MEMBER(b0,      3, 1)
    ADD_BITFIELD_MEMBER(b321,    0, 3)
END_BITFIELD_TYPE()

#endif // PALETTE_H
