include ( ../../config.mak )
include ( ../../settings.pro )

TEMPLATE = lib
TARGET = mythtv-$$LIBVERSION
CONFIG += thread dll
target.path = $${LIBDIR}
INSTALLS = target

INCLUDEPATH += ../.. .. .
INCLUDEPATH += ../libmyth ../libavcodec ../libavutil ../libmythmpeg2
INCLUDEPATH += ./dvbdev ./mpeg ./iptv
INCLUDEPATH += ../libmythlivemedia/BasicUsageEnvironment/include
INCLUDEPATH += ../libmythlivemedia/groupsock/include
INCLUDEPATH += ../libmythlivemedia/liveMedia/include
INCLUDEPATH += ../libmythlivemedia/UsageEnvironment/include

DEPENDPATH  += ../libmyth ../libavcodec ../libavformat ../libavutil
DEPENDPATH  += ../libmythmpeg2 ../libmythdvdnav
DEPENDPATH  += ./dvbdev ./mpeg ./hdhomerun ./iptv
DEPENDPATH  += ../libmythlivemedia/BasicUsageEnvironment/include
DEPENDPATH  += ../libmythlivemedia/BasicUsageEnvironment
DEPENDPATH  += ../libmythlivemedia/groupsock/include
DEPENDPATH  += ../libmythlivemedia/groupsock
DEPENDPATH  += ../libmythlivemedia/liveMedia/include
DEPENDPATH  += ../libmythlivemedia/liveMedia
DEPENDPATH  += ../libmythlivemedia/UsageEnvironment/include
DEPENDPATH  += ../libmythlivemedia/UsageEnvironment

LIBS += -L../libmyth -L../libavutil -L../libavcodec -L../libavformat 
LIBS += -L../libmythui -L../libmythupnp
LIBS += -L../libmythmpeg2 -L../libmythdvdnav
LIBS += -L../libmythfreemheg -L../libmythlivemedia
LIBS += -lmyth-$$LIBVERSION         -lmythavutil-$$LIBVERSION
LIBS += -lmythavcodec-$$LIBVERSION  -lmythavformat-$$LIBVERSION
LIBS += -lmythui-$$LIBVERSION       -lmythupnp-$$LIBVERSION
LIBS += -lmythmpeg2-$$LIBVERSION    -lmythdvdnav-$$LIBVERSION
LIBS += -lmythfreemheg-$$LIBVERSION -lmythlivemedia-$$LIBVERSION
LIBS += -lz $$EXTRA_LIBS

TARGETDEPS += ../libmyth/libmyth-$${MYTH_SHLIB_EXT}
TARGETDEPS += ../libavutil/libmythavutil-$${MYTH_SHLIB_EXT}
TARGETDEPS += ../libavcodec/libmythavcodec-$${MYTH_SHLIB_EXT}
TARGETDEPS += ../libavformat/libmythavformat-$${MYTH_SHLIB_EXT}
TARGETDEPS += ../libmythmpeg2/libmythmpeg2-$${MYTH_LIB_EXT}
TARGETDEPS += ../libmythdvdnav/libmythdvdnav-$${MYTH_LIB_EXT}
TARGETDEPS += ../libmythfreemheg/libmythfreemheg-$${MYTH_SHLIB_EXT}
using_live: TARGETDEPS += ../libmythlivemedia/libmythlivemedia-$${MYTH_SHLIB_EXT}


DEFINES += _LARGEFILE_SOURCE
QMAKE_CXXFLAGS += $${FREETYPE_CFLAGS}
QMAKE_LFLAGS_SHLIB += $${FREETYPE_LIBS}

macx {
    # Mac OS X Frameworks
    FWKS = AGL ApplicationServices Carbon Cocoa OpenGL QuickTime
    PFWKS = DVD

    using_firewire:using_backend: FWKS += IOKit

    # The following trick shortens the command line, but depends on
    # the shell expanding Csh-style braces. Luckily, Bash and Zsh do.
    FC = $$join(FWKS,",","{","}")
    PFC = $$join(PFWKS,",","{","}")

    QMAKE_CXXFLAGS += -F/System/Library/Frameworks/$${FC}.framework/Frameworks
    QMAKE_CXXFLAGS += -F/System/Library/PrivateFrameworks/$${PFC}.framework/Frameworks
    LIBS           += -framework $$join(FWKS," -framework ")
    LIBS           += -F/System/Library/PrivateFrameworks
    LIBS           += -framework $$join(PFWKS," -framework ")

    using_firewire:using_backend {
        QMAKE_CXXFLAGS += -F$${CONFIG_MAC_AVC}
        LIBS += -F$${CONFIG_MAC_AVC} -framework AVCVideoServices
        # Recent versions of this framework use /usr/lib/libstdc++.6.dylib
        # which may clash with symbols in /usr/lib/gcc/darwin/3.3/libstdc++.a
        # In that case, rebuild the framework with your (old) Xcode version
    }

    using_dvdv {
        # DVDUtils uses Objective-C++, activated by .mm suffix
        QMAKE_EXT_CPP += .mm
    }

    QMAKE_LFLAGS_SHLIB += -seg1addr 0xC9000000
}

