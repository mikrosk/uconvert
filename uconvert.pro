TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        args.cpp \
        fp8.c \
        uconvert.cpp \
        uimg.cpp

# the per scanline palette conversion (fp8.c) is slow without optimisation
QMAKE_CFLAGS += -O2

#QMAKE_CXXFLAGS += $$system(GraphicsMagick++-config --cppflags --cxxflags)
QMAKE_CXXFLAGS += $$system(GraphicsMagick++-config --cppflags)
QMAKE_LFLAGS   += $$system(GraphicsMagick++-config --ldflags)
QMAKE_LIBS     += $$system(GraphicsMagick++-config --libs)

HEADERS += \
    args.h \
    bitfield.h \
    fp8.h \
    helpers.h \
    palette.h \
    uimg.h \
    version.h

DISTFILES += \
    Makefile
