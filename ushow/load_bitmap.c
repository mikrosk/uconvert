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

void* load_bitmap(FILE* f, const BitmapInfo* bitmap_info, const ScreenInfo* screen_info)
{
    char* screen = (char*)Mxalloc((screen_info->width * screen_info->height * screen_info->bpp / 8) + 15, MX_STRAM);
    if (!screen) {
        fprintf(stderr, "Not enough ST-RAM.\r\n");
        getchar();
        exit(EXIT_FAILURE);
    }

    bool c2p = bitmap_info->bpc == 1 && (bitmap_info->bpp == 4 || bitmap_info->bpp == 8)
            && (screen_info->mode & 0x07) != BPS8C;

    char* screen_aligned = (char*)(((uintptr_t)screen + 15) & 0xfffffff0);
    memset(screen_aligned, 0, screen_info->width * screen_info->height * screen_info->bpp / 8);

    char* c2p_buffer = NULL;

    size_t screen_x_offset = 0;
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
        screen_x_offset = (screen_info->width - bitmap_info->width) / 2; // left and right border
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
            fprintf(stderr, "Not enough RAM for C2P.\r\n");
            getchar();
            exit(EXIT_FAILURE);
        }
    }

    bool file_error = false;
    char* p = screen_aligned + screen_y_offset * (screen_info->width * screen_info->bpp / 8);
    for (size_t i = 0; i < final_height; ++i) {
        p += screen_x_offset * screen_info->bpp / 8;

        if (!c2p) {
            if (fread(p, sizeof(*p), final_width * screen_info->bpp / 8, f) != final_width * screen_info->bpp / 8) {
                file_error = true;
                break;
            }
        } else {
            if (fread(c2p_buffer, sizeof(*c2p_buffer), final_width * bitmap_info->bpc, f) != final_width * bitmap_info->bpc) {
                file_error = true;
                break;
            }

            if (bitmap_info->bpp == 4)
                c2p1x1_4_falcon(c2p_buffer, c2p_buffer + (final_width / 8) * 8, p); // original source code had *4 there, hm...
            else if (bitmap_info->bpp == 8)
                c2p1x1_8_falcon(c2p_buffer, c2p_buffer + (final_width / 8) * 8, p);
        }

        p += final_width * screen_info->bpp / 8;
        p += screen_x_offset * screen_info->bpp / 8;

        if (fseek(f, seek_offset, SEEK_CUR) != 0) {
            file_error = true;
            break;
        }
    }

    if (file_error) {
        fprintf(stderr, "I/O error.\r\n");
        getchar();
        exit(EXIT_FAILURE);
    }

    free(c2p_buffer);

    return screen_aligned;
}
