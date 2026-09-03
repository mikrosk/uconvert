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

#include "bitmap_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uimg.h"

BitmapInfo load_bitmap_info(FILE* f, const VdoValue vdo_val, char* error)
{
    BitmapInfo bitmap_info = {};
    FileHeader file_header;

    if (fread(&file_header, sizeof(file_header), 1, f) != 1) {
        sprintf(error, "Read error.");
        return bitmap_info;
    }

    if (strncmp(file_header.id, "UIMG", 4) != 0) {
        sprintf(error, "Invalid header: '%.4s'.", file_header.id);
        return bitmap_info;
    }

    bitmap_info.bpp = file_header.bitsPerPixel;
    bitmap_info.bpc = file_header.bytesPerChunk;

    bitmap_info.palette_type = PaletteTypeNone;
    if ((file_header.flags & 0b111) == 0b001)
        bitmap_info.palette_type = PaletteTypeSTE;
    else if ((file_header.flags & 0b111) == 0b010)
        bitmap_info.palette_type = PaletteTypeTT;
    else if ((file_header.flags & 0b111) == 0b011)
        bitmap_info.palette_type = PaletteTypeFalcon;
    else if ((file_header.flags & 0b111) == 0b100)
        bitmap_info.palette_type = PaletteTypeVDI;

    fread(&bitmap_info.width, sizeof(bitmap_info.width), 1, f);
    fread(&bitmap_info.height, sizeof(bitmap_info.height), 1, f);

    if (bitmap_info.width == 0 || bitmap_info.height == 0) {
        sprintf(error, "Bitmap data not present.");
        return bitmap_info;
    }

    if (bitmap_info.palette_type == PaletteTypeTT && vdo_val != VdoValueTT) {
        sprintf(error, "TT palette can be set only on TT.");
        return bitmap_info;
    }

    if (bitmap_info.palette_type == PaletteTypeFalcon && vdo_val != VdoValueFalcon) {
        sprintf(error, "Falcon palette can be set only on Falcon.");
        return bitmap_info;
    }

    if (bitmap_info.bpc == 1 && bitmap_info.bpp != 4 && bitmap_info.bpp != 6 && bitmap_info.bpp != 8) {
        sprintf(error, "Unsupported C2P configuration (bpp: %d, bpc: %d).", bitmap_info.bpp, bitmap_info.bpc);
        return bitmap_info;
    }

    if (bitmap_info.palette_type == PaletteTypeSTE) {
        fread(bitmap_info.palette.ste, sizeof(bitmap_info.palette.ste[0]), 1 << bitmap_info.bpp, f);
    } else if (bitmap_info.palette_type == PaletteTypeTT) {
        fread(bitmap_info.palette.tt, sizeof(bitmap_info.palette.tt[0]), 1 << bitmap_info.bpp, f);
    } else if (bitmap_info.palette_type == PaletteTypeFalcon) {
        fread(bitmap_info.palette.falcon, sizeof(bitmap_info.palette.falcon[0]), 1 << bitmap_info.bpp, f);
    } else if (bitmap_info.palette_type == PaletteTypeVDI) {
        fread(bitmap_info.palette.vdi, sizeof(bitmap_info.palette.vdi[0]), 1 << bitmap_info.bpp, f);
    }

    return bitmap_info;
}
