#ifndef LOAD_BITMAP_H
#define LOAD_BITMAP_H

#include <stdbool.h>
#include <stdio.h>

#include "bitmap_info.h"
#include "screen_info.h"

// exits on error
void* load_bitmap(FILE* f, const BitmapInfo* bitmap_info, const ScreenInfo* screen_info, bool c2p);

#endif
