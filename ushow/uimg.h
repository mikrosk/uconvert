#ifndef UIMG_H
#define UIMG_H

#include <stdint.h>

typedef struct {
    char        id[5];
    uint16_t    version;
    uint16_t    flags;
    uint8_t     bitsPerPixel;
    int8_t      bytesPerChunk;
} __attribute__((packed)) FileHeader;

#endif