cygwin:QMAKE_LFLAGS_SHLIB += -Wl,--noinhibit-exec
cygwin:DEFINES += _WIN32

# Enable Linux Open Sound System support
using_oss:DEFINES += USING_OSS
# Enable Valgrind, i.e. disable some timeouts
using_valgrind:DEFINES += USING_VALGRIND

# old libvbitext (Caption decoder)
!win32 {
    HEADERS += vbitext/cc.h vbitext/dllist.h vbitext/hamm.h vbitext/lang.h 
    HEADERS += vbitext/vbi.h vbitext/vt.h
    SOURCES += vbitext/cc.cpp vbitext/vbi.c vbitext/hamm.c vbitext/lang.c
}

# mmx macros from avlib
contains( HAVE_MMX, yes ) {
    HEADERS += ../../libs/libavcodec/i386/mmx.h ../../libs/libavcodec/dsputil.h
}

QMAKE_CLEAN += $(TARGET) $(TARGETA) $(TARGETD) $(TARGET0) $(TARGET1) $(TARGET2)

##########################################################################
# libmythtv proper

# Headers needed by frontend & backend
HEADERS += filter.h                 format.h
HEADERS += frame.h                  compat.h

# LZO / RTjpegN, used by NuppelDecoder & NuppelVideoRecorder
HEADERS += lzoconf.h
HEADERS += minilzo.h                RTjpegN.h
SOURCES += minilzo.cpp              RTjpegN.cpp

# Misc. needed by backend/frontend
HEADERS += programinfo.h            proglist.h
HEADERS += RingBuffer.h             avfringbuffer.h
HEADERS += ThreadedFileWriter.h     previouslist.h
HEADERS += dbcheck.h                customedit.h
HEADERS += remoteutil.h             tv.h
HEADERS += recordingtypes.h         jobqueue.h
HEADERS += filtermanager.h          recordingprofile.h
HEADERS += remoteencoder.h          videosource.h
HEADERS += cardutil.h               sourceutil.h
HEADERS += cc608decoder.h
HEADERS += cc708decoder.h           cc708window.h
HEADERS += sr_dialog.h              sr_root.h
HEADERS += sr_items.h               scheduledrecording.h
HEADERS += signalmonitorvalue.h     viewschdiff.h
HEADERS += livetvchain.h
HEADERS += playgroup.h              progdetails.h
HEADERS += channeleditor.h          channelsettings.h
HEADERS += previewgenerator.h       transporteditor.h
HEADERS += importicons.h

SOURCES += programinfo.cpp          proglist.cpp
SOURCES += RingBuffer.cpp           avfringbuffer.cpp
SOURCES += ThreadedFileWriter.cpp   previouslist.cpp
SOURCES += dbcheck.cpp              customedit.cpp
SOURCES += remoteutil.cpp           tv.cpp
SOURCES += recordingtypes.cpp       jobqueue.cpp
SOURCES += filtermanager.cpp        recordingprofile.cpp
SOURCES += remoteencoder.cpp        videosource.cpp
SOURCES += cardutil.cpp             sourceutil.cpp
SOURCES += cc608decoder.cpp
SOURCES += cc708decoder.cpp         cc708window.cpp
SOURCES += sr_dialog.cpp            sr_root.cpp
SOURCES += sr_items.cpp             scheduledrecording.cpp
SOURCES += signalmonitorvalue.cpp
SOURCES += viewschdiff.cpp
SOURCES += livetvchain.cpp
SOURCES += playgroup.cpp
SOURCES += progdetails.cpp
SOURCES += channeleditor.cpp        channelsettings.cpp
SOURCES += previewgenerator.cpp     transporteditor.cpp
SOURCES += importicons.cpp

