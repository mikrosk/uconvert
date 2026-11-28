/* linepal.c: routine selector for the linepal routines (linepal.h) */
#include <stddef.h>
#include <mint/falcon.h>
#include "linepal.h"

int linepal_l8_1x(const void*, const uint32_t*, int, int, int);
int linepal_l8_2x(const void*, const uint32_t*, int, int, int);
int linepal_l8_rgb320(const void*, const uint32_t*, int, int, int);
int linepal_l8_rgb640(const void*, const uint32_t*, int, int, int);


LinepalRoutine linepal_routine(int16_t mode, int writes)
{
	int bpp = mode & 7;		/* 2: 4 bpp, 3: 8 bpp */
	int dbl = (mode & 0x100) != 0;	/* VERTFLAG: VGA line-doubled, RGB/TV interlaced */
	int mon = VgetMonitor();

	if (bpp != 2 && bpp != 3)
		return NULL;
	if (writes != 8)
		return NULL;
	if (mode & 0x10) {		/* VGA */
		if ((mode & 0xf0) != 0x10 || mon != MON_VGA)	/* PAL, overscan, ST-compatible clear */
			return NULL;
		return dbl ? linepal_l8_2x : linepal_l8_1x;
	}
	/* RGB/TV: not interlaced, not ST-compatible, overscan only with PAL */
	if (dbl || (mode & 0x80) || (mon != MON_COLOR && mon != MON_TV))
		return NULL;
	if ((mode & 0x60) == 0x40)	/* NTSC overscan: not measured */
		return NULL;
	return (mode & 8) ? linepal_l8_rgb640 : linepal_l8_rgb320;
}
