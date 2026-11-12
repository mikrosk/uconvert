/* fp8: a true-colour picture as an 8 bpp Falcon picture whose palette is
 * partly rewritten on every scanline.
 *
 * The display starts scanline 0 with a full palette of `entries` colours.
 * After every scanline a run of `per_line` consecutive entries gets new
 * colours, which are in force from the next scanline on.  The runs are
 * taken in turn: entries 0.., per_line.., 2 * per_line.., and again from
 * 0 (when per_line does not divide entries a last run ends at the last
 * entry).  Colours have 6 bits per gun. */
#ifndef FP8_H
#define FP8_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width, height;  /* pixels */
    int entries;        /* palette entries: 256 (16 for 4 bpp) */
    int per_line;       /* entries rewritten after every scanline, 1 .. entries - 1 */
    int effort;         /* 0 .. 9: annealing moves per palette colour 0, 25, 50, 100 (default 3), ... 6400 */
    int dither;         /* 0: every pixel as close as possible; 1: the picture as the eye sees it */
    uint64_t seed;      /* of the annealing, any value */
} fp8_params;

/* rgb:   height * width * 3 bytes, gamma-coded R, G, B
 * pix:   height * width entry numbers
 * pal:   entries * 3 levels (0 .. 63: R, G, B) in force on scanline 0
 * rec:   (height - 1) * per_line * 3 levels: record r holds the colours
 *        written after scanline r, in force from scanline r + 1
 * first: (height - 1) values: the first entry record r writes
 * Returns 0, or -1 for parameters out of range or no memory. */
int fp8_convert(const fp8_params *par, const uint8_t *rgb, uint8_t *pix, uint8_t *pal, uint8_t *rec,
                uint16_t *first);

#ifdef __cplusplus
}
#endif

#endif
