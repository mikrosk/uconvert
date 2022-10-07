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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/osbind.h>

#include "bitmap_info.h"
#include "load_bitmap.h"
#include "screen-asm.h"
#include "screen_info.h"

static struct {
    BitmapInfo bitmap_info;
    ScreenInfo screen_info;
    void*      screen;
} page[256];

static void print_help()
{
    fprintf(stderr, "Usage: ushow.ttp <filename.ext> [<filename.ext>]...\r\n");
    fprintf(stderr, "Press enter to exit.\r\n");
    getchar();
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_help();
    }

    long vdo_val = VdoValueST << 16;
    Getcookie(C__VDO, &vdo_val);
    vdo_val >>= 16;	// interested in the upper word only

    if (vdo_val < VdoValueST || vdo_val > VdoValueFalcon) {
        fprintf(stderr, "Not an Atari compatible video.\r\n");
        getchar();
        return EXIT_FAILURE;
    }

    for (int i = 0; i < argc-1; ++i) {
        FILE* f = fopen(argv[i+1], "rb");
        if (!f) {
            fprintf(stderr, "Failed to open '%s'.\r\n", argv[i+1]);
            getchar();
            return EXIT_FAILURE;
        }

        printf("Processing '%s' ...\r\n", argv[i+1]);

        page[i].bitmap_info = load_bitmap_info(f, vdo_val);

        if (page[i].bitmap_info.width == 0 || page[i].bitmap_info.height == 0) {
            fprintf(stdout, "No bitmap data - nothing to show.\r\n");
            getchar();
            return EXIT_SUCCESS;
        }

        page[i].screen_info = get_screen_info(&page[i].bitmap_info, vdo_val);

        if (page[i].screen_info.rez == -1 && page[i].screen_info.mode == -1) {
            fprintf(stderr, "Unable to display: %dx%d@%dbpp (%s).\r\n",
                    page[i].bitmap_info.width, page[i].bitmap_info.height, page[i].bitmap_info.bpp,
                    page[i].bitmap_info.bpc == 0 ? "planar": "chunky");

            char* pal_str = "Unknown (?)";
            switch (page[i].bitmap_info.palette_type) {
            case PaletteTypeNone:
                pal_str = "None";
                break;
            case PaletteTypeSTE:
                pal_str = "ST/E";
                break;
            case PaletteTypeTT:
                pal_str = "TT";
                break;
            case PaletteTypeFalcon:
                pal_str = "Falcon";
                break;
            }
            fprintf(stderr, "Palette: %s.\r\n", pal_str);

            getchar();
            return EXIT_FAILURE;
        }

        page[i].screen = load_bitmap(f, &page[i].bitmap_info, &page[i].screen_info);

        fclose(f);
    }

    switch (vdo_val) {
    case VdoValueST:
    case VdoValueSTE:
        Supexec(asm_screen_ste_save);
        break;
    case VdoValueTT:
        Supexec(asm_screen_tt_save);
        break;
    case VdoValueFalcon:
        Supexec(asm_screen_falcon_save);
        break;
    }

    void* old_physbase = Physbase();

    //////////////////////////////////////////////////////////////////////////

    uint8_t ch = 0xff;
    int page_index = 0;
    do {
        int prev_page_index = page_index;

        if (ch == 0x4d) {   // right arrow
            page_index++;
            if (page_index > argc-2)
                page_index = 0;
        } else if (ch == 0x4b) {    // left arrow
            page_index--;
            if (page_index < 0)
                page_index = argc-2;
        }

        if (prev_page_index == page_index && ch != 0xff)
            continue;

        // Vsync() is needed for catching up raw palette access with (V)setScreen()
        // otherwise the new mode will reset the newly set palette
        Vsync();

        if (page[page_index].screen_info.rez != -1) {
            Setscreen(SCR_NOCHANGE, page[page_index].screen, page[page_index].screen_info.rez);
        } else if (page[page_index].screen_info.mode != -1) {
            // VsetScreen(SCR_NOCHANGE, page[page_index].screen, SCR_MODECODE, page[page_index].screen_infomode);
            // doesn't work as expected -- it not only reinitialises VT52 (useless for us)
            // but also expects *both* logbase and physbase be of an equal size, nicely
            // crashing if logbase is smaller. So don't bother, just use two separate calls.
            VsetMode(page[page_index].screen_info.mode);
            VsetScreen(SCR_NOCHANGE, page[page_index].screen, SCR_NOCHANGE, SCR_NOCHANGE);
        }

        if (page[page_index].bitmap_info.palette_type != PaletteTypeNone) {
            int32_t ssp = Super(0L);

            // Don't use Setpalette() / EsetPalette() / VsetRGB() here as we want to work
            // we the native palette format for given machine
            if (page[page_index].bitmap_info.palette_type == PaletteTypeSTE)
                asm_screen_set_ste_palette(page[page_index].bitmap_info.palette.ste, 1 << page[page_index].bitmap_info.bpp);
            else if (page[page_index].bitmap_info.palette_type == PaletteTypeTT)
                asm_screen_set_tt_palette(page[page_index].bitmap_info.palette.tt, 1 << page[page_index].bitmap_info.bpp);
            else if (page[page_index].bitmap_info.palette_type == PaletteTypeFalcon) {
                asm_screen_set_falcon_palette(page[page_index].bitmap_info.palette.falcon, 1 << page[page_index].bitmap_info.bpp);
            }

            Super(ssp);
        }
    } while ((ch = ((Crawcin() >> 16) & 0xff)) != 0x01);    // ESC

    //////////////////////////////////////////////////////////////////////////

    if (page[0].screen_info.old_rez != -1) {
        Setscreen(SCR_NOCHANGE, old_physbase, page[0].screen_info.old_rez);
    } else if (page[0].screen_info.old_mode != -1) {
        // make VDI (and SuperVidel registers) happy
        //VsetScreen(SCR_NOCHANGE, old_physbase, SCR_NOCHANGE, page[0].old_falcon_mode);
        VsetScreen(SCR_NOCHANGE, old_physbase, SCR_NOCHANGE, SCR_NOCHANGE);
        VsetMode(page[0].screen_info.old_mode);
    }

    switch (vdo_val) {
    case VdoValueST:
    case VdoValueSTE:
        // restoring also the Shifter/video base state, redudant
        Supexec(asm_screen_ste_restore);
        break;
    case VdoValueTT:
        // STE palette and Shifter are mapped to TT ones, no need to restore them
        // restoring also the Shifter/video base state, redudant
        Supexec(asm_screen_tt_restore);
        break;
    case VdoValueFalcon:
        // as VsetMode(old_falcon_mode) isn't 100% reliable, it's not redudant at all
        Supexec(asm_screen_falcon_restore);
        break;
    }

    return EXIT_SUCCESS;
}