# DiSEqC
HEADERS += diseqc.h                 diseqcsettings.h
SOURCES += diseqc.cpp               diseqcsettings.cpp

# Listings downloading classes
HEADERS += datadirect.h
SOURCES += datadirect.cpp

# Teletext stuff
HEADERS += teletextdecoder.h        vbilut.h
SOURCES += teletextdecoder.cpp      vbilut.cpp

# MPEG parsing stuff
HEADERS += mpeg/tspacket.h          mpeg/pespacket.h
HEADERS += mpeg/mpegtables.h        mpeg/atsctables.h
HEADERS += mpeg/dvbtables.h         mpeg/premieretables.h
HEADERS += mpeg/mpegstreamdata.h    mpeg/atscstreamdata.h
HEADERS += mpeg/dvbstreamdata.h     mpeg/scanstreamdata.h
HEADERS += mpeg/mpegdescriptors.h   mpeg/atscdescriptors.h
HEADERS += mpeg/dvbdescriptors.h    mpeg/dishdescriptors.h
HEADERS += mpeg/premieredescriptors.h
HEADERS += mpeg/atsc_huffman.h      mpeg/iso639.h
HEADERS += mpeg/freesat_huffman.h   mpeg/freesat_tables.h
HEADERS += mpeg/iso6937tables.h
HEADERS += mpeg/tsstats.h           mpeg/streamlisteners.h
HEADERS += mpeg/h264utils.h

SOURCES += mpeg/tspacket.cpp        mpeg/pespacket.cpp
SOURCES += mpeg/mpegtables.cpp      mpeg/atsctables.cpp
SOURCES += mpeg/dvbtables.cpp       mpeg/premieretables.cpp
SOURCES += mpeg/mpegstreamdata.cpp  mpeg/atscstreamdata.cpp
SOURCES += mpeg/dvbstreamdata.cpp   mpeg/scanstreamdata.cpp
SOURCES += mpeg/mpegdescriptors.cpp mpeg/atscdescriptors.cpp
SOURCES += mpeg/dvbdescriptors.cpp  mpeg/dishdescriptors.cpp
SOURCES += mpeg/premieredescriptors.cpp
SOURCES += mpeg/atsc_huffman.cpp    mpeg/iso639.cpp
SOURCES += mpeg/freesat_huffman.cpp
SOURCES += mpeg/iso6937tables.cpp
SOURCES += mpeg/h264utils.cpp

DEFINES += USING_H264TOOLS

# Channels, and the multiplexes that transmit them
HEADERS += frequencies.h
HEADERS += frequencytables.h        channelutil.h
HEADERS += dtvmultiplex.h           dtvconfparserhelpers.h
SOURCES += frequencies.c
SOURCES += frequencytables.cpp      channelutil.cpp
SOURCES += dtvmultiplex.cpp         dtvconfparserhelpers.cpp

