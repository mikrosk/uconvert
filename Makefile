TARGET	= uconvert

LINK.o   = $(LINK.cc)	# use $(CXX) for linking
CPPFLAGS += $(shell GraphicsMagick++-config --cppflags)
CXXFLAGS += -Wall -std=c++17 $(shell GraphicsMagick++-config --cxxflags)
CFLAGS   += -Wall -O2
LDFLAGS  += $(shell GraphicsMagick++-config --ldflags)
LDLIBS   += $(shell GraphicsMagick++-config --libs)

all: $(TARGET)

$(TARGET): args.o fp8.o uconvert.o uimg.o

.PHONY: clean
clean:
	rm -f $(TARGET) *.o *~
