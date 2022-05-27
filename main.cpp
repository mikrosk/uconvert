#include <iostream>

#include <GraphicsMagick/Magick++.h>

using namespace std;
using namespace Magick;

int main(int argc, char* argv[])
{
    //if (argc != 2)
    //    return EXIT_FAILURE;

    InitializeMagick(*argv);

    Image image;
    try {
        image.quiet(false);
        //image.read(argv[1]);
        image.read("/home/mikro/office.png");

        if (image.type() != PaletteType) {
            cerr << "Not a palette type" << endl;
            return EXIT_FAILURE;
        }

        if (image.classType() != PseudoClass) {
            cerr << "Not a pseudo class" << endl;
            return EXIT_FAILURE;
        }

        // MaxColormapSize
        cout << "Colormap size: " << image.colorMapSize() << endl;
        for (size_t i = 0; i < image.colorMapSize(); ++i) {
            ColorRGB colorRgb(image.colorMap(i));

//            cout << "Color #" << i << ": ("
//                 << colorRgb.red() * 255 << ", "
//                 << colorRgb.green() * 255 << ", "
//                 << colorRgb.blue() * 255 << ")" << endl;
        }

        for (size_t i = 0; i < image.rows(); ++i) {
            for (size_t j = 0; j < image.columns(); ++j) {
                PixelPacket pixelPacket = *image.getPixels(j, i, 1, 1);

                IndexPacket indexPacket = *image.getConstIndexes();
                Color c = image.colorMap(indexPacket);

                if (pixelPacket.red != c.redQuantum()
                        || pixelPacket.green != c.greenQuantum()
                        || pixelPacket.blue != c.blueQuantum()) {
                    cerr << "Bad value" << endl;
                    return EXIT_FAILURE;
                }
            }
        }
        cout << image.columns() << "x" << image.rows() << "@" << image.totalColors() << endl;
    }
    catch(exception& ex)
    {
        cerr << "Caught exception: " << ex.what() << endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
