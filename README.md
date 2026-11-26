
uConvert - bitmap converter into Atari ST/STE/TT/Falcon-specific formats
========================================================================

## Motivation

When it comes to helper utilities, coders are usually not very keen to release them. Or to create them in the first place. ;) So when I needed to process some graphics on ST/Falcon, my workflow would be something like:
- is it ST graphics? => google (yet again) that tool which I vaguely remember that outputs .PI1 and .NEO images (XnConvert was it?)
- is it Falcon bitplane graphics? => convert it into GIF, load in Pixart, save as .PIX
- is it Falcon hicolour graphics? => convert it into TGA (first find a tool which still outputs uncompressed TGA...), load in EscPaint, save as .TRP

And yes, every time I had to modify the source graphics, I went through this again and again. IIRC at one point I did code a TGA -> TRP converter but that was it. But the older we get, the less time we have for such perks... I tried to find something to ease my pain but (un)surprisingly except the ST-oriented [PNGconv](https://github.com/troed/PNGconv) by Troed and [MI-3](https://www.mirari.fr/file/browse?f=913&u=506) by Rockyone ([AF thread](https://www.atari-forum.com/viewtopic.php?t=28191)) there was virtually nothing.

So I told myself: ok, let's create a _really nice_ tool which will feature all the things I've been missing so much over the years! Namely:
- store arbitrary sizes, not only screen sizes (applies mainly to ST graphics)
- support every Atari machine's bitmap formats
- swich to/experiment with various palette sizes and screen modes
- import as many formats as possible with some common (C/C++) API
- keep original bit depth without any hidden implicit conversions
- offer optional resizing/filtering/dithering but offer also a way to keep the original format/bit depth
- import its own format so one can do two-way conversions (i.e. with bitmap data generated in assembler)
- easy to extend
- multiplatform

After some research, I came up with [GraphicsMagick](http://www.graphicsmagick.org) (a fork of well-known [ImageMagick](https://imagemagick.org)). There's a plenty of documentation (for both projects which are interchangeable) and a simple (but definitely not very elegant) [C++ API](http://www.graphicsmagick.org/Magick++/index.html).

Please note that this is mostly a converter (it has it in its name!), so if you are looking for a tool to get best possible visual results, try some of the more specialised tools:
- Zerkman's [MPP](https://zerkman.sector1.fr/index.php?post/2013/12/10/MPP-version-1.1)
- dml's [PhotoChrome](http://www.leonik.net/dml/sec_pcs.py) and [CryptoChrome](http://www.leonik.net/dml/sec_crypto.py) ([AF thread](https://www.atari-forum.com/viewtopic.php?t=24166), [Bitbucket repository](https://bitbucket.org/d_m_l/agtools/wiki/CryptoChrome))
- Anima's [Image Color Remapping Tool](http://tool.anides.de/remap.html) ([AF thread](https://www.atari-forum.com/viewtopic.php?t=42128)), [Experimental Atari STE image enhancement website](http://atari.anides.de) ([AF thread](https://www.atari-forum.com/viewtopic.php?t=26462) and [AF post](https://www.atari-forum.com/viewtopic.php?p=250595#p250595)), [Retro Image Tool](http://tool.anides.de) ([AF thread](https://www.atari-forum.com/viewtopic.php?t=27556), [AF thread](https://www.atari-forum.com/viewtopic.php?t=29370), [AF thread](https://www.atari-forum.com/viewtopic.php?t=29314)) and [Image converter for the Atari Falcon high colour mode](http://tc.anides.de)
- cyg's [ST hicolor video encoder & player](https://www.pouet.net/prod.php?which=63527)
- ppera's [page about movie playback](https://atari.8bitchip.info/movpst.php)
-  DecAns's [DaDither image converter](https://www.dadither.com)

## Building

uConvert is distributed in source form only, sorry. For Linux and Mac users that perhaps makes sense (it's a command line tool after all) but for possible Windows users I'd recommend installing either a Virtual Machine + Ubuntu or even better, Microsoft's very own [WSL](https://learn.microsoft.com/en-us/windows/wsl). In theory it is possible to build uConvert as a native Windows binary but that requires some non-trivial fun with [GraphicsMagick's Windows installation](http://www.graphicsmagick.org/INSTALL-windows.html). Patches are welcome. :)

Then it's really simple (assuming you have `g++` and `make` already installed):

`sudo apt install libgraphicsmagick++1-dev` (Ubuntu)

`sudo pacman -S graphicsmagick` (Arch Linux)

`make`

There are also project files for Qt Creator available but you don't really need them.

## Usage

uConvert offers a quick summary every time you enter an uknown option but it's better to explain in more detail here. Options can go in random order, only the source bitmap must be the last. Also, all options offer some sane defaults.

### `-width <num>` & `-height <num>`
Resize input bitmap to given dimensions. Aspect ratio is **not** preserved but a warning message is printed if it has changed. Resizing takes `-filter` switch into account. It is possible to enter just one dimension, the other one is taken from source bitmap (same as entering value of `-1`).

Setting either of them to `0` stores just a palette and no bitmap data. `-bpp` still says how many entries it has (`1<<bpp`, i.e. 2, 4, 16, 64 or 256).

### `-filter`
Filter (smoothen) edges when resizing. This usually leads to increased number of colours and an implicit colour conversion process takes place. This conversion can be altered using `-dither`.

### `-dither`
For output bitmap with 1 - 8 bpp an implicit conversion takes place if source bitmap has more colours than `1<<bpp` (a bitmap with few enough indexed colours keeps them, indices included). uConvert computes the palette itself: every colour is the one of the whole palette grid (e.g. all 262144 18-bit colours) which gives the lowest error with the other colours fixed, see Arnaud Carré's [Brute Force Colors!](https://arnaud-carre.github.io/2022-12-30-amiga-ham/).

Without `-dither` every pixel takes the nearest colour. With `-dither` the picture is optimised as the eye sees it: the error is measured on the bitmap and the source both blurred slightly, brightness blurred less than colour, as the eye forgives colour noise more easily; every pixel takes the colour which lowers that error most (given its neighbours) and the colours follow, starting from Floyd-Steinberg error diffusion. Whether that looks better than no dithering depends on the picture and on the number of colours (with 16 colours it often does not), so it is worth trying both. It takes from seconds to minutes.

Note: 16, 24 and 32 bpp bitmaps are never converted, individual pixels are just stored with as many bits as possible.

### `-bpp <num>`
Bits per pixel in destination bitmap. `1` - `8` can be stored both in bitplane and chunky formats, `16` - `32` in chunky only. `16` uses Falcon hicolour RGB565 format.

### `-bpc <num>`
Bytes per chunk in destination bitmap. `0` means generating bitplane data, `1` - `4` means chunky data of 1, 2, 3 or 4 bytes per pixel (default for bpp > 8). `-1` means a special packed chunky mode, where pixels are stored as dense as possible, i.e. for 2 bpp it would be `0bAABBCCDD` per byte (instead of `0b000000AA`, `0b000000BB`, `0b000000CC`, `0b000000DD` with `-bpc 1`).

### `-pal <num>`
Number of bits for each palette entry. Palette generation can be disabled using `0` (i.e. only header & bitmap data would be stored). By default, palette is exported in Falcon palette format (`RRRRRRrr GGGGGGgg 00000000 BBBBBBbb`), this can be changed for 9- and 12-bit palette using `-st` and `-tt` respectively.

### `-st`
Store 9- and 12-bit palette in 16-bit ST/E palette format (`0000 rRRR gGGG bBBB`).

### `-tt`
Store 9- and 12-bit palette in 16-bit TT palette format (`0000 RRRR GGGG BBBB`).

### `-vdi`
Store the palette the way `vq_color()` returns it: three 16-bit words per entry, each `0` - `1000`. VDI pen order instead of by colour index.

### `-linecol <num>`
Number of palette entries (`1` - `10`) which get new colours before every line, `0` (default) keeps one palette for the whole bitmap. Before each line a block of `<num>` consecutive entries is rewritten (one `movem.l` for a raster routine), uConvert chooses where the block starts on each line; for example, `-bpp 8 -linecol 8` gives up to 256 + 8 * (height - 1) colours in the bitmap.

Works only with 4 and 8 bits per pixel, a Falcon palette (no `-st`, `-tt` nor `-vdi`) and an Atari bitmap (`.bpX` or `.cXX`) as output. The block starts are searched for the lowest error (every start of every line), so a 640x480 bitmap takes tens of minutes.

### `-out <filename.ext>`
Export source bitmap as `<filename.ext>`. If `<ext>` is the one the Atari switches would produce anyway, an Atari bitmap is written, otherwise the format is taken from `<ext>` - all popular formats like GIF, JPEG, PNG, WEBP, ... [whatever GraphicsMagick supports](http://www.graphicsmagick.org/formats.html); Atari switches are then ignored (but still validated) and only resizing/dithering is applied. Useful for reading uConvert's native Atari formats and displaying on the host platform but usable as a generic bitmap converter, too.

## UIMG Bitmap format

All values are stored in big endian format.

```c++
// 'UIMG' (4 bytes)
char        id[4];
// 0xAABB (AA = major, BB = minor, 2 bytes)
uint16_t    version;
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
uint16_t    flags;
// 1, 2, 4, 6, 8, 16, 24, 32
uint8_t     bitsPerPixel;
// -1, 0, 1, 2, 3, 4
int8_t      bytesPerChunk;
// present only with a multiline palette (version 0x0300): entries
// rewritten before every line, 1 .. (1<<bitsPerPixel)
uint8_t     changedColorsPerLine;
// present only with a multiline palette: 0, keeps the rest word aligned
uint8_t     pad;

// in pixels, 0 if there is no bitmap data
uint16_t    width;
// in pixels, 0 if there is no bitmap data
uint16_t    height;

// (1<<bitsPerPixel) palette entries, present only if flags & 0b111 != 0b000
union {
  uint16_t stePaletteEntry;
  uint16_t ttPaletteEntry;
  uint32_t falconPaletteEntry;
  uint16_t vdiPaletteEntry[3];
} Palette[1<<bitsPerPixel];

// present only with a multiline palette: the block of entries rewritten
// before each line, entries firstEntry .. firstEntry+changedColorsPerLine-1
// (same entry type as Palette); the entries keep their colours until a later
// block rewrites them; line 0's block is entries 0 .. changedColorsPerLine-1
// as in Palette
struct {
  uint16_t firstEntry;
  Palette  entries[changedColorsPerLine];
} linePalettes[height];

// present only if neither width nor height is 0
char* bitmapData;
```

For example of UIMG handling, see [ushow](https://github.com/mikrosk/uconvert/tree/master/ushow).

## Examples

- `uconvert -width 320 -height 200 -bpp 4 -pal 12 test.png` outputs `test.bp4` in native ST Low format using STE's 12-bit palette
- `uconvert -width 320 -height 240 -bpp 8 -bpc 1 -pal 24 test.png` outputs `test.c08` in 8-bit (SuperVidel compatible) chunky format using SuperVidel's 24-bit palette
- `uconvert -width 320 -height 240 -filter -bpp 16 test.png` outputs `test.c16` in 16-bit (Falcon compatible) chunky format with smoothened edges
- `uconvert -width 640 -height 480 -bpp 8 -pal 18 -linecol 8 test.png` outputs `test.bp8` in 8 bitplanes with 18-bit Falcon palette in which a block of 8 entries is rewritten before every line
- `uconvert -width 640 -height 400 -out test.gif test.bp4` outputs `test.gif` in twice as big dimensions as the original `test.bp4` from above
- `uconvert -out test.bp8 test.bp4` outputs `test.bp8` with `test.bp4` stored in 8 bitplanes
- asm-generated bitmap (save as `vasm -Fbin -o img.c04 img.asm`):
```m68k
        dc.b    'UIMG'          ; id
        dc.w    $0100           ; v1.0
        dc.w    %11             ; falcon palette present
        dc.b    4               ; 4 bpp
        dc.b    1               ; 1 byte per chunk
        dc.w    304             ; width
        dc.w    150             ; height

        REPT    16              ; use 'REPTN' (0-16) as blue in
        dc.l    128+REPTN*4     ; RRRRRRrr GGGGGGgg 00000000 BBBBBBbb
        ENDR

        ; 150 lines
        REPT    150

        dc.b    0,0

        ; 300 pixels
        REPT    10
        ; 30 pixels
        dc.b    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1
        ENDR

        ; pad with 2 pixels (must be divisible by 16!)
        dc.b    0,0

        ENDR    ; REPT 150

```
Save as a Falcon 4bpp bitplane bitmap: `uconvert -bpp 4 img.c04`

By the way, if you are looking for an easy way how to create gradients for the Atari, see [Gradient Blaster](https://github.com/grahambates/gradient-blaster). It supports all ST/STE/TT/Falcon/SuperVidel colour formats and exports to a directly usable output.
