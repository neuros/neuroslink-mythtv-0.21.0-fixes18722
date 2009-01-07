include ( ../../config.mak )
include ( ../../settings.pro )
include ( ../programs-libs.pro )

TEMPLATE = app
CONFIG += thread
TARGET = mythtv
target.path = $${PREFIX}/bin
INSTALLS = target

QMAKE_CLEAN += $(TARGET)

# Input
SOURCES += main.cpp

macx {
    mac_bundle {
        QMAKE_POST_LINK = ../../contrib/OSX/makebundle.sh mythtv
    }
}

using_x11:DEFINES += USING_X11