using_frontend {
    # Recording profile stuff
    HEADERS += profilegroup.h
    SOURCES += profilegroup.cpp

    # XBox LED control
    HEADERS += xbox.h
    SOURCES += xbox.cpp

    # Video playback
    HEADERS += tv_play.h                NuppelVideoPlayer.h
    HEADERS += DVDRingBuffer.h
    SOURCES += tv_play.cpp              NuppelVideoPlayer.cpp
    SOURCES += DVDRingBuffer.cpp

    # Text subtitle parser
    HEADERS += textsubtitleparser.h     xine_demux_sputext.h
    SOURCES += textsubtitleparser.cpp   xine_demux_sputext.c

    # A/V decoders
    HEADERS += decoderbase.h
    HEADERS += nuppeldecoder.h          avformatdecoder.h
    SOURCES += decoderbase.cpp
    SOURCES += nuppeldecoder.cpp        avformatdecoder.cpp 

    using_ivtv:HEADERS += ivtvdecoder.h
    using_ivtv:SOURCES += ivtvdecoder.cpp

    # On screen display (video output overlay)
    using_fribidi:DEFINES += USING_FRIBIDI
    HEADERS += osd.h                    osdtypes.h
    HEADERS += osdsurface.h             osdlistbtntype.h
    HEADERS += osdimagecache.h          osdtypeteletext.h
    HEADERS += udpnotify.h 
    SOURCES += osd.cpp                  osdtypes.cpp
    SOURCES += osdsurface.cpp           osdlistbtntype.cpp
    SOURCES += osdimagecache.cpp        osdtypeteletext.cpp
    SOURCES += udpnotify.cpp 

    # Video output
    HEADERS += videooutbase.h           videoout_null.h
    HEADERS += videobuffers.h           vsync.h
    HEADERS += jitterometer.h           yuv2rgb.h
    HEADERS += videodisplayprofile.h    mythcodecid.h
    HEADERS += videoouttypes.h
    SOURCES += videooutbase.cpp         videoout_null.cpp
    SOURCES += videobuffers.cpp         vsync.cpp
    SOURCES += jitterometer.cpp         yuv2rgb.cpp
    SOURCES += videodisplayprofile.cpp  mythcodecid.cpp

    macx:HEADERS +=               videoout_dvdv.h
    macx:HEADERS +=               videoout_quartz.h
    macx:SOURCES +=               videoout_quartz.cpp

    using_dvdv:DEFINES +=         USING_DVDV
    using_dvdv:HEADERS +=         videoout_dvdv_private.h
    using_dvdv:HEADERS +=         util-osx-cocoa.h
    using_dvdv:SOURCES +=         videoout_dvdv.mm
    using_dvdv:SOURCES +=         util-osx-cocoa.mm

    using_directfb:HEADERS +=     videoout_directfb.h
    using_directfb:SOURCES +=     videoout_directfb.cpp
    using_directfb:DEFINES +=     USING_DIRECTFB

    using_directx:HEADERS +=      videoout_dx.h
    using_directx:SOURCES +=      videoout_dx.cpp

    using_ivtv:HEADERS +=         videoout_ivtv.h
    using_ivtv:SOURCES +=         videoout_ivtv.cpp

    using_x11:DEFINES += USING_X11

    using_xv:HEADERS += videoout_xv.h   XvMCSurfaceTypes.h
    using_xv:HEADERS += osdxvmc.h       osdchromakey.h
    using_xv:HEADERS += xvmctextures.h  util-xvmc.h
    using_xv:HEADERS += util-xv.h
    using_xv:SOURCES += videoout_xv.cpp  XvMCSurfaceTypes.cpp
    using_xv:SOURCES += osdxvmc.cpp      osdchromakey.cpp
    using_xv:SOURCES += xvmctextures.cpp util-xvmc.cpp
    using_xv:SOURCES += util-xv.cpp

    using_xv:DEFINES += USING_XV

    using_xvmc:DEFINES += USING_XVMC
    using_xvmcw:DEFINES += USING_XVMCW
    using_xvmc_vld:DEFINES += USING_XVMC_VLD
    using_xvmc_pbuffer:DEFINES += USING_XVMC_PBUFFER

    using_opengl {
        CONFIG += opengl
        DEFINES += USING_OPENGL
        HEADERS += util-opengl.h        openglcontext.h
        SOURCES += util-opengl.cpp      openglcontext.cpp
    }
    using_xvmc_opengl:DEFINES += USING_XVMC_OPENGL
    using_opengl_vsync:DEFINES += USING_OPENGL_VSYNC

    using_opengl_video:DEFINES += USING_OPENGL_VIDEO
    using_opengl_video:HEADERS += openglvideo.h
    using_opengl_video:SOURCES += openglvideo.cpp

    using_glx_proc_addr_arb:DEFINES += USING_GLX_PROC_ADDR_ARB

    # Misc. frontend
    HEADERS += guidegrid.h              infostructs.h
    HEADERS += progfind.h               ttfont.h
    SOURCES += guidegrid.cpp            infostructs.cpp
    SOURCES += progfind.cpp             ttfont.cpp

    # DSMCC stuff
    HEADERS += dsmcc.h                  dsmcccache.h
    HEADERS += dsmccbiop.h              dsmccobjcarousel.h
    HEADERS += dsmccreceiver.h
    SOURCES += dsmcc.cpp                dsmcccache.cpp
    SOURCES += dsmccbiop.cpp            dsmccobjcarousel.cpp

    # MHEG/MHI stuff
    HEADERS += interactivetv.h          mhi.h
    SOURCES += interactivetv.cpp        mhi.cpp

    # C stuff
    HEADERS += blend.h
    SOURCES += blend.c

    DEFINES += USING_FRONTEND
}

