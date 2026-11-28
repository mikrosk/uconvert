/* linepal.c: routine selector for the linepal routines (linepal.h) */
#include <stddef.h>
#include "linepal.h"

int linepal_l8_1x(const void*, const uint32_t*, int, int, int);
int linepal_l8_2x(const void*, const uint32_t*, int, int, int);

LinepalRoutine linepal_routine(int16_t mode, int writes)
{
	int bpp = mode & 7;		/* 2: 4 bpp, 3: 8 bpp */
	int dbl = (mode & 0x100) != 0;	/* VERTFLAG: 240 lines, line-doubled */

	if ((mode & 0xf0) != 0x10)	/* VGA set; PAL, overscan, ST-compatible clear */
		return NULL;
	if (bpp != 2 && bpp != 3)
		return NULL;
	if (writes == 8)
		return dbl ? linepal_l8_2x : linepal_l8_1x;
	return NULL;
}
