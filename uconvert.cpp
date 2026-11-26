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

#include <GraphicsMagick/Magick++.h>
using namespace Magick;

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "args.h"
#include "helpers.h"
#include "palopt.h"
#include "palette.h"
#include "version.h"
#include "uimg.h"

// all values must be big endian
static void save_header(std::ofstream& ofs, const uint16_t width, const uint16_t height, const uint8_t changedColorsPerLine)
{
    // ID
    ofs.write("UIMG", 4);
    const uint16_t version = changedColorsPerLine ? UIMG_MULTILINE_VERSION : UIMG_VERSION;
    ofs.put(version >> 8);
    ofs.put(version & 0xff);
    // flags: bit 15-8 7 6 5 4 3 2 1 0
    //                           | | |
    //                           +-+-+- 000: no palette
    //                                  001: ST/E compatible palette
    //                                  010: TT compatible palette
    //                                  011: Falcon compatible palette
    //                                  100: VDI palette
    //                                  101: ST/E compatible palette, multiline
    //                                  110: TT compatible palette, multiline
    //                                  111: Falcon compatible palette, multiline
    uint16_t flags = 0;
    if (*paletteBits) {
        if (*stCompatiblePalette)
            flags |= 0b001;
        else if (*ttCompatiblePalette)
            flags |= 0b010;
        else if (*vdiCompatiblePalette)
            flags |= 0b100;
        else
            flags |= 0b011;

        if (changedColorsPerLine)
            flags |= 0b100;
    }

    ofs.put(flags >> 8);
    ofs.put(flags);
    // bits per pixel
    ofs.put(*bitsPerPixel);
    // bytes per chunk (0 if planar words, -1 if packed)
    ofs.put(*bytesPerChunk);
    // palette entries changed on every line and a pad byte keeping
    // the rest word aligned (multiline palette only)
    if (changedColorsPerLine) {
        ofs.put(changedColorsPerLine);
        ofs.put(0);
    }

    // width and height, 0 if there is no bitmap data
    ofs.put(width >> 8);
    ofs.put(width);
    ofs.put(height >> 8);
    ofs.put(height);

    // palette (st(e)/tt/falcon; if present)

    // palette blocks of the lines (multiline palette only)

    // bitmap data (if present)
}

template<typename T>
static T make_palette_entry(const Color& color)
{
    T palEntry = {};

    constexpr size_t shift = QuantumDepth - 8;

    const uint8_t r = (color.redQuantum()   >> shift) >> (8 - *paletteBits/3);
    const uint8_t g = (color.greenQuantum() >> shift) >> (8 - *paletteBits/3);
    const uint8_t b = (color.blueQuantum()  >> shift) >> (8 - *paletteBits/3);

    if constexpr (std::is_same_v<T, VdiPaletteEntry>) {
        const unsigned int max = (1u << (*paletteBits/3)) - 1;

        palEntry.r = r * 1000 / max;
        palEntry.g = g * 1000 / max;
        palEntry.b = b * 1000 / max;
    } else if constexpr (std::is_same_v<T, FalconPaletteEntry>) {
        switch (*paletteBits) {
        case 24:
            palEntry.r10 = r & 0x03;
            palEntry.g10 = g & 0x03;
            palEntry.b10 = b & 0x03;
            [[fallthrough]];
        case 18:
            // shift by 0 (18-bit) or 2 (24-bit) bits
            palEntry.r765432 = r >> ((*paletteBits - 18)/3);
            palEntry.g765432 = g >> ((*paletteBits - 18)/3);
            palEntry.b765432 = b >> ((*paletteBits - 18)/3);
            break;
        case 12:
        case 9:
            // no need to handle the 18- vs. 24-bit difference
            // as the bottom two bits are always zero
            palEntry.r765432 = r << (6 - *paletteBits/3);
            palEntry.g765432 = g << (6 - *paletteBits/3);
            palEntry.b765432 = b << (6 - *paletteBits/3);
            break;
        default:
            throw_oss<std::invalid_argument>(std::ostringstream()
                << "Unexpected number of palette bits: " << *paletteBits
            );
        }
    } else if constexpr (std::is_same_v<T, TtPaletteEntry>) {
        switch (*paletteBits) {
        case 12:
        case 9:
            // shift by 1 (9-bit ST) or 0 (12-bit STE) bits
            palEntry.r3210 = r << (4 - *paletteBits/3);
            palEntry.g3210 = g << (4 - *paletteBits/3);
            palEntry.b3210 = b << (4 - *paletteBits/3);
            break;
        default:
            throw_oss<std::invalid_argument>(std::ostringstream()
                << "Unexpected number of palette bits: " << *paletteBits
            );
        }
    } else if constexpr (std::is_same_v<T, StePaletteEntry>) {
        switch (*paletteBits) {
        case 12:
            palEntry.r0 = r & 0x01;
            palEntry.g0 = g & 0x01;
            palEntry.b0 = b & 0x01;
            [[fallthrough]];
        case 9:
            // shift by 0 (9-bit ST) or 1 (12-bit STE) bits
            palEntry.r321 = r >> ((*paletteBits - 9)/3);
            palEntry.g321 = g >> ((*paletteBits - 9)/3);
            palEntry.b321 = b >> ((*paletteBits - 9)/3);
            break;
        default:
            throw_oss<std::invalid_argument>(std::ostringstream()
                << "Unexpected number of palette bits: " << *paletteBits
            );
        }
    } else
        static_assert(bool_value<false, T>::value, "Unsupported palette type");

    return palEntry;
}

