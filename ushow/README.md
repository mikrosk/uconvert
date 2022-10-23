uShow - Atari ST/STE/TT/Falcon-specific bitmap viewer
=====================================================

## Motivation

When I was working on [uConvert](https://github.com/mikrosk/uconvert) I had noticed that I had no easy way to test its output except some quickly hacked routines. But how about images bigger/smaller than the screen size? How about checking correct palette format written? How about comparing ST vs. STE, Falcon vs. SuperVidel? Oh and how about testing SuperVidel formats in general? How about testing chunky output?

I had quickly realised that I needed a visual tool for testing uConvert's output and generally, I liked the idea of having custom image viewer with features which would allow me to see results without using them in my demo engine or whatever. And most importantly: that I would have the certainty that what I see in my viewer would be 100% the same of what I would see in my custom code (i.e. no further palette/bitmap manipulation will be needed). 

## Features

While uShow can look like a simple stupid tool, it actually hides a lot under its hood:

- works on every Atari in every screen resolution
- full support for every Atari machine's screen format (native formats for the ST, STE, TT, Falcon, SuperVidel) and palette; no conversion takes place
- detection of current machine's video possibilities and loading only native formats (this is a feature, not a bug! :))
- automatic C2P rendering for 4, 6 and 8 bpp
- slideshow for any number of images (only restricted by the amout of free memory available): useful for comparing different bit depths / palette settings
- compatible with all operating systems, memory protection friendly
- emulation of ST modes on TT and Falcon
- native SuperVidel output: extended modes, 24-bit palette, chunky modes; possible side by side comparison Videl (VGA) vs. SuperVidel (DVI)
- able to display images smaller/bigger than screen size

## Usage

Drag&drop any number of images on `ushow.ttp`. Warning: TOS desktop doesn't accept more than one argument, if you want more, you have to type them manually when double-clicking on the app or use a modern desktop like Teradesk.

`Left`/`Right` arrow: switch back and forth between loaded images

`ESC`: exit to desktop

A few words about the ST emulation. Not everyone knows that while ST registers are quite compatible in both TT and Falcon (incl. the 3rd bit oddity from STE palette), TT palette is different - its entries are 16-bit same as STE but there's 3-2-1-0 bit order for each channel (in comparison to STE's 0-3-2-1) and this configuration isn't emulated even on Falcon. That is why TT palette is possible to show only on TT and Falcon palette only on Falcon even though both offer 256 palette registers.

However STE palette is emulated on both TT and Falcon, hardware registers are the same, too. Therefore uShow can display ST/E picture natively, without any further conversion. An interesting situation happens on the Falcon: should uShow keep also the ST Low/Mid/High dimensions or show them all in 320x2[04]0 / 640x4[08]0 ? I chose the former, so if you prepare a picture in ST Mid, it will get displayed identically on all machines.

## Screen dimensions

ST/E and TT machines do not offer much flexibility -- each bit depth is defined by its screen dimensions (ST/E and TT resolutions are treated separately, see above) so images must be adapted to those hard limits. If the image is smaller, it is centered on the screen (both vertically and horizontally); if it is bigger, only the top-left portion of the image is shown (so 1920x1080 doesn't occupy megabytes of memory but just the screen size space; this may change in the future though).

Falcon offers way more flexibility; in theory ushow could adapt to basically any image size loaded. However I chose to work only with "OS defined" resolutions, i.e. the resolutions you can set in the desktop. One argument is system compatibility, i.e. if your Centurbo II/Nemesis/Phantom overrides XBIOS video functions, ushow will use them. Another argument is my laziness. ;)

The algorithm goes like this for Falcon-only resolutions:
1. If SM124 or compatible => set 640x400
2. Else if RGB/TV => set 320x200
3. Else (VGA) => set 320x240
4. If image width or height is bigger than screen size and we are on RGB/TV => try overscan (size x 1.2 in both directions)
5. If image width or height is bigger than screen size and we are not on VGA with 16bpp and overscan is not enough or not applicable => try double size (in both directions)
6. If image width or height is bigger than screen size and we are on RGB/TV and double size is not anough => try overscan (size x 2 x 1.2 in both directions)
7. If this is still not enough either cut the image so only the upper-left corner is shown
8. Or if SuperVidel is present, try one-by-one its system resolutions, regardless of bit depth: 640x480, 800x600, 1024x768, 1280x720, 1280x1024, 1680x1050, 1600x1200, 1920x1080.

It's not ideal but I for now I'm happy with its current state. Also, as you can see, uShow is a nice showcase of how to set various Falcon resolutions, esp. on SuperVidel.

## Known bugs

- When setting ST High on TT and Falcon, writes into ST palette are ignored. On Falcon, palette registers $ffff9800 and $ffff9804 seems to be used instead.

- When having SuperVidel isntalled, in certain situations uShow doesn't return to proper desktop resolution: https://www.atari-forum.com/viewtopic.php?p=439510#p439510
