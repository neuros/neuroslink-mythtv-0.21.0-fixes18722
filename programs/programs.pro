include ( ../config.mak )
include ( ../settings.pro )

TEMPLATE = subdirs
 
# Directories
using_frontend {
    SUBDIRS += mythtv mythfrontend mythcommflag
    SUBDIRS += mythtvosd mythjobqueue mythlcdserver
    SUBDIRS += mythwelcome mythshutdown mythtranscode/replex
}

using_backend {
    SUBDIRS += mythbackend mythfilldatabase mythtv-setup
}

using_frontend:using_backend {
    SUBDIRS += mythtranscode
}

mingw: SUBDIRS -= mythtranscode mythtranscode/replex
