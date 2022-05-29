#include <GraphicsMagick/Magick++.h>
using namespace Magick;

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "palette.h"

static int planesCount = -1;

template<typename T>
static size_t sizeof_vector(const std::vector<T>& vec)
{
    return sizeof(T) * vec.size();
}

static void save_header(std::ofstream& ofs, const Image& image)
{
    // ID
    ofs.put(planesCount + '0');
    ofs.write("BPL", 3);
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

static void save_palette(std::ofstream& ofs, const Image& image)
{
    std::vector<FalconPaletteEntry> pal(1 << planesCount);

    // yes, the getter is non-const, sigh...
    for (size_t i = 0; i < const_cast<Image&>(image).colorMapSize(); ++i) {
        ColorRGB colorRgb(image.colorMap(i));

        pal[i].r = colorRgb.red() * 255;
        pal[i].g = colorRgb.green() * 255;
        pal[i].b = colorRgb.blue() * 255;
    }

    ofs.write((char*)pal.data(), sizeof_vector(pal));
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

                for (int j = 0; j < planesCount; ++j) {
                    planes[j] |= ((paletteIndex >> j) & 0x01) << (15 - i);
                }
            }

            T* p = buffer.data() + y * image.columns() + x;
            for (int i = 0; i < planesCount; ++i) {
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

        if (image.colorMapSize() != 256) {
            std::cerr << "Color map must have 256 entries" << std::endl;
            return EXIT_FAILURE;
        }

        planesCount = 8;

        std::ofstream ofs("/home/mikro/projects/magick_test/illumn2.8bp", std::ofstream::binary);
        if (!ofs) {
            std::cerr << "Opening destination file failed" << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << image.columns() << "x" << image.rows() << "@" << image.totalColors() << std::endl;

        save_header(ofs, image);
        save_palette(ofs, image);

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
