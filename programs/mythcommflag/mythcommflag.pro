include ( ../../config.mak )
include (../../settings.pro)
include ( ../programs-libs.pro )

TEMPLATE = app
CONFIG += thread
target.path = $${PREFIX}/bin
INSTALLS = target

QMAKE_CLEAN += $(TARGET)

# Input
SOURCES += main.cpp CommDetectorFactory.cpp CommDetectorBase.cpp
SOURCES += Histogram.cpp
SOURCES += ClassicLogoDetector.cpp
SOURCES += ClassicSceneChangeDetector.cpp
SOURCES += ClassicCommDetector.cpp
SOURCES += quickselect.c
SOURCES += CommDetector2.cpp
SOURCES += pgm.cpp
SOURCES += EdgeDetector.cpp CannyEdgeDetector.cpp
SOURCES += PGMConverter.cpp BorderDetector.cpp
SOURCES += FrameAnalyzer.cpp
SOURCES += TemplateFinder.cpp TemplateMatcher.cpp
SOURCES += HistogramAnalyzer.cpp
SOURCES += BlankFrameDetector.cpp
SOURCES += SceneChangeDetector.cpp

HEADERS += SlotRelayer.h CustomEventRelayer.h
HEADERS += CommDetectorFactory.h CommDetectorBase.h
HEADERS += Histogram.h
HEADERS += LogoDetectorBase.h
HEADERS += SceneChangeDetectorBase.h
HEADERS += ClassicLogoDetector.h
HEADERS += ClassicSceneChangeDetector.h
HEADERS += ClassicCommDetector.h
HEADERS += quickselect.h
HEADERS += CommDetector2.h
HEADERS += pgm.h
HEADERS += EdgeDetector.h CannyEdgeDetector.h
HEADERS += PGMConverter.h BorderDetector.h
HEADERS += FrameAnalyzer.h
HEADERS += TemplateFinder.h TemplateMatcher.h
HEADERS += HistogramAnalyzer.h
HEADERS += BlankFrameDetector.h
HEADERS += SceneChangeDetector.h
