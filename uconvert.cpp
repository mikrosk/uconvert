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
#include "palette.h"
#include "version.h"
#include "uimg.h"

// all values must be big endian
static void save_header(std::ofstream& ofs, const uint16_t width, const uint16_t height)
{
    // ID
    ofs.write("UIMG", 4);
    ofs.put(VERSION >> 8);
    ofs.put(VERSION & 0xff);
    // flags: bit 15-8 7 6 5 4 3 2 1 0
    //                             | |
    //                             +-+- 00: no palette
    //                                  01: ST/E compatible palette
    //                                  10: TT compatible palette
    //                                  11: Falcon compatible palette
    uint16_t flags = 0;
    if (*paletteBits) {
        if (*stCompatiblePalette)
            flags |= 0b01;
        else if (*ttCompatiblePalette)
            flags |= 0b10;
        else
            flags |= 0b11;
    }

    ofs.put(flags >> 8);
    ofs.put(flags);
    // bits per pixel (0 if bitmap not present)
    ofs.put(*bitsPerPixel);
    // bytes per chunk (0 if planar words or bitmap not present, -1 if packed)
    ofs.put(*bytesPerChunk);

    if (*bitsPerPixel) {
        // width
        ofs.put(width >> 8);
        ofs.put(width);
        // height
        ofs.put(height >> 8);
        ofs.put(height);
    }

    // palette (st(e)/tt/falcon; if present)

    // bitmap data (if present)
}

