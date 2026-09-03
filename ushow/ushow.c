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

#include <gem.h>
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
    char*      path;        // kept for the bitmaps we draw only when they are shown
} page[256];

static char error[256];

static void print_error(const char* text)
{
    fprintf(stderr, "%s\r\n", text);
    fprintf(stderr, "Press enter to exit.\r\n");
    getchar();
    exit(EXIT_FAILURE);
}

static void print_help()
{
    fprintf(stderr, "Usage: ushow.ttp <filename.ext> [<filename.ext>]...\r\n");
    fprintf(stderr, "Press enter to exit.\r\n");
    getchar();
    exit(EXIT_FAILURE);
}

static int16_t saved_palette[256][3];

static void save_vdi_palette(int16_t vdi_handle, size_t colors)
{
    for (int16_t pen = 0; pen < (int16_t)colors; ++pen)
        vq_color(vdi_handle, pen, 1, saved_palette[pen]);
}

static void restore_vdi_palette(int16_t vdi_handle, size_t colors)
{
    for (int16_t pen = 0; pen < (int16_t)colors; ++pen)
        vs_color(vdi_handle, pen, saved_palette[pen]);
}

static void set_vdi_palette(const BitmapInfo* bitmap_info, int16_t vdi_handle)
{
    for (int16_t pen = 0; pen < (1 << bitmap_info->bpp); ++pen)
        vs_color(vdi_handle, pen, (int16_t*)bitmap_info->palette.vdi[pen]);
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_help();
    }

    long vdo_val = VdoValueST << 16;
    Getcookie(C__VDO, &vdo_val);
    vdo_val >>= 16;	// interested in the upper word only

    if (vdo_val < VdoValueST || vdo_val > VdoValueFalcon)
        print_error("Not an Atari compatible video.");

    // GEM first: get_screen_info() needs the size of the screen the VDI is using
    // to tell whether a VDI palette bitmap fits it
    int16_t app_id = appl_init();
    bool aes_present = aes_global[0] != 0x0000;

    if (app_id == -1 && aes_present)
        print_error("appl_init() failed.");

    int16_t vdi_handle = 0;
    size_t vdi_width = 0, vdi_height = 0, vdi_bpp = 0;

    if (app_id != -1) {
        int16_t work_in[11];
        int16_t work_out[57];
        int16_t dummy;

        for (size_t i = 0; i < 10; ++i)
            work_in[i] = 1;
        work_in[10] = 2;

        vdi_handle = graf_handle(&dummy, &dummy, &dummy, &dummy);
        if (vdi_handle >= 1) {
            v_opnvwk(work_in, &vdi_handle, work_out);

            if (vdi_handle != 0) {
                vdi_width  = work_out[0] + 1;
                vdi_height = work_out[1] + 1;
                for (vdi_bpp = 1; (1L << vdi_bpp) < work_out[13]; ++vdi_bpp)
                    ;

                save_vdi_palette(vdi_handle, 1L << vdi_bpp);
            }
        } else {
            vdi_handle = 0;
        }
    }

    bool vdi_pages = false, other_pages = false;

    for (int i = 0; i < argc-1; ++i) {
        FILE* f = fopen(argv[i+1], "rb");
        if (!f) {
            snprintf(error, sizeof(error), "Failed to open '%s'.", argv[i+1]);
            break;
        }

        printf("Processing '%s' ...\r\n", argv[i+1]);

        page[i].bitmap_info = load_bitmap_info(f, vdo_val, error);
        if (error[0]) {
            fclose(f);
            break;
        }

        if (page[i].bitmap_info.palette_type == PaletteTypeVDI)
            vdi_pages = true;
        else
            other_pages = true;

        // one draws on the desktop's screen, the others take it over
        if (vdi_pages && other_pages)
            sprintf(error, "Can't mix VDI palette bitmaps with the rest.");
        else if (page[i].bitmap_info.width == 0 || page[i].bitmap_info.height == 0)
            sprintf(error, "No bitmap data - nothing to show.");

        if (error[0]) {
            fclose(f);
            break;
        }

        if (page[i].bitmap_info.palette_type == PaletteTypeVDI)
            page[i].screen_info = get_vdi_screen_info(&page[i].bitmap_info, vdi_width, vdi_height, vdi_bpp);
        else
            page[i].screen_info = get_screen_info(&page[i].bitmap_info, vdo_val);

        if (!page[i].screen_info.keep_screen
                && page[i].screen_info.rez == -1 && page[i].screen_info.mode == -1) {
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
            case PaletteTypeVDI:
                pal_str = "VDI";
                break;
            }
            snprintf(error, sizeof(error), "Unable to display: %dx%d@%dbpp (%s), palette: %s.",
                     page[i].bitmap_info.width, page[i].bitmap_info.height, page[i].bitmap_info.bpp,
                     page[i].bitmap_info.bpc == 0 ? "planar": "chunky", pal_str);
            fclose(f);
            break;
        }

        page[i].path = argv[i+1];

        // one screen, several bitmaps: those go in when they are actually shown
        if (!page[i].screen_info.keep_screen)
            page[i].screen = load_bitmap(f, &page[i].bitmap_info, &page[i].screen_info, error);

        fclose(f);

        if (error[0])
            break;
    }

    if (error[0])
        goto exit_gem;

    // a VDI palette bitmap is shown without touching any of this
    if (!vdi_pages) {
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
    }

    //////////////////////////////////////////////////////////////////////////

    // OS area
    {
        if (app_id != -1)
            wind_update(BEG_UPDATE);

        void* old_physbase = Physbase();

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

            if (!page[page_index].screen_info.keep_screen) {
                // Vsync() is needed for catching up raw palette access with (V)setScreen()
                // otherwise the new mode will reset the newly set palette
                Vsync();
            }

            if (page[page_index].screen_info.keep_screen) {
                // nothing to set up, it goes straight into the screen we are drawing on
                FILE* f = fopen(page[page_index].path, "rb");
                if (!f) {
                    snprintf(error, sizeof(error), "Failed to open '%s'.", page[page_index].path);
                    break;
                }

                BitmapInfo bitmap_info = load_bitmap_info(f, vdo_val, error);
                if (!error[0])
                    load_bitmap(f, &bitmap_info, &page[page_index].screen_info, error);
                fclose(f);

                if (error[0])
                    break;
            } else if (page[page_index].screen_info.rez != -1) {
                Setscreen(SCR_NOCHANGE, page[page_index].screen, page[page_index].screen_info.rez);
            } else if (page[page_index].screen_info.mode != -1) {
                // VsetScreen(SCR_NOCHANGE, page[page_index].screen, SCR_MODECODE, page[page_index].screen_infomode);
                // doesn't work as expected -- it not only reinitialises VT52 (useless for us)
                // but also expects *both* logbase and physbase be of an equal size, nicely
                // crashing if logbase is smaller. So don't bother, just use two separate calls.
                VsetMode(page[page_index].screen_info.mode);
                VsetScreen(SCR_NOCHANGE, page[page_index].screen, SCR_NOCHANGE, SCR_NOCHANGE);
            }

            if (page[page_index].bitmap_info.palette_type == PaletteTypeVDI) {
                // not under Super(), these are VDI calls
                set_vdi_palette(&page[page_index].bitmap_info, vdi_handle);
            } else if (page[page_index].bitmap_info.palette_type != PaletteTypeNone) {
                int32_t ssp = Super(0L);

                // Don't use Setpalette() / EsetPalette() / VsetRGB() here as we want to work
                // with the native palette format for given machine
                if (page[page_index].bitmap_info.palette_type == PaletteTypeSTE)
                    asm_screen_set_ste_palette(page[page_index].bitmap_info.palette.ste, 1 << page[page_index].bitmap_info.bpp);
                else if (page[page_index].bitmap_info.palette_type == PaletteTypeTT)
                    asm_screen_set_tt_palette(page[page_index].bitmap_info.palette.tt, 1 << page[page_index].bitmap_info.bpp);
                else if (page[page_index].bitmap_info.palette_type == PaletteTypeFalcon) {
                    asm_screen_set_falcon_palette(page[page_index].bitmap_info.palette.falcon, 1 << page[page_index].bitmap_info.bpp);
                }

                Super(ssp);
            }
        // loop until ESC = 0x01 is pressed
        } while ((aes_present && (ch = ((evnt_keybd() >> 8) & 0xff)) != 0x01)
                 || (!aes_present && (ch = ((Crawcin() >> 16) & 0xff)) != 0x01));

        if (page[0].screen_info.keep_screen) {
            // the VDI remembers what vs_color() was told, so give it back its own
            // colours before letting GEM redraw the desktop
            restore_vdi_palette(vdi_handle, 1L << vdi_bpp);
            form_dial(FMD_FINISH, 0, 0, 0, 0, 0, 0, vdi_width, vdi_height);
        } else if (page[0].screen_info.old_rez != -1) {
            Setscreen(SCR_NOCHANGE, old_physbase, page[0].screen_info.old_rez);
        } else if (page[0].screen_info.old_mode != -1) {
            // make VDI (and SuperVidel registers) happy
            //VsetScreen(SCR_NOCHANGE, old_physbase, SCR_NOCHANGE, page[0].old_falcon_mode);
            VsetScreen(SCR_NOCHANGE, old_physbase, SCR_NOCHANGE, SCR_NOCHANGE);
            VsetMode(page[0].screen_info.old_mode);
        }

        if (app_id != -1)
            wind_update(END_UPDATE);
    }

    //////////////////////////////////////////////////////////////////////////

    if (!vdi_pages) {
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
    }

exit_gem:
    if (vdi_handle != 0)
        v_clsvwk(vdi_handle);

    appl_exit();

    if (error[0])
        print_error(error);

    return EXIT_SUCCESS;
}
