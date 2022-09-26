TARGET	= uconvert

LINK.o   = $(LINK.cc)	# use $(CXX) for linking
CPPFLAGS = -O2 -Wall -std=c++17 $(shell GraphicsMagick++-config --cppflags)
LDFLAGS  = $(shell GraphicsMagick++-config --ldflags)
LDLIBS   = $(shell GraphicsMagick++-config --libs)

all: $(TARGET)

$(TARGET): args.o uconvert.o

.PHONY: clean
clean:
	rm -f $(TARGET) *.o *~
