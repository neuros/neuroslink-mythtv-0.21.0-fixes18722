include ( ../../config.mak )
include ( ../../settings.pro )

TEMPLATE = lib
TARGET = mythfreesurround-$$LIBVERSION
CONFIG += thread staticlib warn_off

INCLUDEPATH += ../libavcodec ../libmyth

#build position independent code since the library is linked into a shared library
QMAKE_CXXFLAGS += -fPIC -DPIC

QMAKE_CLEAN += $(TARGET) $(TARGETA) $(TARGETD) $(TARGET0) $(TARGET1) $(TARGET2)

# Input
HEADERS += el_processor.h
HEADERS += freesurround.h

SOURCES += el_processor.cpp
SOURCES += freesurround.cpp

contains( CONFIG_LIBFFTW3, yes ) {
    #required until its rewritten to use avcodec fft lib
    LIBS += -lfftw3f
    DEFINES += USE_FFTW3
} else {
    DEPENDPATH += ../libavcodec
    LIBS += -L../libavcodec -lavcodec
    INCLUDEPATH += ../libavutil
}

include ( ../libs-targetfix.pro )
