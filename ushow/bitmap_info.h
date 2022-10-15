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

#ifndef BITMAP_INFO_H
#define BITMAP_INFO_H

#include <stdint.h>
#include <stdio.h>

#include "palette_type.h"
#include "vdo.h"

typedef struct {
    uint8_t     bpp;
    int8_t      bpc;
    PaletteType palette_type;
    union {
        uint16_t ste[16];
        uint16_t tt[256];
        uint32_t falcon[256];
    } palette;
    uint16_t    width;
    uint16_t    height;
} BitmapInfo;

// exits on error
BitmapInfo load_bitmap_info(FILE* f, const VdoValue vdo_val);

#endif