template<typename T>
static void save_palette(std::ofstream& ofs, const Image& image, const size_t paletteSize)
{
    std::vector<T> pal(paletteSize);

    for (size_t i = 0; i < image.colorMapSize(); ++i) {
        const Color color = image.colorMap(i);

        constexpr size_t shift = QuantumDepth - 8;

        const uint8_t r = (color.redQuantum()   >> shift) >> (8 - *paletteBits/3);
        const uint8_t g = (color.greenQuantum() >> shift) >> (8 - *paletteBits/3);
        const uint8_t b = (color.blueQuantum()  >> shift) >> (8 - *paletteBits/3);

        if constexpr (std::is_same_v<T, FalconPaletteEntry>) {
            switch (*paletteBits) {
            case 24:
                pal[i].r10 = r & 0x03;
                pal[i].g10 = g & 0x03;
                pal[i].b10 = b & 0x03;
                [[fallthrough]];
            case 18:
                // shift by 0 (18-bit) or 2 (24-bit) bits
                pal[i].r765432 = r >> ((*paletteBits - 18)/3);
                pal[i].g765432 = g >> ((*paletteBits - 18)/3);
                pal[i].b765432 = b >> ((*paletteBits - 18)/3);
                break;
            case 12:
            case 9:
                // no need to handle the 18- vs. 24-bit difference
                // as the bottom two bits are always zero
                pal[i].r765432 = r << (6 - *paletteBits/3);
                pal[i].g765432 = g << (6 - *paletteBits/3);
                pal[i].b765432 = b << (6 - *paletteBits/3);
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
                pal[i].r3210 = r << (4 - *paletteBits/3);
                pal[i].g3210 = g << (4 - *paletteBits/3);
                pal[i].b3210 = b << (4 - *paletteBits/3);
                break;
            default:
                throw_oss<std::invalid_argument>(std::ostringstream()
                    << "Unexpected number of palette bits: " << *paletteBits
                );
            }
        } else if constexpr (std::is_same_v<T, StePaletteEntry>) {
            switch (*paletteBits) {
            case 12:
                pal[i].r0 = r & 0x01;
                pal[i].g0 = g & 0x01;
                pal[i].b0 = b & 0x01;
                [[fallthrough]];
            case 9:
                // shift by 0 (9-bit ST) or 1 (12-bit STE) bits
                pal[i].r321 = r >> ((*paletteBits - 9)/3);
                pal[i].g321 = g >> ((*paletteBits - 9)/3);
                pal[i].b321 = b >> ((*paletteBits - 9)/3);
                break;
            default:
                throw_oss<std::invalid_argument>(std::ostringstream()
                    << "Unexpected number of palette bits: " << *paletteBits
                );
            }
        } else
            static_assert(bool_value<false, T>::value, "Unsupported palette type");
    }

    for (const auto& pal_entry : pal) {
        if constexpr (std::is_same_v<T, FalconPaletteEntry>) {
            ofs.put(pal_entry.wrapper.value >> 24); // MSB
            ofs.put(pal_entry.wrapper.value >> 16);
            ofs.put(pal_entry.wrapper.value >>  8);
            ofs.put(pal_entry.wrapper.value);   // LSB
        } else if constexpr (std::is_same_v<T, TtPaletteEntry> || std::is_same_v<T, StePaletteEntry>) {
            ofs.put(pal_entry.wrapper.value >> 8);  // MSB
            ofs.put(pal_entry.wrapper.value);   // LSB
        } else
            static_assert(bool_value<false, T>::value, "Unsupported palette type");
    }
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

            const uint8_t a = pPixelPackets->opacity >> shift;
            const uint8_t r = pPixelPackets->red     >> shift;
            const uint8_t g = pPixelPackets->green   >> shift;
            const uint8_t b = pPixelPackets->blue    >> shift;
            chunk = (a << 24) | (r << 16) | (g << 8) | b;    // ARGB
        } break;

        default:
            throw_oss<std::invalid_argument>(std::ostringstream()
                << "Unexpected number of bit per pixel: " << *bitsPerPixel
            );
        }

        int shift = (sizeof(T) - 1) * 8;    // go from MSB to LSB
        while (shift >= 0) {
            // quick hack to skip opacity
            if (*bitsPerPixel != 24 || shift != (sizeof(T) - 1) * 8) {
                buffer.push_back(chunk >> shift);
            }
            shift -= 8;
        };
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

        image.quiet(false);

        if (is_uimg(argv[argc-1])) {
            image = load_uimg(argv[argc-1]);
        } else {
            image.read(argv[argc-1]);
        }

        if (*bitmapWidth == -1)
            bitmapWidth = image.columns();
        else if (*bitmapWidth <= 0)
            throw std::invalid_argument("Width must be a positive number.");

        if (*bitmapHeight == -1)
            bitmapHeight = image.rows();
        else if (*bitmapHeight <= 0)
            throw std::invalid_argument("Height must be a positive number.");

        if (static_cast<unsigned int>(*bitmapWidth) != image.columns() || static_cast<unsigned int>(*bitmapHeight) != image.rows()) {
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
            if (*bitsPerPixel && *bitsPerPixel <= 8) {
                size_t totalColors = image.totalColors();
                if (totalColors > (1u << *bitsPerPixel)) {
                    std::cout << "Converting from " << totalColors << " to " << (1ul << *bitsPerPixel) << " colours." << std::endl;

                    image.quantizeDither(*dither);
                    image.quantizeColors(1u << *bitsPerPixel);
                    image.quantize();

                    totalColors = image.totalColors();
                }

                if (image.classType() != PseudoClass)
                    throw std::runtime_error("Not a pseudo class.");

                if (image.colorMapSize() > (1u << *bitsPerPixel)) {
                	std::cerr << "Warning, adjusting colorMapSize from " << image.colorMapSize()
                		<< " to " <<  (1u << *bitsPerPixel) 
                		<< " (totalColors: " << totalColors << ")" 
                		<< std::endl;
                	image.colorMapSize(1u << *bitsPerPixel);
                }

                //if (*paletteBits && image.type() != PaletteType)
                //    throw std::runtime_error("Not a palette type.");

                //if (*bitsPerPixel && image.colorMapSize() > (1u << *bitsPerPixel)) {
                //    throw_oss<std::runtime_error>(std::ostringstream()
                //        << "Too few bpp for " << image.colorMapSize() << " colours."
                //    );
                //}

                if (image.columns() % 16 != 0)
                    throw std::runtime_error("Width must be divisible by 16.");
            }

            std::ofstream ofs(outputFilename, std::ofstream::binary);
            if (!ofs)
                throw std::runtime_error("Opening destination file failed.");

            save_header(ofs, image.columns(), image.rows());
            if (*paletteBits) {
                if (*stCompatiblePalette)
                    save_palette<StePaletteEntry>(ofs, image, (1 << *bitsPerPixel));
                else if (*ttCompatiblePalette)
                    save_palette<TtPaletteEntry>(ofs, image, (1 << *bitsPerPixel));
                else
                    save_palette<FalconPaletteEntry>(ofs, image, (1 << *bitsPerPixel));
            }

            if (*bitsPerPixel) {
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

    std::cout << "File " << outputFilename
              << " (" << *bitmapWidth << "x" << *bitmapHeight;

    if (saving_uimg)
        std::cout << "@" << *bitsPerPixel;

    std::cout << ") has been saved." << std::endl;

    return EXIT_SUCCESS;
}
