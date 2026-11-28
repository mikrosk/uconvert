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

#include <mint/osbind.h>

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

    // 101, 110, 111: ST/E, TT, Falcon palette with a block of entries rewritten before every line
    uint16_t palette_bits = file_header.flags & 0b111;
    if ((palette_bits & 0b100) && (palette_bits & 0b011)) {
        uint8_t pad;
        fread(&bitmap_info.line_colors, sizeof(bitmap_info.line_colors), 1, f);
        fread(&pad, sizeof(pad), 1, f);
        palette_bits &= 0b011;
    }

    bitmap_info.palette_type = PaletteTypeNone;
    if (palette_bits == 0b001)
        bitmap_info.palette_type = PaletteTypeSTE;
    else if (palette_bits == 0b010)
        bitmap_info.palette_type = PaletteTypeTT;
    else if (palette_bits == 0b011)
        bitmap_info.palette_type = PaletteTypeFalcon;
    else if (palette_bits == 0b100)
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

    if (bitmap_info.line_colors) {
        if (bitmap_info.palette_type != PaletteTypeFalcon || (bitmap_info.bpp != 4 && bitmap_info.bpp != 8)) {
            sprintf(error, "Multiline palette supported only with Falcon palette and 4/8 bpp.");
            return bitmap_info;
        }

        // per line: the colours, then the register address of the first entry;
        // a display routine writes 8 of them, so the block is padded with the
        // colours the entries already hold (moved down if it would run past the
        // end of the palette)
        const size_t entries = 1 << bitmap_info.bpp;
        const size_t k = bitmap_info.line_colors;
        const size_t l = 8;
        if (k > l) {
            sprintf(error, "Multiline palette with more than %u colours per line.", (unsigned)l);
            return bitmap_info;
        }
        bitmap_info.line_writes = l;
        // a display routine reads one record ahead: the one after the last is there, unused
        // read during the picture with the data cache off: TT-RAM, if there is any, is faster
        const size_t record = l * sizeof(uint32_t) + sizeof(uint16_t);
        const size_t size = (bitmap_info.height + 1) * record;
        const long mem = Mxalloc(size, MX_PREFTTRAM);
        if (mem <= 0) {
            sprintf(error, "Not enough memory.");
            return bitmap_info;
        }
        bitmap_info.line_records = (void*)mem;
        memset(bitmap_info.line_records, 0, size);

        // the palette as the display has it before each line
        uint32_t current[256];
        memcpy(current, bitmap_info.palette.falcon, entries * sizeof(current[0]));

        for (size_t y = 0; y < bitmap_info.height; ++y) {
            uint16_t first;
            uint32_t colors[8];
            if (fread(&first, sizeof(first), 1, f) != 1 || fread(colors, sizeof(colors[0]), k, f) != k) {
                sprintf(error, "Read error.");
                return bitmap_info;
            }
            if (first + k > entries) {
                sprintf(error, "Line %u rewrites entries beyond the palette.", (unsigned)y);
                return bitmap_info;
            }
            for (size_t i = 0; i < k; ++i)
                current[first + i] = colors[i];

            const size_t start = first + l > entries ? entries - l : first;
            char* r = (char*)bitmap_info.line_records + y * record;
            const uint16_t reg = 0x9800 + 4 * start;   // $ffff9800 + 4 * start as a short address
            memcpy(r, current + start, l * sizeof(uint32_t));
            memcpy(r + l * sizeof(uint32_t), &reg, sizeof(reg));
        }
    }

    return bitmap_info;
}
