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

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/osbind.h>

#include "bitmap_info.h"
#include "load_bitmap.h"
#include "screen-asm.h"
#include "screen_info.h"

static void print_help()
{
    fprintf(stderr, "Usage: ushow.ttp [-c2p] <filename.ext> [<filename.ext>]\r\n");
    fprintf(stderr, "Press enter to exit.\r\n");
    getchar();
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 4) {
        print_help();
    }

    bool c2p = false;

    if (argc > 2) {
        if (strcmp(argv[1], "-c2p") == 0)
            c2p = true;
        // TODO: two filenames
    }

    long vdo_val = VdoValueST << 16;
    Getcookie(C__VDO, &vdo_val);
    vdo_val >>= 16;	// interested in the upper word only

    if (vdo_val < VdoValueST || vdo_val > VdoValueFalcon) {
        fprintf(stderr, "Not an Atari compatible video.\r\n");
        getchar();
        return EXIT_FAILURE;
    }

    FILE* f = fopen(argv[argc-1], "rb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s'.\r\n", argv[argc-1]);
        getchar();
        return EXIT_FAILURE;
    }

    BitmapInfo bitmap_info = load_bitmap_info(f, vdo_val, c2p);

    if (bitmap_info.width == 0 || bitmap_info.height == 0) {
        fprintf(stdout, "No bitmap data - nothing to show.\r\n");
        getchar();
        return EXIT_SUCCESS;
    }

    ScreenInfo screen_info = get_screen_info(&bitmap_info, vdo_val, c2p);

    if (screen_info.rez == -1 && screen_info.mode == -1) {
        fprintf(stderr, "Unable to display: %dx%d@%dbpp (%s).\r\n",
                bitmap_info.width, bitmap_info.height, bitmap_info.bpp,
                bitmap_info.bpc == 0 ? "planar": "chunky");

        char* pal_str = "Unknown (?)";
        switch (bitmap_info.palette_type) {
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

    char* screen_aligned = load_bitmap(f, &bitmap_info, &screen_info, c2p);

    fclose(f);

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

    if (screen_info.rez != -1) {
        Setscreen(SCR_NOCHANGE, screen_aligned, screen_info.rez);
    } else if (screen_info.mode != -1) {
        // VsetScreen(SCR_NOCHANGE, screen_aligned, SCR_MODECODE, falcon_mode);
        // doesn't work as expected -- it not only reinitialises VT52 (useless for us)
        // but also expects *both* logbase and physbase be of an equal size, nicely
        // crashing if logbase is smaller. So don't bother, just use two separate calls.
        VsetMode(screen_info.mode);
        VsetScreen(SCR_NOCHANGE, screen_aligned, SCR_NOCHANGE, SCR_NOCHANGE);
    }

    if (bitmap_info.palette_type != PaletteTypeNone) {
        // Vsync() is needed for catching up with (V)setScreen()
        // otherwise the new mode will reset the newly set palette
        Vsync();

        int32_t ssp = Super(0L);

        // Don't use Setpalette() / EsetPalette() / VsetRGB() here as we want to work
        // we the native palette format for given machine
        if (bitmap_info.palette_type == PaletteTypeSTE)
            asm_screen_set_ste_palette(bitmap_info.palette.ste, 1 << bitmap_info.bpp);
        else if (bitmap_info.palette_type == PaletteTypeTT)
            asm_screen_set_tt_palette(bitmap_info.palette.tt, 1 << bitmap_info.bpp);
        else if (bitmap_info.palette_type == PaletteTypeFalcon) {
            asm_screen_set_falcon_palette(bitmap_info.palette.falcon, 1 << bitmap_info.bpp);
        }

        Super(ssp);
    }

    getchar();

    if (screen_info.old_rez != -1) {
        Setscreen(SCR_NOCHANGE, old_physbase, screen_info.old_rez);
    } else if (screen_info.old_mode != -1) {
        // make VDI (and SuperVidel registers) happy
        //VsetScreen(SCR_NOCHANGE, old_physbase, SCR_NOCHANGE, old_falcon_mode);
        VsetScreen(SCR_NOCHANGE, old_physbase, SCR_NOCHANGE, SCR_NOCHANGE);
        VsetMode(screen_info.old_mode);
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
