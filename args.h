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

#ifndef ARGS_H
#define ARGS_H

#include <cstdint>
#include <string>
#include <optional>

extern std::optional<int16_t>  bitmapWidth;           // -1 (if original width) or any number
extern std::optional<int16_t>  bitmapHeight;          // -1 (if original height) or any number
extern std::optional<bool>     convert;               // if true, convert to desired bit depth

extern std::optional<int16_t>   bitsPerPixel;         // 1, 2, 4, 8 (both planar and chunky); 15, 16, 24, 32 (chunky only) or 0 (if explicitly disabled)
extern std::optional<int16_t>   bytesPerChunk;        // -1 (if implicit/packed), 1, 2, 3, 4 or 0 (if disabled)
extern std::optional<int16_t>   paletteBits;          // 9, 12, 18, 24 or 0 (if bitsPerPixel > 8 or explicitly disabled)
extern std::optional<bool>      stCompatiblePalette;  // if true, use the ST/E palette registers
extern std::optional<bool>      ttCompatiblePalette;  // if true, use the TT palette registers

extern std::string parse_arguments(int argc, char* argv[]);

#endif // ARGS_H
