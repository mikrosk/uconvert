#ifndef PALETTE_H
#define PALETTE_H

#include <cstdint>

struct FalconPaletteEntry {
    unsigned char r;
    unsigned char g;
    unsigned char unused;
    unsigned char b;
} __attribute__((packed));

#endif // PALETTE_H
