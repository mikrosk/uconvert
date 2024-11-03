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

#include "args.h"

#include <cstdlib>
#include <iomanip>
#include <ios>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "version.h"

std::optional<int16_t>  bitmapWidth;          // -1 (if original width) or any number
std::optional<int16_t>  bitmapHeight;         // -1 (if original height) or any number
std::optional<bool>     filter;               // if true, use filtering when resizing
std::optional<bool>     dither;               // if true, use dithering when resizing and/or converting colours

std::optional<int16_t>  bitsPerPixel;         // 1, 2, 4, 6, 8 (both planar and chunky); 16, 24, 32 (chunky only) or 0 (if explicitly disabled)
std::optional<int16_t>  bytesPerChunk;        // -1 (if implicit/packed), 1, 2, 3, 4 or 0 (if disabled)
std::optional<int16_t>  paletteBits;          // 9, 12, 18, 24 or 0 (if bitsPerPixel > 8 or explicitly disabled)
std::optional<bool>     stCompatiblePalette;  // if true, use ST/E palette registers
std::optional<bool>     ttCompatiblePalette;  // if true, use TT palette registers
// Possible TODOs:
//  - grayscale
//  - cat picture.gif | giftopnm | pnmquant 16 | ppmtoneo > picture.neo (call Netpbm instead of using GraphicsMagick)

// defaults
constexpr int16_t    DEFAULT_BITMAP_WIDTH = -1;
constexpr int16_t   DEFAULT_BITMAP_HEIGHT = -1;
constexpr bool            DEFAULT_FILTER  = false;
constexpr bool            DEFAULT_DITHER  = false;

constexpr int16_t  DEFAULT_ST_BITS_PER_PIXEL = 4;
constexpr int16_t    DEFAULT_BITS_PER_PIXEL  = 8;
constexpr int16_t    DEFAULT_BYTES_PER_CHUNK = 0;
constexpr int16_t DEFAULT_ST_TT_PALETTE_BITS = 12;
constexpr int16_t      DEFAULT_PALETTE_BITS  = 24;
constexpr bool        DEFAULT_ST_COMPATIBLE  = false;
constexpr bool        DEFAULT_TT_COMPATIBLE  = false;

std::unordered_map<std::string, std::pair<std::unordered_set<int16_t>, std::optional<int16_t>&>> allowedValues = {
    { "-bpp",    { { 0, 1, 2, 4, 6, 8, 16, 24, 32 }, bitsPerPixel  } },
    { "-bpc",    { { -1, 0, 1, 2, 3, 4 },            bytesPerChunk } },
    { "-pal",    { { 0, 9, 12, 18, 24 },             paletteBits   } },
    { "-width",  { { },                              bitmapWidth   } },
    { "-height", { { },                              bitmapHeight  } }
};

std::unordered_map<std::string, std::optional<bool>&> allowedFlags = {
    { "-st",     stCompatiblePalette  },
    { "-tt",     ttCompatiblePalette  },
    { "-filter", filter               },
    { "-dither", dither               },
};

static void print_help(const char* name)
{
    std::ostringstream oss;
    oss << "Usage: " << name << " [OPTION...] FILE" << std::endl
        << "Convert bitmap FILE into an Atari ST/STE/TT/Falcon-specific format." << std::endl
        << "Version " << (VERSION>>8) << "." << std::setfill('0') << std::setw(2) << (VERSION&0xFFu) << " (c) 2022 Miro Kropacek <miro.kropacek@gmail.com>." << std::endl
        << std::endl
        << "Possible options:" << std::endl
        << "  -width <num>     specify new bitmap width [default " << DEFAULT_BITMAP_WIDTH << "]" << std::endl
        << "  -height <num>    specify new bitmap height [default " << DEFAULT_BITMAP_HEIGHT << "]" << std::endl
        << "  -filter          use filtering when resizing [default " << std::boolalpha << DEFAULT_FILTER << "]" << std::endl
        << "  -dither          use dithering when resizing and/or converting colours [default " << std::boolalpha << DEFAULT_DITHER << "]" << std::endl
        << "  -bpp <num>       bits per pixel, i.e. colour depth (0, 1, 2, 4, 6, 8, 16 [RGB565], 24, 32) [default " << DEFAULT_BITS_PER_PIXEL << "]" << std::endl
        << "  -bpc <num>       bytes per chunk (-1 for packed chunky pixels [default for bpp > 8], 0, 1, 2, 3, 4) [default " << DEFAULT_BYTES_PER_CHUNK << "]" << std::endl
        << "  -pal <num>       number of bits per palette entry where applicable (0, 9, 12, 18, 24; implicitly disabled for bpp > 8) [default " << DEFAULT_PALETTE_BITS << "]" << std::endl
        << "  -st              output palette in ST/E-specific format (only 9/12-bit palette) [default " << std::boolalpha << DEFAULT_ST_COMPATIBLE << "]" << std::endl
        << "  -tt              output palette in TT-specific format (only 9/12-bit palette) [default " << std::boolalpha << DEFAULT_TT_COMPATIBLE << "]" << std::endl
        << "  -out <filename>  output bitmap as <filename> ('-bpp', '-bpc', '-pal', '-st' and '-tt' are ignored but still validated)"  << std::endl;

    throw std::invalid_argument(oss.str());
}

