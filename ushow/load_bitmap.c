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

#include "load_bitmap.h"

#include "mint/falcon.h"
#include <mint/osbind.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c2p-asm.h"

void* load_bitmap(FILE* f, const BitmapInfo* bitmap_info, const ScreenInfo* screen_info, char* error)
{
    char* screen = NULL;

    if (!screen_info->keep_screen) {
        screen = (char*)Mxalloc((screen_info->width * screen_info->height * screen_info->bpp / 8) + 15, MX_STRAM);
        if (!screen) {
            sprintf(error, "Not enough ST-RAM.");
            return NULL;
        }
    }

    bool c2p = bitmap_info->bpc == 1 && (bitmap_info->bpp == 4 || bitmap_info->bpp == 6 || bitmap_info->bpp == 8)
            && (screen_info->keep_screen || (screen_info->mode & 0x07) != BPS8C);

    // no screen of our own: draw into the one the VDI is using
    char* screen_aligned = screen_info->keep_screen
            ? (char*)Logbase()
            : (char*)(((uintptr_t)screen + 15) & 0xfffffff0);
    memset(screen_aligned, 0, screen_info->width * screen_info->height * screen_info->bpp / 8);

    char* c2p_buffer = NULL;

    size_t screen_x_offset_l = 0, screen_x_offset_r = 0;
    size_t final_width = screen_info->width;
    size_t seek_offset = 0;
    if (bitmap_info->width > screen_info->width) {
        seek_offset = bitmap_info->width - screen_info->width;
        if (!c2p) {
            seek_offset *= screen_info->bpp;
            seek_offset /= 8;
        } else {
            seek_offset *= bitmap_info->bpc;
        }
    } else {
        size_t center_x = (screen_info->width - bitmap_info->width) / 2;
        screen_x_offset_l = center_x - (center_x % 16); // left border
        screen_x_offset_r = center_x + (center_x % 16); // right border
        final_width = bitmap_info->width;
    }

    size_t screen_y_offset = 0;
    size_t final_height = screen_info->height;
    if (bitmap_info->height < screen_info->height) {
        screen_y_offset = (screen_info->height - bitmap_info->height) / 2; // top and bottom border
        final_height = bitmap_info->height;
    }

    if (c2p) {
        c2p_buffer = (char*)malloc(final_width * bitmap_info->bpc);
        if (!c2p_buffer) {
            sprintf(error, "Not enough RAM for C2P.");
            return NULL;
        }
    }

    bool file_error = false;
    char* p = screen_aligned + screen_y_offset * (screen_info->width * screen_info->bpp / 8);
    for (size_t i = 0; i < final_height; ++i) {
        p += screen_x_offset_l * screen_info->bpp / 8;

        if (!c2p) {
            // allow to skip unused bitplanes after each 16-pixel tuple
            const size_t pixels_at_once = (screen_info->bpp != bitmap_info->bpp) ? 16 : final_width;

            for (size_t j = 0; j < final_width; j += pixels_at_once) {
                if (fread(p, sizeof(*p), pixels_at_once * bitmap_info->bpp / 8, f) != pixels_at_once * bitmap_info->bpp / 8) {
                    file_error = true;
                    break;
                }
                p += pixels_at_once * screen_info->bpp / 8;
            }
        } else {
            if (fread(c2p_buffer, sizeof(*c2p_buffer), final_width * bitmap_info->bpc, f) != final_width * bitmap_info->bpc) {
                file_error = true;
                break;
            }

            if (bitmap_info->bpp == 4)
                c2p1x1_4_falcon(c2p_buffer, c2p_buffer + (final_width / 8) * 8, p);
            else if (bitmap_info->bpp == 6)
                c2p1x1_6_falcon(c2p_buffer, c2p_buffer + (final_width / 8) * 8, p);
            else if (bitmap_info->bpp == 8)
                c2p1x1_8_falcon(c2p_buffer, c2p_buffer + (final_width / 8) * 8, p);

            p += final_width * screen_info->bpp / 8;
        }

        p += screen_x_offset_r * screen_info->bpp / 8;

        if (fseek(f, seek_offset, SEEK_CUR) != 0) {
            file_error = true;
            break;
        }
    }

    if (file_error) {
        sprintf(error, "I/O error.");
        return NULL;
    }

    free(c2p_buffer);

    return screen_aligned;
}
