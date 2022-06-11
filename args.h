#ifndef ARGS_H
#define ARGS_H

#include <cstdint>
#include <string>
#include <optional>

extern std::optional<uint16_t>   bitsPerPixel;         // 1, 2, 4, 8 (both planar and chunky); 15, 16, 24, 32 (chunky only) or 0 (if explicitly disabled)
extern std::optional<uint16_t>   bytesPerChunk;        // 1, 2, 3, 4 or 0 (if disabled)
extern std::optional<uint16_t>   paletteBits;          // 9, 12, 18, 24 or 0 (if bitsPerPixel > 8 or explicitly disabled)
extern std::optional<bool>       stCompatiblePalette;  // if true, use the ST/E palette registers
extern std::optional<bool>       packedChunks;         // if true, pack pixels into more than one chunk

extern std::string parse_arguments(int argc, char* argv[]);

#endif // ARGS_H
