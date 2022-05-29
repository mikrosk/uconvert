#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <mint/falcon.h>
#include <mint/osbind.h>

#include "screen-asm.h"

// TODO: libcmini, flags na chunky, st/e/falcon paletu

struct FalconPaletteEntry {
    unsigned char r;
    unsigned char g;
    unsigned char unused;
    unsigned char b;
} __attribute__((packed));

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

    char id[4+1] = {};
    if (fread(id, 1, 4, f) != 4) {
        fprintf(stderr, "Read error\n");
        getchar();
        return EXIT_FAILURE;
    }

    int planes = -1;
    if (strcmp(id, "1BPL") == 0)
        planes = 1;
    else if (strcmp(id, "2BPL") == 0)
        planes = 2;
    else if (strcmp(id, "4BPL") == 0)
        planes = 4;
    else if (strcmp(id, "8BPL") == 0)
        planes = 8;

    if (planes == -1) {
        fprintf(stderr, "Invalid header: %s\n", id);
        getchar();
        return EXIT_FAILURE;
    }

    uint16_t width;
    fread(&width, sizeof(width), 1, f);

    uint16_t height;
    fread(&height, sizeof(height), 1, f);

    struct FalconPaletteEntry pal[256];
    fread(pal, sizeof(pal[0]), 256, f);

    char* p = (char*)Mxalloc(width * height + 15, MX_STRAM);
    if (!p) {
        fprintf(stderr, "Not enough ST-RAM\n");
        getchar();
        return EXIT_FAILURE;
    }

    char* pAligned = (char*)(((uintptr_t)p + 15) & 0xfffffff0);
    fread(pAligned, sizeof(*pAligned), width * height, f);

    fclose(f);

    int32_t ssp = Super(0L);

    asm_screen_save();

    VsetMode(0x23); // 320x240x256c@60 Hz

    asm_screen_set_vram(pAligned);
    asm_screen_set_palette((uint32_t*)pal);

    getchar();

    asm_screen_restore();

    Super(ssp);

    Mfree(p);

    return EXIT_SUCCESS;
}
