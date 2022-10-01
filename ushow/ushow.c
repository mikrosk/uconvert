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

#include "c2p-asm.h"
#include "screen-asm.h"

typedef struct {
    char        id[5];
    uint16_t    version;
    uint16_t    flags;
    int8_t      bitsPerPixel;
    int8_t      bytesPerChunk;
} __attribute__((packed)) FileHeader;

typedef enum {
    PaletteTypeNone,
    PaletteTypeSTE,
    PaletteTypeTT,
    PaletteTypeFalcon
} PaletteType;

typedef enum {
    VdoValueST     = 0x0000,
    VdoValueSTE    = 0x0001,
    VdoValueTT     = 0x0002,
    VdoValueFalcon = 0x0003
} VdoValue;

typedef enum {
    RezValueSTLow  = 0,    // 320x200@4bpp, ST palette
    RezValueSTMid  = 1,    // 640x200@2bpp, ST palette
    RezValueSTHigh = 2,    // 640x400@1bpp, ST palette
    RezValueTTLow  = 7,    // 320x480@8bpp, TT palette
    RezValueTTMid  = 4,    // 640x480@4bpp, TT palette
    RezValueTTHigh = 6     // 1280x960@1bpp, TT palette
} SteTtRezValue;

static FileHeader file_header;
static uint16_t width;
static uint16_t height;

static size_t screen_width  = 0;
static size_t screen_height = 0;
static size_t screen_bpp    = 0;

static void print_help()
{
    fprintf(stderr, "Usage: ushow.ttp [-c2p] <filename.ext>\r\n");
    fprintf(stderr, "Press enter to exit.\r\n");
    getchar();
    exit(EXIT_FAILURE);
}

