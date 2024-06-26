TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        args.cpp \
        uconvert.cpp \
        uimg.cpp

#QMAKE_CXXFLAGS += $$system(GraphicsMagick++-config --cppflags --cxxflags)
QMAKE_CXXFLAGS += $$system(GraphicsMagick++-config --cppflags)
QMAKE_LFLAGS   += $$system(GraphicsMagick++-config --ldflags)
QMAKE_LIBS     += $$system(GraphicsMagick++-config --libs)

HEADERS += \
    args.h \
    bitfield.h \
    helpers.h \
    palette.h \
    uimg.h \
    version.h

DISTFILES += \
    Makefile
