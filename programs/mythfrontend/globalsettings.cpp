
// -*- Mode: c++ -*-

// Standard UNIX C headers
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

// Qt headers
#include <qapplication.h>
#include <qevent.h>
#include <qnamespace.h>
#include <qstylefactory.h>
#include <qsqldatabase.h>
#include <qfile.h>
#include <qdialog.h>
#include <qcursor.h>
#include <qdir.h>
#include <qimage.h>

// MythTV headers
#include "libmyth/mythconfig.h"
#include "libmyth/mythcontext.h"
#include "libmyth/mythdbcon.h"
#include "libmyth/dbsettings.h"
#include "libmyth/langsettings.h"
#include "libmythtv/mpeg/iso639.h"
#include "playbackbox.h"
#include "globalsettings.h"
#include "libmythtv/recordingprofile.h"
#include "libmythtv/scheduledrecording.h"
#include "libmyth/util-x11.h"
#include "libmyth/DisplayRes.h"
#include "libmyth/uitypes.h"
#include "libmythtv/cardutil.h"
#include "libmyth/themeinfo.h"
#include "libmyth/mythconfig.h"

static HostComboBox *AudioOutputDevice()
{
    HostComboBox *gc = new HostComboBox("AudioOutputDevice", true);
    gc->setLabel(QObject::tr("Audio output device"));

#ifdef USING_ALSA
    gc->addSelection("ALSA:default",       "ALSA:default");
    gc->addSelection("ALSA:spdif",         "ALSA:spdif");
    gc->addSelection("ALSA:surround51",    "ALSA:surround51");
    gc->addSelection("ALSA:analog",        "ALSA:analog");
    gc->addSelection("ALSA:digital",       "ALSA:digital");
    gc->addSelection("ALSA:mixed-analog",  "ALSA:mixed-analog");
    gc->addSelection("ALSA:mixed-digital", "ALSA:mixed-digital");
#endif
#ifdef USING_OSS
    QDir dev("/dev", "dsp*", QDir::Name, QDir::System);
    gc->fillSelectionsFromDir(dev);
    dev.setNameFilter("adsp*");
    gc->fillSelectionsFromDir(dev);

    dev.setPath("/dev/sound");
    if (dev.exists())
    {
        dev.setNameFilter("dsp*");
        gc->fillSelectionsFromDir(dev);
        dev.setNameFilter("adsp*");
        gc->fillSelectionsFromDir(dev);
    }
#endif
#ifdef USING_ARTS
    gc->addSelection("ARTS:", "ARTS:");
#endif
#ifdef USING_JACK
    gc->addSelection("JACK:output", "JACK:output"); 
#endif
#ifdef USING_COREAUDIO
    gc->addSelection("CoreAudio:", "CoreAudio:"); 
#endif
#ifdef USING_MINGW
	gc->addSelection("Windows:");
#endif
#ifdef USING_DIRECTX
	gc->addSelection("DirectX:");
#endif
    gc->addSelection("NULL", "NULL");

    return gc;
}

static HostComboBox *MaxAudioChannels()
{
    HostComboBox *gc = new HostComboBox("MaxChannels",false);
    gc->setLabel(QObject::tr("Max Audio Channels"));
    gc->addSelection(QObject::tr("Stereo"), "2", true); // default
    gc->addSelection(QObject::tr("5.1"), "6");
    gc->setHelpText(
            QObject::tr(
                "Set the maximum number of audio channels to be decoded. "
                "This is for multi-channel/surround audio playback."));
    return gc;
}

static HostComboBox *AudioUpmixType()
{
    HostComboBox *gc = new HostComboBox("AudioUpmixType",false);
    gc->setLabel(QObject::tr("Upmix"));
    gc->addSelection(QObject::tr("Passive"), "0", true); // default
    gc->addSelection(QObject::tr("Active Simple"), "1");
    gc->addSelection(QObject::tr("Active Linear"), "2");
    gc->setHelpText(
            QObject::tr(
                "Set the audio upmix type for 2ch to 6ch conversion. "
                "This is for multi-channel/surround audio playback. "
                "'Passive' is the least demanding on the CPU. "
                "'Active Simple' is more demanding and 'Active Linear' "
                "is the most demanding (but highest quality)."));
    return gc;
}

static HostComboBox *PassThroughOutputDevice()
{
    HostComboBox *gc = new HostComboBox("PassThruOutputDevice", true);

    gc->setLabel(QObject::tr("Passthrough output device"));
    gc->addSelection(QObject::tr("Default"), "Default");
#ifndef USING_MINGW
    gc->addSelection("ALSA:iec958:{ AES0 0x02 }", "ALSA:iec958:{ AES0 0x02 }");
#endif

    gc->setHelpText(QObject::tr("Audio output device to use for AC3 and "
                    "DTS passthrough. Default is the same as Audio output "
                    "device. This value is currently only used with ALSA "
                    "sound output."));
    return gc;
}

static HostCheckBox *MythControlsVolume()
{
    HostCheckBox *gc = new HostCheckBox("MythControlsVolume");
    gc->setLabel(QObject::tr("Use internal volume controls"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("MythTV can control the PCM and master "
                    "mixer volume.  If you prefer to use an external mixer "
                    "program, then disable this option."));
    return gc;
}

static HostComboBox *MixerDevice()
{
    HostComboBox *gc = new HostComboBox("MixerDevice", true);
    gc->setLabel(QObject::tr("Mixer Device"));

#ifdef USING_OSS
    QDir dev("/dev", "mixer*", QDir::Name, QDir::System);
    gc->fillSelectionsFromDir(dev);

    dev.setPath("/dev/sound");
    if (dev.exists())
    {
        gc->fillSelectionsFromDir(dev);
    }
#endif
#ifdef USING_ALSA
    gc->addSelection("ALSA:default", "ALSA:default");
#endif
#ifdef USING_DIRECTX
    gc->addSelection("DirectX:", "DirectX:");
#endif
#ifdef USING_WINAUDIO
    gc->addSelection("Windows:", "Windows:");
#endif

    return gc;
}

static const char* MixerControlControls[] = { "PCM",
                                              "Master" };

static HostComboBox *MixerControl()
{
    HostComboBox *gc = new HostComboBox("MixerControl", true);
    gc->setLabel(QObject::tr("Mixer Controls"));
    for (unsigned int i = 0; i < sizeof(MixerControlControls) / sizeof(char*); 
         ++i) 
    {
        gc->addSelection(QObject::tr(MixerControlControls[i]), 
                         MixerControlControls[i]);
    }

    gc->setHelpText(QObject::tr("Changing the volume adjusts the selected mixer."));
    return gc;
}

static HostSlider *MixerVolume()
{
    HostSlider *gs = new HostSlider("MasterMixerVolume", 0, 100, 1);
    gs->setLabel(QObject::tr("Master Mixer Volume"));
    gs->setValue(70);
    gs->setHelpText(QObject::tr("Initial volume for the Master Mixer.  "
                    "This affects all sound created by the sound card.  "
                    "Note: Do not set this too low."));
    return gs;
}

static HostSlider *PCMVolume()
{
    HostSlider *gs = new HostSlider("PCMMixerVolume", 0, 100, 1);
    gs->setLabel(QObject::tr("PCM Mixer Volume"));
    gs->setValue(70);
    gs->setHelpText(QObject::tr("Initial volume for PCM output.  Using the "
                    "volume keys in MythTV will adjust this parameter."));
    return gs;
}

static HostCheckBox *IndividualMuteControl()
{
    HostCheckBox *gc = new HostCheckBox("IndividualMuteControl");
    gc->setLabel(QObject::tr("Independent Muting of Left and Right Audio "
                 "Channels"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable muting of just the left or right "
                    "channel.  Useful if your broadcaster puts the "
                    "original language on one channel, and a dubbed "
                    "version of the program on the other one.  This "
                    "modifies the behavior of the Mute key."));
    return gc;
}

static HostCheckBox *AC3PassThrough()
{
    HostCheckBox *gc = new HostCheckBox("AC3PassThru");
    gc->setLabel(QObject::tr("Enable AC3 to SPDIF passthrough"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable sending AC3 audio directly to your "
                    "sound card's SPDIF output, on sources which contain "
                    "AC3 soundtracks (usually digital TV).  Requires that "
                    "the audio output device be set to something suitable."));
    return gc;
}

static HostCheckBox *DTSPassThrough()
{
    HostCheckBox *gc = new HostCheckBox("DTSPassThru");
    gc->setLabel(QObject::tr("Enable DTS to SPDIF passthrough"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Enable sending DTS audio directly to your "
                    "sound card's SPDIF output, on sources which contain "
                    "DTS soundtracks (usually DVDs).  Requires that the "
                    "audio output device be set to something suitable."));
    return gc;
}

static HostCheckBox *DecodeExtraAudio()
{
    HostCheckBox *gc = new HostCheckBox("DecodeExtraAudio");
    gc->setLabel(QObject::tr("Extra audio buffering"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("Enable this setting if MythTV is playing "
                    "\"crackly\" audio and you are using hardware encoding. "
                    "This setting will have no effect "
                    "on MPEG-4 or RTJPEG video. MythTV will keep extra "
                    "audio data in its internal buffers to workaround "
                    "this bug."));
    return gc;
}

static HostComboBox *PIPLocationComboBox()
{
    HostComboBox *gc = new HostComboBox("PIPLocation");
    gc->setLabel(QObject::tr("PIP Video Location"));
    for (uint loc = 0; loc < kPIP_END; loc++)
        gc->addSelection(toString((PIPLocation) loc), QString::number(loc));
    gc->setHelpText(QObject::tr("Location of PIP Video window."));
    return gc;
}

static GlobalLineEdit *AllRecGroupPassword()
{
    GlobalLineEdit *be = new GlobalLineEdit("AllRecGroupPassword");
    be->setLabel(QObject::tr("Password required to view all recordings"));
    be->setValue("");
    be->setHelpText(QObject::tr("If given, a password must be entered to "
                    "view the complete list of all recordings."));
    return be;
}

static HostComboBox *DisplayRecGroup()
{
    HostComboBox *gc = new HostComboBox("DisplayRecGroup");
    gc->setLabel(QObject::tr("Default group filter to apply"));

    gc->addSelection(QObject::tr("All Programs"), QString("All Programs"));
    gc->addSelection(QObject::tr("Default"), QString("Default"));

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT DISTINCT recgroup from recorded;");

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        while (query.next())
        {
            if (query.value(0).toString() != "Default")
            {
                QString recgroup = QString::fromUtf8(query.value(0).toString());
                gc->addSelection(recgroup, recgroup);
            }
        }
    }

    query.prepare("SELECT DISTINCT category from recorded;");

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        while (query.next())
        {
            QString key = QString::fromUtf8(query.value(0).toString());
            gc->addSelection(key, key);
        }
    }

    gc->setHelpText(QObject::tr("Default group filter to apply "
                    "on the View Recordings screen."));
    return gc;
}

static HostCheckBox *QueryInitialFilter()
{
    HostCheckBox *gc = new HostCheckBox("QueryInitialFilter");
    gc->setLabel(QObject::tr("Always prompt for initial group filter"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Always prompt the user for the initial filter "
                    "to apply when entering the Watch Recordings screen."));

    return gc;
}

static HostCheckBox *RememberRecGroup()
{
    HostCheckBox *gc = new HostCheckBox("RememberRecGroup");
    gc->setLabel(QObject::tr("Save current group filter when changed"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("Remember the last selected filter "
                    "instead of displaying the default filter "
                    "whenever you enter the playback screen."));

    return gc;
}

static HostComboBox *DefaultView()
{
    HostComboBox *gc = new HostComboBox("DisplayGroupDefaultView");
    gc->setLabel(QObject::tr("Default View"));

    gc->addSelection(QObject::tr("Show Titles only"),
            QString::number(PlaybackBox::TitlesOnly));
    gc->addSelection(QObject::tr("Show Titles and Categories"),
            QString::number(PlaybackBox::TitlesCategories));
    gc->addSelection(QObject::tr(
                "Show Titles, Categories, and Recording Groups"),
            QString::number(PlaybackBox::TitlesCategoriesRecGroups));
    gc->addSelection(QObject::tr("Show Titles and Recording Groups"),
            QString::number(PlaybackBox::TitlesRecGroups));
    gc->addSelection(QObject::tr("Show Categories only"),
            QString::number(PlaybackBox::Categories));
    gc->addSelection(QObject::tr("Show Categories and Recording Groups"),
            QString::number(PlaybackBox::CategoriesRecGroups));
    gc->addSelection(QObject::tr("Show Recording Groups only"),
            QString::number(PlaybackBox::RecGroups));

    gc->setHelpText(QObject::tr("Select what type of grouping to show on the "
                    "Watch Recordings screen by default."));

    return gc;
}

static HostCheckBox *UseGroupNameAsAllPrograms()
{
    HostCheckBox *gc = new HostCheckBox("DispRecGroupAsAllProg");
    gc->setLabel(QObject::tr("Show filter name instead of \"All Programs\""));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Use the name of the display filter currently "
                    "applied in place of the term \"All Programs\" in the "
                    "playback screen."));
    return gc;
}

static HostCheckBox *LiveTVInAllPrograms()
{
    HostCheckBox *gc = new HostCheckBox("LiveTVInAllPrograms");
    gc->setLabel(QObject::tr("Show 'LiveTV' recordings when using "
                             "\"All Programs\" filter"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If this setting is disabled, LiveTV "
                    "recordings will only be visible via the special "
                    "\"LiveTV\" Recording Group."));
    return gc;
}

static HostCheckBox *PBBStartInTitle()
{
    HostCheckBox *gc = new HostCheckBox("PlaybackBoxStartInTitle");
    gc->setLabel(QObject::tr("Start in Title section"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, the selector highlight will "
                    "start on the Program titles window, otherwise the "
                    "selector will default to the recordings."));
    return gc;
}

static HostCheckBox *PBBShowGroupSummary()
{
    HostCheckBox *gc = new HostCheckBox("ShowGroupInfo");
    gc->setLabel(QObject::tr("Show group summary"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("While selecting a group, show a group "
                    "summary instead of showing info about the first episode "
                    "in that group."));
    return gc;
}

static HostCheckBox *SmartForward()
{
    HostCheckBox *gc = new HostCheckBox("SmartForward");
    gc->setLabel(QObject::tr("Smart Fast Forwarding"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If enabled, then immediately after "
                    "rewinding, only skip forward the same amount as "
                    "skipping backwards."));
    return gc;
}

static HostCheckBox *ExactSeeking()
{
    HostCheckBox *gc = new HostCheckBox("ExactSeeking");
    gc->setLabel(QObject::tr("Seek to exact frame"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If enabled, seeking is frame exact, but "
                    "slower."));
    return gc;
}

static GlobalComboBox *CommercialSkipMethod()
{
    GlobalComboBox *bc = new GlobalComboBox("CommercialSkipMethod");
    bc->setLabel(QObject::tr("Commercial Skip Method"));

    // need to keep this in sync with libs/libmythtv/channelsettings.cpp
    bc->addSelection(QObject::tr("All Available Methods"), "255");
    bc->addSelection(QObject::tr("Blank Frame Detection"), "1");
    bc->addSelection(QObject::tr("Blank Frame + Scene Change"), "3");
    bc->addSelection(QObject::tr("Scene Change Detection"), "2");
    bc->addSelection(QObject::tr("Logo Detection"), "4");
    bc->addSelection(QObject::tr("Experimental"), "511");
    bc->setHelpText(QObject::tr("This determines the method used by MythTV to "
                    "detect when commercials start and end."));
    return bc;
}

static HostComboBox *AutoCommercialSkip()
{
    HostComboBox *gc = new HostComboBox("AutoCommercialSkip");
    gc->setLabel(QObject::tr("Automatically Skip Commercials"));
    gc->addSelection(QObject::tr("Off"), "0");
    gc->addSelection(QObject::tr("Notify, but do not skip"), "2");
    gc->addSelection(QObject::tr("Automatically Skip"), "1");
    gc->setHelpText(QObject::tr("Automatically skip commercial breaks that "
                    "have been flagged during Automatic Commercial Flagging "
                    "or by the mythcommflag program, or just notify that a "
                    "commercial has been detected."));
    return gc;
}

static GlobalCheckBox *AutoCommercialFlag()
{
    GlobalCheckBox *bc = new GlobalCheckBox("AutoCommercialFlag");
    bc->setLabel(QObject::tr("Run commercial flagger"));
    bc->setValue(true);
    bc->setHelpText(QObject::tr("This is the default value used for the Auto-"
                    "Commercial Flagging setting when a new scheduled "
                    "recording is created."));
    return bc;
}

static GlobalCheckBox *AutoTranscode()
{
    GlobalCheckBox *bc = new GlobalCheckBox("AutoTranscode");
    bc->setLabel(QObject::tr("Run transcoder"));
    bc->setValue(false);
    bc->setHelpText(QObject::tr("This is the default value used for the Auto-"
                    "Transcode setting when a new scheduled "
                    "recording is created."));
    return bc;
}

static GlobalComboBox *DefaultTranscoder()
{
    GlobalComboBox *bc = new GlobalComboBox("DefaultTranscoder");
    bc->setLabel(QObject::tr("Default Transcoder"));
    RecordingProfile::fillSelections(bc, RecordingProfile::TranscoderGroup,
                                     true);
    bc->setHelpText(QObject::tr("This is the default value used for the "
                    "transcoder setting when a new scheduled "
                    "recording is created."));
    return bc;
}

static GlobalSpinBox *DeferAutoTranscodeDays()
{
    GlobalSpinBox *gs = new GlobalSpinBox("DeferAutoTranscodeDays", 0, 365, 1);
    gs->setLabel(QObject::tr("Deferral days for Auto-Transcode jobs"));
    gs->setHelpText(QObject::tr("If non-zero, Auto-Transcode jobs will be "
                    "scheduled to run this many days after a recording "
                    "completes instead of immediately afterwards."));
    gs->setValue(0);
    return gs;
}

static GlobalCheckBox *AutoRunUserJob(uint job_num)
{
    QString dbStr = QString("AutoRunUserJob%1").arg(job_num);
    QString label = QObject::tr("Run User Job #%1")
        .arg(job_num);
    GlobalCheckBox *bc = new GlobalCheckBox(dbStr);
    bc->setLabel(label);
    bc->setValue(false);
    bc->setHelpText(QObject::tr("This is the default value used for the "
                    "'Run %1' setting when a new scheduled "
                    "recording is created.")
                    .arg(gContext->GetSetting(QString("UserJobDesc%1")
                         .arg(job_num))));
    return bc;
}

static GlobalCheckBox *AggressiveCommDetect()
{
    GlobalCheckBox *bc = new GlobalCheckBox("AggressiveCommDetect");
    bc->setLabel(QObject::tr("Strict Commercial Detection"));
    bc->setValue(true);
    bc->setHelpText(QObject::tr("Enable stricter Commercial Detection code.  "
                    "Disable if some commercials are not being detected."));
    return bc;
}

static GlobalCheckBox *CommSkipAllBlanks()
{
    GlobalCheckBox *bc = new GlobalCheckBox("CommSkipAllBlanks");
    bc->setLabel(QObject::tr("Skip blank frames after commercials"));
    bc->setValue(true);
    bc->setHelpText(QObject::tr("When using Blank Frame Detection and "
                    "Auto-Flagging, include blank frames following commercial "
                    "breaks as part of the commercial break."));
    return bc;
}

static HostSpinBox *CommRewindAmount()
{
    HostSpinBox *gs = new HostSpinBox("CommRewindAmount", 0, 10, 1);
    gs->setLabel(QObject::tr("Commercial Skip Auto-Rewind Amount"));
    gs->setHelpText(QObject::tr("If set, MythTV will automatically rewind "
                    "this many seconds after performing a commercial skip."));
    gs->setValue(0);
    return gs;
}

static HostSpinBox *CommNotifyAmount()
{
    HostSpinBox *gs = new HostSpinBox("CommNotifyAmount", 0, 10, 1);
    gs->setLabel(QObject::tr("Commercial Skip Notify Amount"));
    gs->setHelpText(QObject::tr("If set, MythTV will act like a commercial "
                    "begins this many seconds early.  This can be useful "
                    "when commercial notification is used in place of "
                    "automatic skipping."));
    gs->setValue(0);
    return gs;
}

static GlobalSpinBox *MaximumCommercialSkip()
{
    GlobalSpinBox *bs = new GlobalSpinBox("MaximumCommercialSkip", 0, 3600, 10);
    bs->setLabel(QObject::tr("Maximum commercial skip (in seconds)"));
    bs->setHelpText(QObject::tr("MythTV will discourage long manual commercial "
                    "skips.  Skips which are longer than this will require the "
                    "user to hit the SKIP key twice.  Automatic commercial "
                    "skipping is not affected by this limit."));
    bs->setValue(3600);
    return bs;
}

static GlobalSpinBox *MergeShortCommBreaks()
{
    GlobalSpinBox *bs = new GlobalSpinBox("MergeShortCommBreaks", 0, 3600, 5);
    bs->setLabel(QObject::tr("Merge short commercial breaks (in seconds)"));
    bs->setHelpText(QObject::tr("Treat consecutive commercial breaks shorter "
                    "than this as one break when skipping forward. Useful if "
                    "you have to skip a few times during breaks. Applies to "
                    "automatic skipping as well. Set to 0 to disable."));
    bs->setValue(0);
    return bs;
}

static GlobalSpinBox *AutoExpireExtraSpace()
{
    GlobalSpinBox *bs = new GlobalSpinBox("AutoExpireExtraSpace", 0, 200, 1);
    bs->setLabel(QObject::tr("Extra Disk Space"));
    bs->setHelpText(QObject::tr("Extra disk space (in Gigabytes) that you want " 
                    "to keep free on the recording file systems beyond what "
                    "MythTV requires."));
    bs->setValue(1);
    return bs;
};

static GlobalCheckBox *AutoExpireInsteadOfDelete()
{
    GlobalCheckBox *cb = new GlobalCheckBox("AutoExpireInsteadOfDelete");
    cb->setLabel(QObject::tr("Auto Expire Instead of Delete Recording"));
    cb->setValue(false);
    cb->setHelpText(QObject::tr("Instead of deleting a recording, "
                    "move recording to the 'Deleted' recgroup and turn on "
                    "autoexpire."));
    return cb;
}

static GlobalSpinBox *DeletedMaxAge()
{
    GlobalSpinBox *bs = new GlobalSpinBox("DeletedMaxAge", 0, 365, 1);
    bs->setLabel(QObject::tr("Deleted Max Age"));
    bs->setHelpText(QObject::tr("When set to a number greater than zero, "
                    "AutoExpire will force expiration of Deleted recordings "
                    "when they are this many days old."));
    bs->setValue(0);
    return bs;
};

static GlobalCheckBox *DeletedFifoOrder()
{
    GlobalCheckBox *cb = new GlobalCheckBox("DeletedFifoOrder");
    cb->setLabel(QObject::tr("Expire in deleted order"));
    cb->setValue(false);
    cb->setHelpText(QObject::tr(
                    "Expire Deleted recordings in the order which they were "
                    "originally deleted."));
    return cb;
};

class DeletedExpireOptions : public TriggeredConfigurationGroup
{
    public:
     DeletedExpireOptions() :
         TriggeredConfigurationGroup(false, false, false, false)
         {
             setLabel(QObject::tr("DeletedExpireOptions"));
             Setting* enabled = AutoExpireInsteadOfDelete();
             addChild(enabled);
             setTrigger(enabled);

             HorizontalConfigurationGroup* settings =
                 new HorizontalConfigurationGroup(false);
             settings->addChild(DeletedFifoOrder());
             settings->addChild(DeletedMaxAge());
             addTarget("1", settings);

             // show nothing if fillEnabled is off
             addTarget("0", new HorizontalConfigurationGroup(true));
         };
};

static GlobalComboBox *AutoExpireMethod()
{
    GlobalComboBox *bc = new GlobalComboBox("AutoExpireMethod");
    bc->setLabel(QObject::tr("Auto Expire Method"));
    bc->addSelection(QObject::tr("Oldest Show First"), "1");
    bc->addSelection(QObject::tr("Lowest Priority First"), "2");
    bc->addSelection(QObject::tr("Weighted Time/Priority Combination"), "3");
    bc->setHelpText(QObject::tr("Method used to determine which recorded "
                    "shows to delete first.  LiveTV recordings will always "
                    "expire before normal recordings."));
    bc->setValue(1);
    return bc;
}

static GlobalCheckBox *AutoExpireWatchedPriority() 
{
    GlobalCheckBox *bc = new GlobalCheckBox("AutoExpireWatchedPriority"); 
    bc->setLabel(QObject::tr("Watched before UNwatched")); 
    bc->setValue(false); 
    bc->setHelpText(QObject::tr("If set, programs that have been marked as "
                    "watched will be expired before programs that have not "
                    "been watched."));
    return bc;
}

static GlobalSpinBox *AutoExpireDayPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("AutoExpireDayPriority", 1, 400, 1);
    bs->setLabel(QObject::tr("Priority Weight"));
    bs->setHelpText(QObject::tr("The number of days bonus a program gets for "
                    "each priority point. This is only used when the Weighted "
                    "Time/Priority Auto Expire Method is selected."));
    bs->setValue(3);
    return bs;
};