std::string get_uimg_filename_ext()
{
    std::ostringstream oss;

    if (!*bitsPerPixel && *paletteBits) {
        // just palette
        oss << ".p" << std::setw(2) << std::setfill('0') << *paletteBits;
    } else if (*bitsPerPixel && *paletteBits && !*bytesPerChunk) {
        // regular planar bitmap
        oss << ".bp" << *bitsPerPixel;
    } else if (*bitsPerPixel && (*bitsPerPixel > 8 || *paletteBits) && *bytesPerChunk) {
        // regular chunky bitmap
        oss << ".c" << std::setw(2) << std::setfill('0') << *bitsPerPixel;
    } else {
        // everything else: separate palette, plane raw words/chunky pixels or just the header...
        oss << ".dat";
    }

    return oss.str();
}

std::string parse_arguments(int argc, char* argv[])
{
    if (argc < 2) {
        print_help("uconvert"/*argv[0]*/);
    }

    std::string outputFilename;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        // last argument => it's a filename
        if (i == argc-1) {
            // defaults (always assumed to be in a legal combination)
            if (!bitmapWidth.has_value())
                bitmapWidth = DEFAULT_BITMAP_WIDTH;

            if (!bitmapHeight.has_value())
                bitmapHeight = DEFAULT_BITMAP_HEIGHT;

            if (!filter.has_value())
                filter = DEFAULT_FILTER;

            if (!dither.has_value())
                dither = DEFAULT_DITHER;

            if (!stCompatiblePalette.has_value())
                stCompatiblePalette = DEFAULT_ST_COMPATIBLE;

            if (!ttCompatiblePalette.has_value())
                ttCompatiblePalette = DEFAULT_TT_COMPATIBLE;

            if (!bitsPerPixel.has_value()) {
                if (*stCompatiblePalette)
                    bitsPerPixel = DEFAULT_ST_BITS_PER_PIXEL;
                else
                    bitsPerPixel = DEFAULT_BITS_PER_PIXEL;
            }

            if (!bytesPerChunk.has_value()) {
                if (*bitsPerPixel > 8)
                    bytesPerChunk = *bitsPerPixel / 8;
                else
                    bytesPerChunk = DEFAULT_BYTES_PER_CHUNK;
            } else if (*bytesPerChunk == -1 && *bitsPerPixel >= 8) {
                // make it easier to parse
                bytesPerChunk = *bitsPerPixel / 8;
            }

            if (!paletteBits.has_value()) {
                if (*bitsPerPixel <= 8) {
                    if (*stCompatiblePalette || *ttCompatiblePalette)
                        paletteBits = DEFAULT_ST_TT_PALETTE_BITS;
                    else
                        paletteBits = DEFAULT_PALETTE_BITS;
                } else {
                    paletteBits = 0;
                }
            }

            // do some sanity checks
            if (*stCompatiblePalette && *ttCompatiblePalette)
                throw std::invalid_argument("Can't set both '-st' and '-tt'.");

            if (*bitsPerPixel > 8 && *paletteBits)
                throw std::invalid_argument("Can't have palette with '-bpp' > 8.");

            if ((!*paletteBits || *paletteBits > 12) && (*stCompatiblePalette || *ttCompatiblePalette))
                throw std::invalid_argument("'-st' and '-tt' require 9- or 12-bit palette.");

            if (*bitsPerPixel > 4 && *stCompatiblePalette)
                throw std::invalid_argument("'-st' requires 1, 2 or 4 bits per pixel.");

            if (*bitsPerPixel > 8 && *ttCompatiblePalette)
                throw std::invalid_argument("'-tt' requires 1, 2, 4, 6 or 8 bits per pixel.");

            if (*bitsPerPixel == 2 && !(*stCompatiblePalette || *ttCompatiblePalette))
                throw std::invalid_argument("'2 bits per pixel work only with '-st' or '-tt'");

            if (*bytesPerChunk && !*bitsPerPixel)
                throw std::invalid_argument("-bpc requires bpp > 0.");

            if (*bytesPerChunk == -1 && *bitsPerPixel == 6)
                throw std::invalid_argument("-bpp 6 requires bpc >= 0.");

            if (*bytesPerChunk > 0 && *bitsPerPixel/8 > *bytesPerChunk)
                throw std::invalid_argument("bpp/8 > bpc.");

            if (outputFilename.empty())
                outputFilename = arg.substr(0, arg.find_last_of('.')) + get_uimg_filename_ext();

            if (outputFilename.find('.') == std::string::npos)
                outputFilename += get_uimg_filename_ext();
            break;
        }

        // flags
        {
            auto it = allowedFlags.find(arg);
            if (it != allowedFlags.end()) {
                it->second = true;
                continue;
            }
        }

        // it must be a pair
        if (i + 1 < argc-1)
            i++;
        else
            print_help("uconvert"/*argv[0]*/);

        // special pair
        if (arg == "-out") {
            outputFilename = argv[i];
            continue;
        }

        // pairs
        {
            auto it = allowedValues.find(arg);
            if (it == allowedValues.end()
                    || (!it->second.first.empty() && it->second.first.find(std::atoi(argv[i])) == it->second.first.end()))
                print_help("uconvert"/*argv[0]*/);

            it->second.second = std::atoi(argv[i]);
            continue;
        }
    }

    if (outputFilename.empty())
        print_help("uconvert"/*argv[0]*/);

    return outputFilename;
}