template<typename T>
static void save_palette_entry(std::ofstream& ofs, const T& palEntry)
{
    if constexpr (std::is_same_v<T, VdiPaletteEntry>) {
        for (const uint16_t component : { palEntry.r, palEntry.g, palEntry.b }) {
            ofs.put(component >> 8);    // MSB
            ofs.put(component);         // LSB
        }
    } else if constexpr (std::is_same_v<T, FalconPaletteEntry>) {
        ofs.put(palEntry.wrapper.value >> 24);  // MSB
        ofs.put(palEntry.wrapper.value >> 16);
        ofs.put(palEntry.wrapper.value >>  8);
        ofs.put(palEntry.wrapper.value);    // LSB
    } else if constexpr (std::is_same_v<T, TtPaletteEntry> || std::is_same_v<T, StePaletteEntry>) {
        ofs.put(palEntry.wrapper.value >> 8);   // MSB
        ofs.put(palEntry.wrapper.value);    // LSB
    } else
        static_assert(bool_value<false, T>::value, "Unsupported palette type");
}

template<typename T>
static void save_palette(std::ofstream& ofs, const Image& image, const size_t paletteSize)
{
    for (size_t entry = 0; entry < paletteSize; ++entry) {
        // a VDI palette is ordered by pen, everything else by colour index
        const size_t i = std::is_same_v<T, VdiPaletteEntry> ? vdi_color_index(entry, paletteSize) : entry;

        // pens are not in colour index order, later ones may still have a colour;
        // this one stays black
        save_palette_entry(ofs, i < image.colorMapSize() ? make_palette_entry<T>(image.colorMap(i)) : T{});
    }
}

// bits per colour channel the palette optimiser works with
static int level_bits()
{
    return *paletteBits ? *paletteBits/3 : 8;
}

// a colour given as palette levels (0 .. (1 << level_bits()) - 1 per channel)
static Color levels_to_color(const Levels& levels)
{
    // the inverse of make_palette_entry()'s conversion
    const unsigned int shift = QuantumDepth - level_bits();
    return Color(levels[0] << shift, levels[1] << shift, levels[2] << shift);
}

