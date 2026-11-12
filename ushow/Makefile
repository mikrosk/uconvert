TARGET	:= ushow

LIBCMINI ?= yes

AS	:= vasmm68k_mot
CC	:= m68k-atari-mintelf-gcc

ASFLAGS	:= -quiet -Felf -opt-allbra
CFLAGS	:= -O2 -fomit-frame-pointer -Wall -std=c99
LDFLAGS	:= -s
#LDFLAGS := -g -Wl,--traditional-format
LDLIBS  := -lgem

COBJS	:= ushow.o bitmap_info.o load_bitmap.o screen_info.o
SOBJS	:= screen-asm.o linepal-asm.o c2p1x1_4.o c2p1x1_6.o c2p1x1_8.o

# 'make TEST=1' builds ushowt.ttp for unattended tests: every bitmap is shown for
# 300 frames without waiting for keys, then the program exits; a watchdog resets
# the machine when the program stops making progress
ifeq ($(TEST),1)
TARGET	:= ushowt
CFLAGS	+= -DTEST
COBJS	:= $(COBJS:.o=.t.o)
SOBJS	+= watchdog-asm.o
endif

ifeq ($(LIBCMINI),yes)
LIBCMINI_ROOT := $(shell $(CC) -print-sysroot)/opt/libcmini
CFLAGS	:= -I$(LIBCMINI_ROOT)/include $(CFLAGS)
LDFLAGS	:= -nostdlib $(LIBCMINI_ROOT)/lib/crt0.o $(LDFLAGS) -L$(LIBCMINI_ROOT)/lib
LDLIBS	:= -lgcc -lcmini -lgcc $(LDLIBS)
endif

all: $(TARGET).ttp

$(TARGET).ttp: $(TARGET)
	cp $< $@

$(TARGET): $(COBJS) $(SOBJS)
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@

%.t.o: %.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

.PHONY: clean
clean:
	rm -f ushow.ttp ushow ushowt.ttp ushowt *.o *~
