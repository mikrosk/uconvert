TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.cpp

#QMAKE_CXXFLAGS += $$system(GraphicsMagick++-config --cppflags --cxxflags)
QMAKE_CXXFLAGS += $$system(GraphicsMagick++-config --cppflags)
QMAKE_LFLAGS   += $$system(GraphicsMagick++-config --ldflags)
QMAKE_LIBS     += $$system(GraphicsMagick++-config --libs)