static GlobalCheckBox *AutoExpireDefault()
{
    GlobalCheckBox *bc = new GlobalCheckBox("AutoExpireDefault");
    bc->setLabel(QObject::tr("Auto Expire Default"));
    bc->setValue(true);
    bc->setHelpText(QObject::tr("When enabled, any new recording schedules "
                    "will be marked as eligible for Auto-Expiration. "
                    "Existing schedules will keep their current value."));
    return bc;
}

static GlobalSpinBox *AutoExpireLiveTVMaxAge()
{
    GlobalSpinBox *bs = new GlobalSpinBox("AutoExpireLiveTVMaxAge", 1, 365, 1);
    bs->setLabel(QObject::tr("LiveTV Max Age"));
    bs->setHelpText(QObject::tr("AutoExpire will force expiration of LiveTV "
                    "recordings when they are this many days old. LiveTV "
                    "recordings may also be expired early if necessary to "
                    "free up disk space."));
    bs->setValue(1);
    return bs;
};

#if 0
static GlobalSpinBox *MinRecordDiskThreshold()
{
    GlobalSpinBox *bs = new GlobalSpinBox("MinRecordDiskThreshold",
                                            0, 1000000, 100);
    bs->setLabel(QObject::tr("New Recording Free Disk Space Threshold "
                 "(in Megabytes)"));
    bs->setHelpText(QObject::tr("MythTV will stop scheduling new recordings on "
                    "a backend when its free disk space falls below this "
                    "value."));
    bs->setValue(300);
    return bs;
}
#endif

static GlobalCheckBox *RerecordWatched() 
{
    GlobalCheckBox *bc = new GlobalCheckBox("RerecordWatched"); 
    bc->setLabel(QObject::tr("Re-record Watched")); 
    bc->setValue(true); 
    bc->setHelpText(QObject::tr("If set, programs that have been marked as "
                    "watched and are auto-expired will be re-recorded if "
                    "they are shown again."));
    return bc;
}

static GlobalSpinBox *RecordPreRoll()
{
    GlobalSpinBox *bs = new GlobalSpinBox("RecordPreRoll", 0, 600, 60, true);
    bs->setLabel(QObject::tr("Time to record before start of show "
                 "(in seconds)"));
    bs->setHelpText(QObject::tr("This global setting allows the recorder "
                    "to start before the scheduled start time. It does "
                    "not affect the scheduler. It is ignored when two shows "
                    "have been scheduled without enough time in between."));
    bs->setValue(0);
    return bs;
}

static GlobalSpinBox *RecordOverTime()
{
    GlobalSpinBox *bs = new GlobalSpinBox("RecordOverTime", 0, 1800, 60, true);
    bs->setLabel(QObject::tr("Time to record past end of show (in seconds)"));
    bs->setValue(0);
    bs->setHelpText(QObject::tr("This global setting allows the recorder "
                    "to record beyond the scheduled end time. It does "
                    "not affect the scheduler. It is ignored when two shows "
                    "have been scheduled without enough time in between."));
    return bs;
}

static GlobalLineEdit *OverTimeCategory()
{
    GlobalLineEdit *ge = new GlobalLineEdit("OverTimeCategory");
    ge->setLabel(QObject::tr("Category of shows to be extended"));
    ge->setValue(QObject::tr("category name"));
    ge->setHelpText(QObject::tr("For a specific category (e.g. "
                    "\"Sports event\"), request that shows be autoextended. "
                    "Only works if a show's category can be determined."));
    return ge;
}

static GlobalSpinBox *CategoryOverTime()
{
    GlobalSpinBox *bs = new GlobalSpinBox("CategoryOverTime",
                                          0, 180, 60, true);
    bs->setLabel(QObject::tr("Record past end of show (in minutes)"));
    bs->setValue(30);
    bs->setHelpText(QObject::tr("For the specified category, an attempt "
                    "will be made to extend the recording by the specified "
                    "time.  It is ignored when two shows have been scheduled "
                    "without enough time in between."));
    return bs;
}

static VerticalConfigurationGroup *CategoryOverTimeSettings()
{
    VerticalConfigurationGroup *vcg =
        new VerticalConfigurationGroup(false, false);

    vcg->setLabel(QObject::tr("Category record over-time"));
    vcg->setUseLabel(true);
    vcg->addChild(OverTimeCategory());
    vcg->addChild(CategoryOverTime());
    return vcg;
}

static QString trunc(const QString &str, uint len)
{
    if (str.length() > len)
        return str.mid(0, len - 5) + " . . . ";
    return str;
}

static QString pad(const QString &str, uint len)
{
    QString tmp = str;

    while (tmp.length() + 4 < len)
        tmp += "    ";

    while (tmp.length() < len)
        tmp += " ";

    return tmp;
}

PlaybackProfileItemConfig::PlaybackProfileItemConfig(ProfileItem &_item) :
    item(_item)
{
    setLabel(QObject::tr("Profile Item"));

    HorizontalConfigurationGroup *row[2];

    row[0]    = new HorizontalConfigurationGroup(false, false, true, true);
    cmp[0]    = new TransComboBoxSetting();
    width[0]  = new TransSpinBoxSetting(0, 1920, 64, true);
    height[0] = new TransSpinBoxSetting(0, 1088, 64, true);
    row[1]    = new HorizontalConfigurationGroup(false, false, true, true);
    cmp[1]    = new TransComboBoxSetting();
    width[1]  = new TransSpinBoxSetting(0, 1920, 64, true);
    height[1] = new TransSpinBoxSetting(0, 1088, 64, true);
    decoder   = new TransComboBoxSetting();
    max_cpus  = new TransSpinBoxSetting(1, ENABLE_THREADS ? 4 : 1, 1, true);
    vidrend   = new TransComboBoxSetting();
    osdrend   = new TransComboBoxSetting();
    osdfade   = new TransCheckBoxSetting();
    deint0    = new TransComboBoxSetting();
    deint1    = new TransComboBoxSetting();
    filters   = new TransLineEditSetting(true);

    for (uint i = 0; i < 2; i++)
    {
        const QString kCMP[6] = { "", "<", "<=", "==", ">=", ">" };
        for (uint j = 0; j < 6; j++)
            cmp[i]->addSelection(kCMP[j]);

        cmp[i]->setLabel(tr("Match Criteria"));
        width[i]->setName(QString("w%1").arg(i));
        width[i]->setLabel(tr("W"));
        height[i]->setName(QString("h%1").arg(i));
        height[i]->setLabel(tr("H"));

        row[i]->addChild(cmp[i]);
        row[i]->addChild(width[i]);
        row[i]->addChild(height[i]);
    }

    HorizontalConfigurationGroup *vid_row =
        new HorizontalConfigurationGroup(false, false, true, true);
    HorizontalConfigurationGroup *osd_row =
        new HorizontalConfigurationGroup(false, false, true, true);

    decoder->setLabel(tr("Decoder"));
    max_cpus->setLabel(tr("Max CPUs"));
    vidrend->setLabel(tr("Video Renderer"));
    osdrend->setLabel(tr("OSD Renderer"));
    osdfade->setLabel(tr("OSD Fade"));
    deint0->setLabel(tr("Primary Deinterlacer"));
    deint1->setLabel(tr("Fallback Deinterlacer"));
    filters->setLabel(tr("Custom Filters"));

    max_cpus->setHelpText(
        tr("Maximum number of CPU cores used for decoding.") +
        (ENABLE_THREADS ? "" :
         tr(" Multithreaded decoding disabled-only one CPU "
            "will be used, please recompile with "
            "--enable-ffmpeg-pthreads to enable.")));

    filters->setHelpText(
        QObject::tr("Example Custom filter list: 'ivtc,denoise3d'"));

    osdfade->setHelpText(
        tr("When unchecked the OSD will not fade away but instead "
           "will disappear abruptly.") + "\n" +
        tr("Uncheck this if the video studders while the OSD is "
           "fading away."));

    vid_row->addChild(decoder);
    vid_row->addChild(max_cpus);

    osd_row->addChild(vidrend);
    osd_row->addChild(osdrend);
    osd_row->addChild(osdfade);

    VerticalConfigurationGroup *grp =
        new VerticalConfigurationGroup(false, false, true, true);
    grp->addChild(row[0]);
    grp->addChild(row[1]);
    grp->addChild(vid_row);
    grp->addChild(osd_row);
    addChild(grp);

    VerticalConfigurationGroup *page2 =
        new VerticalConfigurationGroup(false, false, true, true);
    page2->addChild(deint0);
    page2->addChild(deint1);
    page2->addChild(filters);
    addChild(page2);

    connect(decoder, SIGNAL(valueChanged(const QString&)),
            this,    SLOT(decoderChanged(const QString&)));\
    connect(vidrend, SIGNAL(valueChanged(const QString&)),
            this,    SLOT(vrenderChanged(const QString&)));
    connect(osdrend, SIGNAL(valueChanged(const QString&)),
            this,    SLOT(orenderChanged(const QString&)));
    connect(deint0, SIGNAL(valueChanged(const QString&)),
            this,    SLOT(deint0Changed(const QString&)));
    connect(deint1, SIGNAL(valueChanged(const QString&)),
            this,    SLOT(deint1Changed(const QString&)));
}

void PlaybackProfileItemConfig::load(void)
{
    for (uint i = 0; i < 2; i++)
    {
        QString     pcmp  = item.Get(QString("pref_cmp%1").arg(i));
        QStringList clist = QStringList::split(" ", pcmp);

        if (clist.size() == 0)
            clist<<((i) ? "" : ">");
        if (clist.size() == 1)
            clist<<"0";
        if (clist.size() == 2)
            clist<<"0";

        cmp[i]->setValue(clist[0]);
        width[i]->setValue(clist[1].toInt());
        height[i]->setValue(clist[2].toInt());
    }

    QString pdecoder  = item.Get("pref_decoder");
    QString pmax_cpus = item.Get("pref_max_cpus");
    QString prenderer = item.Get("pref_videorenderer");
    QString posd      = item.Get("pref_osdrenderer");
    QString posdfade  = item.Get("pref_osdfade");
    QString pdeint0   = item.Get("pref_deint0");
    QString pdeint1   = item.Get("pref_deint1");
    QString pfilter   = item.Get("pref_filters");
    bool    found     = false;

    QString     dech = VideoDisplayProfile::GetDecoderHelp();
    QStringList decr = VideoDisplayProfile::GetDecoders();
    QStringList decn = VideoDisplayProfile::GetDecoderNames();
    QStringList::const_iterator itr = decr.begin();
    QStringList::const_iterator itn = decn.begin();
    for (; (itr != decr.end()) && (itn != decn.end()); itr++, itn++)
    {
        decoder->addSelection(*itn, *itr, (*itr == pdecoder));
        found |= (*itr == pdecoder);
    }
    if (!found && !pdecoder.isEmpty())
    {
        decoder->SelectSetting::addSelection(
            VideoDisplayProfile::GetDecoderName(pdecoder), pdecoder, true);
    }
    decoder->setHelpText(VideoDisplayProfile::GetDecoderHelp(pdecoder));

    if (!pmax_cpus.isEmpty())
        max_cpus->setValue(pmax_cpus.toUInt());
    if (!prenderer.isEmpty())
        vidrend->setValue(prenderer);
    if (!posd.isEmpty())
        osdrend->setValue(posd);

    osdfade->setValue((!posdfade.isEmpty()) ? (bool) posdfade.toInt() : true);

    if (!pdeint0.isEmpty())
        deint0->setValue(pdeint0);
    if (!pdeint1.isEmpty())
        deint1->setValue(pdeint1);
    if (!pfilter.isEmpty())
        filters->setValue(pfilter);
}

void PlaybackProfileItemConfig::save(void)
{
    for (uint i = 0; i < 2; i++)
    {
        QString val = QString("pref_cmp%1").arg(i);
        QString data = "";
        if (!cmp[i]->getValue().isEmpty())
        {
            data = QString("%1 %2 %3")
                .arg(cmp[i]->getValue())
                .arg(width[i]->intValue())
                .arg(height[i]->intValue());
        }
        item.Set(val, data);
    }

    item.Set("pref_decoder",       decoder->getValue());
    item.Set("pref_max_cpus",      max_cpus->getValue());
    item.Set("pref_videorenderer", vidrend->getValue());
    item.Set("pref_osdrenderer",   osdrend->getValue());
    item.Set("pref_osdfade",       (osdfade->boolValue()) ? "1" : "0");
    item.Set("pref_deint0",        deint0->getValue());
    item.Set("pref_deint1",        deint1->getValue());

    QString tmp0 = filters->getValue();
    QString tmp1 = vidrend->getValue();
    QString tmp3 = VideoDisplayProfile::IsFilterAllowed(tmp1) ? tmp0 : "";
    item.Set("pref_filters", tmp3);
}

void PlaybackProfileItemConfig::decoderChanged(const QString &dec)
{
    QString     vrenderer = vidrend->getValue();
    QStringList renderers = VideoDisplayProfile::GetVideoRenderers(dec);
    QStringList::const_iterator it;

    QString prenderer = QString::null;
    for (it = renderers.begin(); it != renderers.end(); it++)
        prenderer = (*it == vrenderer) ? vrenderer : prenderer;
    if (prenderer.isEmpty())
        prenderer = VideoDisplayProfile::GetPreferredVideoRenderer(dec);

    vidrend->clearSelections();
    for (it = renderers.begin(); it != renderers.end(); it++)
    {
        if (*it != "null")
            vidrend->addSelection(*it, *it, (*it == prenderer));
    }

    decoder->setHelpText(VideoDisplayProfile::GetDecoderHelp(dec));
}

void PlaybackProfileItemConfig::vrenderChanged(const QString &renderer)
{
    QStringList osds    = VideoDisplayProfile::GetOSDs(renderer);
    QStringList deints  = VideoDisplayProfile::GetDeinterlacers(renderer);
    QString     losd    = osdrend->getValue();
    QString     ldeint0 = deint0->getValue();
    QString     ldeint1 = deint1->getValue();
    QStringList::const_iterator it;

    osdrend->clearSelections();
    for (it = osds.begin(); it != osds.end(); it++)
        osdrend->addSelection(*it, *it, (*it == losd));

    deint0->clearSelections();
    for (it = deints.begin(); it != deints.end(); it++)
    {
        deint0->addSelection(VideoDisplayProfile::GetDeinterlacerName(*it),
                             *it, (*it == ldeint0));
    }

    deint1->clearSelections();
    for (it = deints.begin(); it != deints.end(); it++)
    {
        if (!(*it).contains("bobdeint") && !(*it).contains("doublerate"))
            deint1->addSelection(VideoDisplayProfile::GetDeinterlacerName(*it),
                                 *it, (*it == ldeint1));
    }

    filters->setEnabled(VideoDisplayProfile::IsFilterAllowed(renderer));
    vidrend->setHelpText(VideoDisplayProfile::GetVideoRendererHelp(renderer));
}

void PlaybackProfileItemConfig::orenderChanged(const QString &renderer)
{
    osdrend->setHelpText(VideoDisplayProfile::GetOSDHelp(renderer));
}

void PlaybackProfileItemConfig::deint0Changed(const QString &deint)
{
    deint0->setHelpText(
        QObject::tr("Main deinterlacing method.") + " " +
        VideoDisplayProfile::GetDeinterlacerHelp(deint));
}

void PlaybackProfileItemConfig::deint1Changed(const QString &deint)
{
    deint1->setHelpText(
        QObject::tr("Fallback deinterlacing method.") + " " +
        VideoDisplayProfile::GetDeinterlacerHelp(deint));
}

PlaybackProfileConfig::PlaybackProfileConfig(const QString &profilename) :
    VerticalConfigurationGroup(false, false, true, true),
    profile_name(profilename), needs_save(false),
    groupid(0), last_main(NULL)
{
    groupid = VideoDisplayProfile::GetProfileGroupID(
        profilename, gContext->GetHostName());

    items = VideoDisplayProfile::LoadDB(groupid);

    InitUI();
}

PlaybackProfileConfig::~PlaybackProfileConfig()
{
}

void PlaybackProfileConfig::InitLabel(uint i)
{
    if (!labels[i])
        return;

    QString andStr = QObject::tr("&", "and");
    QString cmp0   = items[i].Get("pref_cmp0");
    QString cmp1   = items[i].Get("pref_cmp1");
    QString str    = QObject::tr("if rez") + " " + cmp0;

    if (!cmp1.isEmpty())
        str += " " + andStr + " " + cmp1;

    str += " -> ";
    str += items[i].Get("pref_decoder");
    str += " " + andStr + " ";
    str += items[i].Get("pref_videorenderer");
    str.replace("-blit", "");
    str.replace("ivtv " + andStr + " ivtv", "ivtv");
    str.replace("xvmc " + andStr + " xvmc", "xvmc");
    str.replace("xvmc", "XvMC");
    str.replace("xv", "XVideo");

    labels[i]->setValue(pad(trunc(str, 48), 48));
}

void PlaybackProfileConfig::InitUI(void)
{
    VerticalConfigurationGroup *main =
        new VerticalConfigurationGroup(false, false, true, true);

    HorizontalConfigurationGroup *rows =
        new HorizontalConfigurationGroup(false, false, true, true);
    VerticalConfigurationGroup *column1 =
        new VerticalConfigurationGroup(false, false, true, true);
    VerticalConfigurationGroup *column2 =
        new VerticalConfigurationGroup(false, false, true, true);

    labels.resize(items.size());

    for (uint i = 0; i < items.size(); i++)
    {
        labels[i] = new TransLabelSetting();
        InitLabel(i);
        column1->addChild(labels[i]);
    }

    editProf.resize(items.size());
    delProf.resize(items.size());
    priority.resize(items.size());

    for (uint i = 0; i < items.size(); i++)
    {
        HorizontalConfigurationGroup *grp =
            new HorizontalConfigurationGroup(false, false, true, true);

        editProf[i] = new TransButtonSetting(QString("edit%1").arg(i));
        delProf[i]  = new TransButtonSetting(QString("del%1").arg(i));
        priority[i] = new TransSpinBoxSetting(1, items.size(), 1);
        priority[i]->setName(QString("pri%1").arg(i));

        editProf[i]->setLabel(QObject::tr("Edit"));
        delProf[i]->setLabel(QObject::tr("Delete"));
        priority[i]->setValue(i + 1);
        items[i].Set("pref_priority", QString::number(i + 1));

        grp->addChild(editProf[i]);
        grp->addChild(delProf[i]);
        grp->addChild(priority[i]);

        connect(editProf[i], SIGNAL(pressed(QString)),
                this,        SLOT  (pressed(QString)));
        connect(delProf[i],  SIGNAL(pressed(QString)),
                this,        SLOT  (pressed(QString)));
        connect(priority[i], SIGNAL(valueChanged(   const QString&, int)),
                this,        SLOT(  priorityChanged(const QString&, int)));

        column2->addChild(grp);
    }

    rows->addChild(column1);
    rows->addChild(column2);

    TransButtonSetting *addEntry = new TransButtonSetting("addentry");
    addEntry->setLabel(QObject::tr("Add New Entry"));

    main->addChild(rows);
    main->addChild(addEntry);

    connect(addEntry, SIGNAL(pressed(QString)),
            this,     SLOT  (pressed(QString)));

    if (last_main)
        replaceChild(last_main, main);
    else
        addChild(main);

    last_main = main;
}

