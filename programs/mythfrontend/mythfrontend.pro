include ( ../../config.mak )
include ( ../../settings.pro )
include ( ../../version.pro )
include ( ../programs-libs.pro )

TEMPLATE = app
CONFIG += thread
TARGET = mythfrontend
target.path = $${PREFIX}/bin
INSTALLS = target

setting.path = $${PREFIX}/share/mythtv/
setting.files += theme.txt mysql.txt
setting.files += info_menu.xml mainmenu.xml media_settings.xml tv_schedule.xml
setting.files += util_menu.xml info_settings.xml main_settings.xml
setting.files += recpriorities_settings.xml tv_search.xml tv_lists.xml
setting.files += library.xml manage_recordings.xml optical_menu.xml tvmenu.xml
setting.files += tv_settings.xml
setting.extra = -ldconfig

INSTALLS += setting

QMAKE_CLEAN += $(TARGET)

# Input
HEADERS += manualbox.h playbackbox.h viewscheduled.h globalsettings.h
HEADERS += manualschedule.h programrecpriority.h channelrecpriority.h
HEADERS += statusbox.h networkcontrol.h custompriority.h
HEADERS += mediarenderer.h
HEADERS += mythappearance.h

SOURCES += main.cpp manualbox.cpp playbackbox.cpp viewscheduled.cpp
SOURCES += globalsettings.cpp manualschedule.cpp programrecpriority.cpp 
SOURCES += channelrecpriority.cpp statusbox.cpp networkcontrol.cpp
SOURCES += mediarenderer.cpp
SOURCES += custompriority.cpp
SOURCES += mythappearance.cpp

macx {
    mac_bundle {
        CONFIG -= console  # Force behaviour of producing .app bundle
        RC_FILE += mythfrontend.icns
        QMAKE_POST_LINK = ../../contrib/OSX/makebundle.sh mythfrontend.app
    }

    # OS X has no ldconfig
    setting.extra -= -ldconfig
}

# OpenBSD ldconfig expects different arguments than the Linux one
openbsd {
    setting.extra -= -ldconfig
    setting.extra += -ldconfig -R
}

using_x11:DEFINES += USING_X11
using_xv:DEFINES += USING_XV
using_ivtv:DEFINES += USING_IVTV
using_xvmc:DEFINES += USING_XVMC
using_xvmc_vld:DEFINES += USING_XVMC_VLD
using_xrandr:DEFINES += USING_XRANDR
using_opengl_vsync:DEFINES += USING_OPENGL_VSYNC

using_alsa:DEFINES += USING_ALSA
using_arts:DEFINES += USING_ARTS
using_jack:DEFINES += USING_JACK
using_oss: DEFINES += USING_OSS
macx:      DEFINES += USING_COREAUDIO
