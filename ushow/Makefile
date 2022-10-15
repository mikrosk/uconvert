TARGET	:= ushow

LIBCMINI ?= yes

AS	:= vasm
CC	:= m68k-atari-mint-gcc

ASFLAGS	:= -quiet -Faout -opt-allbra
CFLAGS	:= -O2 -fomit-frame-pointer -Wall -std=c99
LDFLAGS	:= -s
#LDFLAGS := -g -Wl,--traditional-format
LDLIBS  := -lgem 

ifeq ($(LIBCMINI),yes)
LIBCMINI_ROOT := $(shell $(CC) -print-sysroot)/opt/libcmini
CFLAGS	:= -I$(LIBCMINI_ROOT)/include $(CFLAGS)
LDFLAGS	:= -nostdlib $(LIBCMINI_ROOT)/lib/crt0.o $(LDFLAGS) -L$(LIBCMINI_ROOT)/lib
LDLIBS	:= -lcmini -lgcc $(LDLIBS)
endif

all: $(TARGET).ttp

$(TARGET).ttp: $(TARGET)
	cp $< $@

$(TARGET): ushow.o bitmap_info.o load_bitmap.o screen_info.o screen-asm.o c2p1x1_4.o c2p1x1_6.o c2p1x1_8.o

.PHONY: clean
clean:
	rm -f $(TARGET).ttp $(TARGET) *.o *~
