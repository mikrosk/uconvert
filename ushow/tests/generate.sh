#!/bin/bash -ex

ARGS="-width 480 -height 270 -filter -dither"

for pal in 9 12 18 24
do
	DIR="falcon.$pal"
	rm -rf "$DIR"
	mkdir "$DIR"

	uconvert $ARGS -bpp 1 -pal $pal -out "$DIR/test.bp1" test.webp
	uconvert $ARGS -bpp 4 -pal $pal -out "$DIR/test.bp4" test.webp
	uconvert $ARGS -bpp 6 -pal $pal -out "$DIR/test.bp6" test.webp
	uconvert $ARGS -bpp 8 -pal $pal -out "$DIR/test.bp8" test.webp

	uconvert $ARGS -bpp 1 -bpc 1 -pal $pal -out "$DIR/test.c01" test.webp
	uconvert $ARGS -bpp 4 -bpc 1 -pal $pal -out "$DIR/test.c04" test.webp
	uconvert $ARGS -bpp 6 -bpc 1 -pal $pal -out "$DIR/test.c06" test.webp
	uconvert $ARGS -bpp 8 -bpc 1 -pal $pal -out "$DIR/test.c08" test.webp

	uconvert $ARGS -bpp 1 -bpc -1 -pal $pal -out "$DIR/test-nib.c01" test.webp
	uconvert $ARGS -bpp 4 -bpc -1 -pal $pal -out "$DIR/test-nib.c04" test.webp
done

for pal in 9 12
do
	DIR="tt.$pal"
	rm -rf "$DIR"
	mkdir "$DIR"

	uconvert $ARGS -bpp 1 -pal $pal -tt -out "$DIR/test.bp1" test.webp
	uconvert $ARGS -bpp 2 -pal $pal -tt -out "$DIR/test.bp2" test.webp
	uconvert $ARGS -bpp 4 -pal $pal -tt -out "$DIR/test.bp4" test.webp
	uconvert $ARGS -bpp 6 -pal $pal -tt -out "$DIR/test.bp6" test.webp
	uconvert $ARGS -bpp 8 -pal $pal -tt -out "$DIR/test.bp8" test.webp

	uconvert $ARGS -bpp 1 -bpc 1 -pal $pal -tt -out "$DIR/test.c01" test.webp
	uconvert $ARGS -bpp 2 -bpc 1 -pal $pal -tt -out "$DIR/test.c02" test.webp
	uconvert $ARGS -bpp 4 -bpc 1 -pal $pal -tt -out "$DIR/test.c04" test.webp
	uconvert $ARGS -bpp 6 -bpc 1 -pal $pal -tt -out "$DIR/test.c06" test.webp
	uconvert $ARGS -bpp 8 -bpc 1 -pal $pal -tt -out "$DIR/test.c08" test.webp

	uconvert $ARGS -bpp 1 -bpc -1 -pal $pal -tt -out "$DIR/test-nib.c01" test.webp
	uconvert $ARGS -bpp 2 -bpc -1 -pal $pal -tt -out "$DIR/test-nib.c02" test.webp
	uconvert $ARGS -bpp 4 -bpc -1 -pal $pal -tt -out "$DIR/test-nib.c04" test.webp
done

for pal in 9 12
do
	DIR="st.$pal"
	rm -rf "$DIR"
	mkdir "$DIR"

	uconvert $ARGS -bpp 1 -pal $pal -st -out "$DIR/test.bp1" test.webp
	uconvert $ARGS -bpp 2 -pal $pal -st -out "$DIR/test.bp2" test.webp
	uconvert $ARGS -bpp 4 -pal $pal -st -out "$DIR/test.bp4" test.webp

	uconvert $ARGS -bpp 1 -bpc 1 -pal $pal -st -out "$DIR/test.c01" test.webp
	uconvert $ARGS -bpp 2 -bpc 1 -pal $pal -st -out "$DIR/test.c02" test.webp
	uconvert $ARGS -bpp 4 -bpc 1 -pal $pal -st -out "$DIR/test.c04" test.webp

	uconvert $ARGS -bpp 1 -bpc -1 -pal $pal -st -out "$DIR/test-nib.c01" test.webp
	uconvert $ARGS -bpp 2 -bpc -1 -pal $pal -st -out "$DIR/test-nib.c02" test.webp
	uconvert $ARGS -bpp 4 -bpc -1 -pal $pal -st -out "$DIR/test-nib.c04" test.webp
done

for bpp in 16 24 32
do
	DIR="bpp.$bpp"
	rm -rf "$DIR"
	mkdir "$DIR"

	uconvert -width 240 -height 135 -filter -bpp $bpp -out "$DIR/test-1.c$bpp" test.webp
	uconvert -width 480 -height 270 -filter -bpp $bpp -out "$DIR/test-2.c$bpp" test.webp
	uconvert -width 960 -height 540 -filter -bpp $bpp -out "$DIR/test-3.c$bpp" test.webp
	uconvert -width 1920 -height 1080 -filter -bpp $bpp -out "$DIR/test-4.c$bpp" test.webp
done
