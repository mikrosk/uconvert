TARGET	= ushow

AS	= vasm
CC	= m68k-atari-mint-gcc

ASFLAGS	= -quiet -Faout -opt-allbra
CFLAGS	= -O2 -fomit-frame-pointer -Wall -std=c99
LDFLAGS	= -s
#LDFLAGS = -g -Wl,--traditional-format

all: $(TARGET).ttp

$(TARGET).ttp: $(TARGET)
	cp $< $@

$(TARGET): ushow.o screen-asm.o c2p1x1_4.o c2p1x1_6.o c2p1x1_8.o

.PHONY: clean
clean:
	rm -f $(TARGET).ttp $(TARGET) *.o *~