void PlaybackProfileConfig::load(void)
{
    // Already loaded data in constructor...
}

void PlaybackProfileConfig::save(void)
{
    if (!needs_save)
        return; // nothing to do..

    bool ok = VideoDisplayProfile::DeleteDB(groupid, del_items);
    if (!ok)
    {
        VERBOSE(VB_IMPORTANT,
                "PlaybackProfileConfig::save() -- failed to delete items");
        return;
    }

    ok = VideoDisplayProfile::SaveDB(groupid, items);
    if (!ok)
    {
        VERBOSE(VB_IMPORTANT,
                "PlaybackProfileConfig::save() -- failed to save items");
        return;
    }
}

void PlaybackProfileConfig::pressed(QString cmd)
{
    if (cmd.left(4) == "edit")
    {
        uint i = cmd.mid(4).toUInt();
        PlaybackProfileItemConfig itemcfg(items[i]);

        if (itemcfg.exec() != QDialog::Accepted)
            VERBOSE(VB_IMPORTANT, QString("edit #%1").arg(i) + " rejected");

        InitLabel(i);
        needs_save = true;
    }
    else if (cmd.left(3) == "del")
    {
        uint i = cmd.mid(3).toUInt();
        del_items.push_back(items[i]);
        items.erase(items.begin() + i);

        InitUI();
        needs_save = true;
    }
    else if (cmd == "addentry")
    {
        ProfileItem item;
        PlaybackProfileItemConfig itemcfg(item);

        if (itemcfg.exec() != QDialog::Accepted)
            VERBOSE(VB_IMPORTANT, "addentry rejected");

        items.push_back(item);
        InitUI();
        needs_save = true;        
    }

    repaint();
}

void PlaybackProfileConfig::priorityChanged(const QString &name, int val)
{
    uint i = name.mid(3).toInt();
    uint j = i;

    priority[i]->SetRelayEnabled(false);

    if (((int)items[i].GetPriority() < val) &&
        (i + 1 < priority.size())           &&
        ((int)items[i+1].GetPriority() == val))
    {
        j++;
        priority[j]->SetRelayEnabled(false);

        swap(i, j);
        priority[j]->setFocus();
    }
    else if (((int)items[i].GetPriority() > val) &&
             (i > 0) &&
             ((int)items[i-1].GetPriority() == val))
    {
        j--;
        priority[j]->SetRelayEnabled(false);

        swap(i, j);

        priority[j]->setFocus();
    }
    else
    {
        priority[i]->setValue((int) items[i].GetPriority());
    }

    needs_save = true;

    repaint();

    priority[i]->SetRelayEnabled(true);
    if (i != j)
        priority[j]->SetRelayEnabled(true);
}

void PlaybackProfileConfig::swap(int i, int j)
{
    int pri_i = items[i].GetPriority();
    int pri_j = items[j].GetPriority();

    ProfileItem item = items[j];
    items[j] = items[i];
    items[i] = item;

    priority[i]->setValue(pri_i);
    priority[j]->setValue(pri_j);

    items[i].Set("pref_priority", QString::number(pri_i));
    items[j].Set("pref_priority", QString::number(pri_j));

    const QString label_i = labels[i]->getValue();
    const QString label_j = labels[j]->getValue();
    labels[i]->setValue(label_j);
    labels[j]->setValue(label_i);
}

PlaybackProfileConfigs::PlaybackProfileConfigs(const QString &str) :
    TriggeredConfigurationGroup(false, true,  true, true,
                                false, false, true, true), grouptrigger(NULL)
{
    setLabel(QObject::tr("Playback Profiles") + str);

    QString host = gContext->GetHostName();
    QStringList profiles = VideoDisplayProfile::GetProfiles(host);
    if (profiles.empty())
    {
        VideoDisplayProfile::CreateProfiles(host);
        profiles = VideoDisplayProfile::GetProfiles(host);
    }
    if (profiles.empty())
        return;

    if (!profiles.contains("Normal") &&
        !profiles.contains("High Quality") &&
        !profiles.contains("Slim"))
    {
        VideoDisplayProfile::CreateNewProfiles(host);
        profiles = VideoDisplayProfile::GetProfiles(host);
    }

    QString profile = VideoDisplayProfile::GetDefaultProfileName(host);
    if (!profiles.contains(profile))
    {
        profile = (profiles.contains("Normal")) ? "Normal" : profiles[0];
        VideoDisplayProfile::SetDefaultProfileName(profile, host);
    }

    grouptrigger = new HostComboBox("DefaultVideoPlaybackProfile");
    grouptrigger->setLabel(QObject::tr("Current Video Playback Profile"));
    QStringList::const_iterator it;
    for (it = profiles.begin(); it != profiles.end(); it++)
        grouptrigger->addSelection(QObject::tr(*it), *it);

    HorizontalConfigurationGroup *grp =
        new HorizontalConfigurationGroup(false, false, true, true);
    TransButtonSetting *addProf = new TransButtonSetting("add");
    TransButtonSetting *delProf = new TransButtonSetting("del");

    addProf->setLabel(QObject::tr("Add New"));
    delProf->setLabel(QObject::tr("Delete"));

    grp->addChild(grouptrigger);
    grp->addChild(addProf);
    grp->addChild(delProf);

    addChild(grp);

    setTrigger(grouptrigger);
    for (it = profiles.begin(); it != profiles.end(); ++it)
        addTarget(*it, new PlaybackProfileConfig(*it));
    setSaveAll(true);

    connect(addProf, SIGNAL(pressed( QString)),
            this,    SLOT  (btnPress(QString)));
    connect(delProf, SIGNAL(pressed( QString)),
            this,    SLOT  (btnPress(QString)));
}

PlaybackProfileConfigs::~PlaybackProfileConfigs()
{
    //VERBOSE(VB_IMPORTANT, "~PlaybackProfileConfigs()");
}

void PlaybackProfileConfigs::btnPress(QString cmd)
{
    if (cmd == "add")
    {
        QString name = QString::null;

        QString host = gContext->GetHostName();
        QStringList not_ok_list = VideoDisplayProfile::GetProfiles(host);

        bool ok = true;
        while (ok)
        {
            QString msg = QObject::tr("Enter Playback Group Name");

            ok = MythPopupBox::showGetTextPopup(
                gContext->GetMainWindow(), msg, msg, name);

            if (!ok)
                return;

            if (not_ok_list.contains(name) || name.isEmpty())
            {
                msg = (name.isEmpty()) ?
                    QObject::tr(
                        "Sorry, playback group\nname can not be blank.") :
                    QObject::tr(
                        "Sorry, playback group name\n"
                        "'%1' is already being used.").arg(name);

                MythPopupBox::showOkPopup(
                    gContext->GetMainWindow(), QObject::tr("Error"), msg);

                continue;
            }

            break;
        }

        VideoDisplayProfile::CreateProfileGroup(name, gContext->GetHostName());
        addTarget(name, new PlaybackProfileConfig(name));

        if (grouptrigger)
            grouptrigger->addSelection(name, name, true);
    }
    else if ((cmd == "del") && grouptrigger)
    {
        const QString name = grouptrigger->getSelectionLabel();
        if (!name.isEmpty())
        {
            removeTarget(name);
            VideoDisplayProfile::DeleteProfileGroup(
                name, gContext->GetHostName());
        }
    }

    repaint();
}

void PlaybackProfileConfigs::triggerChanged(const QString &trig)
{
    TriggeredConfigurationGroup::triggerChanged(trig);
}

static HostComboBox *PlayBoxOrdering()
{
    QString str[4] =
    {
        QObject::tr("Sort all sub-titles/multi-titles Ascending"),
        QObject::tr("Sort all sub-titles/multi-titles Descending"),
        QObject::tr("Sort sub-titles Descending, multi-titles Ascending"),
        QObject::tr("Sort sub-titles Ascending, multi-titles Descending"),
    };
    QString help = QObject::tr(
        "Selects how to sort show episodes. Sub-titles refers to the "
        "episodes listed under a specific show title. Multi-title "
        "refers to sections (e.g. \"All Programs\") which list "
        "multiple titles. Sections in parentheses are not affected.");

    HostComboBox *gc = new HostComboBox("PlayBoxOrdering");
    gc->setLabel(QObject::tr("Episode sort orderings"));

    for (int i = 0; i < 4; i++)
        gc->addSelection(str[i], QString::number(i));

    gc->setValue(1);
    gc->setHelpText(help);

    return gc;
}

static HostComboBox *PlayBoxEpisodeSort()
{
    HostComboBox *gc = new HostComboBox("PlayBoxEpisodeSort");
    gc->setLabel(QObject::tr("Sort Episodes"));
    gc->addSelection(QObject::tr("Record date"), "Date");
    gc->addSelection(QObject::tr("Original Air date"), "OrigAirDate");
    gc->addSelection(QObject::tr("Program ID"), "Id");
    gc->setHelpText(QObject::tr("Selects how to sort a shows episodes"));
    return gc;
}

static HostCheckBox *StickyKeys()
{
    HostCheckBox *gc = new HostCheckBox("StickyKeys");
    gc->setLabel(QObject::tr("Sticky keys"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If enabled, fast forward and rewind "
                    "continue after the key is released.  Pressing the key "
                    "again increases the fast forward or rewind speed.  The "
                    "alternate fast forward and rewind keys always behave in "
                    "this way."));
    return gc;
}

static HostSpinBox *FFRewReposTime()
{
    HostSpinBox *gs = new HostSpinBox("FFRewReposTime", 0, 200, 5);
    gs->setLabel(QObject::tr("Fast forward/rewind reposition amount"));
    gs->setValue(100);
    gs->setHelpText(QObject::tr("When exiting sticky keys fast forward/rewind "
                    "mode, reposition this many 1/100th seconds before "
                    "resuming normal playback. This "
                    "compensates for the reaction time between seeing "
                    "where to resume playback and actually exiting seeking."));
    return gs;
}

static HostCheckBox *FFRewReverse()
{
    HostCheckBox *gc = new HostCheckBox("FFRewReverse");
    gc->setLabel(QObject::tr("Reverse direction in fast forward/rewind"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, pressing the sticky rewind key "
                    "in fast forward mode switches to rewind mode, and "
                    "vice versa.  If disabled, it will decrease the "
                    "current speed or switch to play mode if "
                    "the speed can't be decreased further."));
    return gc; 
}

static HostSpinBox *OSDGeneralTimeout()
{
    HostSpinBox *gs = new HostSpinBox("OSDGeneralTimeout", 1, 30, 1);
    gs->setLabel(QObject::tr("General OSD time-out (sec)"));
    gs->setValue(2);
    gs->setHelpText(QObject::tr("Length of time an on-screen display "
                    "window will be visible."));
    return gs;
}

static HostSpinBox *OSDProgramInfoTimeout()
{
    HostSpinBox *gs = new HostSpinBox("OSDProgramInfoTimeout", 1, 30, 1);
    gs->setLabel(QObject::tr("Program Info OSD time-out"));
    gs->setValue(3);
    gs->setHelpText(QObject::tr("Length of time the on-screen display "
                    "will display program information."));
    return gs;
}

static HostSpinBox *OSDNotifyTimeout()
{
    HostSpinBox *gs = new HostSpinBox("OSDNotifyTimeout", 1, 30, 1);
    gs->setLabel(QObject::tr("UDP Notify OSD time-out"));
    gs->setValue(5);
    gs->setHelpText(QObject::tr("How many seconds an on-screen display "
                    "will be active for UDP Notify events."));
    return gs;
}

static HostSpinBox *ThemeCacheSize()
{
    HostSpinBox *gs = new HostSpinBox("ThemeCacheSize", 1, 1000, 1, true);
    gs->setLabel(QObject::tr("Theme cache size"));
    gs->setValue(1);
    gs->setHelpText(QObject::tr(
                        "Maximum number of prescaled themes to cache."));

    return gs;
}

static HostComboBox *MenuTheme()
{
    HostComboBox *gc = new HostComboBox("MenuTheme");
    gc->setLabel(QObject::tr("Menu theme"));

    QDir themes(gContext->GetThemesParentDir());
    themes.setFilter(QDir::Dirs);
    themes.setSorting(QDir::Name | QDir::IgnoreCase);
    gc->addSelection(QObject::tr("Default"), "default");
    const QFileInfoList *fil = themes.entryInfoList(QDir::Dirs);
    if (!fil)
        return gc;

    QFileInfoListIterator it( *fil );
    QFileInfo *theme;

    for( ; it.current() != 0 ; ++it ) {
        theme = it.current();
        QFileInfo xml(theme->absFilePath() + "/mainmenu.xml");

        if (theme->fileName()[0] != '.' && xml.exists())
            gc->addSelection(theme->fileName());
    }

    return gc;
}

static HostComboBox *OSDFont()
{
    HostComboBox *gc = new HostComboBox("OSDFont");
    gc->setLabel(QObject::tr("OSD font"));
    QDir ttf(gContext->GetFontsDir(), gContext->GetFontsNameFilter());
    gc->fillSelectionsFromDir(ttf, false);

    return gc;
}

static HostComboBox *OSDCCFont()
{
    HostComboBox *gc = new HostComboBox("OSDCCFont");
    gc->setLabel(QObject::tr("CC font"));
    QDir ttf(gContext->GetFontsDir(), gContext->GetFontsNameFilter());
    gc->fillSelectionsFromDir(ttf, false);
    gc->setHelpText(QObject::tr("Closed Caption font"));

    return gc;
}

static HostSpinBox *OSDCC708TextZoomPercentage(void)
{
    HostSpinBox *gs = new HostSpinBox("OSDCC708TextZoom", 50, 200, 5);
    gs->setLabel(QObject::tr("Text zoom percentage"));
    gs->setValue(100);
    gs->setHelpText(QObject::tr("Use this to enlarge or shrink captions."));

    return gs;
}

static HostComboBox *OSDCC708DefaultFontType(void)
{
    HostComboBox *hc = new HostComboBox("OSDCC708DefaultFontType");
    hc->setLabel(QObject::tr("Default Caption Font Type"));
    hc->setHelpText(
        QObject::tr("This allows you to set which font type to use "
                    "when the broadcaster does not specify a font."));

    QString types[] =
    {
        "MonoSerif", "PropSerif", "MonoSansSerif", "PropSansSerif",
        "Casual",    "Cursive",   "Capitals",
    };
    QString typeNames[] =
    {
        QObject::tr("Monospaced serif"),
        QObject::tr("Proportional serif"),
        QObject::tr("Monospaced sans serif"),
        QObject::tr("Proportional sans serif"),
        QObject::tr("Casual"),
        QObject::tr("Cursive"),
        QObject::tr("Capitals"),
    };
    for (uint i = 0; i < 7; i++)
        hc->addSelection(typeNames[i], types[i]);
    return hc;
}

static VerticalConfigurationGroup *OSDCC708Settings(void)
{
    VerticalConfigurationGroup *grp =
        new VerticalConfigurationGroup(false, true, true, true);
    grp->setLabel(QObject::tr("ATSC Caption Settings"));

// default text zoom 1.0
    grp->addChild(OSDCC708TextZoomPercentage());

// force X lines of captions
// force caption character color
// force caption character border color
// force background color
// force background opacity

// set default font type
    grp->addChild(OSDCC708DefaultFontType());

    return grp;
}

static HostComboBox *OSDCC708Font(
    const QString &subtype, const QString &subtypeName,
    const QString &subtypeNameForHelp)
{
    HostComboBox *gc = new HostComboBox(
        QString("OSDCC708%1Font").arg(subtype));

    gc->setLabel(subtypeName);
    QDir ttf(gContext->GetFontsDir(), gContext->GetFontsNameFilter());
    gc->fillSelectionsFromDir(ttf, false);
    gc->setHelpText(
        QObject::tr("ATSC %1 closed caption font.").arg(subtypeNameForHelp));

    return gc;
}

static HorizontalConfigurationGroup *OSDCC708Fonts(void)
{
    HorizontalConfigurationGroup *grpmain =
        new HorizontalConfigurationGroup(false, true, true, true);
    grpmain->setLabel(QObject::tr("ATSC Caption Fonts"));
    VerticalConfigurationGroup *col[] =
    {
        new VerticalConfigurationGroup(false, false, true, true),
        new VerticalConfigurationGroup(false, false, true, true),
    };
    QString types[] =
    {
        "MonoSerif", "PropSerif", "MonoSansSerif", "PropSansSerif",
        "Casual",    "Cursive",   "Capitals",
    };
    QString typeNames[] =
    {
        QObject::tr("Monospaced Serif"),
        QObject::tr("Proportional Serif"),
        QObject::tr("Monospaced Sans Serif"),
        QObject::tr("Proportional Sans Serif"),
        QObject::tr("Casual"),
        QObject::tr("Cursive"),
        QObject::tr("Capitals"),
    };
    QString subtypes[] = { "%1", "%1Italic", };

    TransLabelSetting *col0 = new TransLabelSetting();
    col0->setValue(QObject::tr("Regular Font"));

    TransLabelSetting *col1 = new TransLabelSetting();
    col1->setValue(QObject::tr("Italic Font"));

    col[0]->addChild(col0);
    col[1]->addChild(col1);

    uint i = 0;
    for (uint j = 0; j < 7; j++)
    {
        col[i]->addChild(OSDCC708Font(subtypes[i].arg(types[j]),
                                      typeNames[j], typeNames[j]));
    }
    grpmain->addChild(col[i]);

    i = 1;
    for (uint j = 0; j < 7; j++)
    {
        col[i]->addChild(OSDCC708Font(
                             subtypes[i].arg(types[j]), "",
                             QObject::tr("Italic") + " " + typeNames[j]));
    }

    grpmain->addChild(col[i]);

    return grpmain;
}

static HostComboBox *OSDThemeFontSizeType()
{
    HostComboBox *gc = new HostComboBox("OSDThemeFontSizeType");
    gc->setLabel(QObject::tr("Font size"));
    gc->addSelection(QObject::tr("default"), "default");
    gc->addSelection(QObject::tr("small"), "small");
    gc->addSelection(QObject::tr("big"), "big");
    gc->setHelpText(QObject::tr("default: TV, small: monitor, big:"));
    return gc;
}

static HostComboBox *ChannelOrdering()
{
    HostComboBox *gc = new HostComboBox("ChannelOrdering");
    gc->setLabel(QObject::tr("Channel ordering"));
    gc->addSelection(QObject::tr("channel number"), "channum");
    gc->addSelection(QObject::tr("channel name"),   "callsign");
    return gc;
}

