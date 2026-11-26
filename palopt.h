/*
 * uconvert: bitmap converter into Atari ST/STE/TT/Falcon-specific format
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

#ifndef PALOPT_H
#define PALOPT_H

#include <array>
#include <cstdint>
#include <vector>

// a colour as palette levels, each 0 .. (1 << levelBits) - 1
using Levels = std::array<uint8_t, 3>;

struct PaletteParams {
    int width;
    int height;
    int entries;        // palette entries (1 << bpp)
    int perLine;        // entries rewritten before every line (a block of consecutive ones), 0 if none
    int levelBits;      // bits per colour channel of a palette entry
    bool dither;        // optimise the picture as the eye sees it instead of every pixel on its own
};

struct PaletteResult {
    std::vector<uint8_t>  indices;      // height * width palette indices
    std::vector<Levels>   palette;      // entries: the palette of line 0
    std::vector<uint16_t> lineFirst;    // height: the first entry of the block rewritten before each line
    std::vector<Levels>   lineColors;   // height * perLine: the colours written there
};

// rgb: height * width * 3 gamma coded R, G, B, each 0 .. 255
PaletteResult optimise_palette(const PaletteParams& params, const std::vector<float>& rgb);

#endif // PALOPT_H
