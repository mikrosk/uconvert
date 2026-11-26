/*
 * ushow: Atari ST/STE/TT/Falcon-specific bitmap viewer
 *
 * Copyright (c) 2022 Miro Kropacek <miro.kropacek@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef LINEPAL_H
#define LINEPAL_H

#include <stdint.h>

// Displays a bitmap with a multiline palette until a key is pressed (supervisor
// mode, after the mode, the screen and the full palette are set). records: h + 1
// records (the last one read, not used), word aligned, of 'writes' colours and a
// word with the low half of the palette register address of the first; y0, h:
// the image lines in screen lines. Returns the key's scancode, -1 if the display
// could not synchronise to the frame.
typedef int (*LinepalRoutine)(const void* records, const uint32_t* palette, int entries, int y0, int h);

// the routine for a VsetMode() mode and 8 or 10 writes per line, NULL if there is none
LinepalRoutine linepal_routine(int16_t mode, int writes);

#endif