static HostSpinBox *VertScanPercentage()
{
    HostSpinBox *gs = new HostSpinBox("VertScanPercentage", -100, 100, 1);
    gs->setLabel(QObject::tr("Vertical scaling"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr(
                        "Adjust this if the image does not fill your "
                        "screen vertically. Range -100% to 100%"));
    return gs;
}

static HostSpinBox *HorizScanPercentage()
{
    HostSpinBox *gs = new HostSpinBox("HorizScanPercentage", -100, 100, 1);
    gs->setLabel(QObject::tr("Horizontal scaling"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr(
                        "Adjust this if the image does not fill your "
                        "screen horizontally. Range -100% to 100%"));
    return gs;
};

static HostSpinBox *XScanDisplacement()
{
    HostSpinBox *gs = new HostSpinBox("XScanDisplacement", -50, 50, 1);
    gs->setLabel(QObject::tr("Scan displacement (X)"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Adjust this to move the image horizontally."));
    return gs; 
}

static HostSpinBox *YScanDisplacement()
{
    HostSpinBox *gs = new HostSpinBox("YScanDisplacement", -50, 50, 1);
    gs->setLabel(QObject::tr("Scan displacement (Y)"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Adjust this to move the image vertically."));
    return gs;
};

static HostCheckBox *AlwaysStreamFiles() 
{
    HostCheckBox *gc = new HostCheckBox("AlwaysStreamFiles");
    gc->setLabel(QObject::tr("Always stream recordings from the backend"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr(
                        "Enable this setting if you want MythTV to "
                        "always stream files from a remote backend "
                        "instead of directly reading a recording "
                        "file if it is accessible locally."));
    return gc;
}

static HostCheckBox *UseVideoTimebase()
{
    HostCheckBox *gc = new HostCheckBox("UseVideoTimebase");
    gc->setLabel(QObject::tr("Use video as timebase"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Use the video as the timebase and warp "
                    "the audio to keep it in sync. (Experimental)"));
    return gc;
}

static HostCheckBox *CCBackground()
{
    HostCheckBox *gc = new HostCheckBox("CCBackground");
    gc->setLabel(QObject::tr("Black background for analog closed captioning"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr(
                        "If enabled, analog captions will be displayed "
                        "over a black space for maximum contrast. Otherwise, "
                        "captions will use outlined text over the picture."));
    return gc;
}

static HostCheckBox *DefaultCCMode()
{
    HostCheckBox *gc = new HostCheckBox("DefaultCCMode");
    gc->setLabel(QObject::tr("Always display closed captioning or subtitles"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr(
                        "If enabled, captions will be displayed "
                        "when playing back recordings or watching "
                        "live TV.  Closed Captioning can be turned on or off "
                        "by pressing \"T\" during playback."));
    return gc;
}

static HostCheckBox *PreferCC708()
{
    HostCheckBox *gc = new HostCheckBox("Prefer708Captions");
    gc->setLabel(QObject::tr("Prefer EIA-708 over EIA-608 captions"));
    gc->setValue(true);
    gc->setHelpText(
        QObject::tr(
            "When enabled the new EIA-708 captions will be preferred over "
            "the old EIA-608 captions in ATSC streams.") + " " +
        QObject::tr(
            "This is the default, but as of early 2008 most stations are "
            "not broadcasting useable EIA-708 captions."));

    return gc;
}

static HostCheckBox *EnableMHEG()
{
    HostCheckBox *gc = new HostCheckBox("EnableMHEG");
    gc->setLabel(QObject::tr("Enable Interactive TV"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr(
                        "If enabled, interactive TV applications (MHEG) will "
                        "be activated.  This is used for teletext and logos for "
                        "radio and channels that are currently off-air."));
    return gc;
}

static HostCheckBox *PersistentBrowseMode()
{
    HostCheckBox *gc = new HostCheckBox("PersistentBrowseMode");
    gc->setLabel(QObject::tr("Always use Browse mode when changing channels "
                 "in LiveTV"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, Browse mode will "
                    "automatically be activated whenever you use Channel "
                    "UP/DOWN while watching Live TV."));
    return gc;
}

static HostCheckBox *AggressiveBuffer()
{
    HostCheckBox *gc = new HostCheckBox("AggressiveSoundcardBuffer");
    gc->setLabel(QObject::tr("Aggressive Sound card Buffering"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If enabled, MythTV will pretend to have "
                   "a smaller sound card buffer than is really present.  This "
                   "may speed up seeking, but can also cause playback "
                   "problems."));
    return gc;
}

static HostCheckBox *ClearSavedPosition()
{
    HostCheckBox *gc = new HostCheckBox("ClearSavedPosition");
    gc->setLabel(QObject::tr("Clear Saved Position on playback"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("Automatically clear saved position on a "
                    "recording when the recording is played back.  If "
                    "disabled, you can mark the beginning with rewind "
                    "then save position."));
    return gc;
}

static HostCheckBox *AltClearSavedPosition()
{
    HostCheckBox *gc = new HostCheckBox("AltClearSavedPosition");
    gc->setLabel(QObject::tr("Alternate Clear Saved Position"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("During playback the Select key "
                    "(Enter or Space) will alternate between \"Position "
                    "Saved\" and \"Position Cleared\". If disabled, the "
                    "Select key will save the current position for each "
                    "keypress."));
    return gc;
}

#ifdef USING_XV
static HostCheckBox *UsePicControls()
{
    HostCheckBox *gc = new HostCheckBox("UseOutputPictureControls");
    gc->setLabel(QObject::tr("Enable picture controls"));
    gc->setValue(false);
    gc->setHelpText(
        QObject::tr(
            "If enabled, MythTV attempts to initialize picture controls "
            "(brightness, contrast, etc.) that are applied during playback."));
    return gc;
}
#endif

static HostCheckBox *AudioNagSetting()
{
    HostCheckBox *gc = new HostCheckBox("AudioNag");
    gc->setLabel(QObject::tr("Warn on no audio output"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, MythTV will warn you "
                    "if it can't access the soundcard."));
    return gc;
}

static HostLineEdit *UDPNotifyPort()
{
    HostLineEdit *ge = new HostLineEdit("UDPNotifyPort");
    ge->setLabel(QObject::tr("UDP Notify Port"));
    ge->setValue("6948");
    ge->setHelpText(QObject::tr("During playback, MythTV will listen for "
                    "connections from the \"mythtvosd\" or \"mythudprelay\" "
                    "programs on this port.  See the README in "
                    "contrib/mythnotify/ for additional information."));
    return ge;
}

static HostComboBox *PlaybackExitPrompt()
{
    HostComboBox *gc = new HostComboBox("PlaybackExitPrompt");
    gc->setLabel(QObject::tr("Action on playback exit"));
    gc->addSelection(QObject::tr("Just exit"), "0");
    gc->addSelection(QObject::tr("Save position and exit"), "2");
    gc->addSelection(QObject::tr("Always prompt (excluding Live TV)"), "1");
    gc->addSelection(QObject::tr("Always prompt (including Live TV)"), "4");
    gc->addSelection(QObject::tr("Prompt for Live TV only"), "8");
    gc->setHelpText(QObject::tr("If set to prompt, a menu will be displayed "
                    "when you exit playback mode.  The options available will "
                    "allow you to save your position, delete the "
                    "recording, or continue watching."));
    return gc;
}

static HostCheckBox *EndOfRecordingExitPrompt()
{
    HostCheckBox *gc = new HostCheckBox("EndOfRecordingExitPrompt");
    gc->setLabel(QObject::tr("Prompt at end of recording"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If set, a menu will be displayed allowing "
                    "you to delete the recording when it has finished "
                    "playing."));
    return gc;
}

static HostCheckBox *JumpToProgramOSD()
{
    HostCheckBox *gc = new HostCheckBox("JumpToProgramOSD");
    gc->setLabel(QObject::tr("Jump to Program OSD"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr(
                        "Set the choice between viewing the current "
                        "recording group in the OSD, or showing the "
                        "'Watch Recording' screen when 'Jump to Program' "
                        "is activated. If set, the recordings are shown "
                        "in the OSD"));
    return gc;
}

static HostCheckBox *ContinueEmbeddedTVPlay()
{
    HostCheckBox *gc = new HostCheckBox("ContinueEmbeddedTVPlay");
    gc->setLabel(QObject::tr("Continue Playback When Embedded"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr(
                    "This option continues TV playback when the TV window "
                    "is embedded in the upcoming program list or recorded "
                    "list. The default is to pause the recorded show when "
                    "embedded."));
    return gc;
}

static HostCheckBox *AutomaticSetWatched()
{
    HostCheckBox *gc = new HostCheckBox("AutomaticSetWatched");
    gc->setLabel(QObject::tr("Automatically mark a recording as watched"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If set, when you exit near the end of a "
                    "recording it will be marked as watched. The automatic "
                    "detection is not foolproof, so do not enable this "
                    "setting if you don't want an unwatched recording marked "
                    "as watched."));
    return gc;
}

static HostCheckBox *GeneratePreviewPixmaps()
{
    HostCheckBox *gc = new HostCheckBox("GeneratePreviewPixmaps");
    gc->setLabel(QObject::tr("Display thumbnail preview images of "
                 "recordings"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If enabled, a static image of the recording will "
                    "be displayed on the \"Watch a Recording\" menu."));
    return gc;
}

static GlobalSpinBox *PreviewPixmapOffset()
{
    GlobalSpinBox *bs = new GlobalSpinBox("PreviewPixmapOffset", 0, 600, 1);
    bs->setLabel(QObject::tr("Time offset for thumbnail preview images"));
    bs->setHelpText(QObject::tr("MythTV will use this offset to make a "
                    "thumbnail image this many seconds from the beginning "
                    "of the recording, unless this offset happens to be "
                    "between cutpoints or inside a flagged advertisement."));
    bs->setValue(64);
    return bs;
}

static HostCheckBox *PreviewFromBookmark()
{
    HostCheckBox *gc = new HostCheckBox("PreviewFromBookmark");
    gc->setLabel(QObject::tr("Generate preview image from a bookmark "
                 "if possible"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, MythTV will ignore the above "
                    "time offset, and use the bookmark inside the recording "
                    "as the offset for creating a thumbnail image. "
                    "As with the above, MythTV will honour cutlists "
                    "and increase this offset if necessary."));
    return gc;
}

static HostCheckBox *PlaybackPreview()
{
    HostCheckBox *gc = new HostCheckBox("PlaybackPreview");
    gc->setLabel(QObject::tr("Display live preview of recordings"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("When enabled, a preview of the recording "
                    "will play in a small window on the \"Watch a "
                    "Recording\" menu."));
    return gc;
}

static HostCheckBox *PlayBoxTransparency()
{
    HostCheckBox *gc = new HostCheckBox("PlayBoxTransparency");
    gc->setLabel(QObject::tr("Use Transparent Boxes"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, the Watch Recording and Delete "
                    "Recording screens will use transparency. Disable "
                    "if selecting the recordings is slow due to high "
                    "CPU usage."));
    return gc;
}

static HostComboBox *PlayBoxShading()
{
    HostComboBox *gc = new HostComboBox("PlayBoxShading");
    gc->setLabel(QObject::tr("Popup Background Shading Method"));
    gc->addSelection(QObject::tr("Fill"), "0");
    gc->addSelection(QObject::tr("Image"), "1");
    gc->addSelection(QObject::tr("None"), "2");

    gc->setHelpText(QObject::tr("\"Fill\" is the quickest shading method. "
                    "\"Image\" is somewhat slow, but has a higher visual "
                    "quality. No shading will be the fastest."));
    return gc;
}

static HostCheckBox *UseVirtualKeyboard()
{
    HostCheckBox *gc = new HostCheckBox("UseVirtualKeyboard");
    gc->setLabel(QObject::tr("Use line edit virtual keyboards"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("Allows you to use a virtual keyboard "
                    "in Myth line edit boxes.  To use, hit OK/Select "
                    "while a line edit is in focus."));
    return gc;
}

static HostComboBox *AllowQuitShutdown()
{
    HostComboBox *gc = new HostComboBox("AllowQuitShutdown");
    gc->setLabel(QObject::tr("System Exit key"));
    gc->addSelection(QObject::tr("ESC"), "4");
    gc->addSelection(QObject::tr("No exit key"), "0");
    gc->addSelection(QObject::tr("Control-ESC"), "1");
    gc->addSelection(QObject::tr("Meta-ESC"), "2");
    gc->addSelection(QObject::tr("Alt-ESC"), "3");
    gc->setHelpText(QObject::tr("MythTV is designed to run continuously. If "
                    "you wish, you may use the ESC key or the ESC key + a "
                    "modifier to exit MythTV. Do not choose a key combination "
                    "that will be intercepted by your window manager."));
    return gc;
}

static HostComboBox *OverrideExitMenu()
{
    HostComboBox *gc = new HostComboBox("OverrideExitMenu");
    gc->setLabel(QObject::tr("Customise exit menu options"));
    gc->addSelection(QObject::tr("Autodetect"), "0");
    gc->addSelection(QObject::tr("Show quit"), "1");
    gc->addSelection(QObject::tr("Show quit and shutdown"), "2");
    gc->addSelection(QObject::tr("Show quit, reboot and shutdown"), "3");
    gc->addSelection(QObject::tr("Show shutdown"), "4");
    gc->addSelection(QObject::tr("Show reboot"), "5");
    gc->addSelection(QObject::tr("Show reboot and shutdown"), "6");
    gc->setHelpText(QObject::tr("By default, only remote frontends are shown "
                    "the shutdown option on the exit menu. Here you can force "
                    "specific shutdown and reboot options to be displayed."));
    return gc;
}

static HostCheckBox *NoPromptOnExit()
{
    HostCheckBox *gc = new HostCheckBox("NoPromptOnExit");
    gc->setLabel(QObject::tr("Confirm Exit"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("When enabled, MythTV will prompt "
                    "for confirmation when you press the System Exit "
                    "key."));
    return gc;
}

static HostLineEdit *RebootCommand()
{
    HostLineEdit *ge = new HostLineEdit("RebootCommand");
    ge->setLabel(QObject::tr("Reboot command"));
    ge->setValue("reboot");
    ge->setHelpText(QObject::tr("Command or script to run if you select "
                    "the reboot option from the exit menu, if the option "
                    "is displayed. You must configure an exit key to "
                    "display the exit menu."));
    return ge;
}

static HostLineEdit *HaltCommand()
{
    HostLineEdit *ge = new HostLineEdit("HaltCommand");
    ge->setLabel(QObject::tr("Halt command"));
    ge->setValue("halt");
    ge->setHelpText(QObject::tr("Command or script to run if you select "
                    "the shutdown option from the exit menu, if the option "
                    "is displayed. You must configure an exit key to "
                    "display the exit menu."));
    return ge;
}

static HostLineEdit *LircKeyPressedApp()
{
    HostLineEdit *ge = new HostLineEdit("LircKeyPressedApp");
    ge->setLabel(QObject::tr("Keypress Application"));
    ge->setValue("");
    ge->setHelpText(QObject::tr("External application or script to run when "
                    "a keypress is received by LIRC."));
    return ge;
}

static HostCheckBox *UseArrowAccels()
{
    HostCheckBox *gc = new HostCheckBox("UseArrowAccels");
    gc->setLabel(QObject::tr("Use Arrow Key Accelerators"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, Arrow key accelerators will "
                    "be used, with LEFT performing an exit action and "
                    "RIGHT selecting the current item."));
    return gc;
}

static HostLineEdit *SetupPinCode()
{
    HostLineEdit *ge = new HostLineEdit("SetupPinCode");
    ge->setLabel(QObject::tr("Setup Pin Code"));
    ge->setHelpText(QObject::tr("This PIN is used to control access to the "
                    "setup menus. If you want to use this feature, then "
                    "setting the value to all numbers will make your life "
                    "much easier.  Set it to blank to disable."));
    return ge;
}

static HostCheckBox *SetupPinCodeRequired()
{
    HostCheckBox *gc = new HostCheckBox("SetupPinCodeRequired");
    gc->setLabel(QObject::tr("Require Setup PIN") + "    ");
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If set, you will not be able to return "
                    "to this screen and reset the Setup PIN without first "
                    "entering the current PIN."));
    return gc;
}

static HostComboBox *XineramaScreen()
{
    HostComboBox *gc = new HostComboBox("XineramaScreen", false);
    int num = GetNumberOfXineramaScreens();
    for (int i=0; i<num; ++i)
        gc->addSelection(QString::number(i), QString::number(i));
    gc->addSelection(QObject::tr("All"), QString::number(-1));
    gc->setLabel(QObject::tr("Display on screen"));
    gc->setValue(0);
    gc->setHelpText(QObject::tr("Run on the specified screen or "
                    "spanning all screens."));
    return gc;
}


static HostComboBox *XineramaMonitorAspectRatio()
{
    HostComboBox *gc = new HostComboBox("XineramaMonitorAspectRatio");
    gc->setLabel(QObject::tr("Monitor Aspect Ratio"));
    gc->addSelection(QObject::tr("4:3"),   "1.3333");
    gc->addSelection(QObject::tr("16:9"),  "1.7777");
    gc->addSelection(QObject::tr("16:10"), "1.6");
    gc->setHelpText(QObject::tr(
                        "The aspect ratio of a Xinerama display can not be "
                        "queried from the display, so it must be specified."));
    return gc;
}

static HostComboBox *LetterboxingColour()
{
    HostComboBox *gc = new HostComboBox("LetterboxColour");
    gc->setLabel(QObject::tr("Letterboxing Color"));
    for (int m = kLetterBoxColour_Black; m < kLetterBoxColour_END; m++)
        gc->addSelection(toString((LetterBoxColour)m), QString::number(m));
    gc->setHelpText(
        QObject::tr(
            "By default MythTV uses black letterboxing to match broadcaster "
            "letterboxing, but those with plasma screens may prefer gray "
            "to minimize burn-in.") +
        QObject::tr("Currently only works with XVideo video renderer."));
    return gc;
}

static HostComboBox *AspectOverride()
{
    HostComboBox *gc = new HostComboBox("AspectOverride");
    gc->setLabel(QObject::tr("Video Aspect Override"));
    for (int m = kAspect_Off; m < kAspect_END; m++)
        gc->addSelection(toString((AspectOverrideMode)m), QString::number(m));
    gc->setHelpText(QObject::tr(
                        "When enabled, these will override the aspect "
                        "ratio specified by any broadcaster for all "
                        "video streams."));
    return gc;
}

static HostComboBox *AdjustFill()
{
    HostComboBox *gc = new HostComboBox("AdjustFill");
    gc->setLabel(QObject::tr("Zoom"));
    for (int m = kAdjustFill_Off; m < kAdjustFill_END; m++)
        gc->addSelection(toString((AdjustFillMode)m), QString::number(m));
    gc->setHelpText(QObject::tr(
                        "When enabled, these will apply a predefined "
                        "zoom to all video playback in MythTV."));
    return gc;
}

// Theme settings

static HostSpinBox *GuiWidth()
{
    HostSpinBox *gs = new HostSpinBox("GuiWidth", 0, 1920, 8, true);
    gs->setLabel(QObject::tr("GUI width (px)"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("The width of the GUI.  Do not make the GUI "
                    "wider than your actual screen resolution.  Set to 0 to "
                    "automatically scale to fullscreen."));
    return gs;
}

static HostSpinBox *GuiHeight()
{
    HostSpinBox *gs = new HostSpinBox("GuiHeight", 0, 1600, 8, true);
    gs->setLabel(QObject::tr("GUI height (px)"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("The height of the GUI.  Do not make the GUI "
                    "taller than your actual screen resolution.  Set to 0 to "
                    "automatically scale to fullscreen."));
    return gs;
}

static HostSpinBox *GuiOffsetX()
{
    HostSpinBox *gs = new HostSpinBox("GuiOffsetX", -3840, 3840, 32, true);
    gs->setLabel(QObject::tr("GUI X offset"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("The horizontal offset the GUI will be "
                    "displayed at.  May only work if run in a window."));
    return gs;
}

static HostSpinBox *GuiOffsetY()
{
    HostSpinBox *gs = new HostSpinBox("GuiOffsetY", -1600, 1600, 8, true);
    gs->setLabel(QObject::tr("GUI Y offset"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("The vertical offset the GUI will be "
                    "displayed at."));
    return gs;
}

#if 0
static HostSpinBox *DisplaySizeWidth()
{
    HostSpinBox *gs = new HostSpinBox("DisplaySizeWidth", 0, 10000, 1);
    gs->setLabel(QObject::tr("Display Size - Width"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Horizontal size of the monitor or TV. Used "
                    "to calculate the actual aspect ratio of the display. This "
                    "will override the DisplaySize from the system."));
    return gs;
}

static HostSpinBox *DisplaySizeHeight()
{
    HostSpinBox *gs = new HostSpinBox("DisplaySizeHeight", 0, 10000, 1);
    gs->setLabel(QObject::tr("Display Size - Height"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Vertical size of the monitor or TV. Used "
                    "to calculate the actual aspect ratio of the display. This "
                    "will override the DisplaySize from the system."));
    return gs;
}
#endif

static HostCheckBox *GuiSizeForTV()
{
    HostCheckBox *gc = new HostCheckBox("GuiSizeForTV");
    gc->setLabel(QObject::tr("Use GUI size for TV playback"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, use the above size for TV, "
                    "otherwise use full screen."));
    return gc;
}

#if defined(USING_XRANDR) || defined(CONFIG_DARWIN)
static HostCheckBox *UseVideoModes()
{
    HostCheckBox *gc = new HostCheckBox("UseVideoModes");
    gc->setLabel(QObject::tr("Separate video modes for GUI and TV playback"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Switch X Window video modes for TV. "
                    "Requires \"xrandr\" support."));
    return gc;
}

static HostSpinBox *VidModeWidth(int idx)
{
    HostSpinBox *gs = new HostSpinBox(QString("VidModeWidth%1").arg(idx),
                                            0, 1920, 16, true);
    gs->setLabel((idx<1) ? QObject::tr("In X"): "");
    gs->setLabelAboveWidget(idx<1);
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Horizontal resolution of video "
                    "which needs a special output resolution."));
    return gs;
}

static HostSpinBox *VidModeHeight(int idx)
{
    HostSpinBox *gs = new HostSpinBox(QString("VidModeHeight%1").arg(idx),
                                            0, 1080, 16, true);
    gs->setLabel((idx<1) ? QObject::tr("In Y"): "");
    gs->setLabelAboveWidget(idx<1);
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Vertical resolution of video "
                    "which needs a special output resolution."));
    return gs;
}

static HostComboBox *GuiVidModeResolution()
{
    HostComboBox *gc = new HostComboBox("GuiVidModeResolution");
    gc->setLabel(QObject::tr("GUI"));
    gc->setLabelAboveWidget(true);
    gc->setHelpText(QObject::tr("Resolution of screen "
                    "when not watching a video."));
    
    const vector<DisplayResScreen> scr = GetVideoModes();
    for (uint i=0; i<scr.size(); ++i)
    {
        int w = scr[i].Width(), h = scr[i].Height();
        QString sel = QString("%1x%2").arg(w).arg(h);
        gc->addSelection(sel, sel);
    }
    
    // if no resolution setting, set it with a reasonable initial value
    if (scr.size() && ("" == gContext->GetSetting("GuiVidModeResolution")))
    {
        int w = 0, h = 0;
        gContext->GetResolutionSetting("GuiVidMode", w, h);
        if ((w <= 0) || (h <= 0))
            (w = 640), (h = 480);

        DisplayResScreen dscr(w, h, -1, -1, -1.0, 0);
        short rate = -1;
        int i = DisplayResScreen::FindBestMatch(scr, dscr, rate);
        gc->setValue((i >= 0) ? i : scr.size()-1);
    }

    return gc;
}

static HostComboBox *TVVidModeResolution(int idx=-1)
{
    QString dhelp = QObject::tr("Default screen resolution "
                                "when watching a video.");
    QString ohelp = QObject::tr("Screen resolution when watching a "
                                "video at a specific resolution.");

    QString qstr = (idx<0) ? "TVVidModeResolution" :
        QString("TVVidModeResolution%1").arg(idx);
    HostComboBox *gc = new HostComboBox(qstr);
    QString lstr = (idx<0) ? QObject::tr("Video Output") :
        ((idx<1) ? QObject::tr("Output") : "");
    QString hstr = (idx<0) ? dhelp : ohelp;

    gc->setLabel(lstr);
    gc->setLabelAboveWidget(idx<1);
    gc->setHelpText(hstr);
    
    const vector<DisplayResScreen> scr = GetVideoModes();
    for (uint i=0; i<scr.size(); ++i)
    {        
        QString sel = QString("%1x%2").arg(scr[i].Width()).arg(scr[i].Height());
        gc->addSelection(sel, sel);
    }
    return gc;
}

static HostRefreshRateComboBox *TVVidModeRefreshRate(int idx=-1)
{
    QString dhelp = QObject::tr("Default refresh rate "
                                "when watching a video.");
    QString ohelp = QObject::tr("Refresh rate when watching a "
                                "video at a specific resolution.");
    QString qstr = (idx<0) ? "TVVidModeRefreshRate" :
        QString("TVVidModeRefreshRate%1").arg(idx);
    HostRefreshRateComboBox *gc = new HostRefreshRateComboBox(qstr);
    QString lstr = (idx<1) ? QObject::tr("Rate") : "";
    QString hstr = (idx<0) ? dhelp : ohelp;

    gc->setLabel(lstr);
    gc->setLabelAboveWidget(idx<1);
    gc->setHelpText(hstr);
    gc->setEnabled(false);
    return gc;
}

static HostComboBox *TVVidModeForceAspect(int idx=-1)
{
    QString dhelp = QObject::tr("Aspect ratio when watching a video.");
    QString ohelp = QObject::tr("Aspect ratio when watching a "
                    "video at a specific resolution.");

    QString qstr = (idx<0) ? "TVVidModeForceAspect" :
        QString("TVVidModeForceAspect%1").arg(idx);
    HostComboBox *gc = new HostComboBox(qstr);
    gc->setLabel( (idx<1) ? QObject::tr("Aspect") : "" );
    gc->setLabelAboveWidget(idx<1);

    QString hstr = (idx<0) ? dhelp : ohelp;
    gc->setHelpText(hstr+"  "+
        QObject::tr("Leave at \"Default\" to use ratio reported by "
                    "the monitor.  Set to 16:9 or 4:3 to "
                    "force a specific aspect ratio."));
    gc->addSelection(QObject::tr("Default"), "0.0");
    gc->addSelection("16:9", "1.77777777777");
    gc->addSelection("4:3",  "1.33333333333");
    return gc;
}

class VideoModeSettings : public TriggeredConfigurationGroup
{
  public:
    VideoModeSettings() :
        TriggeredConfigurationGroup(false, true, false, false)
    {
        setLabel(QObject::tr("Video Mode Settings"));
        setUseLabel(false);

        Setting *videomode = UseVideoModes();
        addChild(videomode);
        setTrigger(videomode);

        ConfigurationGroup* defaultsettings =
            new HorizontalConfigurationGroup(false, false);

        HostComboBox *res = TVVidModeResolution();
        HostRefreshRateComboBox *rate = TVVidModeRefreshRate();
        defaultsettings->addChild(GuiVidModeResolution());
        defaultsettings->addChild(res);
        defaultsettings->addChild(rate);
        defaultsettings->addChild(TVVidModeForceAspect());
        connect(res, SIGNAL(valueChanged(const QString&)),
                rate, SLOT(ChangeResolution(const QString&)));

        ConfigurationGroup* overrides =
            new GridConfigurationGroup(5, true, true, false, true);
        overrides->setLabel("Overrides for specific video sizes");
            
        for (int idx = 0; idx < 3; ++idx)
        {
            //input side
            overrides->addChild(VidModeWidth(idx));
            overrides->addChild(VidModeHeight(idx));
            // output side
            overrides->addChild(res = TVVidModeResolution(idx));
            overrides->addChild(rate = TVVidModeRefreshRate(idx));
            overrides->addChild(TVVidModeForceAspect(idx));
            connect(res, SIGNAL(valueChanged(const QString&)),
                    rate, SLOT(ChangeResolution(const QString&)));
        }

        ConfigurationGroup* settings = new VerticalConfigurationGroup(false);
        settings->addChild(defaultsettings);
        settings->addChild(overrides);

        addTarget("1", settings);
        addTarget("0", new VerticalConfigurationGroup(true));
    }
};
#endif

static HostCheckBox *HideMouseCursor()
{
    HostCheckBox *gc = new HostCheckBox("HideMouseCursor");
    gc->setLabel(QObject::tr("Hide Mouse Cursor in MythTV"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("Toggles mouse cursor visibility. "
                    "Most of the MythTV GUI does not respond "
                    "to mouse clicks. Use this option to avoid "
                    "\"losing\" your mouse cursor."));
    return gc;
};


static HostCheckBox *RunInWindow()
{
    HostCheckBox *gc = new HostCheckBox("RunFrontendInWindow");
    gc->setLabel(QObject::tr("Run the frontend in a window"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Toggles between windowed and "
                    "borderless operation."));
    return gc;
}

static HostCheckBox *RandomTheme()
{
    HostCheckBox *gc = new HostCheckBox("RandomTheme");
    gc->setLabel(QObject::tr("Use a random theme"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Use a random theme each time MythTV is "
                    "started."));
    return gc;
}

static HostComboBox *MythDateFormat()
{
    HostComboBox *gc = new HostComboBox("DateFormat");
    gc->setLabel(QObject::tr("Date format"));

    QDate sampdate = QDate::currentDate();
    QString sampleStr =
        QObject::tr("Samples are shown using today's date.");

    if (sampdate.month() == sampdate.day())
    {
        sampdate = sampdate.addDays(1);
        sampleStr =
            QObject::tr("Samples are shown using tomorrow's date.");
    }

    gc->addSelection(sampdate.toString("ddd MMM d"), "ddd MMM d");
    gc->addSelection(sampdate.toString("ddd d MMM"), "ddd d MMM");
    gc->addSelection(sampdate.toString("ddd MMMM d"), "ddd MMMM d");
    gc->addSelection(sampdate.toString("ddd d MMMM"), "ddd d MMMM");
    gc->addSelection(sampdate.toString("dddd MMM d"), "dddd MMM d");
    gc->addSelection(sampdate.toString("dddd d MMM"), "dddd d MMM");
    gc->addSelection(sampdate.toString("MMM d"), "MMM d");
    gc->addSelection(sampdate.toString("d MMM"), "d MMM");
    gc->addSelection(sampdate.toString("MM/dd"), "MM/dd");
    gc->addSelection(sampdate.toString("dd/MM"), "dd/MM");
    gc->addSelection(sampdate.toString("MM.dd"), "MM.dd");
    gc->addSelection(sampdate.toString("dd.MM"), "dd.MM");
    gc->addSelection(sampdate.toString("M/d/yyyy"), "M/d/yyyy");
    gc->addSelection(sampdate.toString("d/M/yyyy"), "d/M/yyyy");
    gc->addSelection(sampdate.toString("MM.dd.yyyy"), "MM.dd.yyyy");
    gc->addSelection(sampdate.toString("dd.MM.yyyy"), "dd.MM.yyyy");
    gc->addSelection(sampdate.toString("yyyy-MM-dd"), "yyyy-MM-dd");
    gc->addSelection(sampdate.toString("ddd MMM d yyyy"), "ddd MMM d yyyy");
    gc->addSelection(sampdate.toString("ddd d MMM yyyy"), "ddd d MMM yyyy");
    gc->addSelection(sampdate.toString("ddd yyyy-MM-dd"), "ddd yyyy-MM-dd");
    gc->setHelpText(QObject::tr("Your preferred date format.") + " " +
                    sampleStr);
    return gc;
}

static HostComboBox *MythShortDateFormat()
{
    HostComboBox *gc = new HostComboBox("ShortDateFormat");
    gc->setLabel(QObject::tr("Short Date format"));

    QDate sampdate = QDate::currentDate();
    QString sampleStr =
        QObject::tr("Samples are shown using today's date.");

    if (sampdate.month() == sampdate.day())
    {
        sampdate = sampdate.addDays(1);
        sampleStr =
            QObject::tr("Samples are shown using tomorrow's date.");
    }

    gc->addSelection(sampdate.toString("M/d"), "M/d");
    gc->addSelection(sampdate.toString("d/M"), "d/M");
    gc->addSelection(sampdate.toString("MM/dd"), "MM/dd");
    gc->addSelection(sampdate.toString("dd/MM"), "dd/MM");
    gc->addSelection(sampdate.toString("MM.dd"), "MM.dd");
    gc->addSelection(sampdate.toString("dd.MM."), "dd.MM.");
    gc->addSelection(sampdate.toString("M.d."), "M.d.");
    gc->addSelection(sampdate.toString("d.M."), "d.M.");
    gc->addSelection(sampdate.toString("MM-dd"), "MM-dd");
    gc->addSelection(sampdate.toString("dd-MM"), "dd-MM");
    gc->addSelection(sampdate.toString("MMM d"), "MMM d");
    gc->addSelection(sampdate.toString("d MMM"), "d MMM");
    gc->addSelection(sampdate.toString("ddd d"), "ddd d");
    gc->addSelection(sampdate.toString("d ddd"), "d ddd");
    gc->addSelection(sampdate.toString("ddd M/d"), "ddd M/d");
    gc->addSelection(sampdate.toString("ddd d/M"), "ddd d/M");
    gc->addSelection(sampdate.toString("M/d ddd"), "M/d ddd");
    gc->addSelection(sampdate.toString("d/M ddd"), "d/M ddd");
    gc->setHelpText(QObject::tr("Your preferred short date format.") + " " +
                    sampleStr);
    return gc;
}

static HostComboBox *MythTimeFormat()
{
    HostComboBox *gc = new HostComboBox("TimeFormat");
    gc->setLabel(QObject::tr("Time format"));

    QTime samptime = QTime::currentTime();

    gc->addSelection(samptime.toString("h:mm AP"), "h:mm AP");
    gc->addSelection(samptime.toString("h:mm ap"), "h:mm ap");
    gc->addSelection(samptime.toString("hh:mm AP"), "hh:mm AP");
    gc->addSelection(samptime.toString("hh:mm ap"), "hh:mm ap");
    gc->addSelection(samptime.toString("h:mm"), "h:mm");
    gc->addSelection(samptime.toString("hh:mm"), "hh:mm");
    gc->addSelection(samptime.toString("hh.mm"), "hh.mm");
    gc->setHelpText(QObject::tr("Your preferred time format.  You must choose "
                    "a format with \"AM\" or \"PM\" in it, otherwise your "
                    "time display will be 24-hour or \"military\" time."));
    return gc;
}

static HostComboBox *ThemeFontSizeType()
{
    HostComboBox *gc = new HostComboBox("ThemeFontSizeType");
    gc->setLabel(QObject::tr("Font size"));
    gc->addSelection(QObject::tr("default"), "default");
    gc->addSelection(QObject::tr("small"), "small");
    gc->addSelection(QObject::tr("big"), "big");
    gc->setHelpText(QObject::tr("default: TV, small: monitor, big:"));
    return gc;
}

static HostComboBox *ThemePainter()
{
    HostComboBox *gc = new HostComboBox("ThemePainter");
    gc->setLabel(QObject::tr("Paint Engine"));
    gc->addSelection(QObject::tr("Qt"), "qt");
    gc->addSelection(QObject::tr("OpenGL"), "opengl");
    gc->setHelpText(QObject::tr("This selects what MythTV uses to draw.  If "
                    "you have decent hardware, select OpenGL. Changing this "
                    "requires a restart."));
    return gc;
}

ThemeSelector::ThemeSelector(QString label):
    HostImageSelect(label) {

    ThemeType themetype = THEME_UI;

    if (label == "Theme")
    {
        themetype = THEME_UI;
        setLabel(QObject::tr("UI Theme"));
    }
    else if (label == "OSDTheme")
    {
        themetype = THEME_OSD;
        setLabel(QObject::tr("OSD Theme"));
    }
    else if (label == "MenuTheme")
    {
        themetype = THEME_MENU;
        setLabel(QObject::tr("Menu Theme"));
    }

    QDir themes(gContext->GetThemesParentDir());
    themes.setFilter(QDir::Dirs);
    themes.setSorting(QDir::Name | QDir::IgnoreCase);

    const QFileInfoList *fil = themes.entryInfoList(QDir::Dirs);
    if (!fil)
        return;

    QFileInfoListIterator it( *fil );
    QFileInfo *theme;

    for( ; it.current() != 0 ; ++it ) {
        theme = it.current();

        if (theme->fileName() == "." || theme->fileName() == ".."
                || theme->fileName() == "default"
                || theme->fileName() == "default-wide")
            continue;

        QFileInfo preview;
        QString name;

        ThemeInfo *themeinfo = new ThemeInfo(theme->absFilePath());

        if (!themeinfo)
            continue;

        name = themeinfo->Name();
        preview = QFileInfo(themeinfo->PreviewPath());

        if (name.isEmpty() || !(themeinfo->Type() & themetype))
        {
            delete themeinfo;
            continue;
        }

        if ((themeinfo->Type() & THEME_UI) & themeinfo->IsWide())
            name += QString(" (%1)").arg(QObject::tr("Widescreen"));

        if (!preview.exists())
        {
            VERBOSE(VB_IMPORTANT, QString("Theme %1 missing preview image.")
                                    .arg(theme->fileName()));
            QString defaultpreview = themes.absPath();
            if (themeinfo->IsWide())
            {
                defaultpreview += "/default-wide/preview.png";
            }
            else
            {
                defaultpreview += "/default/preview.png";
            }
            preview = QFileInfo(defaultpreview);
        }

        delete themeinfo;

        QImage* previewImage = new QImage(preview.absFilePath());
        if (previewImage->width() == 0 || previewImage->height() == 0) {
            VERBOSE(VB_IMPORTANT, QString("Problem reading theme preview image"
                                          " %1").arg(preview.filePath()));
            continue;
        }

        addImageSelection(name, previewImage, theme->fileName());
    }

    if (themetype & THEME_UI)
        setValue("G.A.N.T");
}

class StyleSetting: public HostComboBox {
public:
    StyleSetting():
        HostComboBox("Style") {
        setLabel(QObject::tr("Qt Style"));
        fillSelections();
        setHelpText(QObject::tr("At startup, MythTV will change the Qt "
                    "widget style to this setting.  If \"Desktop Style\" "
                    "is selected, MythTV will use the existing desktop "
                    "setting."));
    };

    void fillSelections(void) {
        clearSelections();
        addSelection(QObject::tr("Desktop Style"), "");
        QStyleFactory factory;
        QStringList list = factory.keys();
        QStringList::iterator iter = list.begin();
        for (; iter != list.end(); iter++ )
            addSelection(*iter);
    };

    void load() {
        fillSelections();
        HostComboBox::load();
    };
};

static HostComboBox *ChannelFormat()
{
    HostComboBox *gc = new HostComboBox("ChannelFormat");
    gc->setLabel(QObject::tr("Channel format"));
    gc->addSelection(QObject::tr("number"), "<num>");
    gc->addSelection(QObject::tr("number callsign"), "<num> <sign>");
    gc->addSelection(QObject::tr("number name"), "<num> <name>");
    gc->addSelection(QObject::tr("callsign"), "<sign>");
    gc->addSelection(QObject::tr("name"), "<name>");
    gc->setHelpText(QObject::tr("Your preferred channel format."));
    gc->setValue(1);
    return gc;
}

static HostComboBox *LongChannelFormat()
{
    HostComboBox *gc = new HostComboBox("LongChannelFormat");
    gc->setLabel(QObject::tr("Long Channel format"));
    gc->addSelection(QObject::tr("number"), "<num>");
    gc->addSelection(QObject::tr("number callsign"), "<num> <sign>");
    gc->addSelection(QObject::tr("number name"), "<num> <name>");
    gc->addSelection(QObject::tr("callsign"), "<sign>");
    gc->addSelection(QObject::tr("name"), "<name>");
    gc->setHelpText(QObject::tr("Your preferred long channel format."));
    gc->setValue(2);
    return gc;
}

static HostCheckBox *SmartChannelChange()
{
    HostCheckBox *gc = new HostCheckBox("SmartChannelChange");
    gc->setLabel(QObject::tr("Change channels immediately without select"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("When a complete channel number is entered "
                    "MythTV will switch to that channel immediately without "
                    "requiring you to hit the select button."));
    return gc;
}

static GlobalCheckBox *LastFreeCard()
{
    GlobalCheckBox *bc = new GlobalCheckBox("LastFreeCard");
    bc->setLabel(QObject::tr("Avoid conflicts between live TV and "
                 "scheduled shows"));
    bc->setValue(false);
    bc->setHelpText(QObject::tr("If enabled, live TV will choose a tuner card "
                    "that is less likely to have scheduled recordings "
                    "rather than the best card available."));
    return bc;
}

static GlobalCheckBox *LiveTVPriority()
{
    GlobalCheckBox *bc = new GlobalCheckBox("LiveTVPriority");
    bc->setLabel(QObject::tr("Allow live TV to move scheduled shows"));
    bc->setValue(false);
    bc->setHelpText(QObject::tr("If enabled, scheduled recordings will "
                    "be moved to other cards (where possible), so that "
                    "live TV will not be interrupted."));
    return bc;
}

static HostSpinBox *QtFontBig()
{
    HostSpinBox *gs = new HostSpinBox("QtFontBig", 1, 48, 1);
    gs->setLabel(QObject::tr("\"Big\" font"));
    gs->setValue(25);
    gs->setHelpText(QObject::tr("Default size is 25."));
    return gs;
}

static HostSpinBox *QtFontMedium()
{
    HostSpinBox *gs = new HostSpinBox("QtFontMedium", 1, 48, 1);
    gs->setLabel(QObject::tr("\"Medium\" font"));
    gs->setValue(16);
    gs->setHelpText(QObject::tr("Default size is 16."));
    return gs;
}

static HostSpinBox *QtFontSmall()
{
    HostSpinBox *gs = new HostSpinBox("QtFontSmall", 1, 48, 1);
    gs->setLabel(QObject::tr("\"Small\" font"));
    gs->setValue(12);
    gs->setHelpText(QObject::tr("Default size is 12."));
    return gs;
}

static HostSpinBox *QtFonTweak()
{
    HostSpinBox *gs = new HostSpinBox("QtFonTweak", -30, 30, 1);
    gs->setLabel(QObject::tr("Fine tune font size (\%)"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Fine tune all font sizes by this percentage. "
                    "Font sizes should be the correct relative size if the "
                    "X11 DPI (dots per inch) is set to 100."));
    return gs;
}

// EPG settings
static HostCheckBox *EPGScrollType()
{
    HostCheckBox *gc = new HostCheckBox("EPGScrollType");
    gc->setLabel(QObject::tr("Floating Program Guide Selector"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If enabled, the program guide's selector "
                    "will be free to move throughout the guide, otherwise "
                    "it will stay in the center of the guide at all times."));
    return gc;
}

static HostComboBox *EPGFillType()
{
    HostComboBox *gc = new HostComboBox("EPGFillType");
    gc->setLabel(QObject::tr("Guide Shading Method"));
    gc->addSelection(QObject::tr("Alpha - Transparent (CPU Usage - High)"),
                     QString::number((int)UIGuideType::Alpha));
    gc->addSelection(QObject::tr("Blender - Transparent (CPU Usage - Middle)"),
                     QString::number((int)UIGuideType::Dense));
    gc->addSelection(QObject::tr("Eco - Transparent (CPU Usage - Low)"),
                     QString::number((int)UIGuideType::Eco));
    gc->addSelection(QObject::tr("Solid (CPU Usage - Middle)"),
                     QString::number((int)UIGuideType::Solid));
    return gc;
};

static HostCheckBox *EPGShowCategoryColors()
{
    HostCheckBox *gc = new HostCheckBox("EPGShowCategoryColors");
    gc->setLabel(QObject::tr("Display Genre Colors"));
    gc->setHelpText(QObject::tr("Colorize program guide using "
                    "genre colors. (Not available for all grabbers.)"));
    gc->setValue(true);
    return gc;
}

static HostCheckBox *EPGShowCategoryText()
{
    HostCheckBox *gc = new HostCheckBox("EPGShowCategoryText");
    gc->setLabel(QObject::tr("Display Genre Text"));
    gc->setHelpText(QObject::tr("(Not available for all grabbers.)"));
    gc->setValue(true);
    return gc;
}

static HostCheckBox *EPGShowChannelIcon()
{
    HostCheckBox *gc = new HostCheckBox("EPGShowChannelIcon");
    gc->setLabel(QObject::tr("Display the channel icons"));
    gc->setHelpText(QObject::tr("Display the icons/logos for the channels "
                    "in the guide.  See section 9.5 of the "
                    "Installation Guide for how to grab icons."));
    gc->setValue(true);
    return gc;
}

static HostCheckBox *EPGShowFavorites()
{
    HostCheckBox *gc = new HostCheckBox("EPGShowFavorites");
    gc->setLabel(QObject::tr("Only display 'favorite' channels"));
    gc->setHelpText(QObject::tr("If enabled, the EPG will initially display "
                    "only the channels marked as favorites. Pressing "
                    "\"4\" will toggle between displaying favorites and all "
                    "channels."));
    gc->setValue(false);
    return gc;
}

static HostSpinBox *EPGChanDisplay()
{
    HostSpinBox *gs = new HostSpinBox("chanPerPage", 3, 12, 1);
    gs->setLabel(QObject::tr("Channels to Display"));
    gs->setValue(5);
    return gs;
}

static HostSpinBox *EPGTimeDisplay()
{
    HostSpinBox *gs = new HostSpinBox("timePerPage", 1, 5, 1);
    gs->setLabel(QObject::tr("Time Blocks (30 mins) to Display"));
    gs->setValue(4);
    return gs;
}

static GlobalCheckBox *EPGEnableJumpToChannel()
{
    GlobalCheckBox *gc = new GlobalCheckBox("EPGEnableJumpToChannel");
    gc->setLabel(QObject::tr("Allow channel jumping in guide"));
    gc->setHelpText(QObject::tr("If enabled, you will be able to press numbers "
                    "and jump the selection to whatever channel you enter."));
    gc->setValue(false);
    return gc;
}

// General RecPriorities settings

static GlobalCheckBox *GRSchedMoveHigher()
{
    GlobalCheckBox *bc = new GlobalCheckBox("SchedMoveHigher");
    bc->setLabel(QObject::tr("Reschedule Higher Priorities"));
    bc->setHelpText(QObject::tr("Move higher priority programs to other "
                    "cards and showings when resolving conflicts.  This "
                    "can be used to record lower priority programs that "
                    "would otherwise not be recorded, but risks missing "
                    "a higher priority program if the schedule changes."));
    bc->setValue(true);
    return bc;
}

static GlobalCheckBox *GRSchedOpenEnd()
{
    GlobalCheckBox *bc = new GlobalCheckBox("SchedOpenEnd");
    bc->setLabel(QObject::tr("Avoid back to back recordings from different "
                    "channels"));
    bc->setHelpText(QObject::tr("If set, the scheduler will avoid assigning "
                    "shows from different channels to the same card if their "
                    "end time and start time match. This will be allowed "
                    "when necessary in order to resolve conflicts."));
    bc->setValue(false);
    return bc;
}

static GlobalSpinBox *GRDefaultStartOffset()
{
    GlobalSpinBox *bs = new GlobalSpinBox("DefaultStartOffset",
                                          -10, 30, 5, true);
    bs->setLabel(QObject::tr("Default 'Start Early' minutes for new "
                             "recording rules"));
    bs->setHelpText(QObject::tr("Set this to '0' unless you expect that the "
                    "majority of your show times will not match your TV "
                    "listings. This sets the initial start early or start "
                    "late time when rules are created. These can then be "
                    "adjusted per recording rule."));
    bs->setValue(0);
    return bs;
}

static GlobalSpinBox *GRDefaultEndOffset()
{
    GlobalSpinBox *bs = new GlobalSpinBox("DefaultEndOffset",
                                          -10, 30, 5, true);
    bs->setLabel(QObject::tr("Default 'End Late' minutes for new "
                             "recording rules"));
    bs->setHelpText(QObject::tr("Set this to '0' unless you expect that the "
                    "majority of your show times will not match your TV "
                    "listings. This sets the initial end late or end early "
                    "time when rules are created. These can then be adjusted "
                    "per recording rule."));
    bs->setValue(0);
    return bs;
}

static GlobalCheckBox *GRComplexPriority()
{
    GlobalCheckBox *bc = new GlobalCheckBox("ComplexPriority");
    bc->setLabel(QObject::tr("Complex Prioritization"));
    bc->setHelpText(QObject::tr("If set, per rule plus type priorities "
                    "will be used for primary prioritization with all other "
                    "priorities having a secondary effect. Unset so that all "
                    "are treated equally in a single total priority as "
                    "described in the HOWTO."));
    bc->setValue(false);
    return bc;
}

static GlobalSpinBox *GRPrefInputRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("PrefInputPriority", 1, 99, 1);
    bs->setLabel(QObject::tr("Preferred Input Priority"));
    bs->setHelpText(QObject::tr("Additional priority when a showing "
                    "matches the preferred input selected in the 'Scheduling "
                    "Options' section of the recording rule."));
    bs->setValue(2);
    return bs;
}

static GlobalSpinBox *GRHDTVRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("HDTVRecPriority", -99, 99, 1);
    bs->setLabel(QObject::tr("HDTV Recording Priority"));
    bs->setHelpText(QObject::tr("Additional priority when a showing "
                    "is marked as an HDTV broadcast in the TV listings."));
    bs->setValue(0);
    return bs;
}

static GlobalSpinBox *GRSingleRecordRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("SingleRecordRecPriority",
                                            -99, 99, 1);
    bs->setLabel(QObject::tr("Single Recordings Priority"));
    bs->setHelpText(QObject::tr("Single Recordings will receive this "
                    "additional recording priority value."));
    bs->setValue(1);
    return bs;
}

static GlobalSpinBox *GRWeekslotRecordRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("WeekslotRecordRecPriority",
                                            -99, 99, 1);
    bs->setLabel(QObject::tr("Weekslot Recordings Priority"));
    bs->setHelpText(QObject::tr("Weekslot Recordings will receive this "
                    "additional recording priority value."));
    bs->setValue(0);
    return bs;
}

static GlobalSpinBox *GRTimeslotRecordRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("TimeslotRecordRecPriority",
                                            -99, 99, 1);
    bs->setLabel(QObject::tr("Timeslot Recordings Priority"));
    bs->setHelpText(QObject::tr("Timeslot Recordings will receive this "
                    "additional recording priority value."));
    bs->setValue(0);
    return bs;
}

static GlobalSpinBox *GRChannelRecordRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("ChannelRecordRecPriority",
                                            -99, 99, 1);
    bs->setLabel(QObject::tr("Channel Recordings Priority"));
    bs->setHelpText(QObject::tr("Channel Recordings will receive this "
                    "additional recording priority value."));
    bs->setValue(0);
    return bs;
}

static GlobalSpinBox *GRAllRecordRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("AllRecordRecPriority",
                                            -99, 99, 1);
    bs->setLabel(QObject::tr("All Recordings Priority"));
    bs->setHelpText(QObject::tr("The 'All' Recording type will receive this "
                    "additional recording priority value."));
    bs->setValue(0);
    return bs;
}

static GlobalSpinBox *GRFindOneRecordRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("FindOneRecordRecPriority",
                                            -99, 99, 1);
    bs->setLabel(QObject::tr("Find One Recordings Priority"));
    bs->setHelpText(QObject::tr("Find One, Find Weekly and Find Daily "
                    "recording types will receive this "
                    "additional recording priority value."));
    bs->setValue(-1);
    return bs;
}

static GlobalSpinBox *GROverrideRecordRecPriority()
{
    GlobalSpinBox *bs = new GlobalSpinBox("OverrideRecordRecPriority",
                                            -99, 99, 1);
    bs->setLabel(QObject::tr("Override Recordings Priority"));
    bs->setHelpText(QObject::tr("Override Recordings will receive this "
                    "additional recording priority value."));
    bs->setValue(0);
    return bs;
}

static HostLineEdit *DefaultTVChannel()
{
    HostLineEdit *ge = new HostLineEdit("DefaultTVChannel");
    ge->setLabel(QObject::tr("Guide starts at channel"));
    ge->setValue("3");
    ge->setHelpText(QObject::tr("The program guide starts on this channel if "
                    "it is run from outside of LiveTV mode."));
    return ge;
}

static HostLineEdit *UnknownTitle()
{
    HostLineEdit *ge = new HostLineEdit("UnknownTitle");
    ge->setLabel(QObject::tr("What to call 'unknown' programs"));
    ge->setValue(QObject::tr("Unknown"));
    return ge;
}

static HostLineEdit *UnknownCategory()
{
    HostLineEdit *ge = new HostLineEdit("UnknownCategory");
    ge->setLabel(QObject::tr("What category to give 'unknown' programs"));
    ge->setValue(QObject::tr("Unknown"));
    return ge;
}

static HostCheckBox *SelectChangesChannel()
{
    HostCheckBox *gc = new HostCheckBox("SelectChangesChannel");
    gc->setLabel(QObject::tr("Use select to change the channel in the program "
                 "guide"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If enabled, the Select key will change the "
                    "channel while using the program guide during live TV.  "
                    "If disabled, the select key will bring up the recording "
                    "options screen."));
    return gc;
}

static HostSpinBox *EPGRecThreshold()
{
    HostSpinBox *gs = new HostSpinBox("SelChangeRecThreshold", 1, 600, 1);
    gs->setLabel(QObject::tr("Record Threshold"));
    gs->setValue(16);
    gs->setHelpText(QObject::tr("If the option to use Select to change the channel "
                    "is on, pressing Select on a show that is at least "
                    "this many minutes into the future will schedule a "
                    "recording."));
    return gs;
}

class AudioSettings : public TriggeredConfigurationGroup
{
  public:
     AudioSettings() :
         TriggeredConfigurationGroup(false, true, false, false)
     {
         setLabel(QObject::tr("Audio"));
         setUseLabel(false);

         addChild(AudioOutputDevice());
         addChild(PassThroughOutputDevice());

         // General boolean settings
         VerticalConfigurationGroup *vgrp0 =
             new VerticalConfigurationGroup(false, false, true, true);
         vgrp0->addChild(AC3PassThrough());
         vgrp0->addChild(DTSPassThrough());

         HorizontalConfigurationGroup *agrp =
             new HorizontalConfigurationGroup(false, false, true, true);
         agrp->addChild(MaxAudioChannels());
         agrp->addChild(AudioUpmixType());
         addChild(agrp);

         VerticalConfigurationGroup *vgrp1 =
             new VerticalConfigurationGroup(false, false, true, true);
         vgrp1->addChild(AggressiveBuffer());

         Setting* volumeControl = MythControlsVolume();
         vgrp1->addChild(volumeControl);

         HorizontalConfigurationGroup *tgrp =
             new HorizontalConfigurationGroup(false, false, true, true);
         tgrp->addChild(vgrp0);
         tgrp->addChild(vgrp1);
         addChild(tgrp);

         // Make volume control trigger for mixer settings
         setTrigger(volumeControl);

         // Mixer settings
         ConfigurationGroup *settings =
             new VerticalConfigurationGroup(false, false, true, true);
         HorizontalConfigurationGroup *mixgrp =
             new HorizontalConfigurationGroup(false, false, true, true);
         mixgrp->addChild(MixerDevice());
         mixgrp->addChild(MixerControl());
         settings->addChild(mixgrp);
         settings->addChild(MixerVolume());
         settings->addChild(PCMVolume());
         settings->addChild(IndividualMuteControl());

         // Show Mixer config only if internal volume controls enabled
         addTarget("0", new VerticalConfigurationGroup(false, false));
         addTarget("1", settings);
     };
};

static HostComboBox *MythLanguage()
{
    HostComboBox *gc = new HostComboBox("Language");
    gc->setLabel(QObject::tr("Language"));
    LanguageSettings::fillSelections(gc);
    gc->setHelpText(
        QObject::tr("Your preferred language for the user interface."));
    return gc;
}

static void ISO639_fill_selections(SelectSetting *widget, uint i)
{
    widget->clearSelections();
    QString q = QString("ISO639Language%1").arg(i);
    QString lang = gContext->GetSetting(q, "").lower();

    if ((lang.isEmpty() || lang == "aar") && 
        !gContext->GetSetting("Language", "").isEmpty())
    {
        lang = iso639_str2_to_str3(gContext->GetLanguage().lower());
    }

    QMap<int,QString>::iterator it  = _iso639_key_to_english_name.begin();
    QMap<int,QString>::iterator ite = _iso639_key_to_english_name.end();
    
    for (; it != ite; ++it)
    {
        QString desc = (*it);
        int idx = desc.find(";");
        if (idx > 0)
            desc = desc.left(idx);

        const QString il = iso639_key_to_str3(it.key());
        widget->addSelection(desc, il, il == lang);
    }
}

static HostComboBox *ISO639PreferredLanguage(uint i)
{
    HostComboBox *gc = new HostComboBox(QString("ISO639Language%1").arg(i));
    gc->setLabel(QObject::tr("Guide Language #%1").arg(i+1));
    // We should try to get language from "MythLanguage"
    // then use code 2 to code 3 map in iso639.h
    ISO639_fill_selections(gc, i);
    gc->setHelpText(
        QObject::tr("Your #%1 preferred language for "
                    "Program Guide Data and captions.").arg(i+1));
    return gc;
}

static HostCheckBox *EnableXbox()
{
    HostCheckBox *gc = new HostCheckBox("EnableXbox");
    gc->setLabel(QObject::tr("Enable Xbox Hardware"));
    gc->setHelpText(QObject::tr("This enables support for Xbox specific "
                    "hardware. Requires a frontend restart for changes to "
                    "take effect."));
    gc->setValue(false);
    return gc;
}

static HostCheckBox *NetworkControlEnabled()
{
    HostCheckBox *gc = new HostCheckBox("NetworkControlEnabled");
    gc->setLabel(QObject::tr("Enable Network Remote Control interface"));
    gc->setHelpText(QObject::tr("This enables support for controlling "
                    "mythfrontend over the network."));
    gc->setValue(false);
    return gc;
}

static HostSpinBox *NetworkControlPort()
{
    HostSpinBox *gs = new HostSpinBox("NetworkControlPort", 1025, 65535, 1);
    gs->setLabel(QObject::tr("Network Remote Control Port"));
    gs->setValue(6546);
    gs->setHelpText(QObject::tr("This specifies what port the Network Remote "
                    "Control interface will listen on for new connections."));
    return gs;
}

static HostCheckBox *RealtimePriority()
{
    HostCheckBox *gc = new HostCheckBox("RealtimePriority");
    gc->setLabel(QObject::tr("Enable realtime priority threads"));
    gc->setHelpText(QObject::tr("When running mythfrontend with root "
                    "privileges, some threads can be given enhanced priority. "
                    "Disable this if mythfrontend freezes during video "
                    "playback."));
    gc->setValue(true);
    return gc;
}

static HostComboBox *XboxBlinkBIN()
{
    HostComboBox *gc = new HostComboBox("XboxBlinkBIN");
    gc->setLabel(QObject::tr("Xbox Linux Distribution"));
    gc->addSelection("GentooX","led");
    gc->addSelection(QObject::tr("Other"),"blink");
    gc->setHelpText(QObject::tr("The program used to control the "
                    "LED on the Xbox is dependant on which distribution is "
                    "installed. \"led\" will be used on GentooX, \"blink\" "
                    "on other Xbox distributions."));
    return gc;
}

static HostComboBox *XboxLEDDefault()
{
    HostComboBox *gc = new HostComboBox("XboxLEDDefault");
    gc->setLabel(QObject::tr("Default LED color"));
    gc->addSelection(QObject::tr("Off"), "nnnn");
    gc->addSelection(QObject::tr("Green"),"gggg");
    gc->addSelection(QObject::tr("Orange"),"oooo");
    gc->addSelection(QObject::tr("Red"),"rrrr");
    gc->setHelpText(QObject::tr("Sets the LED color when it is not "
                    "being used for status indication."));
    return gc;
}

static HostComboBox *XboxLEDRecording()
{
    HostComboBox *gc = new HostComboBox("XboxLEDRecording");
    gc->setLabel(QObject::tr("Recording LED mode"));
    gc->addSelection(QObject::tr("Off"), "nnnn");
    gc->addSelection(QObject::tr("Green"),"gggg");
    gc->addSelection(QObject::tr("Orange"),"oooo");
    gc->addSelection(QObject::tr("Red"),"rrrr");
    gc->setHelpText(QObject::tr("Sets the LED color when a backend is "
                    "recording."));
    return gc;
}

static HostCheckBox *XboxLEDNonLiveTV()
{
    HostCheckBox *gc = new HostCheckBox("XboxLEDNonLiveTV");
    gc->setLabel(QObject::tr("Ignore LiveTV Recordings"));
    gc->setHelpText(QObject::tr("Recording monitoring will "
                    "ignore LiveTV recordings."));
    gc->setValue(false);
    return gc;
}

static HostSpinBox *XboxCheckRec()
{
    HostSpinBox *gs = new HostSpinBox("XboxCheckRec", 1, 600, 2);
    gs->setLabel(QObject::tr("Recording Check Frequency"));
    gs->setValue(5);
    gs->setHelpText(QObject::tr("This specifies how frequently "
                    "(in seconds) to check if a recording is in "
                    "progress in order to update the Xbox LED."));
    return gs;
}

static HostCheckBox *EnableMediaMon()
{
    HostCheckBox *gc = new HostCheckBox("MonitorDrives");
    gc->setLabel(QObject::tr("Monitor CD/DVD") +
                 QObject::tr(" (and other removable devices)"));
    gc->setHelpText(QObject::tr("This enables support for monitoring "
                    "your CD/DVD drives for new disks and launching "
                    "the proper plugin to handle them."));
    gc->setValue(false);
    return gc;
}

static HostCheckBox *EnableMediaEvents()
{
    HostCheckBox *gc = new HostCheckBox("MediaChangeEvents");
    gc->setLabel(QObject::tr("Use new media"));
    gc->setHelpText(QObject::tr("This will cause MythTV to jump, "
                    "to an appropriate plugin, when new media is inserted."));
    gc->setValue(false);
    return gc;
}

static HostLineEdit *IgnoreMedia()
{
    HostLineEdit *ge = new HostLineEdit("IgnoreDevices");
    ge->setLabel(QObject::tr("Ignore Devices"));
    ge->setValue("");
    ge->setHelpText(QObject::tr("If there are any devices that you do not want "
                                "to be monitored, list them here with commas "
                                "in-between. The plugins will ignore them"));
    return ge;
}

class MythMediaSettings : public TriggeredConfigurationGroup
{
  public:
     MythMediaSettings() :
         TriggeredConfigurationGroup(false, true, false, false)
     {
         setLabel(QObject::tr("MythMediaMonitor"));
         setUseLabel(false);

         Setting* enabled = EnableMediaMon();
         addChild(enabled);
         setTrigger(enabled);

         ConfigurationGroup* settings = new VerticalConfigurationGroup(false);
         settings->addChild(EnableMediaEvents());
         settings->addChild(IgnoreMedia());
         addTarget("1", settings);

         // show nothing if fillEnabled is off
         addTarget("0", new VerticalConfigurationGroup(true));
     };
};


static HostComboBox *DisplayGroupTitleSort()
{
    HostComboBox *gc = new HostComboBox("DisplayGroupTitleSort");
    gc->setLabel(QObject::tr("Sort Titles"));
    gc->addSelection(QObject::tr("Alphabetically"),
            QString::number(PlaybackBox::TitleSortAlphabetical));
    gc->addSelection(QObject::tr("By Recording Priority"),
            QString::number(PlaybackBox::TitleSortRecPriority));
    gc->setHelpText(QObject::tr("Sets the Title sorting order when the "
                    "view is set to Titles only."));
    return gc;
}

class DefaultViewSettings : public TriggeredConfigurationGroup
{
  public:
    DefaultViewSettings() :
        TriggeredConfigurationGroup(false, false, true, true)
    {
        HostComboBox *defaultView = DefaultView();
        addChild(defaultView);
        setTrigger(defaultView);

        HostComboBox *titleSort = DisplayGroupTitleSort();

        for (unsigned int ii = 0; ii < PlaybackBox::ViewTypes; ii++)
        {
            if (ii == PlaybackBox::TitlesOnly)
                addTarget(QString::number(ii), titleSort);
            else
                addTarget(QString::number(ii),
                        new VerticalConfigurationGroup(false, false));
        }
    }
};

static HostCheckBox *PlaybackWatchList()
{
    HostCheckBox *gc = new HostCheckBox("PlaybackWatchList");
    gc->setLabel(QObject::tr("Include the 'Watch List' group"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("The 'Watch List' is an abbreviated list of "
                                "recordings sorted to highlight series and "
                                "shows that need attention in order to "
                                "keep up to date."));
    return gc;
}

static HostCheckBox *PlaybackWLStart()
{
    HostCheckBox *gc = new HostCheckBox("PlaybackWLStart");
    gc->setLabel(QObject::tr("Start from the Watch List view"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If set, the 'Watch List' will be the "
                    "initial view each time you enter the "
                    "Watch Recordings screen"));
    return gc;
}

static HostCheckBox *PlaybackWLAutoExpire()
{
    HostCheckBox *gc = new HostCheckBox("PlaybackWLAutoExpire");
    gc->setLabel(QObject::tr("Exclude recordings not set for Auto-Expire"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("Set this if you turn off auto-expire only "
                                "for recordings that you've seen and intend "
                                "to keep. This option will exclude these "
                                "recordings from the 'Watch List'."));
    return gc;
}

static HostSpinBox *PlaybackWLMaxAge()
{
    HostSpinBox *gs = new HostSpinBox("PlaybackWLMaxAge", 30, 180, 10);
    gs->setLabel(QObject::tr("Maximum days counted in the score"));
    gs->setValue(60);
    gs->setHelpText(QObject::tr("The 'Watch List' scores are based on 1 point "
                                "equals one day since recording. This option "
                                "limits the maximum score due to age and "
                                "affects other weighting factors."));
    return gs;
}

static HostSpinBox *PlaybackWLBlackOut()
{
    HostSpinBox *gs = new HostSpinBox("PlaybackWLBlackOut", 1, 5, 1);
    gs->setLabel(QObject::tr("Days to exclude weekly episodes after delete"));
    gs->setValue(2);
    gs->setHelpText(QObject::tr("When an episode is deleted or marked as "
                                "watched, other episodes of the series are "
                                "excluded from the 'Watch List' for this "
                                "interval of time. Daily shows also have a "
                                "smaller interval based on this setting."));
    return gs;
}

class WatchListSettings : public TriggeredConfigurationGroup
{
  public:
     WatchListSettings() :
         TriggeredConfigurationGroup(false, false, true, true)
     {

         Setting* watchList = PlaybackWatchList();
         addChild(watchList);
         setTrigger(watchList);

         ConfigurationGroup* settings = new VerticalConfigurationGroup(false);
         settings->addChild(PlaybackWLStart());
         settings->addChild(PlaybackWLAutoExpire());
         settings->addChild(PlaybackWLMaxAge());
         settings->addChild(PlaybackWLBlackOut());
         addTarget("1", settings);

         addTarget("0", new VerticalConfigurationGroup(true));
    };
};

#ifdef USING_IVTV
static HostCheckBox *PVR350OutputEnable()
{
    HostCheckBox *gc = new HostCheckBox("PVR350OutputEnable");
    gc->setLabel(QObject::tr("Use the PVR-350's TV out / MPEG decoder"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("MythTV can use the PVR-350's TV out and MPEG "
                    "decoder for high quality playback.  This requires that "
                    "the ivtv-fb kernel module is also loaded and configured "
                    "properly."));
    return gc;
}
#endif

#ifdef USING_IVTV
PVR350VideoDevice::PVR350VideoDevice() :
    PathSetting(this, false),
    HostDBStorage(this, "PVR350VideoDev")
{
    setLabel(QObject::tr("Video device for the PVR-350 MPEG decoder"));

    QDir dev("/dev/v4l", "video*", QDir::Name, QDir::System);
    fillSelectionsFromDir(dev, 16, 31, QString::null, "ivtv", false);

    dev.setPath("/dev");
    fillSelectionsFromDir(dev, 16, 31, QString::null, "ivtv", false);
}
#endif // USING_IVTV

#ifdef USING_IVTV
uint PVR350VideoDevice::fillSelectionsFromDir(const QDir &dir,
                                              uint minor_min, uint minor_max,
                                              QString card, QString driver,
                                              bool allow_duplicates)
{
    uint cnt = 0;
    const QFileInfoList *il = dir.entryInfoList();
    if (!il)
        return cnt;
        
    QFileInfoListIterator it( *il );
    QFileInfo *fi;

    for (; (fi = it.current()) != 0; ++it)
    {
        struct stat st;
        QString filepath = fi->absFilePath();
        int err = lstat(filepath, &st);

        if (0 != err)
        {
            VERBOSE(VB_IMPORTANT,
                    QString("Could not stat file: %1").arg(filepath));
            continue;
        }

        // is this is a character device?
        if (!S_ISCHR(st.st_mode))
            continue;

        // is this device is in our minor range?
        uint minor_num = minor(st.st_rdev);
        if (minor_min > minor_num || minor_max < minor_num)
            continue;

        // ignore duplicates if allow_duplicates not set
        if (!allow_duplicates && minor_list[minor_num])
            continue;

        // if the driver returns any info add this device to our list
        int videofd = open(filepath.ascii(), O_RDWR);
        if (videofd >= 0)
        {
            QString cn, dn;
            if (CardUtil::GetV4LInfo(videofd, cn, dn) &&
                (driver.isEmpty() || (dn == driver))  &&
                (card.isEmpty()   || (cn == card)))
            {
                addSelection(filepath);
                cnt++;
            }
            close(videofd);
        }

        // add to list of minors discovered to avoid duplicates
        minor_list[minor_num] = 1;
    }

    return cnt;
};
#endif

#ifdef USING_IVTV
static HostSpinBox *PVR350EPGAlphaValue()
{
    HostSpinBox *gs = new HostSpinBox("PVR350EPGAlphaValue", 0, 255, 1);
    gs->setLabel(QObject::tr("Program Guide Alpha"));
    gs->setValue(164);
    gs->setHelpText(QObject::tr("How much to blend the program guide over the "
                    "live TV image.  Higher numbers mean more guide and less "
                    "TV."));
    return gs;
}
#endif

#ifdef USING_IVTV
static HostCheckBox *PVR350UseInternalSound()
{
    HostCheckBox *gc = new HostCheckBox("PVR350InternalAudioOnly");
    gc->setLabel(QObject::tr("TV audio through PVR-350 only"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr(
                        "Normally, PVR-350 audio is looped into a sound card; "
                        "here you can indicate when that is not the case. "
                        "MythTV cannot control TV volume when this option "
                        "is checked."));
    return gc;
}
#endif

#ifdef USING_OPENGL_VSYNC
static HostCheckBox *UseOpenGLVSync()
{
    HostCheckBox *gc = new HostCheckBox("UseOpenGLVSync");
    gc->setLabel(QObject::tr("Enable OpenGL vertical sync for timing"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr(
                        "If it is supported by your hardware/drivers, "
                        "MythTV will use OpenGL vertical syncing for "
                        "video timing, reducing frame jitter."));
    return gc;
}
#endif

#ifdef USING_IVTV
class PVR350HWDecoderSettings : public TriggeredConfigurationGroup
{
  public:
     PVR350HWDecoderSettings(const QString &tmp) :
         TriggeredConfigurationGroup(false, true, false, false)
     {
         setLabel(QObject::tr("PVR-350 Hardware Decoder Settings") + tmp);

         Setting *pvr350output = PVR350OutputEnable();
         addChild(pvr350output);
         setTrigger(pvr350output);

         ConfigurationGroup* settings = new VerticalConfigurationGroup(false);
         settings->addChild(new PVR350VideoDevice());
         settings->addChild(PVR350EPGAlphaValue());
         settings->addChild(PVR350UseInternalSound());

         addTarget("1", settings);
         addTarget("0", new VerticalConfigurationGroup(true));
    };
};
#endif

static GlobalCheckBox *LogEnabled()
{
    GlobalCheckBox *bc = new GlobalCheckBox("LogEnabled");
    bc->setLabel(QObject::tr("Log MythTV events to database"));
    bc->setValue(false);
    bc->setHelpText(QObject::tr("If enabled, MythTV modules will send event "
                    "details to the database, where they can be viewed with "
                    "MythLog or periodically emailed to the administrator."));
    return bc;
}

static HostSpinBox *LogMaxCount()
{
    HostSpinBox *gs = new HostSpinBox("LogMaxCount", 0, 500, 10);
    gs->setLabel(QObject::tr("Maximum Number of Entries per Module"));
    gs->setValue(100);
    gs->setHelpText(QObject::tr("If there are more than this number of entries "
                    "for a module, the oldest log entries will be deleted to "
                    "reduce the count to this number.  Set to 0 to disable."));
    return gs;
}

static HostCheckBox *LogCleanEnabled()
{
    HostCheckBox *gc = new HostCheckBox("LogCleanEnabled");
    gc->setLabel(QObject::tr("Automatic Log Cleaning Enabled"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("This enables the periodic cleanup of the "
                    "events stored in the Myth database (see \"Log MythTV "
                    "events to database\" on the previous page)."));
    return gc;
}

static HostSpinBox *LogCleanPeriod()
{
    HostSpinBox *gs = new HostSpinBox("LogCleanPeriod", 0, 60, 1);
    gs->setLabel(QObject::tr("Log Cleanup Frequency (Days)"));
    gs->setValue(14);
    gs->setHelpText(QObject::tr("The number of days between log cleanup runs."));
    return gs;
}

static HostSpinBox *LogCleanDays()
{
    HostSpinBox *gs = new HostSpinBox("LogCleanDays", 0, 60, 1);
    gs->setLabel(QObject::tr("Number of days to keep acknowledged log "
                 "entries"));
    gs->setValue(14);
    gs->setHelpText(QObject::tr("The number of days before a log entry that "
                    "has been acknowledged will be deleted by the log cleanup "
                    "process."));
    return gs;
}

static HostSpinBox *LogCleanMax()
{
    HostSpinBox *gs = new HostSpinBox("LogCleanMax", 0, 60, 1);
    gs->setLabel(QObject::tr("Number of days to keep unacknowledged log "
                 "entries"));
    gs->setValue(30);
    gs->setHelpText(QObject::tr("The number of days before a log entry that "
                    "has NOT been acknowledged will be deleted by the log "
                    "cleanup process."));
    return gs;
}

static HostComboBox *LogPrintLevel()
{
    HostComboBox *gc = new HostComboBox("LogPrintLevel");
    gc->setLabel(QObject::tr("Log Print Threshold"));
    gc->addSelection(QObject::tr("All Messages"), "8");
    gc->addSelection(QObject::tr("Debug and Higher"), "7");
    gc->addSelection(QObject::tr("Info and Higher"), "6");
    gc->addSelection(QObject::tr("Notice and Higher"), "5");
    gc->addSelection(QObject::tr("Warning and Higher"), "4");
    gc->addSelection(QObject::tr("Error and Higher"), "3");
    gc->addSelection(QObject::tr("Critical and Higher"), "2");
    gc->addSelection(QObject::tr("Alert and Higher"), "1");
    gc->addSelection(QObject::tr("Emergency Only"), "0");
    gc->addSelection(QObject::tr("Disable Printed Output"), "-1");
    gc->setHelpText(QObject::tr("This controls what messages will be printed "
                    "out as well as being logged to the database."));
    return gc;
}

static GlobalCheckBox *MythFillEnabled()
{
    GlobalCheckBox *bc = new GlobalCheckBox("MythFillEnabled");
    bc->setLabel(QObject::tr("Automatically run mythfilldatabase"));
    bc->setValue(false);
    bc->setHelpText(QObject::tr("This enables the automatic execution of "
                    "mythfilldatabase."));
    return bc;
}

static GlobalSpinBox *MythFillPeriod()
{
    GlobalSpinBox *bs = new GlobalSpinBox("MythFillPeriod", 1, 30, 1);
    bs->setLabel(QObject::tr("mythfilldatabase Run Frequency (Days)"));
    bs->setValue(1);
    bs->setHelpText(QObject::tr("The number of days between mythfilldatabase "
                    "runs."));
    return bs;
}

static GlobalSpinBox *MythFillMinHour()
{
    GlobalSpinBox *bs = new GlobalSpinBox("MythFillMinHour", 0, 23, 1);
    bs->setLabel(QObject::tr("mythfilldatabase Execution Start"));
    bs->setValue(2);
    bs->setHelpText(QObject::tr("This setting and the following one define a "
                    "time period when the mythfilldatabase process is "
                    "allowed to run.  For example, setting Start to 11 and "
                    "End to 13 would mean that the process would only "
                    "run between 11:00 AM and 1:59 PM."));
    return bs;
}

static GlobalSpinBox *MythFillMaxHour()
{
    GlobalSpinBox *bs = new GlobalSpinBox("MythFillMaxHour", 0, 23, 1);
    bs->setLabel(QObject::tr("mythfilldatabase Execution End"));
    bs->setValue(5);
    bs->setHelpText(QObject::tr("This setting and the preceding one define a "
                    "time period when the mythfilldatabase process is "
                    "allowed to run.  For example, setting Start to 11 and "
                    "End to 13 would mean that the process would only "
                    "run between 11:00 AM and 1:59 PM."));
    return bs;
}

static GlobalCheckBox *MythFillGrabberSuggestsTime()
{
    GlobalCheckBox *bc = new GlobalCheckBox("MythFillGrabberSuggestsTime");
    bc->setLabel(QObject::tr("Run mythfilldatabase at time suggested by the "
                             "grabber."));
    bc->setValue(true);
    bc->setHelpText(QObject::tr("This setting allows a DataDirect guide data "
                    "provider to specify the next download time in order to "
                    "distribute load on their servers. If this setting is "
                    "enabled, mythfilldatabase Execution Start/End times are "
                    "ignored."));
    return bc;
}

static GlobalLineEdit *MythFillDatabasePath()
{
    GlobalLineEdit *be = new GlobalLineEdit("MythFillDatabasePath");
    be->setLabel(QObject::tr("mythfilldatabase Program"));
    be->setValue("mythfilldatabase");
    be->setHelpText(QObject::tr(
                        "Use 'mythfilldatabase' or the name of a custom "
                        "script that will populate the program guide info "
                        "for all your video sources."));
    return be;
}

static GlobalLineEdit *MythFillDatabaseArgs()
{
    GlobalLineEdit *be = new GlobalLineEdit("MythFillDatabaseArgs");
    be->setLabel(QObject::tr("mythfilldatabase Arguments"));
    be->setValue("");
    be->setHelpText(QObject::tr("Any arguments you want passed to the "
                    "mythfilldatabase program."));
    return be;
}

static GlobalLineEdit *MythFillDatabaseLog()
{
    GlobalLineEdit *be = new GlobalLineEdit("MythFillDatabaseLog");
    be->setLabel(QObject::tr("mythfilldatabase Log Path"));
    be->setValue("");
    be->setHelpText(QObject::tr("File or directory to use for logging "
                    "output from the mythfilldatabase program.  Leave blank "
                    "to disable logging."));
    return be;
}

class MythLogSettings : public TriggeredConfigurationGroup
{
  public:
    MythLogSettings() : TriggeredConfigurationGroup(false, true, false, false)
    {
         setLabel(QObject::tr("Myth Database Logging"));
//         setUseLabel(false);

         Setting* logEnabled = LogEnabled();
         addChild(logEnabled);
         setTrigger(logEnabled);
         addChild(LogMaxCount());

         ConfigurationGroup* settings = new VerticalConfigurationGroup(false);
         settings->addChild(LogPrintLevel());
         settings->addChild(LogCleanEnabled());
         settings->addChild(LogCleanPeriod());
         settings->addChild(LogCleanDays());
         settings->addChild(LogCleanMax());
         addTarget("1", settings);

         // show nothing if logEnabled is off
         addTarget("0", new VerticalConfigurationGroup(true));
     };
};

class MythFillSettings : public TriggeredConfigurationGroup
{
  public:
     MythFillSettings() :
         TriggeredConfigurationGroup(false, true, false, false)
     {
         setLabel(QObject::tr("Mythfilldatabase"));
         setUseLabel(false);

         Setting* fillEnabled = MythFillEnabled();
         addChild(fillEnabled);
         setTrigger(fillEnabled);

         ConfigurationGroup* settings = new VerticalConfigurationGroup(false);
         settings->addChild(MythFillDatabasePath());
         settings->addChild(MythFillDatabaseArgs());
         settings->addChild(MythFillDatabaseLog());
         settings->addChild(MythFillPeriod());
         settings->addChild(MythFillMinHour());
         settings->addChild(MythFillMaxHour());
         settings->addChild(MythFillGrabberSuggestsTime());
         addTarget("1", settings);

         // show nothing if fillEnabled is off
         addTarget("0", new VerticalConfigurationGroup(true));
     };
};

static HostCheckBox *LCDShowTime()
{
    HostCheckBox *gc = new HostCheckBox("LCDShowTime");
    gc->setLabel(QObject::tr("Display Time"));
    gc->setHelpText(QObject::tr("Display current time on idle LCD display. "));
    gc->setValue(true);
    return gc;
}

static HostCheckBox *LCDShowRecStatus()
{
    HostCheckBox *gc = new HostCheckBox("LCDShowRecStatus");
    gc->setLabel(QObject::tr("Display Recording Status"));
    gc->setHelpText(QObject::tr("Display current recordings information on "
                                "LCD display."));
    gc->setValue(false);
    return gc;
}

static HostCheckBox *LCDShowMenu()
{
    HostCheckBox *gc = new HostCheckBox("LCDShowMenu");
    gc->setLabel(QObject::tr("Display Menus"));
    gc->setHelpText(QObject::tr("Display selected menu on LCD display. "));
    gc->setValue(true);
    return gc;
}

static HostSpinBox *LCDPopupTime()
{
    HostSpinBox *gs = new HostSpinBox("LCDPopupTime", 1, 300, 1, true);
    gs->setLabel(QObject::tr("Menu Pop-up Time"));
    gs->setHelpText(QObject::tr("The time (in seconds) that the menu will "
                    "remain visible after navigation."));
    gs->setValue(5);
    return gs;
}

static HostCheckBox *LCDShowMusic()
{
    HostCheckBox *gc = new HostCheckBox("LCDShowMusic");
    gc->setLabel(QObject::tr("Display Music Artist and Title"));
    gc->setHelpText(QObject::tr("Display playing artist and song title in "
                    "MythMusic on LCD display."));
    gc->setValue(true);
    return gc;
}

static HostComboBox *LCDShowMusicItems()
{
    HostComboBox *gc = new HostComboBox("LCDShowMusicItems");
    gc->setLabel(QObject::tr("Items"));
    gc->addSelection(QObject::tr("Artist - Title"), "ArtistTitle");
    gc->addSelection(QObject::tr("Artist [Album] Title"), "ArtistAlbumTitle");
    gc->setHelpText(QObject::tr("Which items to show when playing music."));
    return gc;
}

static HostCheckBox *LCDShowChannel()
{
    HostCheckBox *gc = new HostCheckBox("LCDShowChannel");
    gc->setLabel(QObject::tr("Display Channel Information"));
    gc->setHelpText(QObject::tr("Display tuned channel information on LCD display."));
    gc->setValue(true);
    return gc;
}

static HostCheckBox *LCDShowVolume()
{
    HostCheckBox *gc = new HostCheckBox("LCDShowVolume");
    gc->setLabel(QObject::tr("Display Volume Information"));
    gc->setHelpText(QObject::tr("Display volume level information "
                                "on LCD display."));
    gc->setValue(true);
    return gc;
}

static HostCheckBox *LCDShowGeneric()
{
    HostCheckBox *gc = new HostCheckBox("LCDShowGeneric");
    gc->setLabel(QObject::tr("Display Generic Information"));
    gc->setHelpText(QObject::tr("Display generic information on LCD display."));
    gc->setValue(true);
    return gc;
}

static HostCheckBox *LCDBacklightOn()
{
    HostCheckBox *gc = new HostCheckBox("LCDBacklightOn");
    gc->setLabel(QObject::tr("Backlight Always On"));
    gc->setHelpText(QObject::tr("Turn on the backlight permanently "
                                "on the LCD display."));
    gc->setValue(true);
    return gc;
}

static HostCheckBox *LCDHeartBeatOn()
{
    HostCheckBox *gc = new HostCheckBox("LCDHeartBeatOn");
    gc->setLabel(QObject::tr("HeartBeat Always On"));
    gc->setHelpText(QObject::tr("Turn on the LCD heartbeat."));
    gc->setValue(false);
    return gc;
}

static HostCheckBox *LCDBigClock()
{
    HostCheckBox *gc = new HostCheckBox("LCDBigClock");
    gc->setLabel(QObject::tr("Display Large Clock"));
    gc->setHelpText(QObject::tr("On multiline displays try and display the time as large as possible."));
    gc->setValue(false);
    return gc;
}

static HostLineEdit *LCDKeyString()
{
    HostLineEdit *ge = new HostLineEdit("LCDKeyString");
    ge->setLabel(QObject::tr("LCD Key order"));
    ge->setValue("ABCDEF");
    ge->setHelpText(QObject::tr("Enter the 6 Keypad Return Codes for your "
            "LCD keypad in the order in which you want the functions "
            "up/down/left/right/yes/no to operate. "
            "(See lcdproc/server/drivers/hd44780.c/keyMapMatrix[] "
            "or the matrix for your display)"));
    return ge;
}

static HostCheckBox *LCDEnable()
{
    HostCheckBox *gc = new HostCheckBox("LCDEnable");
    gc->setLabel(QObject::tr("Enable LCD device"));
    gc->setHelpText(QObject::tr("Use an LCD display to view MythTV status "
                    "information."));
    gc->setValue(false);
    return gc;
}

class LcdSettings : public TriggeredConfigurationGroup
{
  public:
    LcdSettings() : TriggeredConfigurationGroup(false, false,  false, false,
                                                false, false, false, false)
    {
         setLabel(QObject::tr("LCD device display"));
         setUseLabel(false);

         Setting* lcd_enable = LCDEnable();
         addChild(lcd_enable);
         setTrigger(lcd_enable);

         ConfigurationGroup *settings =
             new VerticalConfigurationGroup(false, true, false, false);
         ConfigurationGroup *setHoriz =
             new HorizontalConfigurationGroup(false, false, false, false);

         ConfigurationGroup* setLeft  =
             new VerticalConfigurationGroup(false, false, false, false);
         ConfigurationGroup* setRight =
             new VerticalConfigurationGroup(false, false, false, false);

         setLeft->addChild(LCDShowTime());
         setLeft->addChild(LCDShowMenu());
         setLeft->addChild(LCDShowMusic());
         setLeft->addChild(LCDShowMusicItems());
         setLeft->addChild(LCDShowChannel());
         setLeft->addChild(LCDShowRecStatus());
         setRight->addChild(LCDShowVolume());
         setRight->addChild(LCDShowGeneric());
         setRight->addChild(LCDBacklightOn());
         setRight->addChild(LCDHeartBeatOn());
         setRight->addChild(LCDBigClock());
         setRight->addChild(LCDKeyString());
         setHoriz->addChild(setLeft);
         setHoriz->addChild(setRight);
         settings->addChild(setHoriz);
         settings->addChild(LCDPopupTime());
         
         addTarget("1", settings);

         addTarget("0", new VerticalConfigurationGroup(true));
    };
};


#ifdef CONFIG_DARWIN
static HostCheckBox *MacGammaCorrect()
{
    HostCheckBox *gc = new HostCheckBox("MacGammaCorrect");
    gc->setLabel(QObject::tr("Enable gamma correction for video"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If checked, QuickTime will correct the gamma "
                    "of the video to match your monitor.  Turning this off can "
                    "save some CPU cycles."));
    return gc;
}

static HostCheckBox *MacScaleUp()
{
    HostCheckBox *gc = new HostCheckBox("MacScaleUp");
    gc->setLabel(QObject::tr("Scale video as necessary"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If checked, video will be scaled to fit your "
                    "window or screen. If unchecked, video will never be made "
                    "larger than its actual pixel size."));
    return gc;
}

static HostSpinBox *MacFullSkip()
{
    HostSpinBox *gs = new HostSpinBox("MacFullSkip", 0, 30, 1, true);
    gs->setLabel(QObject::tr("Frames to skip in fullscreen mode"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Video displayed in fullscreen or non-windowed "
                    "mode will skip this many frames for each frame drawn. "
                    "Set to 0 to show every frame. Only valid when either "
                    "\"Use GUI size for TV playback\" or \"Run the frontend "
                    "in a window\" is not checked."));
    return gs;
}

static HostCheckBox *MacMainEnabled()
{
    HostCheckBox *gc = new HostCheckBox("MacMainEnabled");
    gc->setLabel(QObject::tr("Video in main window"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If checked, video will be displayed in the "
                    "main GUI window. Disable this when you only want video "
                    "on the desktop or in a floating window. Only valid when "
                    "\"Use GUI size for TV playback\" and \"Run the "
                    "frontend in a window\" are checked."));
    return gc;
}

static HostSpinBox *MacMainSkip()
{
    HostSpinBox *gs = new HostSpinBox("MacMainSkip", 0, 30, 1, true);
    gs->setLabel(QObject::tr("Frames to skip"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Video in the main window will skip this many "
                    "frames for each frame drawn. Set to 0 to show "
                    "every frame."));
    return gs;
}

static HostSpinBox *MacMainOpacity()
{
    HostSpinBox *gs = new HostSpinBox("MacMainOpacity", 0, 100, 5, false);
    gs->setLabel(QObject::tr("Opacity"));
    gs->setValue(100);
    gs->setHelpText(QObject::tr("The opacity of the main window. Set to "
                    "100 for completely opaque, set to 0 for completely "
                    "transparent."));
    return gs;
}

class MacMainSettings : public TriggeredConfigurationGroup
{
  public:
    MacMainSettings() : TriggeredConfigurationGroup(false)
    {
        setLabel(QObject::tr("Video in main window"));
        setUseLabel(false);
        Setting *gc = MacMainEnabled();
        addChild(gc);
        setTrigger(gc);

        VerticalConfigurationGroup *opts = 
            new VerticalConfigurationGroup(false, false);
        opts->addChild(MacMainSkip());
        opts->addChild(MacMainOpacity());
        
        addTarget("1", opts);
        addTarget("0", new VerticalConfigurationGroup(false, false));
    }
};

static HostCheckBox *MacFloatEnabled()
{
    HostCheckBox *gc = new HostCheckBox("MacFloatEnabled");
    gc->setLabel(QObject::tr("Video in floating window"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If checked, video will be displayed in a "
                    "floating window. Only valid when \"Use GUI size for TV "
                    "playback\" and \"Run the frontend in a window\" are "
                    "checked."));
    return gc;
}

static HostSpinBox *MacFloatSkip()
{
    HostSpinBox *gs = new HostSpinBox("MacFloatSkip", 0, 30, 1, true);
    gs->setLabel(QObject::tr("Frames to skip"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Video in the floating window will skip "
                    "this many frames for each frame drawn. Set to 0 to show "
                    "every frame."));
    return gs;
}

static HostSpinBox *MacFloatOpacity()
{
    HostSpinBox *gs = new HostSpinBox("MacFloatOpacity", 0, 100, 5, false);
    gs->setLabel(QObject::tr("Opacity"));
    gs->setValue(100);
    gs->setHelpText(QObject::tr("The opacity of the floating window. Set to "
                    "100 for completely opaque, set to 0 for completely "
                    "transparent."));
    return gs;
}

class MacFloatSettings : public TriggeredConfigurationGroup
{
  public:
    MacFloatSettings() : TriggeredConfigurationGroup(false)
    {
        setLabel(QObject::tr("Video in floating window"));
        setUseLabel(false);
        Setting *gc = MacFloatEnabled();
        addChild(gc);
        setTrigger(gc);

        VerticalConfigurationGroup *opts = 
            new VerticalConfigurationGroup(false, false);
        opts->addChild(MacFloatSkip());
        opts->addChild(MacFloatOpacity());
        
        addTarget("1", opts);
        addTarget("0", new VerticalConfigurationGroup(false, false));
    }
};

static HostCheckBox *MacDockEnabled()
{
    HostCheckBox *gc = new HostCheckBox("MacDockEnabled");
    gc->setLabel(QObject::tr("Video in the dock"));
    gc->setValue(true);
    gc->setHelpText(QObject::tr("If checked, video will be displayed in the "
                    "application's dock icon. Only valid when \"Use GUI size "
                    "for TV playback\" and \"Run the frontend in a window\" "
                    "are checked."));
    return gc;
}

static HostSpinBox *MacDockSkip()
{
    HostSpinBox *gs = new HostSpinBox("MacDockSkip", 0, 30, 1, true);
    gs->setLabel(QObject::tr("Frames to skip"));
    gs->setValue(3);
    gs->setHelpText(QObject::tr("Video in the dock icon will skip this many "
                    "frames for each frame drawn. Set to 0 to show "
                    "every frame."));
    return gs;
}

class MacDockSettings : public TriggeredConfigurationGroup
{
  public:
    MacDockSettings() : TriggeredConfigurationGroup(false)
    {
        setLabel(QObject::tr("Video in the dock"));
        setUseLabel(false);
        Setting *gc = MacDockEnabled();
        addChild(gc);
        setTrigger(gc);

        Setting *skip = MacDockSkip();
        addTarget("1", skip);
        addTarget("0", new HorizontalConfigurationGroup(false, false));
    }
};

static HostCheckBox *MacDesktopEnabled()
{
    HostCheckBox *gc = new HostCheckBox("MacDesktopEnabled");
    gc->setLabel(QObject::tr("Video on the desktop"));
    gc->setValue(false);
    gc->setHelpText(QObject::tr("If checked, video will be displayed on the "
                    "desktop, behind the Finder icons. Only valid when \"Use "
                    "GUI size for TV playback\" and \"Run the frontend in a "
                    "window\" are checked."));
    return gc;
}

static HostSpinBox *MacDesktopSkip()
{
    HostSpinBox *gs = new HostSpinBox("MacDesktopSkip", 0, 30, 1, true);
    gs->setLabel(QObject::tr("Frames to skip"));
    gs->setValue(0);
    gs->setHelpText(QObject::tr("Video on the desktop will skip this many "
                    "frames for each frame drawn. Set to 0 to show "
                    "every frame."));
    return gs;
}

class MacDesktopSettings : public TriggeredConfigurationGroup
{
  public:
    MacDesktopSettings() : TriggeredConfigurationGroup(false)
    {
        setLabel(QObject::tr("Video on the desktop"));
        setUseLabel(false);
        Setting *gc = MacDesktopEnabled();
        addChild(gc);
        setTrigger(gc);

        Setting *skip = MacDesktopSkip();
        addTarget("1", skip);
        addTarget("0", new HorizontalConfigurationGroup(false, false));
    }
};
#endif

static HostCheckBox *WatchTVGuide()
{
    HostCheckBox *gc = new HostCheckBox("WatchTVGuide");
    gc->setLabel(QObject::tr("Show the program guide when starting Live TV"));
    gc->setHelpText(QObject::tr("This starts the program guide immediately "
             "upon starting to watch Live TV."));
    gc->setValue(false);
    return gc;
}

MainGeneralSettings::MainGeneralSettings()
{
    DatabaseSettings::addDatabaseSettings(this);

    AudioSettings *audio = new AudioSettings();
    addChild(audio);

    VerticalConfigurationGroup *general =
        new VerticalConfigurationGroup(false, true, false, false);
    general->setLabel(QObject::tr("General"));
    HorizontalConfigurationGroup *row =
        new HorizontalConfigurationGroup(false, false, true, true);
    VerticalConfigurationGroup *col1 =
        new VerticalConfigurationGroup(false, false, true, true);
    VerticalConfigurationGroup *col2 =
        new VerticalConfigurationGroup(false, false, true, true);
    col1->addChild(AllowQuitShutdown());
    col1->addChild(NoPromptOnExit());
    col2->addChild(UseArrowAccels());
    col2->addChild(NetworkControlEnabled());
    row->addChild(col1);
    row->addChild(col2);

    MythMediaSettings *mediaMon = new MythMediaSettings();

    general->addChild(LircKeyPressedApp());
    general->addChild(row);
    general->addChild(NetworkControlPort());
    general->addChild(mediaMon);
    addChild(general);

    VerticalConfigurationGroup* misc = new VerticalConfigurationGroup(false);
    misc->setLabel(QObject::tr("Miscellaneous"));

    ConfigurationGroup *pin = new HorizontalConfigurationGroup();
    pin->setLabel(QObject::tr("Settings Access"));
    pin->addChild(SetupPinCodeRequired());
    pin->addChild(SetupPinCode());

    VerticalConfigurationGroup* shutdownSettings =
        new VerticalConfigurationGroup(true);
    shutdownSettings->setLabel(QObject::tr("Shutdown/Reboot Settings"));
    shutdownSettings->addChild(OverrideExitMenu());
    shutdownSettings->addChild(HaltCommand());
    shutdownSettings->addChild(RebootCommand());

    misc->addChild(pin);
    misc->addChild(shutdownSettings);
    misc->addChild(EnableXbox());
    addChild(misc);

    MythLogSettings *mythlog = new MythLogSettings();
    addChild(mythlog);

    MythFillSettings *mythfill = new MythFillSettings();
    addChild(mythfill);
}

PlaybackSettings::PlaybackSettings()
{
    uint i = 0, total = 8;
#ifdef USING_IVTV
    total += 1;
#endif // USING_IVTV
#ifdef CONFIG_DARWIN
    total += 2;
#endif // USING_DARWIN


    VerticalConfigurationGroup* general1 =
        new VerticalConfigurationGroup(false);
    general1->setLabel(QObject::tr("General Playback") +
                      QString(" (%1/%2)").arg(++i).arg(total));

    HorizontalConfigurationGroup *columns =
        new HorizontalConfigurationGroup(false, false, true, true);

    VerticalConfigurationGroup *column1 =
        new VerticalConfigurationGroup(false, false, true, true);
    column1->addChild(RealtimePriority());
    column1->addChild(DecodeExtraAudio());
    column1->addChild(AudioNagSetting());
    column1->addChild(UseVideoTimebase());
    columns->addChild(column1);

    VerticalConfigurationGroup *column2 =
        new VerticalConfigurationGroup(false, false, true, true);
    column2->addChild(ClearSavedPosition());
    column2->addChild(AltClearSavedPosition());
    column2->addChild(JumpToProgramOSD());
    column2->addChild(ContinueEmbeddedTVPlay());
    column2->addChild(AutomaticSetWatched());
    columns->addChild(column2);

    general1->addChild(columns);
    general1->addChild(AlwaysStreamFiles());
#ifdef USING_OPENGL_VSYNC
    general1->addChild(UseOpenGLVSync());
#endif // USING_OPENGL_VSYNC
#ifdef USING_XV
    general1->addChild(UsePicControls());
#endif // USING_XV
    addChild(general1);

    VerticalConfigurationGroup* general2 =
        new VerticalConfigurationGroup(false);
    general2->setLabel(QObject::tr("General Playback") +
                      QString(" (%1/%2)").arg(++i).arg(total));

    HorizontalConfigurationGroup* oscan =
        new HorizontalConfigurationGroup(false, false, true, true);
    VerticalConfigurationGroup *ocol1 =
        new VerticalConfigurationGroup(false, false, true, true);
    VerticalConfigurationGroup *ocol2 =
        new VerticalConfigurationGroup(false, false, true, true);
    ocol1->addChild(VertScanPercentage());
    ocol1->addChild(HorizScanPercentage());
    ocol2->addChild(XScanDisplacement());
    ocol2->addChild(YScanDisplacement());
    oscan->addChild(ocol1);
    oscan->addChild(ocol2);

    HorizontalConfigurationGroup* aspect_fill =
        new HorizontalConfigurationGroup(false, false, true, true);
    aspect_fill->addChild(AspectOverride());
    aspect_fill->addChild(AdjustFill());

    general2->addChild(oscan);
    general2->addChild(aspect_fill);
    general2->addChild(LetterboxingColour());
    general2->addChild(PIPLocationComboBox());
    general2->addChild(PlaybackExitPrompt());
    general2->addChild(EndOfRecordingExitPrompt());
    addChild(general2);

    QString tmp = QString(" (%1/%2)").arg(++i).arg(total);
    addChild(new PlaybackProfileConfigs(tmp));

    VerticalConfigurationGroup* pbox = new VerticalConfigurationGroup(false);
    pbox->setLabel(QObject::tr("View Recordings") +
                   QString(" (%1/%2)").arg(++i).arg(total));
    pbox->addChild(PlayBoxOrdering());
    pbox->addChild(PlayBoxEpisodeSort());
    pbox->addChild(GeneratePreviewPixmaps());
    pbox->addChild(PreviewPixmapOffset());
    pbox->addChild(PreviewFromBookmark());
    pbox->addChild(PlaybackPreview());
    pbox->addChild(PBBStartInTitle());
    pbox->addChild(PBBShowGroupSummary());
    addChild(pbox);

    VerticalConfigurationGroup* pbox2 = new VerticalConfigurationGroup(false);
    pbox2->setLabel(QObject::tr("Recording Groups") +
                    QString(" (%1/%2)").arg(++i).arg(total));
    pbox2->addChild(AllRecGroupPassword());
    pbox2->addChild(DisplayRecGroup());
    pbox2->addChild(QueryInitialFilter());
    pbox2->addChild(RememberRecGroup());
    pbox2->addChild(UseGroupNameAsAllPrograms());
    pbox2->addChild(LiveTVInAllPrograms());
    addChild(pbox2);

    VerticalConfigurationGroup* pbox3 = new VerticalConfigurationGroup(false);
    pbox3->setLabel(QObject::tr("View Recordings") +
                    QString(" (%1/%2)").arg(++i).arg(total));
    pbox3->addChild(new DefaultViewSettings());
    pbox3->addChild(new WatchListSettings());
    addChild(pbox3);

    VerticalConfigurationGroup* seek = new VerticalConfigurationGroup(false);
    seek->setLabel(QObject::tr("Seeking") +
                   QString(" (%1/%2)").arg(++i).arg(total));
    seek->addChild(SmartForward());
    seek->addChild(StickyKeys());
    seek->addChild(FFRewReposTime());
    seek->addChild(FFRewReverse());
    seek->addChild(ExactSeeking());
    addChild(seek);

    VerticalConfigurationGroup* comms = new VerticalConfigurationGroup(false);
    comms->setLabel(QObject::tr("Commercial Skip") +
                    QString(" (%1/%2)").arg(++i).arg(total));
    comms->addChild(AutoCommercialSkip());
    comms->addChild(CommRewindAmount());
    comms->addChild(CommNotifyAmount());
    comms->addChild(MaximumCommercialSkip());
    comms->addChild(MergeShortCommBreaks());
    comms->addChild(CommSkipAllBlanks());
    addChild(comms);

#ifdef USING_IVTV
    QString tmp2 = QString(" (%1/%2)").arg(++i).arg(total);
    addChild(new PVR350HWDecoderSettings(tmp2));
#endif // USING_IVTV

#ifdef CONFIG_DARWIN
    VerticalConfigurationGroup* mac1 = new VerticalConfigurationGroup(false);
    mac1->setLabel(QObject::tr("Mac OS X video settings") +
                   QString(" (%1/%2)").arg(++i).arg(total));
    mac1->addChild(MacGammaCorrect());
    mac1->addChild(MacScaleUp());
    mac1->addChild(MacFullSkip());
    addChild(mac1);

    VerticalConfigurationGroup* mac2 = new VerticalConfigurationGroup(false);
    mac2->setLabel(QObject::tr("Mac OS X video settings") +
                   QString(" (%1/%2)").arg(++i).arg(total));
    mac2->addChild(new MacMainSettings());
    mac2->addChild(new MacFloatSettings());
    mac2->addChild(new MacDockSettings());
    mac2->addChild(new MacDesktopSettings());
    addChild(mac2);
#endif
}

OSDSettings::OSDSettings()
{
    VerticalConfigurationGroup* osd = new VerticalConfigurationGroup(false);
    osd->setLabel(QObject::tr("On-screen display"));

    osd->addChild(new ThemeSelector("OSDTheme"));
    osd->addChild(OSDGeneralTimeout());
    osd->addChild(OSDProgramInfoTimeout());
    osd->addChild(OSDFont());
    osd->addChild(OSDThemeFontSizeType());
    osd->addChild(EnableMHEG());
    osd->addChild(PersistentBrowseMode());
    addChild(osd);

    VerticalConfigurationGroup *udp = new VerticalConfigurationGroup(false);
    udp->setLabel(QObject::tr("UDP OSD Notifications"));
    udp->addChild(OSDNotifyTimeout());
    udp->addChild(UDPNotifyPort());
    addChild(udp);

    VerticalConfigurationGroup *cc = new VerticalConfigurationGroup(false);
    cc->setLabel(QObject::tr("Analog Closed Captions"));
    cc->addChild(OSDCCFont());
    cc->addChild(CCBackground());
    cc->addChild(DefaultCCMode());
    cc->addChild(PreferCC708());
    addChild(cc);

    addChild(OSDCC708Settings());
    addChild(OSDCC708Fonts());

#ifdef CONFIG_DARWIN
    // Any Mac OS-specific OSD stuff would go here.
    // Note that this define should be Q_WS_MACX
#endif
}

GeneralSettings::GeneralSettings()
{
    VerticalConfigurationGroup* general = new VerticalConfigurationGroup(false);
    general->setLabel(QObject::tr("General (Basic)"));
    general->addChild(ChannelOrdering());
    general->addChild(ChannelFormat());
    general->addChild(LongChannelFormat());
    general->addChild(SmartChannelChange());
    general->addChild(LastFreeCard());
    general->addChild(LiveTVPriority());
    addChild(general);

    VerticalConfigurationGroup* autoexp = new VerticalConfigurationGroup(false);
    autoexp->setLabel(QObject::tr("General (AutoExpire)"));
    autoexp->addChild(AutoExpireMethod());

    VerticalConfigurationGroup *expgrp0 =
        new VerticalConfigurationGroup(false, false, true, true);
    expgrp0->addChild(AutoExpireDefault());
    expgrp0->addChild(RerecordWatched());
    expgrp0->addChild(AutoExpireWatchedPriority());

    VerticalConfigurationGroup *expgrp1 =
        new VerticalConfigurationGroup(false, false, true, true);
    expgrp1->addChild(AutoExpireLiveTVMaxAge());
    expgrp1->addChild(AutoExpireDayPriority());
    expgrp1->addChild(AutoExpireExtraSpace());

    HorizontalConfigurationGroup *expgrp =
        new HorizontalConfigurationGroup(false, false, true, true);
    expgrp->addChild(expgrp0);
    expgrp->addChild(expgrp1);

    autoexp->addChild(expgrp);
    autoexp->addChild(new DeletedExpireOptions());

    addChild(autoexp);

    VerticalConfigurationGroup* jobs = new VerticalConfigurationGroup(false);
    jobs->setLabel(QObject::tr("General (Jobs)"));
    jobs->addChild(CommercialSkipMethod());
    jobs->addChild(AggressiveCommDetect());
    jobs->addChild(DefaultTranscoder());
    jobs->addChild(DeferAutoTranscodeDays());

    VerticalConfigurationGroup* autogrp0 =
        new VerticalConfigurationGroup(false, false, true, true);
    autogrp0->addChild(AutoCommercialFlag());
    autogrp0->addChild(AutoTranscode());
    autogrp0->addChild(AutoRunUserJob(1));

    VerticalConfigurationGroup* autogrp1 =
        new VerticalConfigurationGroup(false, false, true, true);
    autogrp1->addChild(AutoRunUserJob(2));
    autogrp1->addChild(AutoRunUserJob(3));
    autogrp1->addChild(AutoRunUserJob(4));

    HorizontalConfigurationGroup *autogrp =
        new HorizontalConfigurationGroup(true, true, false, true);
    autogrp->setLabel(
        QObject::tr("Default JobQueue settings for new scheduled recordings"));
    autogrp->addChild(autogrp0);
    autogrp->addChild(autogrp1);
    jobs->addChild(autogrp);

    addChild(jobs);

    VerticalConfigurationGroup* general2 = new VerticalConfigurationGroup(false);
    general2->setLabel(QObject::tr("General (Advanced)"));
    general2->addChild(RecordPreRoll());
    general2->addChild(RecordOverTime());
    general2->addChild(CategoryOverTimeSettings());
    addChild(general2);

}

EPGSettings::EPGSettings()
{
    VerticalConfigurationGroup* epg = new VerticalConfigurationGroup(false);
    epg->setLabel(QObject::tr("Program Guide") + " 1/2");
    epg->addChild(EPGFillType());
    epg->addChild(EPGShowCategoryColors());
    epg->addChild(EPGShowCategoryText());
    epg->addChild(EPGScrollType());
    epg->addChild(EPGShowChannelIcon());
    epg->addChild(EPGShowFavorites());
    epg->addChild(WatchTVGuide());
    epg->addChild(EPGChanDisplay());
    epg->addChild(EPGTimeDisplay());
    addChild(epg);

    VerticalConfigurationGroup* gen = new VerticalConfigurationGroup(false);
    gen->setLabel(QObject::tr("Program Guide") + " 2/2");
    gen->addChild(UnknownTitle());
    gen->addChild(UnknownCategory());
    gen->addChild(DefaultTVChannel());
    gen->addChild(SelectChangesChannel());
    gen->addChild(EPGRecThreshold());
    gen->addChild(EPGEnableJumpToChannel());
    addChild(gen);
}

GeneralRecPrioritiesSettings::GeneralRecPrioritiesSettings()
{
    VerticalConfigurationGroup* sched = new VerticalConfigurationGroup(false);
    sched->setLabel(QObject::tr("Scheduler Options"));

    sched->addChild(GRSchedMoveHigher());
    sched->addChild(GRSchedOpenEnd());
    sched->addChild(GRDefaultStartOffset());
    sched->addChild(GRDefaultEndOffset());
    sched->addChild(GRComplexPriority());
    sched->addChild(GRPrefInputRecPriority());
    sched->addChild(GRHDTVRecPriority());
    addChild(sched);

    VerticalConfigurationGroup* rtype = new VerticalConfigurationGroup(false);
    rtype->setLabel(QObject::tr("Recording Type Priority Settings"));

    rtype->addChild(GRSingleRecordRecPriority());
    rtype->addChild(GROverrideRecordRecPriority());
    rtype->addChild(GRFindOneRecordRecPriority());
    rtype->addChild(GRWeekslotRecordRecPriority());
    rtype->addChild(GRTimeslotRecordRecPriority());
    rtype->addChild(GRChannelRecordRecPriority());
    rtype->addChild(GRAllRecordRecPriority());
    addChild(rtype);
}

AppearanceSettings::AppearanceSettings()
{
    VerticalConfigurationGroup* theme = new VerticalConfigurationGroup(false);
    theme->setLabel(QObject::tr("Theme"));

    theme->addChild(new ThemeSelector("Theme"));

    HorizontalConfigurationGroup *hgrp1 =
        new HorizontalConfigurationGroup(false, false, false, false);
    hgrp1->addChild(RandomTheme());
    hgrp1->addChild(ThemeCacheSize());
    theme->addChild(hgrp1);

    theme->addChild(ThemePainter());
    theme->addChild(new StyleSetting());
    theme->addChild(ThemeFontSizeType());
    theme->addChild(MenuTheme());
    addChild(theme);

    VerticalConfigurationGroup* screen = new VerticalConfigurationGroup(false);
    screen->setLabel(QObject::tr("Screen settings"));

    if (GetNumberOfXineramaScreens() > 1)
    {
        screen->addChild(XineramaScreen());
        screen->addChild(XineramaMonitorAspectRatio());
    }

//    screen->addChild(DisplaySizeHeight());
//    screen->addChild(DisplaySizeWidth());

    VerticalConfigurationGroup *column1 =
        new VerticalConfigurationGroup(false, false, false, false);

    VerticalConfigurationGroup *column2 =
        new VerticalConfigurationGroup(false, false, false, false);

    column1->addChild(GuiWidth());
    column1->addChild(GuiHeight());
    column1->addChild(GuiSizeForTV());
    column2->addChild(GuiOffsetX());
    column2->addChild(GuiOffsetY());
    column2->addChild(HideMouseCursor());

    HorizontalConfigurationGroup *columns =
        new HorizontalConfigurationGroup(false, false, false, false);

    columns->addChild(column1);
    columns->addChild(column2);

    screen->addChild(columns);
    screen->addChild(RunInWindow());

    addChild(screen);

#if defined(USING_XRANDR) || defined(CONFIG_DARWIN)
    const vector<DisplayResScreen> scr = GetVideoModes();
    if (scr.size())
        addChild(new VideoModeSettings());
#endif
    VerticalConfigurationGroup* dates = new VerticalConfigurationGroup(false);
    dates->setLabel(QObject::tr("Localization"));
    dates->addChild(MythLanguage());
    dates->addChild(ISO639PreferredLanguage(0));
    dates->addChild(ISO639PreferredLanguage(1));
    dates->addChild(MythDateFormat());
    dates->addChild(MythShortDateFormat());
    dates->addChild(MythTimeFormat());
    addChild(dates);

    VerticalConfigurationGroup* qttheme = new VerticalConfigurationGroup(false);
    qttheme->setLabel(QObject::tr("QT"));
    qttheme->addChild(QtFontSmall());
    qttheme->addChild(QtFontMedium());
    qttheme->addChild(QtFontBig());
    qttheme->addChild(QtFonTweak());
    qttheme->addChild(PlayBoxTransparency());
    qttheme->addChild(PlayBoxShading());
    qttheme->addChild(UseVirtualKeyboard());
    addChild(qttheme );

    addChild(new LcdSettings());
}

XboxSettings::XboxSettings()
{
    VerticalConfigurationGroup* xboxset = new VerticalConfigurationGroup(false);

    xboxset->setLabel(QObject::tr("Xbox"));
    xboxset->addChild(XboxBlinkBIN());
    xboxset->addChild(XboxLEDDefault());
    xboxset->addChild(XboxLEDRecording());
    xboxset->addChild(XboxLEDNonLiveTV());
    xboxset->addChild(XboxCheckRec());
    addChild(xboxset);
}

// vim:set sw=4 ts=4 expandtab:
