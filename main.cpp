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

// all values must be big endian
static void save_header(std::ofstream& ofs, const uint16_t width, const uint16_t height)
{
    // ID
    ofs.write("UIMG", 4);
    // flags: bit 15-8 7 6 5 4 3 2 1 0
    //                         | | | |
    //                         | | +-+- 00: no palette
    //                         | |      01: ST/E compatible palette
    //                         | |      10: Falcon compatible palette
    //                         | |
    //                         +-+----- 00: planar words
    //                                  01: packed pixels in one chunk
    //                                  10: one pixel in one chunk
    uint16_t flags = 0;
    if (*paletteBits)
        flags |= (*stCompatiblePalette ? 0b01 : 0b10) << 0;
    if (*bytesPerChunk)
        flags |= (*packedChunks ? 0b01 : 0b10) << 2;

    ofs.put(flags >> 8);
    ofs.put(flags);
    // bits per pixel (0 if bitmap not present)
    ofs.put(*bitsPerPixel);
    // bytes per chunk (0 if planar words or bitmap not present)
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

        const uint r = colorRgb.red()   * ((1 << (*paletteBits/3)) - 1);
        const uint g = colorRgb.green() * ((1 << (*paletteBits/3)) - 1);
        const uint b = colorRgb.blue()  * ((1 << (*paletteBits/3)) - 1);

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

            ofs.put(pal[i].wrapper.value >> 24);    // MSB
            ofs.put(pal[i].wrapper.value >> 16);
            ofs.put(pal[i].wrapper.value >>  8);
            ofs.put(pal[i].wrapper.value);  // LSB
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

            ofs.put(pal[i].wrapper.value >> 8);    // MSB
            ofs.put(pal[i].wrapper.value);  // LSB
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
    assert(image.columns() * image.rows() == buffer.size());

    // unfortunately, we really need to call this one even if it's useless
    image.getConstPixels(0, 0, image.columns(), image.rows());
    const IndexPacket* pIndexPackets = image.getConstIndexes();

    for (size_t y = 0; y < image.rows(); ++y) {
        for (size_t x = 0; x < image.columns(); x += 16) {
            std::vector<uint16_t> planes(*bitsPerPixel); // 16 pixels = 16 bits x bit depth

            for (size_t i = 0; i < 16; ++i) {
                uint8_t paletteIndex = pIndexPackets[y * image.columns() + x + i];

                for (size_t j = 0; j < *bitsPerPixel; ++j) {
                    planes[j] |= ((paletteIndex >> j) & 0x01) << (15 - i);
                }
            }

            uint8_t* p = buffer.data() + y * image.columns() + x;
            for (size_t i = 0; i < *bitsPerPixel; ++i) {
                *p++ = planes[i] >> 8;  // MSB
                *p++ = planes[i];   // LSB
            }
        }
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

        if (image.type() != PaletteType)
            throw std::runtime_error("Not a palette type.");

        if (image.classType() != PseudoClass)
            throw std::runtime_error("Not a pseudo class.");

        if (*paletteBits) {
            if ((*stCompatiblePalette && image.colorMapSize() > 16) || (!*stCompatiblePalette && image.colorMapSize() > 256))
                throw std::runtime_error(
                        (std::ostringstream() << "Color map must have less or equal than 16/256 entries (currently: " << image.colorMapSize() << ").").str());

            if (image.columns() % 16 != 0)
                throw std::runtime_error("Width must be divisible by 16.");
        }

        if (*bitsPerPixel && (int)image.totalColors() > (1 << *bitsPerPixel))
            throw std::runtime_error((std::ostringstream() << "Too few bpp for " << image.totalColors() << " colours.").str());

        std::ofstream ofs(outputFilename, std::ofstream::binary);
        if (!ofs)
            throw std::runtime_error("Opening destination file failed.");

        save_header(ofs, image.columns(), image.rows());
        if (*paletteBits) {
            if (!*stCompatiblePalette)
                save_palette<FalconPaletteEntry>(ofs, image, 256);
            else
                save_palette<StePaletteEntry>(ofs, image, 16);
        }

        std::vector<uint8_t> atariImage(image.rows() * image.columns());
        if (!*bytesPerChunk) {
            c2p(atariImage, image);
        } else {
            // TODO
        }

        save_buffer(ofs, atariImage);

        ofs.close();
    }
    catch(std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "File " << outputFilename
              << " (" << image.columns() << "x" << image.rows() << "@" << image.totalColors() << ") "
              << "has been saved." << std::endl;

    return EXIT_SUCCESS;
}
