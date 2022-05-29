#include <GraphicsMagick/Magick++.h>
using namespace Magick;

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <type_traits>
#include <vector>

#include "helpers.h"
#include "palette.h"

// defaults
static size_t chunkyBits  = 0;   // 4, 8, 16, 24, 32 or 0 (if disabled)
static size_t paletteBits = 24; // 9, 12, 18, 24 or 0 (if chunkyBits >= 16)
static size_t planesCount = 8;  // 1, 2, 4, 8 or 0 (if chunkyBits != 0)
static bool stCompatible  = false;  // if true, use the ST/E palette registers
// TODO: "convert" = convert into desired bit depth

static void save_header(std::ofstream& ofs, const Image& image)
{
    // ID
    ofs.put(planesCount + '0');
    ofs.write("BPL", 3);
    // width
    const uint16_t width = image.columns();
    ofs.put(width >> 8);    // MSB
    ofs.put(width); // LSB
    // height
    const uint16_t height = image.rows();
    ofs.put(height >> 8);    // MSB
    ofs.put(height); // LSB
}

template<typename T>
static void save_palette(std::ofstream& ofs, const Image& image)
{
    std::vector<T> pal(1 << planesCount);

    // yes, the getter is non-const, sigh...
    for (size_t i = 0; i < const_cast<Image&>(image).colorMapSize(); ++i) {
        ColorRGB colorRgb(image.colorMap(i));

        const uint r = colorRgb.red()   * ((1 << (paletteBits/3)) - 1);
        const uint g = colorRgb.green() * ((1 << (paletteBits/3)) - 1);
        const uint b = colorRgb.blue()  * ((1 << (paletteBits/3)) - 1);

        if constexpr (std::is_same_v<T, FalconPaletteEntry>) {
            switch (paletteBits) {
            case 24:
                pal[i].r10 = r & 0x03;
                pal[i].g10 = g & 0x03;
                pal[i].b10 = b & 0x03;
                [[fallthrough]];
            case 18:
                // shift by 0 (18-bit) or 2 (24-bit) bits
                pal[i].r765432 = r >> ((paletteBits - 18)/3);
                pal[i].g765432 = g >> ((paletteBits - 18)/3);
                pal[i].b765432 = b >> ((paletteBits - 18)/3);
                break;
            case 12:
            case 9:
                // no need to handle the 18- vs. 24-bit difference
                // as the bottom two bits are always zero
                pal[i].r765432 = r << (6 - paletteBits/3);
                pal[i].g765432 = g << (6 - paletteBits/3);
                pal[i].b765432 = b << (6 - paletteBits/3);
                break;
            default:
                throw std::invalid_argument(
                    (std::ostringstream()
                        << "Unexpected number of palette bits: " << paletteBits
                    ).str()
                );
            }

            ofs.put(pal[i].wrapper.value >> 24);    // MSB
            ofs.put(pal[i].wrapper.value >> 16);
            ofs.put(pal[i].wrapper.value >>  8);
            ofs.put(pal[i].wrapper.value);  // LSB
        } else if constexpr (std::is_same_v<T, StePaletteEntry>) {
            switch (paletteBits) {
            case 12:
                pal[i].r0 = r & 0x01;
                pal[i].g0 = g & 0x01;
                pal[i].b0 = b & 0x01;
                [[fallthrough]];
            case 9:
                // shift by 0 (9-bit ST) or 1 (12-bit STE) bits
                pal[i].r321 = r >> ((paletteBits - 9)/3);
                pal[i].g321 = g >> ((paletteBits - 9)/3);
                pal[i].b321 = b >> ((paletteBits - 9)/3);
                break;
            default:
                throw std::invalid_argument(
                    (std::ostringstream()
                        << "Unexpected number of palette bits: " << paletteBits
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

template<typename T>
static void c2p(std::vector<T>& buffer, const Image& image)
{
    assert(image.columns() * image.rows() == buffer.size());

    // unfortunately, we really need to call this one even if it's useless
    image.getConstPixels(0, 0, image.columns(), image.rows());
    const IndexPacket* pIndexPackets = image.getConstIndexes();

    for (size_t y = 0; y < image.rows(); ++y) {
        for (size_t x = 0; x < image.columns(); x += 16) {
            std::vector<uint16_t> planes(planesCount); // 16 pixels = 16 bits x bit depth

            for (size_t i = 0; i < 16; ++i) {
                uint8_t paletteIndex = pIndexPackets[y * image.columns() + x + i];

                for (size_t j = 0; j < planesCount; ++j) {
                    planes[j] |= ((paletteIndex >> j) & 0x01) << (15 - i);
                }
            }

            T* p = buffer.data() + y * image.columns() + x;
            for (size_t i = 0; i < planesCount; ++i) {
                *p++ = planes[i] >> 8;  // MSB
                *p++ = planes[i];   // LSB
            }
        }
    }
}

int main(int argc, char* argv[])
{
    //if (argc != 2)
    //    return EXIT_FAILURE;
    (void)argc;

    InitializeMagick(*argv);

    Image image;
    try {
        image.quiet(false);
        //image.read(argv[1]);
        image.read("/home/mikro/projects/magick_test/illumn2.png");

        if (image.type() != PaletteType) {
            std::cerr << "Not a palette type" << std::endl;
            return EXIT_FAILURE;
        }

        if (image.classType() != PseudoClass) {
            std::cerr << "Not a pseudo class" << std::endl;
            return EXIT_FAILURE;
        }

        if (image.columns() % 16 != 0) {
            std::cerr << "Width must be divisible by 16" << std::endl;
            return EXIT_FAILURE;
        }

        planesCount = 8;

        if (image.colorMapSize() > (size_t)(1 << planesCount)) {
            std::cerr << "Color map must have fewer than " << (1 << planesCount) << " entries" << std::endl;
            return EXIT_FAILURE;
        }

        std::ofstream ofs("/home/mikro/projects/magick_test/illumn2.8bp", std::ofstream::binary);
        if (!ofs) {
            std::cerr << "Opening destination file failed" << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << image.columns() << "x" << image.rows() << "@" << image.totalColors() << std::endl;

        save_header(ofs, image);
        if (chunkyBits <= 8) {  // that includes the disabled state
            if (!stCompatible)
                save_palette<FalconPaletteEntry>(ofs, image);
            else
                save_palette<StePaletteEntry>(ofs, image);
        }

        std::vector<uint8_t> atariImage(image.rows() * image.columns());
        c2p(atariImage, image);
        save_buffer(ofs, atariImage);

        ofs.close();
    }
    catch(std::exception& ex)
    {
        std::cerr << "Caught exception: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
