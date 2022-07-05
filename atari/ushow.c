#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <mint/falcon.h>
#include <mint/osbind.h>

#include "screen-asm.h"

// TODO: libcmini
// TODO: c2p (BPLSIZE in the Kalms routines should be set to xres*yres/8)

typedef struct {
    char	id[5];
    uint16_t	version;
    uint16_t	flags;
    uint8_t	bitsPerPixel;
    uint8_t	pixelsPerChunk;
    uint8_t	bytesPerChunk;
} __attribute__((packed)) FileHeader;

typedef enum {
    PaletteTypeNone, 
    PaletteTypeSte,
    PaletteTypeFalcon
} PaletteType;

static void print_help()
{
    fprintf(stderr, "Usage: ushow.ttp <filename.ext>\n");
    fprintf(stderr, "Press enter to exit.\n");
    getchar();
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        print_help();
    }

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
        palette_type = PaletteTypeSte;
    else if ((file_header.flags & 0b11) == 0b10)
        palette_type = PaletteTypeFalcon;
    
    //bool chunky_pixels = !!(file_header.flags & 0b100);
    
    uint16_t width = 0;
    uint16_t height = 0;
    if (file_header.bitsPerPixel > 0) {
        fread(&width, sizeof(width), 1, f);
        fread(&height, sizeof(height), 1, f);
    }
    
    uint16_t ste_palette[16];
    uint32_t falcon_palette[256];
    if (palette_type == PaletteTypeSte) {
        fread(ste_palette, sizeof(ste_palette[0]), 16, f);
    } else if (palette_type == PaletteTypeFalcon) {
        fread(falcon_palette, sizeof(falcon_palette[0]), 256, f);
    }    

    char* bitmap = NULL;
    char* bitmap_aligned = NULL;
    if (file_header.bitsPerPixel > 0) {
        bitmap = (char*)Mxalloc(width * height + 15, MX_STRAM);
        if (!bitmap) {
            fprintf(stderr, "Not enough ST-RAM\n");
            getchar();
            return EXIT_FAILURE;
        }

        bitmap_aligned = (char*)(((uintptr_t)bitmap + 15) & 0xfffffff0);
        fread(bitmap_aligned, sizeof(*bitmap_aligned), width * height, f);
    }

    fclose(f);

    int32_t ssp = Super(0L);

    asm_screen_save();

    VsetMode(0x23); // 320x240x256c@60 Hz

    asm_screen_set_vram(bitmap_aligned);
    asm_screen_set_falcon_palette(falcon_palette);

    getchar();

    asm_screen_restore();

    Super(ssp);

    Mfree(bitmap);

    return EXIT_SUCCESS;
}
