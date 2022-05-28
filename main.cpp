#include <iostream>

#include <GraphicsMagick/Magick++.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>

#include "palette.h"

using namespace Magick;

static void save_header(std::ofstream& ofs, const Image& image)
{
    // ID
    ofs.write("8BPL", 4);
    // width
    const uint16_t width = image.columns();
    // save as big endian
    ofs.put(width >> 8);    // MSB
    ofs.put(width); // LSB
    // height
    const uint16_t height = image.rows();
    // save as big endian
    ofs.put(height >> 8);    // MSB
    ofs.put(height); // LSB
}

static void save_palette(std::ofstream& ofs, const Image& image, size_t colorMapSize)
{
    FalconPaletteEntry pal[256] = {};

    for (size_t i = 0; i < colorMapSize; ++i) {
        ColorRGB colorRgb(image.colorMap(i));

        pal[i].r = colorRgb.red() * 255;
        pal[i].g = colorRgb.green() * 255;
        pal[i].b = colorRgb.blue() * 255;
    }

    ofs.write((char*)pal, sizeof(pal));
}

static void save_buffer(std::ofstream& ofs, const uint8_t* pBuffer, size_t bufferSize)
{
    ofs.write((char*)pBuffer, bufferSize);
}

static void c2p(uint8_t* pBuffer, size_t bufferSize, Image& image)
{
    assert(image.columns() * image.rows() == bufferSize);

    const PixelPacket* pixelPackets = image.getPixels(0, 0, image.columns(), image.rows());
    (void)pixelPackets;
    const IndexPacket* pIndexPackets = image.getConstIndexes();

    for (size_t y = 0; y < image.rows(); ++y) {
        for (size_t x = 0; x < image.columns(); x += 16) {
            uint16_t planes[8] = {}; // 16 pixels, 8-bit depth

            for (size_t i = 0; i < 16; ++i) {
                uint8_t paletteIndex = pIndexPackets[y * image.columns() + x + i];

                planes[0] |= ((paletteIndex >> 0) & 0x01) << (15 - i);
                planes[1] |= ((paletteIndex >> 1) & 0x01) << (15 - i);
                planes[2] |= ((paletteIndex >> 2) & 0x01) << (15 - i);
                planes[3] |= ((paletteIndex >> 3) & 0x01) << (15 - i);
                planes[4] |= ((paletteIndex >> 4) & 0x01) << (15 - i);
                planes[5] |= ((paletteIndex >> 5) & 0x01) << (15 - i);
                planes[6] |= ((paletteIndex >> 6) & 0x01) << (15 - i);
                planes[7] |= ((paletteIndex >> 7) & 0x01) << (15 - i);
            }

            uint8_t* p = pBuffer + y * image.columns() + x;
            *p++ = planes[0] >> 8;
            *p++ = planes[0];
            *p++ = planes[1] >> 8;
            *p++ = planes[1];
            *p++ = planes[2] >> 8;
            *p++ = planes[2];
            *p++ = planes[3] >> 8;
            *p++ = planes[3];
            *p++ = planes[4] >> 8;
            *p++ = planes[4];
            *p++ = planes[5] >> 8;
            *p++ = planes[5];
            *p++ = planes[6] >> 8;
            *p++ = planes[6];
            *p++ = planes[7] >> 8;
            *p++ = planes[7];
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
        image.read("/home/mikro/projects/magick_test.git/illumn2.png");

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

        if (image.colorMapSize() != 256) {
            std::cerr << "Color map must have 256 entries" << std::endl;
            return EXIT_FAILURE;
        }

        std::ofstream ofs("/home/mikro/projects/magick_test.git/illumn2.8bp", std::ofstream::binary);
        if (!ofs) {
            std::cerr << "Opening destination file failed" << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << image.columns() << "x" << image.rows() << "@" << image.totalColors() << std::endl;

        save_header(ofs, image);
        save_palette(ofs, image, image.colorMapSize());

        const auto atariImageSize = image.rows() * image.columns();
        std::unique_ptr<uint8_t[]> atariImage(new uint8_t[atariImageSize]);

        c2p(atariImage.get(), atariImageSize, image);
        save_buffer(ofs, atariImage.get(), atariImageSize);

        ofs.close();
    }
    catch(std::exception& ex)
    {
        std::cerr << "Caught exception: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