using_backend {
    # Channel stuff
    HEADERS += channelbase.h               dtvchannel.h
    HEADERS += signalmonitor.h             dtvsignalmonitor.h
    HEADERS += inputinfo.h                 inputgroupmap.h
    SOURCES += channelbase.cpp             dtvchannel.cpp
    SOURCES += signalmonitor.cpp           dtvsignalmonitor.cpp
    SOURCES += inputinfo.cpp               inputgroupmap.cpp

    # Channel scanner stuff
    HEADERS += scanwizard.h                scanwizardhelpers.h
    HEADERS += dvbconfparser.h
    HEADERS += siscan.h
    HEADERS += scanwizardscanner.h
    SOURCES += scanwizard.cpp              scanwizardhelpers.cpp
    SOURCES += siscan.cpp
    SOURCES += dvbconfparser.cpp
    SOURCES += scanwizardscanner.cpp

    # EIT stuff
    HEADERS += eithelper.h                 eitscanner.h
    HEADERS += eitfixup.h                  eitcache.h
    HEADERS += eit.h
    SOURCES += eithelper.cpp               eitscanner.cpp
    SOURCES += eitfixup.cpp                eitcache.cpp
    SOURCES += eit.cpp

    # non-EIT EPG stuff
    HEADERS += programdata.h
    SOURCES += programdata.cpp

    # TVRec & Recorder base classes
    HEADERS += tv_rec.h
    HEADERS += recorderbase.h              DeviceReadBuffer.h
    HEADERS += dtvrecorder.h
    SOURCES += tv_rec.cpp
    SOURCES += recorderbase.cpp            DeviceReadBuffer.cpp
    SOURCES += dtvrecorder.cpp

    # Simple NuppelVideo Recorder
    using_ffmpeg_threads:DEFINES += USING_FFMPEG_THREADS
    HEADERS += NuppelVideoRecorder.h       fifowriter.h
    SOURCES += NuppelVideoRecorder.cpp     fifowriter.cpp

    # Support for Video4Linux devices
    using_v4l {
        HEADERS += channel.h                   analogsignalmonitor.h
        SOURCES += channel.cpp                 analogsignalmonitor.h

        DEFINES += USING_V4L
    }

    # Support for cable boxes that provide Firewire out
    using_firewire {
        HEADERS += firewirechannel.h           firewirerecorder.h
        HEADERS += firewiresignalmonitor.h     firewiredevice.h
        HEADERS += avcinfo.h
        SOURCES += firewirechannel.cpp         firewirerecorder.cpp
        SOURCES += firewiresignalmonitor.cpp   firewiredevice.cpp
        SOURCES += avcinfo.cpp

        macx {
            HEADERS += darwinfirewiredevice.h   darwinavcinfo.h
            SOURCES += darwinfirewiredevice.cpp darwinavcinfo.cpp
            DEFINES += USING_OSX_FIREWIRE
        }
        
        !macx {
            HEADERS += linuxfirewiredevice.h   linuxavcinfo.h
            SOURCES += linuxfirewiredevice.cpp linuxavcinfo.cpp
            DEFINES += USING_LINUX_FIREWIRE
        }

        DEFINES += USING_FIREWIRE
        using_libavc_5_3:DEFINES += USING_LIBAVC_5_3
    }

    # Support for set top boxes (Nokia DBox2 etc.)
    using_dbox2:SOURCES += dbox2recorder.cpp dbox2channel.cpp dbox2epg.cpp
    using_dbox2:HEADERS += dbox2recorder.h dbox2channel.h dbox2epg.h
    using_dbox2:DEFINES += USING_DBOX2
    
    # Support for MPEG2 TS streams (including FreeBox http://adsl.free.fr/)
    using_iptv {
        HEADERS += iptvchannel.h              iptvrecorder.h
        HEADERS += iptvsignalmonitor.h
        HEADERS += iptv/iptvchannelfetcher.h  iptv/iptvchannelinfo.h
        HEADERS += iptv/iptvmediasink.h
        HEADERS += iptv/iptvfeeder.h          iptv/iptvfeederwrapper.h
        HEADERS += iptv/iptvfeederrtsp.h      iptv/iptvfeederudp.h
        HEADERS += iptv/iptvfeederfile.h      iptv/iptvfeederlive.h
        HEADERS += iptv/iptvfeederrtp.h
        HEADERS += iptv/urlfetcher.h          iptv/timeoutedtaskscheduler.h

        SOURCES += iptvchannel.cpp            iptvrecorder.cpp
        SOURCES += iptvsignalmonitor.cpp
        SOURCES += iptv/iptvchannelfetcher.cpp
        SOURCES += iptv/iptvmediasink.cpp
        SOURCES += iptv/iptvfeeder.cpp        iptv/iptvfeederwrapper.cpp
        SOURCES += iptv/iptvfeederrtsp.cpp    iptv/iptvfeederudp.cpp
        SOURCES += iptv/iptvfeederfile.cpp    iptv/iptvfeederlive.cpp
        SOURCES += iptv/iptvfeederrtp.cpp
        SOURCES += iptv/urlfetcher.cpp        iptv/timeoutedtaskscheduler.cpp

        DEFINES += USING_IPTV
    }

    # Support for HDHomeRun box
    using_hdhomerun {
        # MythTV HDHomeRun glue
        HEADERS += hdhrsignalmonitor.h   hdhrchannel.h
        HEADERS += hdhrrecorder.h

        SOURCES += hdhrsignalmonitor.cpp hdhrchannel.cpp
        SOURCES += hdhrrecorder.cpp

        DEFINES += USING_HDHOMERUN

        # HDHomeRun library
        HEADERS += hdhomerun/hdhomerun_pkt.h   hdhomerun/hdhomerun_discover.h
        HEADERS += hdhomerun/hdhomerun_video.h hdhomerun/hdhomerun_control.h
        HEADERS += hdhomerun/hdhomerun_os.h

        SOURCES += hdhomerun/hdhomerun_pkt.c   hdhomerun/hdhomerun_discover.c
        SOURCES += hdhomerun/hdhomerun_video.c hdhomerun/hdhomerun_control.c
    }

    # Support for PVR-150/250/350/500, etc. on Linux
    using_ivtv:HEADERS += mpegrecorder.h
    using_ivtv:SOURCES += mpegrecorder.cpp
    using_ivtv:DEFINES += USING_IVTV

    # Support for Linux DVB drivers
    using_dvb {
        # Basic DVB types
        HEADERS += dvbtypes.h
        SOURCES += dvbtypes.cpp

        # Channel stuff
        HEADERS += dvbchannel.h           dvbsignalmonitor.h
        HEADERS += dvbcam.h
        SOURCES += dvbchannel.cpp         dvbsignalmonitor.cpp
        SOURCES += dvbcam.cpp

        # DVB Recorder
        HEADERS += dvbrecorder.h          dvbstreamhandler.h
        SOURCES += dvbrecorder.cpp        dvbstreamhandler.cpp

        # Misc
        HEADERS += dvbdev/dvbci.h
        SOURCES += dvbdev/dvbci.cpp

        DEFINES += USING_DVB
    }

    DEFINES += USING_BACKEND
}

