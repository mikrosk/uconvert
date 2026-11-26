/* linepal.h: per-line palette blocks on Falcon VGA screens.
 *
 * A routine shows the current screen while it writes, before every image
 * line, a block of `writes` (8 or 10) consecutive palette entries:
 *   records  h+1 records, contiguous, word aligned; record i (image line
 *            i) = `writes` colour longs (Falcon $RRGG00BB) followed by one
 *            16-bit word holding 0x9800 + 4 * first (low word of the
 *            register of the block's first entry): 4*writes+2 bytes per
 *            record. Record h is read but not used.
 *   palette  the full palette (`entries` = 16 or 256 longs); written at the
 *            top of every frame (vertical blank) and on return.
 *   y0, h    first image line and number of image lines in screen lines
 *            (0..239 on the line-doubled 240-line modes); record i is
 *            written before screen line y0+i. h >= 1, y0+h <= screen lines.
 * Returns the IKBD make code of the first key pressed, -1 when the frame
 * sync failed in 8 frames in a row, -2 when the test frame limit ran out
 * (test builds only).
 * Call in supervisor mode after VsetMode/VsetScreen and the full palette.
 * While it runs, interrupts are off (MFP disabled, interrupt mask 7) and
 * IKBD mouse reporting is off; everything is restored before it returns.
 *
 * linepal_routine(): routine for a VsetMode mode and `writes`, or NULL.
 * Supported (measured on the Falcon): VGA modes (bit 4 set; PAL, overscan
 * and ST-compatible bits clear), 4 or 8 bpp, 320 or 640 columns, with or
 * without VERTFLAG (240 lines, line-doubled); writes = 10 is refused for
 * 640x480 at 8 bpp (measured margin too small: 2 to 162 wrong rows in 30
 * frames depending on the run).
 */
#ifndef LINEPAL_H
#define LINEPAL_H

#include <stdint.h>

typedef int (*LinepalRoutine)(const void* records, const uint32_t* palette, int entries, int y0, int h);
LinepalRoutine linepal_routine(int16_t mode, int writes);   /* NULL if unsupported */

#ifdef LINEPAL_TEST
/* test builds (linepal routines assembled from deliver/test/, never commit):
 * frames until a routine restores everything and returns -2 (0: no limit);
 * frames dropped because a wait expired; frames run */
extern int linepal_test_frames;
extern long linepal_test_misses;
extern long linepal_test_done;
/* dry lines that missed, entry waits that expired, frames whose block
 * started after its first target; VFC, HHC (low words) and d3 at the last
 * miss */
extern long linepal_test_diag[6];
#endif

#endif
