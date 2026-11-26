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
        uint16_t vdi[256][3];
    } palette;
    uint16_t    width;
    uint16_t    height;
    // multiline palette: entries rewritten before every line, 0 if none;
    // per line a record of line_writes colours (line_colors padded to 8 or 10
    // with colours the entries already hold) and a word with the low half of
    // the register address of the first; height + 1 records, word aligned
    uint8_t     line_colors;
    uint8_t     line_writes;
    void*       line_records;
} BitmapInfo;

// on failure 'error' is filled in and the result is not usable
BitmapInfo load_bitmap_info(FILE* f, const VdoValue vdo_val, char* error);

#endif
