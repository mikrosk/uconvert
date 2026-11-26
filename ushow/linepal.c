/* linepal.c: routine selector for the linepal routines (linepal.h) */
#include <stddef.h>
#include "linepal.h"

int linepal_l8_1x(const void*, const uint32_t*, int, int, int);
int linepal_l8_2x(const void*, const uint32_t*, int, int, int);
int linepal_l10_320_1x(const void*, const uint32_t*, int, int, int);
int linepal_l10_320_2x(const void*, const uint32_t*, int, int, int);
int linepal_l10_640_1x(const void*, const uint32_t*, int, int, int);
int linepal_l10_640_2x(const void*, const uint32_t*, int, int, int);

#ifdef LINEPAL_TEST
int linepal_test_frames;
long linepal_test_misses;
long linepal_test_done;
long linepal_test_diag[6];
#endif

LinepalRoutine linepal_routine(int16_t mode, int writes)
{
	int bpp = mode & 7;		/* 2: 4 bpp, 3: 8 bpp */
	int dbl = (mode & 0x100) != 0;	/* VERTFLAG: 240 lines, line-doubled */
	int w640 = (mode & 0x08) != 0;	/* COL80 */

	if ((mode & 0xf0) != 0x10)	/* VGA set; PAL, overscan, ST-compatible clear */
		return NULL;
	if (bpp != 2 && bpp != 3)
		return NULL;
	if (writes == 8)
		return dbl ? linepal_l8_2x : linepal_l8_1x;
	if (writes == 10) {
		if (w640 && !dbl && bpp == 3)	/* 640x480x8: margin too small (linepal.h) */
			return NULL;
		if (w640)
			return dbl ? linepal_l10_640_2x : linepal_l10_640_1x;
		return dbl ? linepal_l10_320_2x : linepal_l10_320_1x;
	}
	return NULL;
}