// replaces 'image' by its indexed version; with lineColors, returns the
// first entry of the block rewritten before each line and its colours
static void optimise(Image& image, std::vector<uint16_t>& lineFirst, std::vector<Color>& lineColorMap)
{
    const unsigned int width = image.columns();
    const unsigned int height = image.rows();
    const unsigned int entries = 1u << *bitsPerPixel;

    std::vector<float> rgb;
    rgb.reserve(3 * width * height);

    const PixelPacket* pPixelPackets = image.getConstPixels(0, 0, width, height);
    for (size_t i = 0; i < width * height; ++i) {
        constexpr double scale = 255.0 / MaxRGB;
        rgb.push_back(pPixelPackets[i].red   * scale);
        rgb.push_back(pPixelPackets[i].green * scale);
        rgb.push_back(pPixelPackets[i].blue  * scale);
    }

    if (*lineColors)
        std::cout << "Optimising " << entries << " colours, " << *lineColors << " of them rewritten before every line." << std::endl;

    const PaletteParams params = {
        static_cast<int>(width), static_cast<int>(height), static_cast<int>(entries), *lineColors,
        level_bits(), *dither
    };
    const PaletteResult result = optimise_palette(params, rgb);

    Image indexed({width, height}, {0, 0, 0});
    indexed.classType(PseudoClass);
    indexed.type(PaletteType);
    indexed.colorMapSize(entries);

    for (unsigned int k = 0; k < entries; ++k)
        indexed.colorMap(k, levels_to_color(result.palette[k]));

    lineFirst = result.lineFirst;
    lineColorMap.clear();
    for (const Levels& levels : result.lineColors)
        lineColorMap.push_back(levels_to_color(levels));

    PixelPacket* pPixels = indexed.getPixels(0, 0, width, height);
    IndexPacket* pIndexes = indexed.getIndexes();
    for (size_t i = 0; i < width * height; ++i) {
        pIndexes[i] = result.indices[i];
        pPixels[i] = indexed.colorMap(result.indices[i]);
    }
    indexed.syncPixels();

    image = indexed;
}

template<typename T>
static void save_buffer(std::ofstream& ofs, const std::vector<T>& buffer)
{
    ofs.write((char*)buffer.data(), sizeof_vector(buffer));
}

static void c2p(std::vector<uint8_t>& buffer, const Image& image)
{
    // unfortunately, we really need to call this one even if it's useless
    image.getConstPixels(0, 0, image.columns(), image.rows());
    const IndexPacket* pIndexPackets = image.getConstIndexes();

    for (const IndexPacket* pIndexPacketsEnd = pIndexPackets + image.columns() * image.rows();
         pIndexPackets != pIndexPacketsEnd; pIndexPackets += 16)
    {
        std::vector<uint16_t> planes(*bitsPerPixel); // 16 pixels = 16 bits x bit depth

        for (size_t i = 0; i < 16; ++i) {
            uint8_t paletteIndex = pIndexPackets[i];

            for (int j = 0; j < *bitsPerPixel; ++j) {
                planes[j] |= ((paletteIndex >> j) & 1) << (15 - i);
            }
        }

        for (int i = 0; i < *bitsPerPixel; ++i) {
            buffer.push_back(planes[i] >> 8);    // MSB
            buffer.push_back(planes[i]);         // LSB
        }
    }
}

template<typename T>
static void copy_buffer(std::vector<uint8_t>& buffer, const Image& image)
{
    const PixelPacket* pPixelPackets = image.getConstPixels(0, 0, image.columns(), image.rows());
    const IndexPacket* pIndexPackets = image.getConstIndexes();
    T chunk = 0;

    for (const PixelPacket* pPixelPacketsEnd = pPixelPackets + image.columns() * image.rows();
         pPixelPackets != pPixelPacketsEnd; pPixelPackets++)
    {
        switch (*bitsPerPixel) {
        case 1:
        case 2:
        case 4:
        case 6:
        case 8: {
            chunk = *pIndexPackets++;
        } break;

        case 16: {
            constexpr size_t shift = QuantumDepth - 8;

            const uint8_t r = (pPixelPackets->red   >> shift) >> (8 - 5);
            const uint8_t g = (pPixelPackets->green >> shift) >> (8 - 6);
            const uint8_t b = (pPixelPackets->blue  >> shift) >> (8 - 5);
            chunk = (r << 11) | (g << 5) | b;
        } break;

        case 24:
        case 32: {
            constexpr size_t shift = QuantumDepth - 8;

            // GraphicsMagick stores transparency, the file wants opacity;
            // a 24-bit pixel has no alpha, its fourth byte is only padding
            const uint8_t a = *bitsPerPixel == 32 ? (MaxRGB - pPixelPackets->opacity) >> shift : 0xff;
            const uint8_t r = pPixelPackets->red     >> shift;
            const uint8_t g = pPixelPackets->green   >> shift;
            const uint8_t b = pPixelPackets->blue    >> shift;
            chunk = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;    // ARGB
        } break;

        default:
            throw_oss<std::invalid_argument>(std::ostringstream()
                << "Unexpected number of bit per pixel: " << *bitsPerPixel
            );
        }

        // go from MSB to LSB, dropping the bytes which don't fit the chunk
        // (i.e. the opacity of a 24-bit pixel stored in three bytes)
        for (int shift = (*bytesPerChunk - 1) * 8; shift >= 0; shift -= 8)
            buffer.push_back(chunk >> shift);
    }
}

