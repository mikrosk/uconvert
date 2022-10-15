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

#include "uimg.h"

#include <cassert>
#include <cstring>
#include <cstdint>
#include <fstream>

#include "helpers.h"
#include "palette.h"

bool is_chunky_uimg(const std::string& filePath)
{
    std::ifstream ifs(filePath, std::ifstream::binary);
    ifs.exceptions(std::ifstream::failbit);

    FileHeader fileHeader;
    ifs.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));

    fileHeader.version = (fileHeader.version >> 8) | (fileHeader.version << 8);
    fileHeader.flags   = (fileHeader.flags >> 8)   | (fileHeader.flags << 8);

    return strcmp(fileHeader.id, "UIMG") == 0
            && fileHeader.bitsPerPixel != 0
            && (fileHeader.bytesPerChunk != 0 || fileHeader.bitsPerPixel == 1)
            && (fileHeader.bitsPerPixel > 8 || (fileHeader.flags & 0b11) != 0b00);
}

Magick::Image load_uimg(const std::string& filePath)
{
    std::ifstream ifs(filePath, std::ifstream::binary);
    ifs.exceptions(std::ifstream::failbit);

    FileHeader fileHeader;
    ifs.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));

    fileHeader.version = (fileHeader.version >> 8) | (fileHeader.version << 8);
    fileHeader.flags   = (fileHeader.flags >> 8)   | (fileHeader.flags << 8);

    uint16_t width = 0;
    width |= ifs.get();
    width <<= 8;
    width |= ifs.get();

    uint16_t height = 0;
    height |= ifs.get();
    height <<= 8;
    height |= ifs.get();

    if (fileHeader.bytesPerChunk == 0 && fileHeader.bitsPerPixel == 1)
        fileHeader.bytesPerChunk = -1;

    Magick::Image image({width, height}, {0, 0, 0});

    if (fileHeader.bitsPerPixel <= 8) {
        image.classType(Magick::PseudoClass);
        image.type(Magick::PaletteType);

        image.colorMapSize(1 << fileHeader.bitsPerPixel);

        for (size_t i = 0; i < image.colorMapSize(); ++i) {
            Magick::Color color;

            switch (fileHeader.flags & 0b11) {
            case 0b01: {
                // ST/E compatible palette
                StePaletteEntry palEntry = {};
                palEntry.wrapper.value |= ifs.get();
                palEntry.wrapper.value <<= 8;
                palEntry.wrapper.value |= ifs.get();

                constexpr size_t shift = QuantumDepth - (3+1);  // 3+1 bits per channel
                color.redQuantum(   ((palEntry.r321 << 1) | palEntry.r0) << shift );
                color.greenQuantum( ((palEntry.g321 << 1) | palEntry.g0) << shift );
                color.blueQuantum(  ((palEntry.b321 << 1) | palEntry.b0) << shift );
            } break;

            case 0b10: {
                // TT compatible palette
                TtPaletteEntry palEntry = {};
                palEntry.wrapper.value |= ifs.get();
                palEntry.wrapper.value <<= 8;
                palEntry.wrapper.value |= ifs.get();

                constexpr size_t shift = QuantumDepth - 4;  // 4 bits per channel
                color.redQuantum(   palEntry.r3210 << shift );
                color.greenQuantum( palEntry.g3210 << shift );
                color.blueQuantum(  palEntry.b3210 << shift );
            } break;

            case 0b11: {
                // Falcon compatible palette
                FalconPaletteEntry palEntry = {};
                palEntry.wrapper.value |= ifs.get();
                palEntry.wrapper.value <<= 8;
                palEntry.wrapper.value |= ifs.get();
                palEntry.wrapper.value <<= 8;
                palEntry.wrapper.value |= ifs.get();
                palEntry.wrapper.value <<= 8;
                palEntry.wrapper.value |= ifs.get();

                constexpr size_t shift = QuantumDepth - 8;  // 8 bits per channel
                color.redQuantum(   ((palEntry.r765432 << 2) | palEntry.r10) << shift );
                color.greenQuantum( ((palEntry.g765432 << 2) | palEntry.g10) << shift );
                color.blueQuantum(  ((palEntry.b765432 << 2) | palEntry.b10) << shift );
            } break;

            default:
                throw_oss<std::invalid_argument>(std::ostringstream()
                    << "Unexpected palette type: " << (fileHeader.flags & 0b11)
                );
            }

            image.colorMap(i, color);
        }
    } else {
        image.classType(Magick::DirectClass);
        image.type(Magick::TrueColorType);
    }

    Magick::PixelPacket*       pPixelPackets = image.getPixels(0, 0, image.columns(), image.rows());
    const Magick::PixelPacket* pPixelPacketsEnd = pPixelPackets + image.columns() * image.rows();

    Magick::IndexPacket*       pIndexPackets = image.getIndexes();
    const Magick::IndexPacket* pIndexPacketsEnd = pIndexPackets + image.columns() * image.rows();

    while ((fileHeader.bytesPerChunk >= 2 && pPixelPackets != pPixelPacketsEnd)
           || (fileHeader.bytesPerChunk < 2 && pIndexPackets != pIndexPacketsEnd)) {
        switch (fileHeader.bytesPerChunk) {
        case -1: {
            uint8_t chunk = ifs.get();

            int shift = 8 - fileHeader.bitsPerPixel;
            while (shift >= 0) {
                *pIndexPackets++ = (chunk >> shift) & ((1 << fileHeader.bitsPerPixel) - 1);
                shift -= fileHeader.bitsPerPixel;
            };
            break;
        }
        case 1:
            *pIndexPackets++ = ifs.get();
            break;

        case 2: {
            uint16_t rgb565 = 0;
            rgb565 |= (ifs.get() << 8) & 0xff00;
            rgb565 |= ifs.get() & 0x00ff;

            pPixelPackets->red   = ((rgb565 >> (6+5)) & 0x1f) << (QuantumDepth - 5);
            pPixelPackets->green = ((rgb565 >> 5)     & 0x3f) << (QuantumDepth - 6);
            pPixelPackets->blue  = (rgb565            & 0x1f) << (QuantumDepth - 5);

            pPixelPackets++;
            break;
        }
        case 4:
            pPixelPackets->opacity = ifs.get();
            [[fallthrough]];
        case 3:
            pPixelPackets->red   = ifs.get() << (QuantumDepth - 8);
            pPixelPackets->green = ifs.get() << (QuantumDepth - 8);
            pPixelPackets->blue  = ifs.get() << (QuantumDepth - 8);

            pPixelPackets++;
            break;

        default:
            throw_oss<std::invalid_argument>(std::ostringstream()
                << "Unexpected number of bytes per chunk: " << fileHeader.bytesPerChunk
            );
        }
    }

    image.syncPixels();

    return image;
}
