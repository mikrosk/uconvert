#include <GraphicsMagick/Magick++.h>
using namespace Magick;

#include <cassert>
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

constexpr uint16_t VERSION = 0x0100;

// all values must be big endian
static void save_header(std::ofstream& ofs, const uint16_t width, const uint16_t height)
{
    // ID
    ofs.write("UIMG\0", 5);
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

    // palette (st(e)/falcon; if present)

    // bitmap data (if present)
}

template<typename T>
static void save_palette(std::ofstream& ofs, const Image& image, const size_t paletteSize)
{
    std::vector<T> pal(paletteSize);

    // yes, the getter is non-const, sigh...
    for (size_t i = 0; i < const_cast<Image&>(image).colorMapSize(); ++i) {
        const ColorRGB colorRgb(image.colorMap(i));

        const uint8_t r = colorRgb.red()   * ((1 << (*paletteBits/3)) - 1);
        const uint8_t g = colorRgb.green() * ((1 << (*paletteBits/3)) - 1);
        const uint8_t b = colorRgb.blue()  * ((1 << (*paletteBits/3)) - 1);

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
                throw std::invalid_argument(
                    (std::ostringstream()
                        << "Unexpected number of palette bits: " << *paletteBits
                    ).str()
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
                throw std::invalid_argument(
                    (std::ostringstream()
                        << "Unexpected number of palette bits: " << *paletteBits
                    ).str()
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
                throw std::invalid_argument(
                    (std::ostringstream()
                        << "Unexpected number of palette bits: " << *paletteBits
                    ).str()
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
                planes[j] |= ((paletteIndex >> j) & 0x01) << (15 - i);
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
        case 8: {
            chunk = *pIndexPackets++;
        } break;

        case 16: {
            const uint8_t r = pPixelPackets->red   * ((1 << 5) - 1);
            const uint8_t g = pPixelPackets->green * ((1 << 6) - 1);
            const uint8_t b = pPixelPackets->blue  * ((1 << 5) - 1);
            chunk = (r << 11) | (g << 5) | b;
        } break;

        case 24:
        case 32: {
            const uint8_t r = pPixelPackets->red   * ((1 << 8) - 1);
            const uint8_t g = pPixelPackets->green * ((1 << 8) - 1);
            const uint8_t b = pPixelPackets->blue  * ((1 << 8) - 1);
            // TODO: true alpha channel?
            chunk = (r << 24) | (g << 16) | (b << 8) | 0xffu;    // RGBA with full opacity
        } break;

        default:
            throw std::invalid_argument(
                (std::ostringstream()
                    << "Unexpected number of bit per pixel: " << *bitsPerPixel
                 ).str()
            );
        }

        int shift = (sizeof(T) - 1) * 8;    // go from MSB to LSB
        while (shift >= 0) {
            buffer.push_back(chunk >> shift);
            shift -= 8;

            // quick hack to skip opacity
            if (shift == 0 && *bitsPerPixel == 24)
                break;
        };
    }
}

static void copy_packed_buffer(std::vector<uint8_t>& buffer, const Image& image)
{
    const PixelPacket* pPixelPackets = image.getConstPixels(0, 0, image.columns(), image.rows());
    const IndexPacket* pIndexPackets = image.getConstIndexes();

    const size_t pixelsPerChunk = 8 / *bitsPerPixel;

    for (const PixelPacket* pPixelPacketsEnd = pPixelPackets + image.columns() * image.rows();
         pPixelPackets != pPixelPacketsEnd; pPixelPackets += pixelsPerChunk)
    {
        uint8_t chunk = 0;
        for (size_t i = 0; i < pixelsPerChunk; ++i) {
            assert(*pIndexPackets < (1 << *bitsPerPixel));
            chunk += *pIndexPackets++;
            chunk <<= *bitsPerPixel;
        }

        buffer.push_back(chunk);
    }
}

int main(int argc, char* argv[])
{
    std::string outputFilename;

    InitializeMagick(*argv);
    Image image;

    try {
        outputFilename = parse_arguments(argc, argv);

        image.quiet(false);
        image.read(argv[argc-1]);

        //image.resize({320*2, 240*2});
        //image.type(PaletteType);

        //image.quantizeDither(false);
        //image.quantizeColors(16);
        //image.quantize();

        if (image.type() != PaletteType)
            throw std::runtime_error("Not a palette type.");

        if (image.classType() != PseudoClass)
            throw std::runtime_error("Not a pseudo class.");

        if (*paletteBits) {
            if ((*stCompatiblePalette && image.colorMapSize() > 16) || (!*stCompatiblePalette && image.colorMapSize() > 256))
                throw std::runtime_error(
                        (std::ostringstream() << "Color map must have less or equal than "
                         << (*stCompatiblePalette ? 16 :256)
                         << " entries (currently: " << image.colorMapSize() << ").").str());

            if (image.columns() % 16 != 0)
                throw std::runtime_error("Width must be divisible by 16.");
        }

        if (*bitsPerPixel && image.colorMapSize() > (1u << *bitsPerPixel))
            throw std::runtime_error((std::ostringstream() << "Too few bpp for " << image.colorMapSize() << " colours.").str());

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
                atariImageSize *= *bitsPerPixel/8;  // this includes packed chunky pixels, too

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
                assert(*bitsPerPixel < 8);
                copy_packed_buffer(atariImage, image);
            } else {
                throw std::invalid_argument(
                    (std::ostringstream()
                        << "Unexpected number of bytes per chunk: " << *bytesPerChunk
                    ).str()
                );
            }

            save_buffer(ofs, atariImage);
        }

        ofs.close();
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "File " << outputFilename
              << " (" << image.columns() << "x" << image.rows() << "@" << image.colorMapSize() << ") "
              << "has been saved." << std::endl;

    return EXIT_SUCCESS;
}
