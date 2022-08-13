#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <mint/cookie.h>
#include <mint/falcon.h>
#include <mint/osbind.h>

#include "screen-asm.h"

// TODO: libcmini
// TODO: c2p (BPLSIZE in the Kalms routines should be set to xres*yres/8)

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
    PaletteTypeTT,  // TODO
    PaletteTypeFalcon
} PaletteType;

typedef enum {
    VdoValueST     = 0x0000,
    VdoValueSTE    = 0x0001,
    VdoValueTT     = 0x0002,
    VdoValueFalcon = 0x0003
} VdoValue;

static void print_help()
{
    fprintf(stderr, "Usage: ushow.ttp [-c2p] <filename.ext>\n");
    fprintf(stderr, "Press enter to exit.\n");
    getchar();
    exit(EXIT_FAILURE);
}

static uint8_t current_st_shifter;
static void get_current_st_shifter()
{
    current_st_shifter = *((volatile uint8_t*)0xffff8260);
}
static void set_current_st_shifter()
{
    *((volatile uint8_t*)0xffff8260) = current_st_shifter;
}

static uint16_t current_tt_shifter;
static void get_current_tt_shifter()
{
    current_tt_shifter = *((volatile uint16_t*)0xffff8262);
}
static void set_current_tt_shifter()
{
    *((volatile uint16_t*)0xffff8262) = current_tt_shifter;
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
        fprintf(stderr, "Not an Atari compatible video.\n");
        getchar();
        return EXIT_FAILURE;
    }

    bool supervidel = Getcookie(C_SupV, NULL) == C_FOUND && VgetMonitor() == MON_VGA;

    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s'\n", argv[1]);
        getchar();
        return EXIT_FAILURE;
    }

    FileHeader file_header;
    if (fread(&file_header, sizeof(file_header), 1, f) != 1) {
        fprintf(stderr, "Read error\n");
        getchar();
        return EXIT_FAILURE;
    }
    
    if (strcmp(file_header.id, "UIMG") != 0) {
        fprintf(stderr, "Invalid header: %s\n", file_header.id);
        getchar();
        return EXIT_FAILURE;
    }
    
    PaletteType palette_type = PaletteTypeNone;
    if ((file_header.flags & 0b11) == 0b01)
        palette_type = PaletteTypeSTE;
    else if ((file_header.flags & 0b11) == 0b11)
        palette_type = PaletteTypeFalcon;
    
    uint16_t width = 0;
    uint16_t height = 0;
    if (file_header.bitsPerPixel > 0) {
        fread(&width, sizeof(width), 1, f);
        fread(&height, sizeof(height), 1, f);
    }

    if (palette_type == PaletteTypeNone && (width == 0 || height == 0)) {
        fprintf(stdout, "No palette and no bitmap data - nothing to do.\n");
        getchar();
        return EXIT_SUCCESS;
    }

    if (palette_type == PaletteTypeFalcon && vdo_val != VdoValueFalcon) {
        fprintf(stdout, "Falcon palette can be set only on Falcon.\n");
        getchar();
        return EXIT_SUCCESS;
    }

    uint16_t ste_palette[16];
    uint32_t falcon_palette[256];
    if (palette_type == PaletteTypeSTE) {
        fread(ste_palette, sizeof(ste_palette[0]), 16, f);
    } else if (palette_type == PaletteTypeFalcon) {
        fread(falcon_palette, sizeof(falcon_palette[0]), 256, f);
    }

    size_t screen_width  = 0;
    size_t screen_height = 0;
    size_t screen_bpp    = 0;
    int8_t st_shifter    = -1;
    int16_t tt_shifter   = -1;
    int16_t falcon_mode  = -1;

    if (vdo_val == VdoValueST || vdo_val == VdoValueSTE) {
        Supexec(get_current_st_shifter);
        bool mono_monitor = (current_st_shifter & 0b11) == 0b10;

        switch (file_header.bitsPerPixel) {
        case 1:
            if (mono_monitor && file_header.bytesPerChunk <= 0) {
                // packed chunks (-1) = planar if 1bpp (0)
                screen_width  = 640;
                screen_height = 400;
                screen_bpp    = 1;
                st_shifter    = 0b10;
            }
            break;
        case 2:
            if (!mono_monitor && file_header.bytesPerChunk == 0) {
                // planar only
                screen_width  = 640;
                screen_height = 200;
                screen_bpp    = 2;
                st_shifter    = 0b01;
            }
            break;
        case 4:
            if (!mono_monitor && (file_header.bytesPerChunk == 0 || (c2p && file_header.bytesPerChunk == 1))) {
                screen_width  = 320;
                screen_height = 200;
                screen_bpp    = 4;
                st_shifter    = 0b00;
            }
            break;
        }
    } else if (vdo_val == VdoValueTT) {
        Supexec(get_current_tt_shifter);
        bool mono_monitor = (current_tt_shifter & 0b0000011100000000) == 0b0000011000000000;

        switch (file_header.bitsPerPixel) {
        case 1:
            if (file_header.bytesPerChunk <= 0) {
                // packed chunks (-1) = planar if 1bpp (0)
                screen_width  = mono_monitor ? 1280 : 640;
                screen_height = mono_monitor ? 960  : 400;
                screen_bpp    = 1;  // duochrome
                tt_shifter    = mono_monitor ? 0b0000011000000000 : 0b0000001000000000;
            }
            break;
        case 2:
            if (!mono_monitor && file_header.bytesPerChunk == 0) {
                // planar only
                screen_width  = 640;
                screen_height = 200;
                screen_bpp    = 2;
                tt_shifter    = 0b0000000100000000;
            }
            break;
        case 4:
            if (!mono_monitor && (file_header.bytesPerChunk == 0 || (c2p && file_header.bytesPerChunk == 1))) {
                screen_width  = 320;
                screen_height = 200;
                screen_bpp    = 4;
                tt_shifter    = 0b0000000000000000;
            }
            break;
        case 8:
            if (!mono_monitor && (file_header.bytesPerChunk == 0 || (c2p && file_header.bytesPerChunk == 1))) {
                screen_width  = 320;
                screen_height = 480;
                screen_bpp    = 8;
                tt_shifter    = 0b0000011100000000;
            }
            break;
        }
    } else if (vdo_val == VdoValueFalcon) {
        bool mono_monitor = VgetMonitor() == MON_MONO;

        switch (file_header.bitsPerPixel) {
        case 1:
            if (file_header.bytesPerChunk <= 0) {
                // packed chunks (-1) = planar if 1bpp (0)
                screen_width  = mono_monitor ? 640 : 320;
                screen_height = mono_monitor ? 400 : (palette_type == PaletteTypeSte || VgetMonitor() != MON_VGA ? 200 : 240);
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
                falcon_mode   = supervidel ? BPS8C : BPS8;
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
            uint16_t old_mode = VsetMode(VM_INQUIRE);

            if (screen_width == 640)
                falcon_mode |= COL80;

            falcon_mode |= (old_mode & VGA);
            falcon_mode |= (old_mode & PAL);

            if (palette_type == PaletteTypeSTE)
                falcon_mode |= STMODES;

            if (((falcon_mode & VGA) && (screen_height == 200 || screen_height == 240))
                 || (!(falcon_mode & ~VGA) && (screen_height == 400 || screen_height == 480)))
                falcon_mode |= VERTFLAG;
        }
    }

    if (st_shifter == -1 && tt_shifter == -1 && falcon_mode == -1) {
        if (palette_type != PaletteTypeNone) {
            fprintf(stderr, "Unable to set %s palette\n", palette_type == PaletteTypeSTE ? "ST/E" : "Falcon");
        }

        if (width != 0 && height != 0) {
            fprintf(stderr, "Unable to display: %dx%d@%dbpp (%s)\n",
                    width, height, file_header.bitsPerPixel,
                    file_header.bytesPerChunk > 0 ? "chunky" : "planar");
        }

        getchar();
        return EXIT_FAILURE;
    }

    // TODO: c2p ... mozno robit po riadkoch s malym bufferom
    char* screen = NULL;
    char* screen_aligned = NULL;
    if (width > 0 && height > 0) {
        // allocate only if there is bitmap data
        screen = (char*)Mxalloc((screen_width * screen_height * screen_bpp / 8) + 15, MX_STRAM);
        if (!screen) {
            fprintf(stderr, "Not enough ST-RAM\n");
            getchar();
            return EXIT_FAILURE;
        }

        screen_aligned = (char*)(((uintptr_t)screen + 15) & 0xfffffff0);
        memset(screen_aligned, 0, screen_width * screen_height * screen_bpp / 8);

        size_t screen_x_offset = 0;
        size_t final_width = screen_width;
        size_t seek_offset = 0;
        if (width > screen_width) {
            seek_offset = width - screen_width;
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

        //printf("fw: %d, fh: %d, bpp: %d, ox: %d, oy: %d, seek: %d, w: %d, h: %d\n",
        //       final_width, final_height, screen_bpp, screen_x_offset, screen_y_offset, seek_offset, width, height);
        //getchar();

        bool file_error = false;
        char* p = screen_aligned + screen_y_offset * (screen_width * screen_bpp / 8);
        for (size_t i = 0; i < final_height; ++i) {
            p += screen_x_offset * screen_bpp / 8;

            if (fread(p, sizeof(*p), final_width * screen_bpp / 8, f) != final_width * screen_bpp / 8) {
                file_error = true;
                break;
            }

            p += final_width * screen_bpp / 8;
            p += screen_x_offset * screen_bpp / 8;

            if (fseek(f, seek_offset * screen_bpp / 8, SEEK_CUR) != 0) {
                file_error = true;
                break;
            }
        }

        if (file_error) {
            fprintf(stderr, "I/O error.\n");
            getchar();
            return EXIT_FAILURE;
        }
    }

    fclose(f);

    int32_t ssp = Super(0L);

    asm_screen_save();	// TODO: does the save works on ST/E, too?

    int16_t old_vsetmode = -1;
    if (screen_aligned != NULL) {
        if (st_shifter != -1) {
            current_st_shifter = st_shifter;
            set_current_st_shifter();
        } else if (tt_shifter != -1) {
            current_tt_shifter = tt_shifter;
            set_current_tt_shifter();
        } else if (falcon_mode != -1) {
            old_vsetmode = VsetMode(falcon_mode);
        }

        asm_screen_set_vram(screen_aligned);
    }

    if (palette_type == PaletteTypeSTE)
        asm_screen_set_ste_palette(ste_palette);
    else if (palette_type == PaletteTypeFalcon)
        asm_screen_set_falcon_palette(falcon_palette);

    getchar();

    // make the OS happy
    if (old_vsetmode != -1)
        VsetMode(old_vsetmode);

    asm_screen_restore();

    Super(ssp);

    Mfree(screen);

    return EXIT_SUCCESS;
}