static void load_bitmap(char* buffer, FILE* f, bool c2p)
{
    char* c2p_buffer = NULL;

    size_t screen_x_offset = 0;
    size_t final_width = screen_width;
    size_t seek_offset = 0;
    if (width > screen_width) {
        seek_offset = width - screen_width;
        if (!c2p) {
            seek_offset *= screen_bpp;
            seek_offset /= 8;
        } else {
            seek_offset *= file_header.bytesPerChunk;
        }
    } else {
        screen_x_offset = (screen_width - width) / 2; // left and right border
        final_width = width;
    }

    size_t screen_y_offset = 0;
    size_t final_height = screen_height;
    if (height < screen_height) {
        screen_y_offset = (screen_height - height) / 2; // top and bottom border
        final_height = height;
    }

    if (c2p) {
        c2p_buffer = (char*)malloc(final_width * file_header.bytesPerChunk);
        if (!c2p_buffer) {
            fprintf(stderr, "Not enough RAM for C2P.\r\n");
            getchar();
            exit(EXIT_FAILURE);
        }
    }

    bool file_error = false;
    char* p = buffer + screen_y_offset * (screen_width * screen_bpp / 8);
    for (size_t i = 0; i < final_height; ++i) {
        p += screen_x_offset * screen_bpp / 8;

        if (!c2p) {
            if (fread(p, sizeof(*p), final_width * screen_bpp / 8, f) != final_width * screen_bpp / 8) {
                file_error = true;
                break;
            }
        } else {
            if (fread(c2p_buffer, sizeof(*c2p_buffer), final_width * file_header.bytesPerChunk, f) != final_width * file_header.bytesPerChunk) {
                file_error = true;
                break;
            }

            if (file_header.bitsPerPixel == 4)
                c2p1x1_4_falcon(c2p_buffer, c2p_buffer + (final_width / 8) * 8, p); // original source code had *4 there, hm...
            else if (file_header.bitsPerPixel == 8)
                c2p1x1_8_falcon(c2p_buffer, c2p_buffer + (final_width / 8) * 8, p);
        }

        p += final_width * screen_bpp / 8;
        p += screen_x_offset * screen_bpp / 8;

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
}

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 3) {
        print_help();
    }

    bool c2p = false;
    if (argc == 3) {
        if (strcmp(argv[1], "-c2p") == 0)
            c2p = true;
        else
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

    FILE* f = fopen(argv[argc-1], "rb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s'.\r\n", argv[argc-1]);
        getchar();
        return EXIT_FAILURE;
    }

    if (fread(&file_header, sizeof(file_header), 1, f) != 1) {
        fprintf(stderr, "Read error.\r\n");
        getchar();
        return EXIT_FAILURE;
    }

    if (strcmp(file_header.id, "UIMG") != 0) {
        fprintf(stderr, "Invalid header: '%s'.\r\n", file_header.id);
        getchar();
        return EXIT_FAILURE;
    }

    PaletteType palette_type = PaletteTypeNone;
    if ((file_header.flags & 0b11) == 0b01)
        palette_type = PaletteTypeSTE;
    else if ((file_header.flags & 0b11) == 0b10)
        palette_type = PaletteTypeTT;
    else if ((file_header.flags & 0b11) == 0b11)
        palette_type = PaletteTypeFalcon;

    if (file_header.bitsPerPixel > 0) {
        fread(&width, sizeof(width), 1, f);
        fread(&height, sizeof(height), 1, f);
    }

    if (width == 0 || height == 0) {
        fprintf(stdout, "No bitmap data - nothing to show.\r\n");
        getchar();
        return EXIT_SUCCESS;
    }

    if (palette_type == PaletteTypeTT && vdo_val != VdoValueTT) {
        fprintf(stdout, "TT palette can be set only on TT.\r\n");
        getchar();
        return EXIT_SUCCESS;
    }

    if (palette_type == PaletteTypeFalcon && vdo_val != VdoValueFalcon) {
        fprintf(stdout, "Falcon palette can be set only on Falcon.\r\n");
        getchar();
        return EXIT_SUCCESS;
    }

    if (c2p && (file_header.bytesPerChunk != 1 || (file_header.bitsPerPixel != 4 && file_header.bitsPerPixel != 8))) {
        fprintf(stdout, "Unsupported C2P configuration (bpp: %d, bpc: %d).\r\n", file_header.bitsPerPixel, file_header.bytesPerChunk);
        getchar();
        return EXIT_SUCCESS;
    }

    uint16_t ste_palette[16];
    uint16_t tt_palette[256];
    uint32_t falcon_palette[256];
    if (palette_type == PaletteTypeSTE) {
        fread(ste_palette, sizeof(ste_palette[0]), 1 << file_header.bitsPerPixel, f);
    } else if (palette_type == PaletteTypeTT) {
        fread(tt_palette, sizeof(tt_palette[0]), 1 << file_header.bitsPerPixel, f);
    } else if (palette_type == PaletteTypeFalcon) {
        fread(falcon_palette, sizeof(falcon_palette[0]), 1 << file_header.bitsPerPixel, f);
    }

    int16_t old_ste_tt_rez  = -1;
    int16_t old_falcon_mode = -1;

    int8_t ste_tt_rez       = -1;
    int16_t falcon_mode     = -1;

    if (vdo_val == VdoValueST || vdo_val == VdoValueSTE) {
        old_ste_tt_rez = Getrez();
        bool mono_monitor = old_ste_tt_rez == 2;

        switch (file_header.bitsPerPixel) {
        case 1:
            if (mono_monitor && file_header.bytesPerChunk <= 0) {
                // packed chunks (-1) = planar (0) if 1bpp
                screen_width  = 640;
                screen_height = 400;
                screen_bpp    = 1;
                ste_tt_rez    = RezValueSTHigh;
            }
            break;
        case 2:
            if (!mono_monitor && file_header.bytesPerChunk == 0) {
                // planar only
                screen_width  = 640;
                screen_height = 200;
                screen_bpp    = 2;
                ste_tt_rez    = RezValueSTMid;
            }
            break;
        case 4:
            if (!mono_monitor && (file_header.bytesPerChunk == 0 || (c2p && file_header.bytesPerChunk == 1))) {
                screen_width  = 320;
                screen_height = 200;
                screen_bpp    = 4;
                ste_tt_rez    = RezValueSTLow;
            }
            break;
        }
    } else if (vdo_val == VdoValueTT) {
        old_ste_tt_rez = Getrez();
        bool mono_monitor = old_ste_tt_rez == RezValueTTHigh;

        switch (file_header.bitsPerPixel) {
        case 1:
            if (file_header.bytesPerChunk <= 0) {
                // packed chunks (-1) = planar (0) if 1bpp
                screen_width  = mono_monitor ? 1280 : 640;
                screen_height = mono_monitor ? 960  : 400;
                screen_bpp    = 1;  // monochrome
                ste_tt_rez    = mono_monitor ? RezValueTTHigh : RezValueSTHigh;
            }
            break;
        case 2:
            if (!mono_monitor && file_header.bytesPerChunk == 0) {
                // planar only
                screen_width  = 640;
                screen_height = 200;
                screen_bpp    = 2;
                ste_tt_rez    = RezValueSTMid;
            }
            break;
        case 4:
            if (!mono_monitor && (file_header.bytesPerChunk == 0 || (c2p && file_header.bytesPerChunk == 1))) {
                screen_width  = palette_type == PaletteTypeSTE ? 320 : 640;
                screen_height = palette_type == PaletteTypeSTE ? 200 : 480;
                screen_bpp    = 4;
                ste_tt_rez    = palette_type == PaletteTypeSTE ? RezValueSTLow : RezValueTTMid;
            }
            break;
        case 8:
            if (!mono_monitor && (file_header.bytesPerChunk == 0 || (c2p && file_header.bytesPerChunk == 1))) {
                screen_width  = 320;
                screen_height = 480;
                screen_bpp    = 8;
                ste_tt_rez    = RezValueTTLow;
            }
            break;
        }
    } else if (vdo_val == VdoValueFalcon) {
        old_falcon_mode = VsetMode(VM_INQUIRE);
        bool supervidel = Getcookie(C_SupV, NULL) == C_FOUND && VgetMonitor() == MON_VGA;
        bool mono_monitor = VgetMonitor() == MON_MONO;

        switch (file_header.bitsPerPixel) {
        case 1:
            if (file_header.bytesPerChunk <= 0) {
                // packed chunks (-1) = planar (0) if 1bpp
                screen_width  = mono_monitor || palette_type == PaletteTypeSTE ? 640 : 320;
                screen_height = mono_monitor || palette_type == PaletteTypeSTE ? 400 : (VgetMonitor() != MON_VGA ? 200 : 240);
                screen_bpp    = 1;  // duochrome
                falcon_mode   = BPS1;
            }
            break;
        case 2:
            if (!mono_monitor && file_header.bytesPerChunk == 0) {
                // planar only
                screen_width  = palette_type == PaletteTypeSTE ? 640 : 320;
                screen_height = palette_type == PaletteTypeSTE || VgetMonitor() != MON_VGA ? 200 : 240;
                screen_bpp    = 2;
                falcon_mode   = BPS2;
            }
            break;
        case 4:
            if (!mono_monitor && (file_header.bytesPerChunk == 0 || (c2p && file_header.bytesPerChunk == 1))) {
                screen_width  = 320;
                screen_height = palette_type == PaletteTypeSTE || VgetMonitor() != MON_VGA ? 200 : 240;
                screen_bpp    = 4;
                falcon_mode   = BPS4;
            }
            break;
        case 8:
            if (!mono_monitor && (file_header.bytesPerChunk == 0 || ((c2p || supervidel) && file_header.bytesPerChunk == 1))) {
                screen_width  = 320;
                screen_height = VgetMonitor() != MON_VGA ? 200 : 240;
                screen_bpp    = 8;
                falcon_mode   = !c2p && file_header.bytesPerChunk == 1 && supervidel ? BPS8C : BPS8;
            }
            break;
        case 16:
            if (!mono_monitor && file_header.bytesPerChunk == 2) {
                screen_width  = 320;
                screen_height = VgetMonitor() != MON_VGA ? 200 : 240;
                screen_bpp    = 16;
                falcon_mode   = BPS16;
            }
            break;
        case 32:
            if (supervidel && file_header.bytesPerChunk == 4) {
                screen_width  = 320;
                screen_height = 240;
                screen_bpp    = 32;
                falcon_mode   = SVEXT | BPS32;
            }
            break;
        }

        if (falcon_mode != -1) {
            // TODO: if bitmap doesn't fit default screen size, try to enlarge it (COL80, VERTFLAG, SuperVidel)

            if (screen_width == 640)
                falcon_mode |= COL80;
            else if (screen_width == 320)
                falcon_mode |= COL40;

            falcon_mode |= (old_falcon_mode & VGA);
            falcon_mode |= (old_falcon_mode & PAL);

            if (palette_type == PaletteTypeSTE || palette_type == PaletteTypeTT)
                falcon_mode |= STMODES;

            if (((falcon_mode & VGA) && (screen_height == 200 || screen_height == 240))
                 || (!(falcon_mode & VGA) && (screen_height == 400 || screen_height == 480)))
                falcon_mode |= VERTFLAG;
        }
    }

    if (ste_tt_rez == -1 && falcon_mode == -1) {
        fprintf(stderr, "Unable to display: %dx%d@%dbpp (%s).\r\n",
                width, height, file_header.bitsPerPixel,
                file_header.bytesPerChunk > 0 ? "chunky" : "planar");

        char* pal_str = "Unknown (?)";
        switch (palette_type) {
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

    char* screen = (char*)Mxalloc((screen_width * screen_height * screen_bpp / 8) + 15, MX_STRAM);
    if (!screen) {
        fprintf(stderr, "Not enough ST-RAM.\r\n");
        getchar();
        return EXIT_FAILURE;
    }

    char* screen_aligned = (char*)(((uintptr_t)screen + 15) & 0xfffffff0);
    memset(screen_aligned, 0, screen_width * screen_height * screen_bpp / 8);

    load_bitmap(screen_aligned, f, c2p);
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

    if (ste_tt_rez != -1) {
        Setscreen(SCR_NOCHANGE, screen_aligned, ste_tt_rez);
    } else if (falcon_mode != -1) {
        // VsetScreen(SCR_NOCHANGE, screen_aligned, SCR_MODECODE, falcon_mode);
        // doesn't work as expected -- it not only reinitialises VT52 (useless for us)
        // but also expects *both* logbase and physbase be of an equal size, nicely
        // crashing if logbase is smaller. So don't bother, just use two separate calls.
        VsetMode(falcon_mode);
        VsetScreen(SCR_NOCHANGE, screen_aligned, SCR_NOCHANGE, SCR_NOCHANGE);
    }

    if (palette_type != PaletteTypeNone) {
        // Vsync() is needed for catching up with (V)setScreen()
        // otherwise the new mode will reset the newly set palette
        Vsync();

        int32_t ssp = Super(0L);

        // Don't use Setpalette() / EsetPalette() / VsetRGB() here as we want to work
        // we the native palette format for given machine
        if (palette_type == PaletteTypeSTE)
            asm_screen_set_ste_palette(ste_palette, 1 << file_header.bitsPerPixel);
        else if (palette_type == PaletteTypeTT)
            asm_screen_set_tt_palette(tt_palette, 1 << file_header.bitsPerPixel);
        else if (palette_type == PaletteTypeFalcon) {
            asm_screen_set_falcon_palette(falcon_palette, 1 << file_header.bitsPerPixel);
        }

        Super(ssp);
    }

    getchar();

    if (old_ste_tt_rez != -1) {
        Setscreen(SCR_NOCHANGE, old_physbase, old_ste_tt_rez);
    } else if (old_falcon_mode != -1) {
        // make VDI (and SuperVidel registers) happy
        //VsetScreen(SCR_NOCHANGE, old_physbase, 3, old_falcon_mode);
        VsetMode(old_falcon_mode);
        VsetScreen(SCR_NOCHANGE, old_physbase, SCR_NOCHANGE, SCR_NOCHANGE);
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

    Mfree(screen);

    return EXIT_SUCCESS;
}