static void copy_packed_buffer(std::vector<uint8_t>& buffer, const Image& image)
{
    // unfortunately, we really need to call this one even if it's useless
    image.getConstPixels(0, 0, image.columns(), image.rows());
    const IndexPacket* pIndexPackets = image.getConstIndexes();

    const size_t pixelsPerChunk = 8 / *bitsPerPixel;

    for (const IndexPacket* pIndexPacketsEnd = pIndexPackets + image.columns() * image.rows();
         pIndexPackets != pIndexPacketsEnd;)
    {
        uint8_t chunk = 0;
        for (size_t i = 0; i < pixelsPerChunk - 1; ++i) {
            assert(*pIndexPackets < (1 << *bitsPerPixel));
            chunk += *pIndexPackets++;
            chunk <<= *bitsPerPixel;
        }
        assert(*pIndexPackets < (1 << *bitsPerPixel));
        chunk += *pIndexPackets++;

        buffer.push_back(chunk);
    }
}

int main(int argc, char* argv[])
{
    std::string outputFilename;
    bool saving_uimg;

    InitializeMagick(argv[0]);
    Image image;

    try {
        outputFilename = parse_arguments(argc, argv);
        saving_uimg = outputFilename.substr(outputFilename.find_last_of('.')) == get_uimg_filename_ext();

        if (*lineColors && !saving_uimg)
            throw std::invalid_argument("'-linecol' requires an Atari bitmap as output.");

        image.quiet(false);

        if (is_uimg(argv[argc-1])) {
            image = load_uimg(argv[argc-1]);
        } else {
            image.read(argv[argc-1]);
        }

        if (*bitmapWidth == -1)
            bitmapWidth = image.columns();
        else if (*bitmapWidth < 0)
            throw std::invalid_argument("Width must be a positive number.");

        if (*bitmapHeight == -1)
            bitmapHeight = image.rows();
        else if (*bitmapHeight < 0)
            throw std::invalid_argument("Height must be a positive number.");

        if (has_bitmap_data()
                && (static_cast<unsigned int>(*bitmapWidth) != image.columns()
                    || static_cast<unsigned int>(*bitmapHeight) != image.rows())) {
            float old_ratio = (float)image.columns() / (float)image.rows();

            Geometry geometry;
            geometry.width(static_cast<unsigned int>(*bitmapWidth));
            geometry.height(static_cast<unsigned int>(*bitmapHeight));
            geometry.aspect(true);

            if (*filter)
                image.resize(geometry);
            else
                image.resize(geometry, FilterTypes::UndefinedFilter, 0.0);

            float new_ratio = (float)image.columns() / (float)image.rows();

            if (std::fabs(old_ratio - new_ratio) > 0.001)
                std::cout << "Aspect ratio changed; old: " << old_ratio << ", new: " << new_ratio << std::endl;
        }

        if (saving_uimg) {
            std::vector<uint16_t> lineFirst;    // height
            std::vector<Color> lineColorMap;    // height * lineColors

            if (*bitsPerPixel <= 8) {
                // before the optimiser spends minutes on it
                if (has_bitmap_data() && image.columns() % 16 != 0)
                    throw std::runtime_error("Width must be divisible by 16.");

                const bool convert = image.classType() != PseudoClass || image.colorMapSize() > (1u << *bitsPerPixel);

                if (*lineColors) {
                    optimise(image, lineFirst, lineColorMap);
                } else if (convert) {
                    const size_t colors = image.classType() == PseudoClass
                            ? image.colorMapSize()
                            : image.totalColors();

                    std::cout << "Converting from " << colors << " to " << (1ul << *bitsPerPixel) << " colours." << std::endl;

                    image.quantizeDither(*dither);
                    image.quantizeColors(1u << *bitsPerPixel);
                    image.quantize();
                }

                if (image.classType() != PseudoClass)
                    throw std::runtime_error("Not a pseudo class.");

                if (image.colorMapSize() > (1u << *bitsPerPixel)) {
                    throw_oss<std::runtime_error>(std::ostringstream()
                        << "Too few bpp for " << image.colorMapSize() << " colours."
                    );
                }
            }

            std::ofstream ofs(outputFilename, std::ofstream::binary);
            if (!ofs)
                throw std::runtime_error("Opening destination file failed.");

            if (has_bitmap_data())
                save_header(ofs, image.columns(), image.rows(), *lineColors);
            else
                save_header(ofs, 0, 0, 0);

            if (*paletteBits) {
                if (*stCompatiblePalette)
                    save_palette<StePaletteEntry>(ofs, image, 1u << *bitsPerPixel);
                else if (*ttCompatiblePalette)
                    save_palette<TtPaletteEntry>(ofs, image, 1u << *bitsPerPixel);
                else if (*vdiCompatiblePalette)
                    save_palette<VdiPaletteEntry>(ofs, image, 1u << *bitsPerPixel);
                else
                    save_palette<FalconPaletteEntry>(ofs, image, 1u << *bitsPerPixel);
            }

            // the palette blocks of the lines; assured by args.cpp: only a Falcon palette has them
            for (size_t y = 0; y < lineFirst.size(); ++y) {
                ofs.put(lineFirst[y] >> 8);
                ofs.put(lineFirst[y]);
                for (int k = 0; k < *lineColors; ++k)
                    save_palette_entry(ofs, make_palette_entry<FalconPaletteEntry>(lineColorMap[y * *lineColors + k]));
            }

            if (has_bitmap_data()) {
                size_t atariImageSize = image.rows() * image.columns();

                if (*bytesPerChunk > 0)
                    atariImageSize *= *bytesPerChunk;
                else
                    atariImageSize = (atariImageSize * *bitsPerPixel) / 8;  // this includes packed chunky pixels, too

                std::vector<uint8_t> atariImage;
                atariImage.reserve(atariImageSize);

                if (!*bytesPerChunk) {
                    c2p(atariImage, image);
                } else if (*bytesPerChunk == 1) {
                    copy_buffer<uint8_t>(atariImage, image);
                } else if (*bytesPerChunk == 2) {
                    copy_buffer<uint16_t>(atariImage, image);
                } else if (*bytesPerChunk == 3) {
                    copy_buffer<uint32_t>(atariImage, image);
                } else if (*bytesPerChunk == 4) {
                    copy_buffer<uint32_t>(atariImage, image);
                } else if (*bytesPerChunk == -1) {
                    // assured by args.cpp
                    assert(*bitsPerPixel < 8 && *bitsPerPixel != 6);
                    copy_packed_buffer(atariImage, image);
                } else {
                    throw_oss<std::invalid_argument>(std::ostringstream()
                        << "Unexpected number of bytes per chunk: " << *bytesPerChunk
                    );
                }

                save_buffer(ofs, atariImage);
            }

            ofs.close();
        } else {
            // save generic image
            image.write(outputFilename);
        }
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "File " << outputFilename << " (";

    if (saving_uimg && !has_bitmap_data()) {
        std::cout << (1u << *bitsPerPixel) << "-colour palette";
    } else {
        std::cout << *bitmapWidth << "x" << *bitmapHeight;

        if (saving_uimg)
            std::cout << "@" << *bitsPerPixel;
    }

    std::cout << ") has been saved." << std::endl;

    return EXIT_SUCCESS;
}
