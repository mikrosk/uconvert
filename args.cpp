#include "args.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

std::optional<int16_t>  bitsPerPixel;         // 1, 2, 4, 8 (both planar and chunky); 16, 24, 32 (chunky only) or 0 (if explicitly disabled)
std::optional<int16_t>  bytesPerChunk;        // -1 (if implicit/packed), 1, 2, 3, 4 or 0 (if disabled)
std::optional<int16_t>  paletteBits;          // 9, 12, 18, 24 or 0 (if bitsPerPixel > 8 or explicitly disabled)
std::optional<bool>     stCompatiblePalette;  // if true, use the ST/E palette registers
std::optional<bool>     ttCompatiblePalette;  // if true, use the TT palette registers
// TODO: "convert" = convert into desired bit depth
// TODO: chunky 6bpp?
// cat picture.gif | giftopnm | pnmquant 16 | ppmtoneo > picture.neo

// defaults
constexpr uint16_t DEFAULT_BITS_PER_PIXEL  = 8;
constexpr uint16_t DEFAULT_BYTES_PER_CHUNK = 0;
constexpr uint16_t   DEFAULT_PALETTE_BITS  = 24;
constexpr bool      DEFAULT_ST_COMPATIBLE  = false;
constexpr bool      DEFAULT_TT_COMPATIBLE  = false;

std::unordered_map<std::string, std::pair<std::unordered_set<int16_t>, std::optional<int16_t>&>> allowedValues = {
    { "-bpp", { { 0, 1, 2, 4, 8, 16, 24, 32 }, bitsPerPixel  } },
    { "-bpc", { { -1, 0, 1, 2, 3, 4 },         bytesPerChunk } },
    { "-pal", { { 0, 9, 12, 18, 24 },          paletteBits   } }
};

static void print_help(const char* name)
{
    std::ostringstream oss;
    oss << "Usage: " << name << " [-bpp <num>] [-bpc <num> [-packed]] [-pal <bits>] [-st] <image file>" << std::endl
           << "       -bpp:    bits per pixel, i.e. colour depth (0, 1, 2, 4, 8, 16 [RGB565], 24, 32) [default " << DEFAULT_BITS_PER_PIXEL << "]" << std::endl
           << "       -bpc:    bytes per chunk (-1 for packed chunky pixels [default for bpp > 8], 0, 1, 2, 3, 4) [default " << DEFAULT_BYTES_PER_CHUNK << "]" << std::endl
           << "       -pal:    number of bits per palette entry where applicable (0, 9, 12, 18, 24; implicitly disabled for bpp > 8) [default " << DEFAULT_PALETTE_BITS << "]" << std::endl
           << "       -st:     output palette in the ST/E-specific format (only 9/12-bit palette) [default " << DEFAULT_ST_COMPATIBLE << "]" << std::endl
           << "       -tt:     output palette in the TT-specific format (only 9/12-bit palette) [default " << DEFAULT_TT_COMPATIBLE << "]";

    throw std::invalid_argument(oss.str());
}

static std::string get_filename_ext()
{
    if (!*bitsPerPixel && *paletteBits) {
        // just palette
        return (std::ostringstream() << ".p" << std::setw(2) << std::setfill('0') << *paletteBits).str();
    } else if (*bitsPerPixel && *paletteBits && !*bytesPerChunk) {
        // regular planar bitmap
        return (std::ostringstream() << ".bp" << *bitsPerPixel).str();
    } else if (*bitsPerPixel && (*bitsPerPixel > 8 || *paletteBits) && *bytesPerChunk) {
        // regular chunky bitmap
        return (std::ostringstream() << ".c" << std::setw(2) << std::setfill('0') << *bitsPerPixel).str();
    } else {
        // everything else: separate palette, plane raw words/chunky pixels or just the header...
        return ".dat";
    }
}

std::string parse_arguments(int argc, char* argv[])
{
    if (argc < 2 || argc > 10) {
        print_help(argv[0]);
    }

    std::string outputFilename;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        // last argument => it's a filename
        if (i == argc-1) {
            // defaults (always assumed to be in a legal combination)
            if (!bitsPerPixel.has_value())
                bitsPerPixel = DEFAULT_BITS_PER_PIXEL;

            if (!bytesPerChunk.has_value()) {
                if (*bitsPerPixel > 8)
                    bytesPerChunk = *bitsPerPixel / 8;
                else
                    bytesPerChunk = DEFAULT_BYTES_PER_CHUNK;
            } else if (*bytesPerChunk == -1 && *bitsPerPixel >= 8) {
                // make it easier to parse
                bytesPerChunk = *bitsPerPixel / 8;
            }

            if (!paletteBits.has_value() && *bitsPerPixel <= 8) // either not set or less than 8bpp
                paletteBits = DEFAULT_PALETTE_BITS;

            if (!stCompatiblePalette.has_value())
                stCompatiblePalette = DEFAULT_ST_COMPATIBLE;

            if (!ttCompatiblePalette.has_value())
                ttCompatiblePalette = DEFAULT_TT_COMPATIBLE;

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
                throw std::invalid_argument("'-tt' requires 1, 2, 4 or 8 bits per pixel.");

            //if (*bitsPerPixel == 2 && !*stCompatiblePalette)
            //    throw std::invalid_argument("'2 bits per pixel work only with '-st'");

            if (*bytesPerChunk && !*bitsPerPixel)
                throw std::invalid_argument("-bpc requires bpp > 0.");

            if (*bytesPerChunk > 0 && *bitsPerPixel/8 > *bytesPerChunk)
                throw std::invalid_argument("bpp/8 > bpc.");

            outputFilename = arg.substr(0, arg.find_last_of('.')) + get_filename_ext();
            break;
        }

        // flags
        if (arg == "-st") {
            stCompatiblePalette = true;
            continue;
        }

        if (arg == "-tt") {
            ttCompatiblePalette = true;
            continue;
        }

        // pairs
        i++;

        auto it = allowedValues.find(arg);
        if (it == allowedValues.end()
                || i == argc || i+1 == argc
                || it->second.first.find(std::atoi(argv[i])) == it->second.first.end())
            print_help(argv[0]);

        it->second.second = std::atoi(argv[i]);
    }

    return outputFilename;
}
