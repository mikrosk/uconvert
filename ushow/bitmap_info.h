#ifndef BITMAP_INFO_H
#define BITMAP_INFO_H

#include <stdint.h>
#include <stdio.h>

#include "palette_type.h"
#include "vdo.h"

typedef struct {
    uint8_t     bpp;
    int8_t      bpc;
    PaletteType palette_type;
    union {
        uint16_t ste[16];
        uint16_t tt[256];
        uint32_t falcon[256];
    } palette;
    uint16_t    width;
    uint16_t    height;
} BitmapInfo;

// exits on error
BitmapInfo load_bitmap_info(FILE* f, const VdoValue vdo_val);

#endif