use_hidesyms {
    QMAKE_CXXFLAGS += -fvisibility=hidden
}

mingw {
    # For easier debugging: 
    target.path = $${PREFIX}/bin

    DEFINES -= USING_OPENGL_VSYNC
    DEFINES += USING_D3D

    HEADERS -= util-opengl.h   openglcontext.h
    HEADERS += videoout_d3d.h
    SOURCES -= util-opengl.cpp openglcontext.cpp NuppelVideoRecorder.cpp
    SOURCES += videoout_d3d.cpp

    LIBS += -lpthread
}

# install headers required by mytharchive
inc.path = $${PREFIX}/include/mythtv/libmythtv/
inc.files = programinfo.h remoteutil.h recordingtypes.h

INSTALLS += inc

unix {
    SOURCES-=dbcheck.cpp
    OBJECTS+=dbcheck.o
    gcc431_workaround.target=dbcheck.o
    gcc431_workaround.commands=$(CXX) -c $(CXXFLAGS) -O0 $(INCPATH) -o dbcheck.o dbcheck.cpp
    gcc431_workaround.depends=dbcheck.h dbcheck.cpp
    gcc431_workaround.depends+=datadirect.h videodisplayprofile.h
    gcc431_workaround.depends+=../libmyth/mythcontext.h
    gcc431_workaround.depends+=../libmyth/mythdbcon.h
    gcc431_workaround.depends+=../libmyth/dbutil.h
    gcc431_workaround.depends+=../libmyth/mythexp.h
    gcc431_workaround.depends+=../libmyth/mythobservable.h
    gcc431_workaround.depends+=../libmyth/mythsocket.h
    gcc431_workaround.depends+=../libmyth/mythverbose.h
    gcc431_workaround.depends+=../libmyth/mythevent.h
    QMAKE_EXTRA_UNIX_TARGETS+=gcc431_workaround
}

include ( ../libs-targetfix.pro )
