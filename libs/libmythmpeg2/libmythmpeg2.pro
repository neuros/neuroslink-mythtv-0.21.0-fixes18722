include ( ../../config.mak )
include ( ../../settings.pro )

TEMPLATE = lib
TARGET = mythmpeg2-$$LIBVERSION
CONFIG += thread staticlib warn_off

#build position independent code since the library is linked into a shared library
QMAKE_CFLAGS += -fPIC -DPIC -fno-common

QMAKE_CLEAN += $(TARGET) $(TARGETA) $(TARGETD) $(TARGET0) $(TARGET1) $(TARGET2)

# Input
HEADERS += attributes.h vlc.h mpeg2_internal.h mpeg2.h

SOURCES += cpu_accel.c cpu_state.c \
           alloc.c header.c decode.c slice.c \
           motion_comp.c idct.c

inc.path = $${PREFIX}/include/mythtv/mpeg2dec/
inc.files = mpeg2.h

INCLUDEPATH += ../../libs/libavcodec

INSTALLS += inc

contains( HAVE_ALTIVEC, yes ) {
    SOURCES += motion_comp_altivec.c idct_altivec.c
}
contains( HAVE_MMX, yes ) {
    HEADERS += ../../libs/libavcodec/i386/mmx.h ../../libs/libavcodec/dsputil.h
    SOURCES += motion_comp_mmx.c idct_mmx.c
}
contains( ARCH_SPARC, yes ) {
    HEADERS += vis.h
    SOURCES += motion_comp_vis.c
}
contains( ARCH_ALPHA, yes ) {
    HEADERS += alpha_asm.h
    SOURCES += motion_comp_alpha.c idct_alpha.c
}

include ( ../libs-targetfix.pro )
