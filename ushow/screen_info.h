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

#ifndef SCREEN_INFO_H
#define SCREEN_INFO_H

#include <stdint.h>
#include <stdlib.h>

#include "bitmap_info.h"
#include "vdo.h"

struct BitmapInfo;

typedef enum {
    RezValueSTLow  = 0,    // 320x200@4bpp, ST palette
    RezValueSTMid  = 1,    // 640x200@2bpp, ST palette
    RezValueSTHigh = 2,    // 640x400@1bpp, ST palette
    RezValueTTLow  = 7,    // 320x480@8bpp, TT palette
    RezValueTTMid  = 4,    // 640x480@4bpp, TT palette
    RezValueTTHigh = 6     // 1280x960@1bpp, TT palette
} SteTtRezValue;

typedef struct
{
    size_t  width;
    size_t  height;
    size_t  bpp;
    int16_t rez, old_rez;   // STE, TT
    int16_t mode, old_mode; // Falcon
} ScreenInfo;

extern ScreenInfo get_screen_info(const BitmapInfo* bitmap_info, const VdoValue vdo_val);

#endif
