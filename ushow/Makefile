TARGET	= ushow

AS	= vasm
CC	= m68k-atari-mint-gcc

ASFLAGS	= -quiet -Faout -opt-allbra
CFLAGS	= -O2 -fomit-frame-pointer -Wall -std=c99
LDFLAGS	= -s

all: $(TARGET).ttp

$(TARGET).ttp: $(TARGET)
	cp $< $@

$(TARGET): ushow.o screen-asm.o

.PHONY: clean
clean:
	rm -f $(TARGET).ttp $(TARGET) *.o *~
