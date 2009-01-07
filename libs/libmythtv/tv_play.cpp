#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <pthread.h>

#include <algorithm>
using namespace std;

#include <qapplication.h>
#include <qregexp.h>
#include <qfile.h>
#include <qtimer.h>
#include <qdir.h>

#include "mythdbcon.h"
#include "tv_play.h"
#include "tv_rec.h"
#include "osd.h"
#include "osdsurface.h"
#include "osdtypes.h"
#include "osdlistbtntype.h"
#include "mythcontext.h"
#include "dialogbox.h"
#include "remoteencoder.h"
#include "remoteutil.h"
#include "guidegrid.h"
#include "progfind.h"
#include "NuppelVideoPlayer.h"
#include "programinfo.h"
#include "udpnotify.h"
#include "vsync.h"
#include "lcddevice.h"
#include "jobqueue.h"
#include "audiooutput.h"
#include "DisplayRes.h"
#include "signalmonitorvalue.h"
#include "scheduledrecording.h"
#include "previewgenerator.h"
#include "config.h"
#include "livetvchain.h"
#include "playgroup.h"
#include "DVDRingBuffer.h"
#include "datadirect.h"
#include "sourceutil.h"
#include "cardutil.h"
#include "util-osx-cocoa.h"
#include "compat.h"

#ifndef HAVE_ROUND
#define round(x) ((int) ((x) + 0.5))
#endif

const int TV::kInitFFRWSpeed  = 0;
const int TV::kMuteTimeout    = 800;
const int TV::kLCDTimeout     = 1;    // seconds
const int TV::kBrowseTimeout  = 30000;
const int TV::kSMExitTimeout  = 2000;
const int TV::kInputKeysMax   = 6;
const int TV::kInputModeTimeout=5000;
const uint TV::kNextSource    = 1;
const uint TV::kPreviousSource= 2;

#define DEBUG_CHANNEL_PREFIX 0 /**< set to 1 to channel prefixing */
#define DEBUG_ACTIONS 0 /**< set to 1 to debug actions */
#define LOC QString("TV: ")
#define LOC_WARN QString("TV Warning: ")
#define LOC_ERR QString("TV Error: ")

/*
 * \brief stores last program info. maintains info so long as
 * mythfrontend is active
 */
QStringList TV::lastProgramStringList = QStringList();

/*
 * \brief function pointer for RunPlaybackBox in playbackbox.cpp
 */
RUNPLAYBACKBOX TV::RunPlaybackBoxPtr = NULL;

/**\ brief function pointer for RunViewScheduled in viewscheduled.cpp
 */
RUNVIEWSCHEDULED TV::RunViewScheduledPtr = NULL;

/*
 \brief returns true if the recording completed when exiting.
 */
bool TV::StartTV (ProgramInfo *tvrec, bool startInGuide, 
                bool inPlaylist, bool initByNetworkCommand)
{
    TV *tv = new TV();
    bool quitAll = false;
    bool showDialogs = true;
    bool playCompleted = false;
    ProgramInfo *curProgram = NULL;
    

    if (tvrec)
        curProgram = new ProgramInfo(*tvrec);
    
    // Initialize TV
    if (!tv->Init())
    {
        VERBOSE(VB_IMPORTANT, "Experienced fatal error:"
                "Failed initializing TV");
        delete tv;
        return false;
    }

    if (!lastProgramStringList.empty())
    {
        ProgramInfo *p = new ProgramInfo();
        p->FromStringList(lastProgramStringList, 0);
        tv->setLastProgram(p);
        delete p;
    }

    gContext->sendPlaybackStart();

    while (!quitAll)
    {
        int freeRecorders = RemoteGetFreeRecorderCount();
        if (curProgram)
        {
            if (!tv->Playback(curProgram))
                quitAll = true;
        }
        else if (!freeRecorders)
        {
            vector<ProgramInfo *> *reclist;
            reclist = RemoteGetCurrentlyRecordingList();
            if (reclist->empty())
            {
                VERBOSE(VB_PLAYBACK, LOC_ERR + 
                        "Failed to get recording show list");
                quitAll = true;
            }

            int numrecordings = reclist->size();
            if (numrecordings > 0)
            {
                ProgramInfo *p = NULL;
                QStringList recTitles;
                QString buttonTitle;
                vector<ProgramInfo *>::iterator it = reclist->begin();
                recTitles.append(tr("Exit"));
                while (it != reclist->end())
                {
                    p = *it;
                    buttonTitle = tr("Chan %1: %2")
                        .arg(p->chanstr).arg(p->title);
                    recTitles.append(buttonTitle);
                    it++;
                }
                DialogCode ret = MythPopupBox::ShowButtonPopup(
                    gContext->GetMainWindow(),
                    "",
                    tr("All Tuners are Busy.\n"
                        "Select a Current Recording"),
                        recTitles, kDialogCodeButton1);

                int idx = MythDialog::CalcItemIndex(ret) - 1;
                if ((0 <= idx) && (idx < (int)reclist->size()))
                {
                    p = reclist->at(idx);
                    curProgram = new ProgramInfo(*p);
                }
                else
                {
                    quitAll = true;
                }
            }

            if (reclist)
                delete reclist;
            continue;
        }
        else
        {
            if (!tv->LiveTV(showDialogs, startInGuide))
            {
                tv->StopLiveTV();
                quitAll = true;
            }
        }
        
        tv->setInPlayList(inPlaylist);
        tv->setUnderNetworkControl(initByNetworkCommand);
        
        // Process Events
        while (tv->GetState() != kState_None)
        {
            qApp->unlock();
            qApp->processEvents();
            usleep(100000);
            qApp->lock();
        }

        if (tv->getJumpToProgram())
        {
            
            ProgramInfo *tmpProgram  = tv->getLastProgram();
            ProgramInfo *nextProgram = new ProgramInfo(*tmpProgram);

            if (curProgram)
            {
                tv->setLastProgram(curProgram);
                delete curProgram;
            }
            else
                tv->setLastProgram(NULL);

            curProgram = nextProgram;

            continue;
        }
        
        if (tv->WantsToQuit())
            quitAll = true;
    }

    while (tv->IsMenuRunning())
    {
        qApp->unlock();
        qApp->processEvents();
        usleep(100000);
        qApp->lock();
    }

    // check if the show has reached the end.
    if (tvrec && tv->getEndOfRecording())
        playCompleted = true;
    
    bool allowrerecord = tv->getAllowRerecord();
    bool deleterecording = tv->getRequestDelete();
    
    delete tv;
    
    if (curProgram)
    {
        if (deleterecording)
        {
            curProgram->UpdateLastDelete(true);
            RemoteDeleteRecording(curProgram, allowrerecord, false);
        }
        else if (!curProgram->isVideo)
        {
            lastProgramStringList.clear();
            curProgram->ToStringList(lastProgramStringList);
        }
        
        delete curProgram;
    }

    gContext->sendPlaybackEnd();

    return playCompleted;
}

/**
 * \brief Import pointers to functions used to embed the TV
 * window into other containers e.g. playbackbox
 */
void TV::SetFuncPtr(const char *string, void *lptr)
{
    QString name(string);
    if (name == "playbackbox")
        RunPlaybackBoxPtr = (RUNPLAYBACKBOX)lptr;
    else if (name == "viewscheduled")
        RunViewScheduledPtr = (RUNVIEWSCHEDULED)lptr;
}

void TV::InitKeys(void)
{
    REG_KEY("TV Frontend", "PAGEUP", "Page Up", "3");
    REG_KEY("TV Frontend", "PAGEDOWN", "Page Down", "9");
    REG_KEY("TV Frontend", "PAGETOP", "Page to top of list", "");
    REG_KEY("TV Frontend", "PAGEMIDDLE", "Page to middle of list", "");
    REG_KEY("TV Frontend", "PAGEBOTTOM", "Page to bottom of list", "");
    REG_KEY("TV Frontend", "DELETE", "Delete Program", "D");
    REG_KEY("TV Frontend", "PLAYBACK", "Play Program", "P");
    REG_KEY("TV Frontend", "TOGGLERECORD", "Toggle recording status of current "
            "program", "R");
    REG_KEY("TV Frontend", "DAYLEFT", "Page the program guide back one day", 
            "Home,7");
    REG_KEY("TV Frontend", "DAYRIGHT", "Page the program guide forward one day",
            "End,1");
    REG_KEY("TV Frontend", "PAGELEFT", "Page the program guide left",
            ",,<");
    REG_KEY("TV Frontend", "PAGERIGHT", "Page the program guide right",
            ">,.");
    REG_KEY("TV Frontend", "TOGGLEFAV", "Toggle the current channel as a "
            "favorite", "?");
    REG_KEY("TV Frontend", "TOGGLEEPGORDER", "Reverse the channel order "
            "in the program guide", "0");
    REG_KEY("TV Frontend", "GUIDE", "Show the Program Guide", "S");
    REG_KEY("TV Frontend", "FINDER", "Show the Program Finder", "#");
    REG_KEY("TV Frontend", "NEXTFAV", "Toggle showing all channels or just "
            "favorites in the program guide.", "/");
    REG_KEY("TV Frontend", "CHANUPDATE", "Switch channels without exiting "
            "guide in Live TV mode.", "X");
    REG_KEY("TV Frontend", "VOLUMEDOWN", "Volume down", "[,{,F10,Volume Down");
    REG_KEY("TV Frontend", "VOLUMEUP",   "Volume up",   "],},F11,Volume Up");
    REG_KEY("TV Frontend", "MUTE",       "Mute",        "|,\\,F9,Volume Mute");
    REG_KEY("TV Frontend", "RANKINC", "Increase program or channel rank",
            "Right");
    REG_KEY("TV Frontend", "RANKDEC", "Decrease program or channel rank",
            "Left");
    REG_KEY("TV Frontend", "UPCOMING", "List upcoming episodes", "O");
    REG_KEY("TV Frontend", "DETAILS", "Show program details", "U");
    REG_KEY("TV Frontend", "VIEWCARD", "Switch Capture Card view", "Y");
    REG_KEY("TV Frontend", "VIEWINPUT", "Switch Capture Card view", "C");
    REG_KEY("TV Frontend", "CUSTOMEDIT", "Edit Custom Record Rule", "E");
    REG_KEY("TV Frontend", "CHANGERECGROUP", "Change Recording Group", "");
    REG_KEY("TV Frontend", "CHANGEGROUPVIEW", "Change Group View", "");

    REG_KEY("TV Playback", "CLEAROSD", "Clear OSD", "Backspace");
    REG_KEY("TV Playback", "PAUSE", "Pause", "P");
    REG_KEY("TV Playback", "DELETE", "Delete Program", "D");
    REG_KEY("TV Playback", "SEEKFFWD", "Fast Forward", "Right");
    REG_KEY("TV Playback", "SEEKRWND", "Rewind", "Left");
    REG_KEY("TV Playback", "ARBSEEK", "Arbitrary Seek", "*");
    REG_KEY("TV Playback", "CHANNELUP", "Channel up", "Up");
    REG_KEY("TV Playback", "CHANNELDOWN", "Channel down", "Down");
    REG_KEY("TV Playback", "NEXTFAV", "Switch to the next favorite channel",
            "/");
    REG_KEY("TV Playback", "PREVCHAN", "Switch to the previous channel", "H");
    REG_KEY("TV Playback", "JUMPFFWD", "Jump ahead", "PgDown");
    REG_KEY("TV Playback", "JUMPRWND", "Jump back", "PgUp");
    REG_KEY("TV Playback", "JUMPBKMRK", "Jump to bookmark", "K");
    REG_KEY("TV Playback", "FFWDSTICKY", "Fast Forward (Sticky) or Forward one "
            "frame while paused", ">,.");
    REG_KEY("TV Playback", "RWNDSTICKY", "Rewind (Sticky) or Rewind one frame "
            "while paused", ",,<");
    REG_KEY("TV Playback", "NEXTSOURCE",    "Next Video Source",     "Y");
    REG_KEY("TV Playback", "PREVSOURCE",    "Previous Video Source", "Ctrl+Y");
    REG_KEY("TV Playback", "NEXTINPUT",     "Next Input",            "C");
    REG_KEY("TV Playback", "NEXTCARD",      "Next Card",             "");
    REG_KEY("TV Playback", "SKIPCOMMERCIAL", "Skip Commercial", "Z,End");
    REG_KEY("TV Playback", "SKIPCOMMBACK", "Skip Commercial (Reverse)",
            "Q,Home");
    REG_KEY("TV Playback", "JUMPSTART", "Jump to the start of the recording.", "Ctrl+B");            
    REG_KEY("TV Playback", "TOGGLEBROWSE", "Toggle channel browse mode", "O");
    REG_KEY("TV Playback", "TOGGLERECORD", "Toggle recording status of current "
            "program", "R");
    REG_KEY("TV Playback", "TOGGLEFAV", "Toggle the current channel as a "
            "favorite", "?");
    REG_KEY("TV Playback", "VOLUMEDOWN", "Volume down", "[,{,F10,Volume Down");
    REG_KEY("TV Playback", "VOLUMEUP",   "Volume up",   "],},F11,Volume Up");
    REG_KEY("TV Playback", "MUTE",       "Mute",        "|,\\,F9,Volume Mute");
    REG_KEY("TV Playback", "TOGGLEPIPMODE", "Toggle Picture-in-Picture mode",
            "V");
    REG_KEY("TV Playback", "TOGGLEPIPWINDOW", "Toggle active PiP window", "B");
    REG_KEY("TV Playback", "SWAPPIP", "Swap PiP/Main", "N");
    REG_KEY("TV Playback", "TOGGLEASPECT",
            "Toggle the video aspect ratio", "Ctrl+W");
    REG_KEY("TV Playback", "TOGGLEFILL", "Next Preconfigured Zoom mode", "W");

    REG_KEY("TV Playback", "TOGGLECC",      "Toggle any captions",   "T");
    REG_KEY("TV Playback", "TOGGLETTC",     "Toggle Teletext Captions","");
    REG_KEY("TV Playback", "TOGGLESUBTITLE","Toggle Subtitles",      "");
    REG_KEY("TV Playback", "TOGGLECC608",   "Toggle VBI CC",         "");
    REG_KEY("TV Playback", "TOGGLECC708",   "Toggle ATSC CC",        "");

    REG_KEY("TV Playback", "TOGGLETTM",     "Toggle Teletext Menu",  "");

    REG_KEY("TV Playback", "SELECTAUDIO_0", "Play audio track 1",    "");
    REG_KEY("TV Playback", "SELECTAUDIO_1", "Play audio track 2",    "");
    REG_KEY("TV Playback", "SELECTSUBTITLE_0","Display subtitle 1",  "");
    REG_KEY("TV Playback", "SELECTSUBTITLE_1","Display subtitle 2",  "");
    REG_KEY("TV Playback", "SELECTCC608_0", "Display VBI CC1",       "");
    REG_KEY("TV Playback", "SELECTCC608_1", "Display VBI CC2",       "");
    REG_KEY("TV Playback", "SELECTCC608_2", "Display VBI CC3",       "");
    REG_KEY("TV Playback", "SELECTCC608_3", "Display VBI CC4",       "");
    REG_KEY("TV Playback", "SELECTCC708_0", "Display ATSC CC1",      "");
    REG_KEY("TV Playback", "SELECTCC708_1", "Display ATSC CC2",      "");
    REG_KEY("TV Playback", "SELECTCC708_2", "Display ATSC CC3",      "");
    REG_KEY("TV Playback", "SELECTCC708_3", "Display ATSC CC4",      "");

    REG_KEY("TV Playback", "NEXTAUDIO",    "Next audio track",         "+");
    REG_KEY("TV Playback", "PREVAUDIO",    "Previous audio track",     "-");
    REG_KEY("TV Playback", "NEXTSUBTITLE", "Next subtitle track",      "");
    REG_KEY("TV Playback", "PREVSUBTITLE", "Previous subtitle track",  "");
    REG_KEY("TV Playback", "NEXTCC608",    "Next VBI CC track",        "");
    REG_KEY("TV Playback", "PREVCC608",    "Previous VBI CC track",    "");
    REG_KEY("TV Playback", "NEXTCC708",    "Next ATSC CC track",       "");
    REG_KEY("TV Playback", "PREVCC708",    "Previous ATSC CC track",   "");
    REG_KEY("TV Playback", "NEXTCC",       "Next of any captions",     "");

    REG_KEY("TV Playback", "NEXTSCAN",    "Next video scan overidemode", "");
    REG_KEY("TV Playback", "QUEUETRANSCODE", "Queue the current recording for "
            "transcoding", "X");
    REG_KEY("TV Playback", "SPEEDINC", "Increase the playback speed", "U");
    REG_KEY("TV Playback", "SPEEDDEC", "Decrease the playback speed", "J");
    REG_KEY("TV Playback", "ADJUSTSTRETCH", "Turn on time stretch control", "A");
    REG_KEY("TV Playback", "STRETCHINC", "Increase time stretch speed", "");
    REG_KEY("TV Playback", "STRETCHDEC", "Decrease time stretch speed", "");
    REG_KEY("TV Playback", "TOGGLESTRETCH", "Toggle time stretch speed", "");
    REG_KEY("TV Playback", "TOGGLEAUDIOSYNC",
            "Turn on audio sync adjustment controls", "");
    REG_KEY("TV Playback", "TOGGLEPICCONTROLS",
            "Playback picture adjustments",                    "F");
    REG_KEY("TV Playback", "TOGGLECHANCONTROLS",
            "Recording picture adjustments for this channel",  "Ctrl+G");
    REG_KEY("TV Playback", "TOGGLERECCONTROLS",
            "Recording picture adjustments for this recorder", "G");
    REG_KEY("TV Playback", "TOGGLEEDIT", "Start Edit Mode", "E");
    REG_KEY("TV Playback", "CYCLECOMMSKIPMODE", "Cycle Commercial Skip mode", "");
    REG_KEY("TV Playback", "GUIDE", "Show the Program Guide", "S");
    REG_KEY("TV Playback", "FINDER", "Show the Program Finder", "#");
    REG_KEY("TV Playback", "TOGGLESLEEP", "Toggle the Sleep Timer", "F8");
    REG_KEY("TV Playback", "PLAY", "Play", "Ctrl+P");
    REG_KEY("TV Playback", "JUMPPREV", "Jump to previously played recording", "");
    REG_KEY("TV Playback", "JUMPREC", "Display menu of recorded programs to jump to", "");
    REG_KEY("TV Playback", "VIEWSCHEDULED", "Display scheduled recording list", "");
    REG_KEY("TV Playback", "SIGNALMON", "Monitor Signal Quality", "Alt+F7");
    REG_KEY("TV Playback", "JUMPTODVDROOTMENU", "Jump to the DVD Root Menu", "");
    REG_KEY("TV Playback", "EXITSHOWNOPROMPTS","Exit Show without any prompts", "");
    REG_KEY("TV Playback", "SCREENSHOT","Save screenshot of current video frame", "");

    /* Interactive Television keys */
    REG_KEY("TV Playback", "MENURED",    "Menu Red",    "F2");
    REG_KEY("TV Playback", "MENUGREEN",  "Menu Green",  "F3");
    REG_KEY("TV Playback", "MENUYELLOW", "Menu Yellow", "F4");
    REG_KEY("TV Playback", "MENUBLUE",   "Menu Blue",   "F5");
    REG_KEY("TV Playback", "TEXTEXIT",   "Menu Exit",   "F6");
    REG_KEY("TV Playback", "MENUTEXT",   "Menu Text",   "F7");
    REG_KEY("TV Playback", "MENUEPG",    "Menu EPG",    "F12");

    /* Editing keys */
    REG_KEY("TV Editing", "CLEARMAP", "Clear editing cut points", "C,Q,Home");
    REG_KEY("TV Editing", "INVERTMAP", "Invert Begin/End cut points", "I");
    REG_KEY("TV Editing", "LOADCOMMSKIP", "Load cut list from commercial skips",
            "Z,End");
    REG_KEY("TV Editing", "NEXTCUT", "Jump to the next cut point", "PgDown");
    REG_KEY("TV Editing", "PREVCUT", "Jump to the previous cut point", "PgUp");
    REG_KEY("TV Editing", "BIGJUMPREW", "Jump back 10x the normal amount",
            ",,<");
    REG_KEY("TV Editing", "BIGJUMPFWD", "Jump forward 10x the normal amount",
            ">,.");
    REG_KEY("TV Editing", "TOGGLEEDIT", "Exit out of Edit Mode", "E");

    /* Teletext keys */
    REG_KEY("Teletext Menu", "NEXTPAGE",    "Next Page",             "Down");
    REG_KEY("Teletext Menu", "PREVPAGE",    "Previous Page",         "Up");
    REG_KEY("Teletext Menu", "NEXTSUBPAGE", "Next Subpage",          "Right");
    REG_KEY("Teletext Menu", "PREVSUBPAGE", "Previous Subpage",      "Left");
    REG_KEY("Teletext Menu", "TOGGLETT",    "Toggle Teletext",       "T"); 
    REG_KEY("Teletext Menu", "MENURED",     "Menu Red",              "F2");
    REG_KEY("Teletext Menu", "MENUGREEN",   "Menu Green",            "F3");
    REG_KEY("Teletext Menu", "MENUYELLOW",  "Menu Yellow",           "F4");
    REG_KEY("Teletext Menu", "MENUBLUE",    "Menu Blue",             "F5");
    REG_KEY("Teletext Menu", "MENUWHITE",   "Menu White",            "F6");
    REG_KEY("Teletext Menu", "TOGGLEBACKGROUND","Toggle Background", "F7");
    REG_KEY("Teletext Menu", "REVEAL",      "Reveal hidden Text",    "F8");

/*
  keys already used:

  Global:           I   M              0123456789
  Playback: ABCDEFGH JK  NOPQRSTUVWXYZ
  Frontend:   CD          OP R  U  XY  01 3   7 9
  Editing:    C E   I       Q        Z
  Teletext:                    T

  Playback: <>,.?/|[]{}\+-*#^
  Frontend: <>,.?/
  Editing:  <>,.

  Global:   PgDown, PgUp,  Right, Left, Home, End, Up, Down, 
  Playback: PgDown, PgUp,  Right, Left, Home, End, Up, Down, Backspace,
  Frontend:                Right, Left, Home, End
  Editing:  PgDown, PgUp,               Home, End
  Teletext:                Right, Left,            Up, Down,

  Global:   Return, Enter, Space, Esc

  Global:   F1,
  Playback:                   F7,F8,F9,F10,F11
  Teletext     F2,F3,F4,F5,F6,F7,F8
  ITV          F2,F3,F4,F5,F6,F7,F12

  Playback: Ctrl-B,Ctrl-G,Ctrl-Y
*/
}

void *SpawnDecode(void *param)
{
    // OS X needs a garbage collector allocated..
    void *decoder_thread_pool = CreateOSXCocoaPool();
    NuppelVideoPlayer *nvp = (NuppelVideoPlayer *)param;
    nvp->StartPlaying();
    nvp->StopPlaying();
    DeleteOSXCocoaPool(decoder_thread_pool);
    return NULL;
}

/** \fn TV::TV(void)
 *  \brief Performs instance initialiation not requiring access to database.
 *  \sa Init(void)
 */
TV::TV(void)
    : QObject(NULL, "TV"),
      // Configuration variables from database
      baseFilters(""), 
      db_channel_format("<num> <sign>"),
      db_time_format("h:mm AP"), db_short_date_format("M/d"),
      fftime(0), rewtime(0),
      jumptime(0), smartChannelChange(false),
      MuteIndividualChannels(false), arrowAccel(false),
      osd_general_timeout(2), osd_prog_info_timeout(3),
      autoCommercialSkip(CommSkipOff), tryUnflaggedSkip(false),
      smartForward(false), stickykeys(0),
      ff_rew_repos(1.0f), ff_rew_reverse(false),
      jumped_back(false),
      vbimode(VBIMode::None),
      // State variables
      internalState(kState_None),
      switchToInputId(0),
      menurunning(false), runMainLoop(false), wantsToQuit(true),
      exitPlayer(false), paused(false), errored(false),
      stretchAdjustment(false),
      audiosyncAdjustment(false), audiosyncBaseline(LONG_LONG_MIN),
      editmode(false),     zoomMode(false),
      sigMonMode(false),
      update_osd_pos(false), endOfRecording(false), 
      requestDelete(false),  allowRerecord(false),
      doSmartForward(false),
      queuedTranscode(false), getRecorderPlaybackInfo(false),
      adjustingPicture(kAdjustingPicture_None),
      adjustingPictureAttribute(kPictureAttribute_None),
      askAllowType(kAskAllowCancel), askAllowLock(true),
      ignoreKeys(false), needToSwapPIP(false), needToJumpMenu(false),
      // Channel Editing
      chanEditMapLock(true), ddMapSourceId(0), ddMapLoaderRunning(false),
      // Sleep Timer
      sleep_index(0), sleepTimer(new QTimer(this)),
      // Idle Timer
      idleTimer(new QTimer(this)),
      // Key processing buffer, lock, and state
      keyRepeat(true), keyrepeatTimer(new QTimer(this)),
      // Fast forward state
      doing_ff_rew(0), ff_rew_index(0), speed_index(0),
      // Time Stretch state
      normal_speed(1.0f), prev_speed(1.5f), 
      // Estimated framerate from recorder
      frameRate(30.0f),
      // CC/Teletext input state variables
      ccInputMode(false), ccInputModeExpires(QTime::currentTime()),
      // Arbritary seek input state variables
      asInputMode(false), asInputModeExpires(QTime::currentTime()),
      // Channel changing state variables
      queuedChanNum(""),
      muteTimer(new QTimer(this)),
      lockTimerOn(false),
      // previous channel functionality state variables
      prevChanKeyCnt(0), prevChanTimer(new QTimer(this)),
      // channel browsing state variables
      browsemode(false), persistentbrowsemode(false),
      browseTimer(new QTimer(this)),
      browsechannum(""), browsechanid(""), browsestarttime(""),
      // Program Info for currently playing video
      recorderPlaybackInfo(NULL),
      playbackinfo(NULL), playbackLen(0),
      lastProgram(NULL), jumpToProgram(false),
      inPlaylist(false), underNetworkControl(false),
      isnearend(false),
      // Video Players
      nvp(NULL), pipnvp(NULL), activenvp(NULL),
      // Remote Encoders
      recorder(NULL), piprecorder(NULL), activerecorder(NULL),
      switchToRec(NULL), lastrecordernum(-1),
      // LiveTVChain
      tvchain(NULL), piptvchain(NULL),
      // RingBuffers
      prbuffer(NULL), piprbuffer(NULL), activerbuffer(NULL),
      // OSD info
      dialogname(""), treeMenu(NULL), udpnotify(NULL), osdlock(true),
      // LCD Info
      lcdTitle(""), lcdSubtitle(""), lcdCallsign(""),
      // Window info (GUI is optional, transcoding, preview img, etc)
      myWindow(NULL), embedWinID(0), embedBounds(0,0,0,0)
{
    for (uint i = 0; i < 2; i++)
    {
        pseudoLiveTVRec[i]   = NULL;
        pseudoLiveTVState[i] = kPseudoNormalLiveTV;
    }

    lastLcdUpdate = QDateTime::currentDateTime();
    lastLcdUpdate.addYears(-1); // make last LCD update last year..
    lastSignalMsgTime.start();
    lastSignalMsgTime.addMSecs(-2 * kSMExitTimeout);

    sleep_times.push_back(SleepTimerInfo(QObject::tr("Off"),       0));
    sleep_times.push_back(SleepTimerInfo(QObject::tr("30m"),   30*60));
    sleep_times.push_back(SleepTimerInfo(QObject::tr("1h"),    60*60));
    sleep_times.push_back(SleepTimerInfo(QObject::tr("1h30m"), 90*60));
    sleep_times.push_back(SleepTimerInfo(QObject::tr("2h"),   120*60));

    gContext->addListener(this);
    gContext->addCurrentLocation("Playback");

    connect(prevChanTimer,    SIGNAL(timeout()), SLOT(SetPreviousChannel()));
    connect(browseTimer,      SIGNAL(timeout()), SLOT(BrowseEndTimer()));
    connect(muteTimer,        SIGNAL(timeout()), SLOT(UnMute()));
    connect(keyrepeatTimer,   SIGNAL(timeout()), SLOT(KeyRepeatOK()));
    connect(sleepTimer,       SIGNAL(timeout()), SLOT(SleepEndTimer()));
    connect(idleTimer,       SIGNAL(timeout()), SLOT(IdleDialog()));
}

/** \fn TV::Init(bool)
 *  \brief Performs instance initialization, returns true on success.
 *
 *  \param createWindow If true a MythDialog is created for display.
 *  \return Returns true on success, false on failure.
 */
bool TV::Init(bool createWindow)
{
    MSqlQuery query(MSqlQuery::InitCon());
    if (!query.isConnected())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Init(): Could not open DB connection in player");
        errored = true;
        return false;
    }

    baseFilters         += gContext->GetSetting("CustomFilters");
    db_channel_format    =gContext->GetSetting("ChannelFormat","<num> <sign>");
    db_time_format       = gContext->GetSetting("TimeFormat", "h:mm AP");
    db_short_date_format = gContext->GetSetting("ShortDateFormat", "M/d");
    smartChannelChange   = gContext->GetNumSetting("SmartChannelChange", 0);
    MuteIndividualChannels=gContext->GetNumSetting("IndividualMuteControl", 0);
    arrowAccel           = gContext->GetNumSetting("UseArrowAccels", 1);
    persistentbrowsemode = gContext->GetNumSetting("PersistentBrowseMode", 0);
    osd_general_timeout  = gContext->GetNumSetting("OSDGeneralTimeout", 2);
    osd_prog_info_timeout= gContext->GetNumSetting("OSDProgramInfoTimeout", 3);
    autoCommercialSkip   = (enum commSkipMode)gContext->GetNumSetting(
                            "AutoCommercialSkip", CommSkipOff);
    tryUnflaggedSkip     = gContext->GetNumSetting("TryUnflaggedSkip", 0);
    smartForward         = gContext->GetNumSetting("SmartForward", 0);
    stickykeys           = gContext->GetNumSetting("StickyKeys");
    ff_rew_repos         = gContext->GetNumSetting("FFRewReposTime", 100)/100.0;
    ff_rew_reverse       = gContext->GetNumSetting("FFRewReverse", 1);
    int def[8] = { 3, 5, 10, 20, 30, 60, 120, 180 };
    for (uint i = 0; i < sizeof(def)/sizeof(def[0]); i++)
        ff_rew_speeds.push_back(
            gContext->GetNumSetting(QString("FFRewSpeed%1").arg(i), def[i]));

    vbimode = VBIMode::Parse(gContext->GetSetting("VbiFormat"));

    if (createWindow)
    {
        MythMainWindow *mainWindow = gContext->GetMainWindow();
        bool fullscreen = !gContext->GetNumSetting("GuiSizeForTV", 0);
        bool switchMode = gContext->GetNumSetting("UseVideoModes", 0);

        saved_gui_bounds = QRect(mainWindow->geometry().topLeft(),
                                 mainWindow->size());

        // adjust for window manager wierdness.
        {
            int xbase, width, ybase, height;
            float wmult, hmult;
            gContext->GetScreenSettings(xbase, width, wmult,
                                        ybase, height, hmult);
            if ((abs(saved_gui_bounds.x()-xbase) < 3) &&
                (abs(saved_gui_bounds.y()-ybase) < 3))
            {
                saved_gui_bounds = QRect(QPoint(xbase, ybase),
                                         mainWindow->size());
            }
        }

        // if width && height are zero users expect fullscreen playback
        if (!fullscreen)
        {
            int gui_width = 0, gui_height = 0;
            gContext->GetResolutionSetting("Gui", gui_width, gui_height);
            fullscreen |= (0 == gui_width && 0 == gui_height);
        }

        player_bounds = saved_gui_bounds;
        if (fullscreen)
        {
            int xbase, width, ybase, height;
            gContext->GetScreenBounds(xbase, ybase, width, height);
            player_bounds = QRect(xbase, ybase, width, height);
        }

        // main window sizing
        DisplayRes *display_res = DisplayRes::GetDisplayRes();
        int maxWidth = 1920, maxHeight = 1440;
        if (switchMode && display_res)
        {
            // The very first Resize needs to be the maximum possible
            // desired res, because X will mask off anything outside
            // the initial dimensions
            maxWidth = display_res->GetMaxWidth();
            maxHeight = display_res->GetMaxHeight();

            // bit of a hack, but it's ok if the window is too
            // big in fullscreen mode
            if (fullscreen)
            {
                player_bounds.setSize(QSize(maxWidth, maxHeight));

                // resize possibly avoids a bug on some systems
                mainWindow->resize(player_bounds.size());
            }
        }

        // player window sizing
        myWindow = new MythDialog(mainWindow, "video playback window");

        myWindow->installEventFilter(this);
        myWindow->setNoErase();
        QRect win_bounds(0, 0, player_bounds.width(), player_bounds.height());
        myWindow->setGeometry(win_bounds);
        myWindow->setFixedSize(win_bounds.size());

        // resize main window
        mainWindow->setGeometry(player_bounds);
        mainWindow->setFixedSize(player_bounds.size());

        // finally we put the player window on screen...
        myWindow->show();
        myWindow->setBackgroundColor(Qt::black);
        qApp->processEvents();
    }

    mainLoopCondLock.lock();
    pthread_create(&event, NULL, EventThread, this);
    mainLoopCond.wait(&mainLoopCondLock);
    mainLoopCondLock.unlock();

    return !IsErrored();
}

TV::~TV(void)
{
    QMutexLocker locker(&osdlock); // prevent UpdateOSDSignal from continuing.

    if (sleepTimer)
    {
        sleepTimer->disconnect();
        sleepTimer->deleteLater();
        sleepTimer = NULL;
    }

    if (idleTimer)
    {
        idleTimer->disconnect();
        idleTimer->deleteLater();
        idleTimer = NULL;
    }

    if (keyrepeatTimer)
    {
        keyrepeatTimer->disconnect();
        keyrepeatTimer->deleteLater();
        keyrepeatTimer = NULL;
    }

    if (muteTimer)
    {
        muteTimer->disconnect();
        muteTimer->deleteLater();
        muteTimer = NULL;
    }

    if (prevChanTimer)
    {
        prevChanTimer->disconnect();
        prevChanTimer->deleteLater();
        prevChanTimer = NULL;
    }

    if (browseTimer)
    {
        browseTimer->disconnect();
        browseTimer->deleteLater();
        browseTimer = NULL;
    }

    gContext->removeListener(this);
    gContext->removeCurrentLocation();

    runMainLoop = false;
    pthread_join(event, NULL);

    if (prbuffer)
        delete prbuffer;
    if (nvp)
        delete nvp;
    if (myWindow)
    {
        myWindow->deleteLater();
        myWindow = NULL;
        MythMainWindow* mwnd = gContext->GetMainWindow();
        mwnd->resize(saved_gui_bounds.size());
        mwnd->setFixedSize(saved_gui_bounds.size());
        mwnd->show();
        if (!gContext->GetNumSetting("GuiSizeForTV", 0))
            mwnd->move(saved_gui_bounds.topLeft());
    }
    if (recorderPlaybackInfo)
        delete recorderPlaybackInfo;

    if (treeMenu)
        delete treeMenu;

    if (playbackinfo)
        delete playbackinfo;

    if (lastProgram)
        delete lastProgram;

    if (class LCD * lcd = LCD::Get())
        lcd->switchToTime();

    if (tvchain)
    {
        VERBOSE(VB_IMPORTANT, LOC + "Deleting TV Chain in destructor");
        tvchain->DestroyChain();
        delete tvchain;
    }

    if (piptvchain)
    {
        VERBOSE(VB_IMPORTANT, LOC + "Deleting PiP TV Chain in destructor");
        piptvchain->DestroyChain();
        delete piptvchain;
    }    

    if (ddMapLoaderRunning)
    {
        pthread_join(ddMapLoader, NULL);
        ddMapLoaderRunning = false;

        if (ddMapSourceId)
        {            
            int *src = new int;
            *src = ddMapSourceId;
            pthread_create(&ddMapLoader, NULL, load_dd_map_post_thunk, src);
            pthread_detach(ddMapLoader);
        }
    }
}

TVState TV::GetState(void) const
{
    if (InStateChange())
        return kState_ChangingState;
    return internalState;
}

void TV::GetPlayGroupSettings(const QString &group)
{
    fftime       = PlayGroup::GetSetting(group, "skipahead", 30);
    rewtime      = PlayGroup::GetSetting(group, "skipback", 5);
    jumptime     = PlayGroup::GetSetting(group, "jump", 10);
    normal_speed = PlayGroup::GetSetting(group, "timestretch", 100) / 100.0;
    if (normal_speed == 1.0f)
        prev_speed = 1.5f;
    else
        prev_speed = normal_speed;
}

/** \fn TV::LiveTV(bool,bool)
 *  \brief Starts LiveTV
 *  \param showDialogs if true error dialogs are shown, if false they are not
 *  \param startInGuide if true the EPG will be shown upon entering LiveTV
 */
int TV::LiveTV(bool showDialogs, bool startInGuide)
{
    requestDelete = false;
    allowRerecord = false;
    
    if (internalState == kState_None && RequestNextRecorder(showDialogs))
    {
        if (tvchain)
        {
            tvchain->DestroyChain();
            delete tvchain;
        }
        tvchain = new LiveTVChain();
        tvchain->InitializeNewChain(gContext->GetHostName());

        ChangeState(kState_WatchingLiveTV);
        switchToRec = NULL;

        GetPlayGroupSettings("Default");

        // Start Idle Timer
        int idletimeout = gContext->GetNumSetting("LiveTVIdleTimeout", 0);
        if (idletimeout > 0)
        {
            idleTimer->start(idletimeout * 60 * 1000, TRUE);
            VERBOSE(VB_GENERAL, QString("Using Idle Timer. %1 minutes")
                                  .arg(idletimeout));
        }

        if (startInGuide || gContext->GetNumSetting("WatchTVGuide", 0))
        {
            MSqlQuery query(MSqlQuery::InitCon()); 
            query.prepare("SELECT keylist FROM keybindings WHERE "
                          "context = 'TV Playback' AND action = 'GUIDE' AND "
                          "hostname = :HOSTNAME ;");
            query.bindValue(":HOSTNAME", gContext->GetHostName());

            if (query.exec() && query.isActive() && query.size() > 0)
            {
                query.next();

                QKeySequence keyseq(query.value(0).toString());

                int keynum = keyseq[0];
                keynum &= ~Qt::UNICODE_ACCEL;
   
                keyList.prepend(new QKeyEvent(QEvent::KeyPress, keynum, 0, 0));
            }
        }

        return 1;
    }
    return 0;
}

int TV::GetLastRecorderNum(void) const
{
    if (!recorder)
        return lastrecordernum;
    return recorder->GetRecorderNumber();
}

void TV::DeleteRecorder()
{
    RemoteEncoder *rec = recorder;
    activerecorder = recorder = NULL;
    if (rec)
    {
        lastrecordernum = rec->GetRecorderNumber();
        delete rec;
    }
}

bool TV::RequestNextRecorder(bool showDialogs)
{
    DeleteRecorder();

    RemoteEncoder *testrec = NULL;
    if (switchToRec)
    {
        // If this is set we, already got a new recorder in SwitchCards()
        testrec = switchToRec;
        switchToRec = NULL;
    }
    else
    {
        // When starting LiveTV we just get the next free recorder
        testrec = RemoteRequestNextFreeRecorder(-1);
    }

    if (!testrec)
        return false;
    
    if (!testrec->IsValidRecorder())
    {
        if (showDialogs)
            ShowNoRecorderDialog();
        
        delete testrec;
        
        return false;
    }
    
    activerecorder = recorder = testrec;
    return true;
}

void TV::FinishRecording(void)
{
    if (!IsRecording())
        return;

    activerecorder->FinishRecording();
}

void TV::AskAllowRecording(const QStringList &msg, int timeuntil,
                           bool hasrec, bool haslater)
{
    //VERBOSE(VB_IMPORTANT, LOC + "AskAllowRecording");
    if (!StateIsLiveTV(GetState()))
       return;

    ProgramInfo *info = new ProgramInfo;
    QStringList::const_iterator it = msg.begin();
    info->FromStringList(it, msg.end());

    QMutexLocker locker(&askAllowLock);
    QString key = info->MakeUniqueKey();
    if (timeuntil > 0)
    {
        // add program to list
        //VERBOSE(VB_IMPORTANT, LOC + "AskAllowRecording -- " +
        //        QString("adding '%1'").arg(info->title));
        QDateTime expiry = QDateTime::currentDateTime().addSecs(timeuntil);
        askAllowPrograms[key] = AskProgramInfo(expiry, hasrec, haslater, info);
    }
    else
    {
        // remove program from list
        VERBOSE(VB_IMPORTANT, LOC + "AskAllowRecording -- " +
                QString("removing '%1'").arg(info->title));
        QMap<QString,AskProgramInfo>::iterator it = askAllowPrograms.find(key);
        if (it != askAllowPrograms.end())
        {
            delete (*it).info;
            askAllowPrograms.erase(it);
        }
        delete info;
    }

    UpdateOSDAskAllowDialog();
}

void TV::UpdateOSDAskAllowDialog(void)
{
    QMutexLocker locker(&askAllowLock);
    uint cardid = recorder->GetRecorderNumber();

    QString single_rec =
        tr("MythTV wants to record \"%1\" on %2 in %d seconds. "
           "Do you want to:");

    QString record_watch  = tr("Record and watch while it records");
    QString let_record1   = tr("Let it record and go back to the Main Menu");
    QString let_recordm   = tr("Let them record and go back to the Main Menu");
    QString record_later1 = tr("Record it later, I want to watch TV");
    QString record_laterm = tr("Record them later, I want to watch TV");
    QString do_not_record1= tr("Don't let it record, I want to watch TV");
    QString do_not_recordm= tr("Don't let them record, I want to watch TV");

    // eliminate timed out programs
    QDateTime timeNow = QDateTime::currentDateTime();
    QMap<QString,AskProgramInfo>::iterator it = askAllowPrograms.begin();
    QMap<QString,AskProgramInfo>::iterator next = it;
    while (it != askAllowPrograms.end())
    {
        next = it; next++;
        if ((*it).expiry <= timeNow)
        {
            //VERBOSE(VB_IMPORTANT, LOC + "UpdateOSDAskAllowDialog -- " +
            //        QString("removing '%1'").arg((*it).info->title));
            delete (*it).info;
            askAllowPrograms.erase(it);
        }
        it = next;
    }        

    AskAllowType type      = kAskAllowCancel;
    int          sel       = 0;
    int          timeuntil = 0;
    QString      message   = QString::null;
    QStringList  options;

    uint conflict_count = askAllowPrograms.size();

    it = askAllowPrograms.begin();
    if ((1 == askAllowPrograms.size()) && ((uint)(*it).info->cardid == cardid))
    {
        (*it).is_in_same_input_group = (*it).is_conflicting = true;
    }
    else if (!askAllowPrograms.empty())
    {
        // get the currently used input on our card
        bool busy_input_grps_loaded = false;
        vector<uint> busy_input_grps;
        TunedInputInfo busy_input;
        RemoteIsBusy(cardid, busy_input);

        // check if current input can conflict
        it = askAllowPrograms.begin();
        for (; it != askAllowPrograms.end(); ++it)
        {
            (*it).is_in_same_input_group =
                (cardid == (uint)(*it).info->cardid);

            if ((*it).is_in_same_input_group)
                continue;

            // is busy_input in same input group as recording
            if (!busy_input_grps_loaded)
            {
                busy_input_grps = CardUtil::GetInputGroups(busy_input.inputid);
                busy_input_grps_loaded = true;
            }

            vector<uint> input_grps =
                CardUtil::GetInputGroups((*it).info->inputid);

            for (uint i = 0; i < input_grps.size(); i++)
            {
                if (find(busy_input_grps.begin(), busy_input_grps.end(),
                         input_grps[i]) !=  busy_input_grps.end())
                {
                    (*it).is_in_same_input_group = true;
                    break;
                }
            }
        }

        // check if inputs that can conflict are ok
        conflict_count = 0;
        it = askAllowPrograms.begin();
        for (; it != askAllowPrograms.end(); ++it)
        {
            if (!(*it).is_in_same_input_group)
                (*it).is_conflicting = false;
            else if ((cardid == (uint)(*it).info->cardid))
                (*it).is_conflicting = true;
            else if (!CardUtil::IsTunerShared(cardid, (*it).info->cardid))
                (*it).is_conflicting = true;
            else if ((busy_input.sourceid == (uint)(*it).info->sourceid) &&
                     (busy_input.mplexid  == (uint)(*it).info->GetMplexID()))
                (*it).is_conflicting = false;
            else
                (*it).is_conflicting = true;

            conflict_count += (*it).is_conflicting ? 1 : 0;
        }
    }

    it = askAllowPrograms.begin();
    for (; it != askAllowPrograms.end() && !(*it).is_conflicting; ++it);

    if (conflict_count == 0)
    {
        VERBOSE(VB_GENERAL, LOC + "The scheduler wants to make "
                "a non-conflicting recording.");
        // TODO take down mplexid and inform user of problem
        // on channel changes.
    }
    else if (conflict_count == 1 && ((uint)(*it).info->cardid == cardid))
    {
        //VERBOSE(VB_IMPORTANT, LOC + "UpdateOSDAskAllowDialog -- " +
        //        "kAskAllowOneRec");

        type = kAskAllowOneRec;
        it = askAllowPrograms.begin();

        QString channel = QDeepCopy<QString>(db_channel_format);
        channel
            .replace("<num>",  (*it).info->chanstr)
            .replace("<sign>", (*it).info->chansign)
            .replace("<name>", (*it).info->channame);
    
        message = single_rec.arg((*it).info->title).arg(channel);
    
        options += record_watch;
        options += let_record1;
        options += ((*it).has_later) ? record_later1 : do_not_record1;

        sel = ((*it).has_rec) ? 2 : 0;
        timeuntil = QDateTime::currentDateTime().secsTo((*it).expiry);
    }
    else
    {
        type = kAskAllowMultiRec;

        if (conflict_count > 1)
        {
            message = QObject::tr(
                "MythTV wants to record these programs in %d seconds:");
            message += "\n";
        }

        bool has_rec = false;
        it = askAllowPrograms.begin();
        for (; it != askAllowPrograms.end(); ++it)
        {
            if (!(*it).is_conflicting)
                continue;

            QString title = (*it).info->title;
            if ((title.length() < 10) && !(*it).info->subtitle.isEmpty())
                title += ": " + (*it).info->subtitle;
            if (title.length() > 20)
                title = title.left(17) + "...";

            QString channel = QDeepCopy<QString>(db_channel_format);
            channel
                .replace("<num>",  (*it).info->chanstr)
                .replace("<sign>", (*it).info->chansign)
                .replace("<name>", (*it).info->channame);

            if (conflict_count > 1)
            {
                message += QObject::tr("\"%1\" on %2").arg(title).arg(channel);
                message += "\n";
            }
            else
            {
                message = single_rec.arg((*it).info->title).arg(channel);
                has_rec = (*it).has_rec;
            }
        }

        if (conflict_count > 1)
        {
            message += "\n";
            message += QObject::tr("Do you want to:");
        }

        bool all_have_later = true;
        it = askAllowPrograms.begin();
        for (; it != askAllowPrograms.end(); ++it)
        {
            if ((*it).is_conflicting)
                all_have_later &= (*it).has_later;
        }

        if (conflict_count > 1)
        {
            options += let_recordm;
            options += (all_have_later) ? record_laterm : do_not_recordm;
            sel = 0;
        }
        else
        {
            options += let_record1;
            options += (all_have_later) ? record_later1 : do_not_record1;
            sel = (has_rec) ? 1 : 0;
        }

        it = askAllowPrograms.begin();
        timeuntil = 9999;
        for (; it != askAllowPrograms.end(); ++it)
        {
            if (!(*it).is_conflicting)
                continue;
            int tmp = QDateTime::currentDateTime().secsTo((*it).expiry);
            timeuntil = min(timeuntil, max(tmp, 0));
        }
        timeuntil = (9999 == timeuntil) ? 0 : timeuntil;
    }


    //VERBOSE(VB_IMPORTANT, LOC + "UpdateOSDAskAllowDialog -- waiting begin");

    while (!GetOSD())
    {
        //cerr<<":";
        qApp->unlock();
        qApp->processEvents();
        usleep(1000);
        qApp->lock();
    }

    //VERBOSE(VB_IMPORTANT, LOC + "UpdateOSDAskAllowDialog -- waiting done");

    if ((dialogname == "allowrecordingbox") &&
        GetOSD()->DialogShowing(dialogname))
    {
        //VERBOSE(VB_IMPORTANT, LOC + "UpdateOSDAskAllowDialog -- closing");
        askAllowType = kAskAllowCancel;
        GetOSD()->HideSet("allowrecordingbox");
        while (GetOSD()->DialogShowing(dialogname))
        {
            //cerr<<".";
            usleep(1000);
        }
    }

    askAllowType = type;

    if (kAskAllowCancel != askAllowType)
    {
        //VERBOSE(VB_IMPORTANT, LOC + "UpdateOSDAskAllowDialog -- opening");
        dialogname = "allowrecordingbox";
        GetOSD()->NewDialogBox(dialogname, message, options, timeuntil, sel);
    }

    //VERBOSE(VB_IMPORTANT, LOC + "UpdateOSDAskAllowDialog -- done");
}

void TV::HandleOSDAskAllowResponse(void)
{
    if (!askAllowLock.tryLock())
    {
        //VERBOSE(VB_IMPORTANT, "allowrecordingbox : askAllowLock is locked");
        return;
    }

    int result = GetOSD()->GetDialogResponse(dialogname);
    if (kAskAllowOneRec == askAllowType)
    {
        //VERBOSE(VB_IMPORTANT, "allowrecordingbox : one : "<<result);
        switch (result)
        {
            default:
            case 1:
                // watch while it records
                recorder->CancelNextRecording(false);
                break;
            case 2:
                // return to main menu
                StopLiveTV();
                break;
            case 3:
                // cancel scheduled recording
                recorder->CancelNextRecording(true);
                break;                        
        }
    }
    else if (kAskAllowMultiRec == askAllowType)
    {
        //VERBOSE(VB_IMPORTANT, "allowrecordingbox : multi : "<<result);
        switch (result)
        {
            default:
            case 1:
                // return to main menu
                StopLiveTV();
                break;
            case 2:
            {
                // cancel conflicting scheduled recordings
                QMap<QString,AskProgramInfo>::iterator it =
                    askAllowPrograms.begin();
                for (; it != askAllowPrograms.end(); ++it)
                {
                    if ((*it).is_conflicting)
                        RemoteCancelNextRecording((*it).info->cardid, true);
                }
                break;
            }
        }
    }
    else
    {
        //VERBOSE(VB_IMPORTANT,
        //        "allowrecordingbox : cancel : "<<result);
    }

    askAllowLock.unlock();
}


int TV::Playback(ProgramInfo *rcinfo)
{
    wantsToQuit   = false;
    jumpToProgram = false;
    allowRerecord = false;
    requestDelete = false;
    
    if (internalState != kState_None)
        return 0;

    playbackLen = rcinfo->CalculateLength();
    playbackinfo = new ProgramInfo(*rcinfo);

    int overrecordseconds = gContext->GetNumSetting("RecordOverTime");
    QDateTime curtime = QDateTime::currentDateTime();
    QDateTime recendts = rcinfo->recendts.addSecs(overrecordseconds);

    if (curtime < recendts && !rcinfo->isVideo)
        ChangeState(kState_WatchingRecording);
    else
        ChangeState(kState_WatchingPreRecorded);

    GetPlayGroupSettings(playbackinfo->playgroup);

    if (class LCD * lcd = LCD::Get())
        lcd->switchToChannel(rcinfo->chansign, rcinfo->title, rcinfo->subtitle);

    return 1;
}

int TV::PlayFromRecorder(int recordernum)
{
    int retval = 0;

    if (recorder)
    {
        VERBOSE(VB_IMPORTANT, LOC +
                QString("PlayFromRecorder(%1): Recorder already exists!")
                .arg(recordernum));
        return -1;
    }

    activerecorder = recorder = RemoteGetExistingRecorder(recordernum);
    if (!recorder)
        return -1;

    if (recorder->IsValidRecorder())
    {
        // let the mainloop get the programinfo from encoder,
        // connecting to encoder won't work from here
        getRecorderPlaybackInfo = true;
        while (getRecorderPlaybackInfo)
        {
            qApp->unlock();
            qApp->processEvents();
            usleep(1000);
            qApp->lock();
        }
    }

    DeleteRecorder();

    if (recorderPlaybackInfo)
    {
        bool fileexists = false;
        if (recorderPlaybackInfo->pathname.left(7) == "myth://")
            fileexists = RemoteCheckFile(recorderPlaybackInfo);
        else
        {
            QFile checkFile(recorderPlaybackInfo->GetPlaybackURL());
            fileexists = checkFile.exists();
        }

        if (fileexists)
        {
            Playback(recorderPlaybackInfo);
            retval = 1;
        }
    }

    return retval;
}

bool TV::StateIsRecording(TVState state)
{
    return (state == kState_RecordingOnly || 
            state == kState_WatchingRecording);
}

bool TV::StateIsPlaying(TVState state)
{
    return (state == kState_WatchingPreRecorded || 
            state == kState_WatchingRecording);
}

bool TV::StateIsLiveTV(TVState state)
{
    return (state == kState_WatchingLiveTV);
}

TVState TV::RemoveRecording(TVState state)
{
    if (StateIsRecording(state))
    {
        if (state == kState_RecordingOnly)
            return kState_None;
        return kState_WatchingPreRecorded;
    }
    return kState_Error;
}

TVState TV::RemovePlaying(TVState state)
{
    if (StateIsPlaying(state))
        return kState_None;
    return kState_Error;
}

#define TRANSITION(ASTATE,BSTATE) \
   ((internalState == ASTATE) && (desiredNextState == BSTATE))

#define SET_NEXT() do { nextState = desiredNextState; changed = true; } while(0);
#define SET_LAST() do { nextState = internalState; changed = true; } while(0);

/** \fn TV::HandleStateChange(void)
 *  \brief Changes the state to the state on the front of the 
 *         state change queue.
 *
 *   Note: There must exist a state transition from any state we can enter
 *   to  the kState_None state, as this is used to shutdown TV in RunTV.
 *
 */
void TV::HandleStateChange(void)
{
    if (IsErrored())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "HandleStateChange(): "
                "Called after fatal error detected.");
        return;
    }

    bool changed = false;

    stateLock.lock();
    TVState nextState = internalState;
    if (!nextStates.size())
    {
        VERBOSE(VB_IMPORTANT, LOC + "HandleStateChange() Warning, "
                "called with no state to change to.");
        stateLock.unlock();
        return;
    }
    TVState desiredNextState = nextStates.dequeue();    
    VERBOSE(VB_GENERAL, LOC + QString("Attempting to change from %1 to %2")
            .arg(StateToString(nextState))
            .arg(StateToString(desiredNextState)));

    if (desiredNextState == kState_Error)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "HandleStateChange(): "
                "Attempting to set to an error state!");
        errored = true;
        stateLock.unlock();
        return;
    }

    if (TRANSITION(kState_None, kState_WatchingLiveTV))
    {
        QString name = "";

        lastSignalUIInfo.clear();
        activerecorder = recorder;
        recorder->Setup();
        
        QDateTime timerOffTime = QDateTime::currentDateTime();
        lockTimerOn = false;

        SET_NEXT();
        recorder->SpawnLiveTV(tvchain->GetID(), false, "");

        tvchain->ReloadAll();

        playbackinfo = tvchain->GetProgramAt(-1);
        if (!playbackinfo)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    "LiveTV not successfully started");
            gContext->RestoreScreensaver();
            DeleteRecorder();

            SET_LAST();
        }
        else
        {
            QString playbackURL = playbackinfo->GetPlaybackURL();

            tvchain->SetProgram(playbackinfo);

            bool opennow = (tvchain->GetCardType(-1) != "DUMMY");
            prbuffer = new RingBuffer(playbackURL, false, true,
                                      opennow ? 12 : (uint)-1);
            prbuffer->SetLiveMode(tvchain);
        }

        gContext->DisableScreensaver();

        bool ok = false;
        if (playbackinfo && StartRecorder(recorder,-1))
        {
            if (StartPlayer(false))
                ok = true;
            else
                StopStuff(true, true, true);
        }
        if (!ok)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    "LiveTV not successfully started");
            gContext->RestoreScreensaver();
            DeleteRecorder();

            SET_LAST();
        }
        else if (!lastLockSeenTime.isValid() ||
                 (lastLockSeenTime < timerOffTime))
        {
            lockTimer.start();
            lockTimerOn = true;
        }
    }
    else if (TRANSITION(kState_WatchingLiveTV, kState_None))
    {
        SET_NEXT();

        StopStuff(true, true, true);
        pbinfoLock.lock();
        if (playbackinfo)
            delete playbackinfo;
        playbackinfo = NULL;
        pbinfoLock.unlock();

        gContext->RestoreScreensaver();
    }
    else if (TRANSITION(kState_WatchingRecording, kState_WatchingPreRecorded))
    {
        SET_NEXT();
    }
    else if (TRANSITION(kState_None, kState_WatchingPreRecorded) ||
             TRANSITION(kState_None, kState_WatchingRecording))
    {
        QString playbackURL;
        if ((playbackinfo->pathname.left(4) == "dvd:") ||
            (playbackinfo->isVideo))
            playbackURL = playbackinfo->pathname;
        else
            playbackURL = playbackinfo->GetPlaybackURL(
                              desiredNextState != kState_WatchingRecording);

        prbuffer = new RingBuffer(playbackURL, false);
        if (prbuffer->IsOpen())
        {
            gContext->DisableScreensaver();
    
            if (desiredNextState == kState_WatchingRecording)
            {
                activerecorder = recorder =
                    RemoteGetExistingRecorder(playbackinfo);
                if (!recorder || !recorder->IsValidRecorder())
                {
                    VERBOSE(VB_IMPORTANT, LOC_ERR + "Couldn't find "
                            "recorder for in-progress recording");
                    desiredNextState = kState_WatchingPreRecorded;
                    DeleteRecorder();
                }
                else
                {
                    recorder->Setup();
                }
            }

            StartPlayer(desiredNextState == kState_WatchingRecording);
            
            SET_NEXT();
            if (!playbackinfo->isVideo)
            {
                QString message = "COMMFLAG_REQUEST ";
                message += playbackinfo->chanid + " " +
                           playbackinfo->recstartts.toString(Qt::ISODate);
                RemoteSendMessage(message);
            }                
        }
        else
        {
            SET_LAST();
            wantsToQuit   = true;
        }
    }
    else if (TRANSITION(kState_WatchingPreRecorded, kState_None) ||
             TRANSITION(kState_WatchingRecording, kState_None))
    {
        SET_NEXT();

        StopStuff(true, true, false);
        gContext->RestoreScreensaver();
    }
    else if (TRANSITION(kState_None, kState_None))
    {
        SET_NEXT();
    }

    // Check that new state is valid
    if (kState_None != nextState &&
        activenvp && !activenvp->IsDecoderThreadAlive())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Decoder not alive, and trying to play..");
        if (nextState == kState_WatchingLiveTV)
        {
            StopStuff(true, true, true);
        }

        nextState = kState_None;
        changed = true;
    }

    // Print state changed message...
    if (!changed)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("Unknown state transition: %1 to %2")
                .arg(StateToString(internalState))
                .arg(StateToString(desiredNextState)));
    }
    else if (internalState != nextState)
    {
        VERBOSE(VB_GENERAL, LOC + QString("Changing from %1 to %2")
                .arg(StateToString(internalState))
                .arg(StateToString(nextState)));
    }

    // update internal state variable
    TVState lastState = internalState;
    internalState = nextState;
    stateLock.unlock();

    if (StateIsLiveTV(internalState))
    {
        UpdateOSDInput();
        UpdateLCD();
        ITVRestart(true);
    }
    else if (StateIsPlaying(internalState) && lastState == kState_None)
    {
        if (GetOSD() && (PlayGroup::GetCount() > 0))
            GetOSD()->SetSettingsText(tr("%1 Settings")
                                      .arg(tr(playbackinfo->playgroup)), 3);
        ITVRestart(false);
    }
    if (prbuffer && prbuffer->isDVD()) {
        UpdateLCD();
    }
    if (recorder)
        recorder->FrontendReady();
}
#undef TRANSITION
#undef SET_NEXT
#undef SET_LAST

/** \fn TV::InStateChange() const
 *  \brief Returns true if there is a state change queued up.
 */
bool TV::InStateChange() const
{
    if (!stateLock.tryLock())
        return true;
    bool inStateChange = nextStates.size() > 0;
    stateLock.unlock();
    return inStateChange;
}

/** \fn TV::ChangeState(TVState nextState)
 *  \brief Puts a state change on the nextState queue.
 */
void TV::ChangeState(TVState nextState)
{
    stateLock.lock();
    nextStates.enqueue(nextState);
    stateLock.unlock();
}

/** \fn TV::ForceNextStateNone()
 *  \brief Removes any pending state changes, and puts kState_None on the queue.
 */
void TV::ForceNextStateNone()
{
    stateLock.lock();
    nextStates.clear();
    nextStates.push_back(kState_None);
    stateLock.unlock();
}

/** \fn TV::StartRecorder(RemoteEncoder*, int)
 *  \brief Starts recorder, must be called before StartPlayer().
 *  \param maxWait How long to wait for RecorderBase to start recording.
 *  \return true when successful, false otherwise.
 */
bool TV::StartRecorder(RemoteEncoder *rec, int maxWait)
{
    maxWait = (maxWait <= 0) ? 40000 : maxWait;
    MythTimer t;
    t.start();
    while (!rec->IsRecording() && !exitPlayer && t.elapsed() < maxWait)
        usleep(5000);
    if (!rec->IsRecording() || exitPlayer)
    {
        if (!exitPlayer)
            VERBOSE(VB_IMPORTANT, LOC_ERR + "StartRecorder() -- "
                    "timed out waiting for recorder to start");
        return false;
    }

    VERBOSE(VB_PLAYBACK, LOC + "StartRecorder(): took "<<t.elapsed()
            <<" ms to start recorder.");

    // Cache starting frame rate for this recorder
    if (rec == recorder)
        frameRate = recorder->GetFrameRate();
    return true;
}

/** \fn TV::StartPlayer(bool, int)
 *  \brief Starts player, must be called after StartRecorder().
 *  \param maxWait How long to wait for NuppelVideoPlayer to start playing.
 *  \return true when successful, false otherwise.
 */
bool TV::StartPlayer(bool isWatchingRecording, int maxWait)
{ 
    SetupPlayer(isWatchingRecording);
    pthread_create(&decode, NULL, SpawnDecode, nvp);

    maxWait = (maxWait <= 0) ? 20000 : maxWait;
#ifdef USING_VALGRIND
    maxWait = (1<<30);
#endif // USING_VALGRIND
    MythTimer t;
    t.start();
    while (!nvp->IsPlaying() && nvp->IsDecoderThreadAlive() &&
           (t.elapsed() < maxWait))
        usleep(50);

    VERBOSE(VB_PLAYBACK, LOC + "StartPlayer(): took "<<t.elapsed()
            <<" ms to start player.");

    if (nvp->IsPlaying())
    {
        nvp->ResetCaptions();
        nvp->ResetTeletext();

        activenvp = nvp;
        activerbuffer = prbuffer;
        StartOSD();
        return true;
    }
    VERBOSE(VB_IMPORTANT, LOC_ERR +
            QString("StartPlayer(): NVP is not playing after %1 msec")
            .arg(maxWait));
    return false;
}

/** \fn TV::StartOSD()
 *  \brief Initializes the on screen display.
 *
 *   If the NuppelVideoPlayer already exists we grab it's OSD via
 *   NuppelVideoPlayer::GetOSD().
 */
void TV::StartOSD()
{
    if (nvp)
    {
        frameRate = nvp->GetFrameRate();
        if (nvp->GetOSD())
            GetOSD()->SetUpOSDClosedHandler(this);
    }
}

/** \fn TV::StopStuff(bool, bool, bool)
 *  \brief Can shut down the ringbuffers, the players, and in LiveTV it can 
 *         shut down the recorders.
 *
 *   The player needs to be partially shutdown before the recorder,
 *   and partially shutdown after the recorder. Hence these are shutdown
 *   from within the same method. Also, shutting down things in the right
 *   order avoids spewing error messages...
 *
 *  \param stopRingBuffers Set to true if ringbuffer must be shut down.
 *  \param stopPlayers     Set to true if player must be shut down.
 *  \param stopRecorders   Set to true if recorder must be shut down.
 */
void TV::StopStuff(bool stopRingBuffers, bool stopPlayers, bool stopRecorders)
{
    VERBOSE(VB_PLAYBACK, LOC + "StopStuff() -- begin");

    if (prbuffer && prbuffer->isDVD())
    {
        VERBOSE(VB_PLAYBACK,LOC + " StopStuff() -- get dvd player out of still frame or wait status");
        prbuffer->DVD()->IgnoreStillOrWait(true);
    }

    if (stopRingBuffers)
    {
        VERBOSE(VB_PLAYBACK, LOC + "StopStuff(): stopping ring buffer[s]");
        if (prbuffer)
        {
            prbuffer->StopReads();
            prbuffer->Pause();
            prbuffer->WaitForPause();
        }

        if (piprbuffer)
        {
            piprbuffer->StopReads();
            piprbuffer->Pause();
            piprbuffer->WaitForPause();
        }
    }

    if (stopPlayers)
    {
        VERBOSE(VB_PLAYBACK, LOC + "StopStuff(): stopping player[s] (1/2)");
        if (nvp)
            nvp->StopPlaying();

        if (pipnvp)
            pipnvp->StopPlaying();
    }

    if (stopRecorders)
    {
        VERBOSE(VB_PLAYBACK, LOC + "StopStuff(): stopping recorder[s]");
        if (recorder)
            recorder->StopLiveTV();

        if (piprecorder)
            piprecorder->StopLiveTV();
    }

    if (stopPlayers)
    {
        VERBOSE(VB_PLAYBACK, LOC + "StopStuff(): stopping player[s] (2/2)");
        if (nvp)
            TeardownPlayer();

        if (pipnvp)
            TeardownPipPlayer();
    }
    VERBOSE(VB_PLAYBACK, LOC + "StopStuff() -- end");
}

void TV::SetupPlayer(bool isWatchingRecording)
{
    if (nvp)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Attempting to setup a player, but it already exists.");
        return;
    }

    nvp = new NuppelVideoPlayer("player", playbackinfo);
    nvp->SetParentWidget(myWindow);
    nvp->SetParentPlayer(this);
    nvp->SetRingBuffer(prbuffer);
    nvp->SetRecorder(recorder);
    nvp->SetAudioInfo(gContext->GetSetting("AudioOutputDevice"),
                      gContext->GetSetting("PassThruOutputDevice"),
                      gContext->GetNumSetting("AudioSampleRate", 44100));
    nvp->SetLength(playbackLen);
    nvp->SetExactSeeks(gContext->GetNumSetting("ExactSeeking", 0));
    nvp->SetAutoCommercialSkip(autoCommercialSkip);
    LoadExternalSubtitles(nvp, prbuffer->GetFilename());
    nvp->SetLiveTVChain(tvchain);

    nvp->SetAudioStretchFactor(normal_speed);

    if (embedWinID > 0)
        nvp->EmbedInWidget(embedWinID, embedBounds.x(), embedBounds.y(),
                           embedBounds.width(), embedBounds.height());

    if (isWatchingRecording)
        nvp->SetWatchingRecording(true);

    int udp_port = gContext->GetNumSetting("UDPNotifyPort");
    if (udp_port > 0)
    {
        if (udpnotify == NULL)
            udpnotify = new UDPNotify(this, udp_port);
    }
    else
        udpnotify = NULL;
}

void TV::SetupPipPlayer(void)
{
    if (pipnvp)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Attempting to setup a PiP player, but it already exists.");
        return;
    }

    pipnvp = new NuppelVideoPlayer("PIP player");
    pipnvp->SetAsPIP();
    pipnvp->SetRingBuffer(piprbuffer);
    pipnvp->SetRecorder(piprecorder);
    pipnvp->SetAudioInfo(gContext->GetSetting("AudioOutputDevice"),
                         gContext->GetSetting("PassThruOutputDevice"),
                         gContext->GetNumSetting("AudioSampleRate", 44100));
    pipnvp->SetExactSeeks(gContext->GetNumSetting("ExactSeeking", 0));
    pipnvp->SetLiveTVChain(piptvchain);

    pipnvp->SetLength(playbackLen);
}

void TV::TeardownPlayer(void)
{
    if (nvp)
    {
        osdlock.lock(); // prevent UpdateOSDSignal from using osd...

        NuppelVideoPlayer *xnvp = nvp;
        pthread_t          xdec = decode;

        nvp            = NULL;
        activenvp      = NULL;
        activerecorder = NULL;
        activerbuffer  = NULL;

        osdlock.unlock();

        // NVP may try to get qapp lock if there is an error,
        // so we need to do this outside of the osdlock.
        pthread_join(xdec, NULL);
        delete xnvp;
    }

    if (udpnotify)
    {
        delete udpnotify;
        udpnotify = NULL;
    }

    paused = false;
    doing_ff_rew = 0;
    ff_rew_index = kInitFFRWSpeed;
    speed_index = 0;
    sleep_index = 0;
    normal_speed = 1.0f;

    pbinfoLock.lock();
    if (playbackinfo)
        delete playbackinfo;
    playbackinfo = NULL;
    pbinfoLock.unlock(); 

    DeleteRecorder();

    if (prbuffer)
    {
        delete prbuffer;
        prbuffer = activerbuffer = NULL;
    }

    if (piprbuffer)
    {
        delete piprbuffer;
        piprbuffer = NULL;
    }

    if (tvchain)
    {
        tvchain->DestroyChain();
        delete tvchain;
        tvchain = NULL;
    }
}

void TV::TeardownPipPlayer(void)
{
    if (pipnvp)
    {
        if (activerecorder == piprecorder)
            ToggleActiveWindow();

        osdlock.lock(); // prevent UpdateOSDSignal from using osd...
        NuppelVideoPlayer *xnvp = pipnvp;
        pthread_t          xdec = pipdecode;
        pipnvp = NULL;
        osdlock.unlock();

        // NVP may try to get qapp lock if there is an error,
        // so we need to do this outside of the osdlock.
        pthread_join(xdec, NULL);
        delete xnvp;
    }

    if (piprecorder)
    {
        delete piprecorder;
        piprecorder = NULL;
    }

    if (piprbuffer)
    {
        delete piprbuffer;
        piprbuffer = NULL;
    }

    if (piptvchain)
    {
        piptvchain->DestroyChain();
        delete piptvchain;
        piptvchain = NULL;
    }
}

void *TV::EventThread(void *param)
{
    TV *thetv = (TV *)param;
    thetv->RunTV();

    return NULL;
}

void TV::RunTV(void)
{ 
    paused = false;

    doing_ff_rew = 0;
    ff_rew_index = kInitFFRWSpeed;
    speed_index = 0;
    normal_speed = 1.0f;
    sleep_index = 0;

    int updatecheck = 0;
    update_osd_pos = false;

    lastLcdUpdate = QDateTime::currentDateTime();
    UpdateLCD();
    
    ClearInputQueues();

    switchToRec = NULL;
    runMainLoop = true;
    exitPlayer = false;

    mainLoopCondLock.lock();
    mainLoopCond.wakeAll();
    mainLoopCondLock.unlock();

    while (runMainLoop)
    {
        stateLock.lock();
        bool doHandle = nextStates.size() > 0;
        stateLock.unlock();
        if (doHandle)
            HandleStateChange();

        if (osdlock.tryLock())
        {
            if (lastSignalMsg.size())
            {   // set last signal msg, so we get some feedback...
                UpdateOSDSignal(lastSignalMsg);
                lastSignalMsg.clear();
            }
            UpdateOSDTimeoutMessage();

            if (!tvchainUpdate.isEmpty())
            {
                tvchainUpdateLock.lock();
                for (QStringList::Iterator it = tvchainUpdate.begin();
                     it != tvchainUpdate.end(); ++it)
                {
                    if (tvchain && nvp && *it == tvchain->GetID())
                    {
                        tvchain->ReloadAll();
                        if (nvp->GetTVChain())
                            nvp->CheckTVChain();
                    }
                    if (piptvchain && pipnvp && *it == piptvchain->GetID())
                    {
                        piptvchain->ReloadAll();
                        if (pipnvp->GetTVChain())
                            pipnvp->CheckTVChain();
                    }
                }
                tvchainUpdate.clear();
                tvchainUpdateLock.unlock();
            }

            osdlock.unlock();
        }

        usleep(1000);

        if (getRecorderPlaybackInfo)
        {
            if (recorderPlaybackInfo)
            {
                delete recorderPlaybackInfo;
                recorderPlaybackInfo = NULL;
            }

            recorder->Setup();

            if (recorder->IsRecording())
            {
                recorderPlaybackInfo = recorder->GetRecording();
                RemoteFillProginfo(recorderPlaybackInfo, 
                                   gContext->GetHostName());
            }

            getRecorderPlaybackInfo = false;
        }

        bool had_key = false;
        if (nvp)
        {
            QKeyEvent *keypressed = NULL;

            keyListLock.lock();
            if (keyList.count() > 0)
            {
                keypressed = keyList.first();
                keyList.removeFirst();
            }
            keyListLock.unlock();

            if (keypressed)
            {
                had_key = true;
                ProcessKeypress(keypressed);
                delete keypressed;
            }
        }

        if (nvp && !had_key && !ignoreKeys)
        {
            QString netCmd = QString::null;
            ncLock.lock();
            if (networkControlCommands.size())
            {
                netCmd = networkControlCommands.front();
                networkControlCommands.pop_front();
            }
            ncLock.unlock();

            if (!netCmd.isEmpty())
                ProcessNetworkControlCommand(netCmd);
        }

        if ((recorder && recorder->GetErrorStatus()) ||
            (nvp && nvp->IsErrored()) || IsErrored())
        {
            exitPlayer = true;
            wantsToQuit = true;
        }

        if (StateIsPlaying(internalState))
        {
#ifdef USING_VALGRIND
            while (!nvp->IsPlaying())
            {
                VERBOSE(VB_IMPORTANT, LOC + "Waiting for Valgrind...");
                sleep(1);
            }
#endif // USING_VALGRIND

            if (!nvp->IsPlaying())
            {
                ChangeState(RemovePlaying(internalState));
                endOfRecording = true;
                wantsToQuit = true;
                if (nvp && gContext->GetNumSetting("AutomaticSetWatched", 0))
                    nvp->SetWatched();
                VERBOSE(VB_PLAYBACK, LOC_ERR + "nvp->IsPlaying() timed out");
            }

            if (nvp->IsNearEnd())
                isnearend = true;
            else
                isnearend = false;
            
            if (isnearend && IsEmbedding() && !paused)
                DoPause();
        }

        if (!endOfRecording)
        {
            if (jumped_back && !isnearend)
                jumped_back = false;
            
            if (internalState == kState_WatchingPreRecorded && !inPlaylist &&
                dialogname == "" && isnearend && !exitPlayer 
                && !underNetworkControl &&
                (gContext->GetNumSetting("EndOfRecordingExitPrompt") == 1) &&
                !jumped_back && !editmode && !IsEmbedding() && !paused)
            {
                PromptDeleteRecording(tr("End Of Recording"));
            }
            
                
            // disable dialog and enable playback after 2 minutes
            if (IsVideoExitDialog() &&
                dialogboxTimer.elapsed() > (2 * 60 * 1000))
            {
                GetOSD()->TurnDialogOff(dialogname);
                dialogname = "";
                DoPause();
                ClearOSD();

                requestDelete = false;
                exitPlayer  = true;
                wantsToQuit = true;
            }
        }
        
        if (exitPlayer)
        {
            while (GetOSD() && GetOSD()->DialogShowing(dialogname))
            {
                GetOSD()->DialogAbort(dialogname);
                GetOSD()->TurnDialogOff(dialogname);
                usleep(1000);
            }
            
            
            if (jumpToProgram && lastProgram)
            {
                bool fileExists = lastProgram->PathnameExists();
                if (!fileExists)
                {
                    if (GetOSD())
                    {
                        QString msg = tr("Last Program: %1 Doesn't Exist")
                            .arg(lastProgram->title);
                        GetOSD()->SetSettingsText(msg, 3);
                    }
                    lastProgramStringList.clear();
                    setLastProgram(NULL);
                    VERBOSE(VB_PLAYBACK, LOC_ERR + "Last Program File does not exist");
                    jumpToProgram = false;
                }
                else
                    ChangeState(kState_None);
            }
            else
                ChangeState(kState_None);

            exitPlayer = false;
        }

        for (uint i = InStateChange() ? 2 : 0; i < 2; i++)
        {
            if (kPseudoChangeChannel != pseudoLiveTVState[i])
                continue;

            VERBOSE(VB_PLAYBACK, "REC_PROGRAM -- channel change "<<i);
            if (activerecorder != recorder)
                ToggleActiveWindow();

            uint    chanid  = pseudoLiveTVRec[i]->chanid.toUInt();
            QString channum = pseudoLiveTVRec[i]->chanstr;
            str_vec_t tmp = prevChan;
            prevChan.clear();
            if (i && activenvp == nvp)
                ToggleActiveWindow();
            ChangeChannel(chanid, channum);
            prevChan = tmp;
            pseudoLiveTVState[i] = kPseudoRecording;

            if (i && pipnvp)
                TogglePIPView();
        }

        if ((doing_ff_rew || speed_index) && 
            activenvp && activenvp->AtNormalSpeed())
        {
            speed_index = 0;
            doing_ff_rew = 0;
            ff_rew_index = kInitFFRWSpeed;
            UpdateOSDSeekMessage(PlayMesg(), osd_general_timeout);
        }

        if (activenvp && (activenvp->GetNextPlaySpeed() != normal_speed) &&
            activenvp->AtNormalSpeed() &&
            !activenvp->PlayingSlowForPrebuffer())
        {
            // got changed in nvp due to close to end of file
            normal_speed = 1.0f;
            UpdateOSDSeekMessage(PlayMesg(), osd_general_timeout);
        }

        if (++updatecheck >= 100)
        {
            OSDSet *oset;
            if (GetOSD() && (oset = GetOSD()->GetSet("status")) &&
                oset->Displaying() && update_osd_pos &&
                (StateIsLiveTV(internalState) ||
                 StateIsPlaying(internalState)))
            {
                struct StatusPosInfo posInfo;
                nvp->calcSliderPos(posInfo);
                GetOSD()->UpdateStatus(posInfo);
            }

            updatecheck = 0;
        }

        // Commit input when the OSD fades away
        if (HasQueuedChannel() && GetOSD())
        {
            OSDSet *set = GetOSD()->GetSet("channel_number");
            if ((set && !set->Displaying()) || !set)
                CommitQueuedInput();
        }
        // Clear closed caption input mode when timer expires
        if (ccInputMode && (ccInputModeExpires < QTime::currentTime()))
        {
            ccInputMode = false;
            ClearInputQueues(true);
        }
        // Clear arbitrary seek input mode when timer expires
        if (asInputMode && (asInputModeExpires < QTime::currentTime()))
        {
            asInputMode = false;
            ClearInputQueues(true);
        }   

        if (switchToInputId)
        {
            uint tmp = switchToInputId;
            switchToInputId = 0;
            SwitchInputs(tmp);
        }

        if (class LCD * lcd = LCD::Get())
        {
            QDateTime curTime = QDateTime::currentDateTime();

            if (lastLcdUpdate.secsTo(curTime) < kLCDTimeout)
                continue;

            float progress = 0.0;
            bool  showProgress = true;

            if (StateIsLiveTV(GetState()))
                ShowLCDChannelInfo();

            if (activerbuffer && activerbuffer->isDVD())
            {
                ShowLCDDVDInfo();
                showProgress = !activerbuffer->InDVDMenuOrStillFrame();
            }

            if (activenvp && showProgress)
            {
                struct StatusPosInfo posInfo;
                nvp->calcSliderPos(posInfo);
                progress = (float)posInfo.position / 1000;
            }
            lcd->setChannelProgress(progress);

            lastLcdUpdate = QDateTime::currentDateTime();
        }

        if (needToSwapPIP)
        {
            ClearOSD();
            SwapPIP();
            needToSwapPIP = false;
        }

        if (needToJumpMenu)
        {
            DoDisplayJumpMenu();
            needToJumpMenu = false;
        }
    }
  
    if (!IsErrored() && (GetState() != kState_None))
    {
        ForceNextStateNone();
        HandleStateChange();
    }
}

bool TV::eventFilter(QObject *o, QEvent *e)
{
    (void)o;

    switch (e->type())
    {
        case QEvent::KeyPress:
        {
            QKeyEvent *k = new QKeyEvent(*(QKeyEvent *)e);
  
            // can't process these events in the Qt event loop. 
            keyListLock.lock();
            keyList.append(k);
            keyListLock.unlock();

            return true;
        }
        case QEvent::Paint:
        {
            if (nvp)
                nvp->ExposeEvent();
            return true;
        }
        case MythEvent::MythEventMessage:
        {
            customEvent((QCustomEvent *)e);
            return true;
        }
        default:
            return false;
    }
}

bool TV::HandleTrackAction(const QString &action)
{
    bool handled = false;
    if (!activenvp)
        return false;

    if (action == "TOGGLECC" && !browsemode)
    {
        handled = true;
        if (ccInputMode)
        {
            bool valid = false;
            int page = GetQueuedInputAsInt(&valid, 16);
            if (vbimode == VBIMode::PAL_TT && valid)
                activenvp->SetTeletextPage(page);
            else if (vbimode == VBIMode::NTSC_CC)
                activenvp->SetTrack(kTrackTypeCC608, max(min(page - 1, 1), 0));
            ccInputModeExpires.start(); // expire ccInputMode now...
            ClearInputQueues(true);
        }
        else if (activenvp->GetCaptionMode() & kDisplayNUVTeletextCaptions)
        {
            ccInputMode        = true;
            ccInputModeExpires = QTime::currentTime()
                .addMSecs(kInputModeTimeout);
            asInputModeExpires = QTime::currentTime();
            ClearInputQueues(false);
            AddKeyToInputQueue(0);
        }
        else
        {
            activenvp->ToggleCaptions();
        }
    }
    else if (action.left(6) == "TOGGLE")
    {
        int type = type_string_to_track_type(action.mid(6));
        if (type == kTrackTypeTeletextMenu)
        {
            handled = true;
            activenvp->EnableTeletext();
        }
        else if (type >= kTrackTypeSubtitle)
        {
            handled = true;
            activenvp->ToggleCaptions(type);
        }
    }
    else if (action.left(6) == "SELECT")
    {
        int type = type_string_to_track_type(action.mid(6));
        int mid  = (kTrackTypeSubtitle == type) ? 15 : 12;
        if (type >= kTrackTypeAudio)
        {
            handled = true;
            activenvp->SetTrack(type, action.mid(mid).toInt());
        }
    }
    else if (action.left(4) == "NEXT" || action.left(4) == "PREV")
    {
        int dir = (action.left(4) == "NEXT") ? +1 : -1;
        int type = type_string_to_track_type(action.mid(4));
        if (type >= kTrackTypeAudio)
        {
            handled = true;
            activenvp->ChangeTrack(type, dir);
        }
        else if (action.right(2) == "CC")
        {
            handled = true;
            activenvp->ChangeCaptionTrack(dir);
        }
    }

    return handled;
}

static bool has_action(QString action, const QStringList &actions)
{
    QStringList::const_iterator it;
    for (it = actions.begin(); it != actions.end(); ++it)
    {
        if (action == *it)
            return true;
    }
    return false;
}

void TV::ProcessKeypress(QKeyEvent *e)
{
#if DEBUG_ACTIONS
    VERBOSE(VB_IMPORTANT, LOC + "ProcessKeypress() ignoreKeys: "<<ignoreKeys);
#endif // DEBUG_ACTIONS

    if (!GetOSD() || !GetOSD()->DialogShowing("idletimeout")
            && StateIsLiveTV(GetState()) && idleTimer->isActive())
        idleTimer->changeInterval(gContext->GetNumSetting("LiveTVIdleTimeout",
                                                          0) * 60 * 1000);

    bool was_doing_ff_rew = false;
    bool redisplayBrowseInfo = false;
    QStringList actions;

    if (ignoreKeys)
    {
        if (!gContext->GetMainWindow()->TranslateKeyPress(
                "TV Playback", e, actions))
        {
            return;
        }

        bool esc   = has_action("ESCAPE", actions);
        bool pause = has_action("PAUSE",  actions);
        bool play  = has_action("PLAY",   actions);

        if ((!esc || browsemode) && !pause && !play)
            return;
    }

    if (editmode)
    {   
        if (!nvp->DoKeypress(e))
            editmode = nvp->GetEditMode();
        if (!editmode)
        {
            paused = !paused;
            DoPause();
        }
        return;
    }

    if (GetOSD() && GetOSD()->IsRunningTreeMenu())
    {
        GetOSD()->TreeMenuHandleKeypress(e);
        return;
    }

    // If text is already queued up, be more lax on what is ok.
    // This allows hex teletext entry and minor channel entry.
    const QString txt = e->text();
    if (HasQueuedInput() && (1 == txt.length()))
    {
        bool ok = false;
        txt.toInt(&ok, 16);
        if (ok || txt=="_" || txt=="-" || txt=="#" || txt==".")
        {
            AddKeyToInputQueue(txt.at(0).latin1());
            return;
        }
    }

    if (GetOSD() && dialogname == "channel_editor")
    {
        ChannelEditKey(e);
        return;
    }

    // Teletext menu
    if (activenvp && (activenvp->GetCaptionMode() == kDisplayTeletextMenu))
    {
        QStringList tt_actions;
        if (gContext->GetMainWindow()->TranslateKeyPress(
                "Teletext Menu", e, tt_actions))
        {
            for (uint i = 0; i < tt_actions.size(); i++)
                if (activenvp->HandleTeletextAction(tt_actions[i]))
                    return;
        }
    }

    // Interactive television
    if (activenvp && activenvp->GetInteractiveTV())
    {
        QStringList itv_actions;
        if (gContext->GetMainWindow()->TranslateKeyPress(
                "TV Playback", e, itv_actions))
        for (uint i = 0; i < itv_actions.size(); i++)
        {
            if (activenvp->ITVHandleAction(itv_actions[i]))
                return;
        }
    }

    if (!gContext->GetMainWindow()->TranslateKeyPress(
            "TV Playback", e, actions))
    {
        return;
    }

    bool handled = false;

    if (browsemode)
    {
        int passThru = 0;

        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "UP" || action == "CHANNELUP")
                BrowseDispInfo(BROWSE_UP);
            else if (action == "DOWN" || action == "CHANNELDOWN")
                BrowseDispInfo(BROWSE_DOWN);
            else if (action == "LEFT")
                BrowseDispInfo(BROWSE_LEFT);
            else if (action == "RIGHT")
                BrowseDispInfo(BROWSE_RIGHT);
            else if (action == "NEXTFAV")
                BrowseDispInfo(BROWSE_FAVORITE);
            else if (action == "0" || action == "1" || action == "2" ||
                     action == "3" || action == "4" || action == "5" ||
                     action == "6" || action == "7" || action == "8" ||
                     action == "9")
            {
                AddKeyToInputQueue('0' + action.toInt());
            }
            else if (action == "TOGGLEBROWSE" || action == "ESCAPE" ||
                     action == "CLEAROSD")
            {
                CommitQueuedInput(); 
                BrowseEnd(false);
            }
            else if (action == "SELECT")
            {
                CommitQueuedInput(); 
                BrowseEnd(true);
            }
            else if (action == "TOGGLERECORD")
                ToggleRecord();
            else if (action == "VOLUMEDOWN" || action == "VOLUMEUP" ||
                     action == "STRETCHINC" || action == "STRETCHDEC" ||
                     action == "MUTE"       || action == "TOGGLEASPECT" ||
                     action == "TOGGLEFILL" )
            {
                passThru = 1;
                handled = false;
            }
            else if (action == "TOGGLEPIPWINDOW" || action == "TOGGLEPIPMODE" ||
                     action == "SWAPPIP")
            {
                passThru = 1;
                handled = false;
                redisplayBrowseInfo = true;
            }
            else
                handled = false;
        }

        if (!passThru)
            return;
    }

    if (zoomMode)
    {
        int passThru = 0;

        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "UP" || action == "CHANNELUP")
                nvp->Zoom(kZoomUp);
            else if (action == "DOWN" || action == "CHANNELDOWN")
                nvp->Zoom(kZoomDown);
            else if (action == "LEFT")
                nvp->Zoom(kZoomLeft);
            else if (action == "RIGHT")
                nvp->Zoom(kZoomRight);
            else if (action == "VOLUMEUP")
                nvp->Zoom(kZoomAspectUp);
            else if (action == "VOLUMEDOWN")
                nvp->Zoom(kZoomAspectDown);
            else if (action == "ESCAPE")
            {
                nvp->Zoom(kZoomHome);
                SetManualZoom(false);
            }
            else if (action == "SELECT")
                SetManualZoom(false);
            else if (action == "JUMPFFWD")
                nvp->Zoom(kZoomIn);
            else if (action == "JUMPRWND")
                nvp->Zoom(kZoomOut);
            else if (action == "VOLUMEDOWN" || action == "VOLUMEUP" ||
                     action == "STRETCHINC" || action == "STRETCHDEC" ||
                     action == "MUTE" || action == "PAUSE" ||
                     action == "CLEAROSD")
            {
                passThru = 1;
                handled = false;
            }
            else
                handled = false;
        }

        if (!passThru)
            return;
    }

    if (dialogname != "" && GetOSD() && GetOSD()->DialogShowing(dialogname))
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "UP")
                GetOSD()->DialogUp(dialogname); 
            else if (action == "DOWN")
                GetOSD()->DialogDown(dialogname);
            else if (((action == "RWNDSTICKY" || action == "SEEKRWND" ||
                        action == "JUMPRWND"))
                    && IsVideoExitDialog() &&
                    !has_action("UP", actions) &&
                    !has_action("DOWN", actions) &&
                    !(has_action("LEFT", actions) && arrowAccel))
            {
                DoPause(false);
                GetOSD()->TurnDialogOff(dialogname);
                dialogname = "";
                ClearInputQueues(true);
                requestDelete = false;
                jumped_back = true;
                if (action == "RWNDSTICKY")
                    ChangeFFRew(-3);
                else if (action == "JUMPRWND")
                    DoSeek(-jumptime * 60, tr("Jump Back"));
                else
                    DoSeek(-rewtime, tr("Skip Back"));
            }
            else if (action == "ESCAPE" && isnearend)
            {
                requestDelete = false;
                exitPlayer    = true;
                wantsToQuit   = true;
            }
            else if (action == "SELECT" || action == "ESCAPE" || 
                     action == "CLEAROSD" ||
                     ((arrowAccel) && (action == "LEFT" || action == "RIGHT")))
            {
                if (action == "ESCAPE" || action == "CLEAROSD" ||
                    (arrowAccel && action == "LEFT"))
                    GetOSD()->DialogAbort(dialogname);
                GetOSD()->TurnDialogOff(dialogname);
                if (dialogname == "alreadybeingedited")
                {
                    int result = GetOSD()->GetDialogResponse(dialogname);
                    if (result == 1) 
                    {
                       playbackinfo->SetEditing(false);
                       editmode = nvp->EnableEdit();
                    }
                    else
                    {
                        paused = !paused;
                        DoPause();
                    }
                }
                else if (dialogname == "exitplayoptions") 
                {
                    int result = GetOSD()->GetDialogResponse(dialogname);

                    if (result > 0)
                    {
                        if (!BookmarkAllowed())
                            result++;
                        if (result > 2 && !DeleteAllowed())
                            result++;
                    }

                    switch (result)
                    {
                        case 0: case 4:
                            DoPause();
                            break;
                        case 1:
                            nvp->SetBookmark();
                            exitPlayer = true;
                            wantsToQuit = true;
                            break;
                        case 3:
                            dialogname = "";
                            DoPause();
                            PromptDeleteRecording(
                                tr("Delete this recording?")); 
                            return;
                        default:
                            exitPlayer = true;
                            wantsToQuit = true;
                            break;
                    }
                }
                else if (dialogname == "askdeleterecording")
                {
                    int result = GetOSD()->GetDialogResponse(dialogname);

                    switch (result)
                    {
                        case 1:
                            exitPlayer = true;
                            wantsToQuit = true;
                            allowRerecord = true;
                            requestDelete = true;
                            break;
                        case 2:
                            exitPlayer = true;
                            wantsToQuit = true;
                            requestDelete = true;
                            break;
                        case 3:
                            exitPlayer = true;
                            wantsToQuit = true;
                            break;
                        default:
                            if (isnearend)
                            {
                                exitPlayer = true;
                                wantsToQuit = true;
                            }
                            else
                                DoPause();
                            break;
                    }
                }
                else if (dialogname == "allowrecordingbox")
                {
                    HandleOSDAskAllowResponse();
                }
                else if (dialogname == "idletimeout")
                {
                    int result = GetOSD()->GetDialogResponse(dialogname);

                    if (result == 1)
                        idleTimer->changeInterval(
                                   gContext->GetNumSetting("LiveTVIdleTimeout",
                                                           0) * 60 * 1000);
                }
                else if (dialogname == "channel_timed_out")
                {
                    lockTimerOn = false;
                }

                while (GetOSD()->DialogShowing(dialogname))
                {
                    usleep(1000);
                }

                dialogname = "";
            }
            else
                handled = false;
        }
        return;
    }

    if (adjustingPicture)
    {
        for (unsigned int i = 0; i < actions.size(); i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "LEFT")
                DoChangePictureAttribute(adjustingPicture,
                                         adjustingPictureAttribute, false);
            else if (action == "RIGHT")
                DoChangePictureAttribute(adjustingPicture,
                                         adjustingPictureAttribute, true);
            else
                handled = false;
        }
    }
   
    if (stretchAdjustment)
    {
        for (unsigned int i = 0; i < actions.size(); i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "LEFT")
                ChangeTimeStretch(-1);
            else if (action == "RIGHT")
                ChangeTimeStretch(1);
            else if (action == "DOWN")
                ChangeTimeStretch(-5);
            else if (action == "UP")
                ChangeTimeStretch(5);
            else if (action == "ADJUSTSTRETCH")
                ClearOSD();
            else
                handled = false;
        }
    }
   
    if (audiosyncAdjustment)
    {
        for (unsigned int i = 0; i < actions.size(); i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "LEFT")
                ChangeAudioSync(-1);
            else if (action == "RIGHT")
                ChangeAudioSync(1);
            else if (action == "UP")
                ChangeAudioSync(-10);
            else if (action == "DOWN")
                ChangeAudioSync(10);
            else if (action == "1")
                ChangeAudioSync(1000000);
            else if (action == "0")
                ChangeAudioSync(-1000000);
            else if (action == "TOGGLEAUDIOSYNC")
                ClearOSD();
            else
                handled = false;
        }
    }

#if DEBUG_ACTIONS
    for (uint i = 0; i < actions.size(); ++i)
        VERBOSE(VB_IMPORTANT, LOC + QString("handled(%1) actions[%2](%3)")
                .arg(handled).arg(i).arg(actions[i]));
#endif // DEBUG_ACTIONS

    if (handled)
        return;

    if (activerbuffer &&
        activerbuffer->isDVD())
    {
        for (unsigned int i = 0; i < actions.size(); i++)
        {
            QString action = actions[i];
            int nb_buttons = activerbuffer->DVD()->NumMenuButtons();
            if (nb_buttons > 0)
            {
                handled = true;
                if (action == "UP" || action == "CHANNELUP")
                    activerbuffer->DVD()->MoveButtonUp();
                else if (action == "DOWN" || action == "CHANNELDOWN")
                    activerbuffer->DVD()->MoveButtonDown();
                else if (action == "LEFT" || action == "SEEKRWND")
                    activerbuffer->DVD()->MoveButtonLeft();
                else if (action == "RIGHT" || action == "SEEKFFWD")
                    activerbuffer->DVD()->MoveButtonRight();
                else if (action == "SELECT")
                    activenvp->ActivateDVDButton();
                else
                    handled = false;
            }
            if (handled)
                return;
       }
    }
             
    for (unsigned int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        handled = true;

        if (action == "SKIPCOMMERCIAL" && !activerbuffer->isDVD())
            DoSkipCommercials(1);
        else if (action == "SKIPCOMMBACK" && !activerbuffer->isDVD())
            DoSkipCommercials(-1);
        else if (action == "QUEUETRANSCODE" && !activerbuffer->isDVD())
            DoQueueTranscode("Default");
        else if (action == "QUEUETRANSCODE_AUTO")
            DoQueueTranscode("Autodetect");
        else if (action == "QUEUETRANSCODE_HIGH")
            DoQueueTranscode("High Quality");
        else if (action == "QUEUETRANSCODE_MEDIUM")
            DoQueueTranscode("Medium Quality");
        else if (action == "QUEUETRANSCODE_LOW")
            DoQueueTranscode("Low Quality");
        else if (action == "PLAY")
            DoPlay();
        else if (action == "PAUSE") 
            DoPause();
        else if (action == "SPEEDINC" &&
                 activerbuffer && !activerbuffer->InDVDMenuOrStillFrame())
            ChangeSpeed(1);
        else if (action == "SPEEDDEC" &&
                 activerbuffer && !activerbuffer->InDVDMenuOrStillFrame())
            ChangeSpeed(-1);
        else if (action == "ADJUSTSTRETCH")
            ChangeTimeStretch(0);   // just display
        else if (action == "TOGGLESTRETCH")
            ToggleTimeStretch();
        else if (action == "CYCLECOMMSKIPMODE") {
            SetAutoCommercialSkip((enum commSkipMode)
                                  ((autoCommercialSkip + 1) % CommSkipModes));
        }
        else if (action == "TOGGLEAUDIOSYNC")
            ChangeAudioSync(0);   // just display
        else if (action == "TOGGLEPICCONTROLS")
        {
            DoTogglePictureAttribute(kAdjustingPicture_Playback);
        }
        else if (action == "NEXTSCAN")
        {
            activenvp->NextScanType();
            if (GetOSD())
            {
                QString msg = toString(activenvp->GetScanType());
                GetOSD()->SetSettingsText(msg, 3);
            }
        }
        else if (action == "ARBSEEK")
        {
            if (asInputMode)
            {
                asInputModeExpires.start();
                ClearInputQueues(true);
                UpdateOSDTextEntry(tr("Seek:"));
            }
            else
            {
                asInputMode        = true;
                asInputModeExpires = QTime::currentTime()
                    .addMSecs(kInputModeTimeout);
                ccInputModeExpires = QTime::currentTime();
                ClearInputQueues(false);
                AddKeyToInputQueue(0);
            }            
        }
        else if (action == "SEEKFFWD" &&
                 activerbuffer && !activerbuffer->InDVDMenuOrStillFrame())
        {
            if (HasQueuedInput())
                DoArbSeek(ARBSEEK_FORWARD);
            else if (paused)
            {
                if (!activerbuffer->isDVD())
                    DoSeek(1.001 / frameRate, tr("Forward"));
            }
            else if (!stickykeys)
            {
                if (smartForward && doSmartForward)
                    DoSeek(rewtime, tr("Skip Ahead"));
                else
                    DoSeek(fftime, tr("Skip Ahead"));
            }
            else
                ChangeFFRew(1);
        }
        else if (action == "FFWDSTICKY" &&
                 activerbuffer && !activerbuffer->InDVDMenuOrStillFrame())
        {
            if (HasQueuedInput())
                DoArbSeek(ARBSEEK_END);
            else if (paused)
            {
                if (!activerbuffer->isDVD())
                    DoSeek(1.0, tr("Forward"));
            }
            else
                ChangeFFRew(1);
        }
        else if (action == "SEEKRWND" &&
                 activerbuffer && !activerbuffer->InDVDMenuOrStillFrame())
        {
            if (HasQueuedInput())
                DoArbSeek(ARBSEEK_REWIND);
            else if (paused)
            {
                if (!activerbuffer->isDVD())
                    DoSeek(-1.001 / frameRate, tr("Rewind"));
            }
            else if (!stickykeys)
            {
                if (smartForward)
                    doSmartForward = true;
                DoSeek(-rewtime, tr("Skip Back"));
            }
            else
                ChangeFFRew(-1);
        }
        else if (action == "RWNDSTICKY" &&
                 activerbuffer && !activerbuffer->InDVDMenuOrStillFrame())
        {
            if (HasQueuedInput())
                DoArbSeek(ARBSEEK_SET);
            else if (paused)
            {
                if (!activerbuffer->isDVD())
                    DoSeek(-1.0, tr("Rewind"));
            }
            else
                ChangeFFRew(-1);
        }
        else if (action == "JUMPRWND")
        {
            if (activerbuffer->isDVD())
                DVDJumpBack();       
            else
                DoSeek(-jumptime * 60, tr("Jump Back"));
        }
        else if (action == "JUMPFFWD")
        {
            if (activerbuffer->isDVD())
                DVDJumpForward();
            else
                DoSeek(jumptime * 60, tr("Jump Ahead"));
        }
        else if (action == "JUMPBKMRK")
        {
            int bookmark = activenvp->GetBookmark();
            if (bookmark > frameRate)
                DoSeek((bookmark - activenvp->GetFramesPlayed()) / frameRate,
                       tr("Jump to Bookmark"));
        }
        else if (action == "JUMPSTART" && activenvp)
        {
            DoSeek(-activenvp->GetFramesPlayed() / frameRate,
                   tr("Jump to Beginning"));
        }
        else if (action == "CLEAROSD")
        {
            ClearOSD();
        }
        else if ((action == "JUMPPREV") ||
                 ((action == "PREVCHAN") && (!StateIsLiveTV(GetState()))))
        {
            if (PromptRecGroupPassword())
            {
                nvp->SetBookmark();
                exitPlayer = true;
                jumpToProgram = true;
            }
        }
        else if (action == "VIEWSCHEDULED")
            EmbedWithNewThread(kViewSchedule);
        else if (action == "JUMPREC")
        {
            if (gContext->GetNumSetting("JumpToProgramOSD", 1) 
                    && StateIsPlaying(internalState))
            {
                DisplayJumpMenuSoon();
            }
            else if (RunPlaybackBoxPtr)
                EmbedWithNewThread(kPlaybackBox);
        }
        else if (action == "SIGNALMON")
        {
            if ((GetState() == kState_WatchingLiveTV) && activerecorder)
            {
                QString input = activerecorder->GetInput();
                uint timeout  = activerecorder->GetSignalLockTimeout(input);

                if (timeout == 0xffffffff)
                {
                    if (GetOSD())
                        GetOSD()->SetSettingsText("No Signal Monitor", 2);
                    return;
                }

                int rate   = sigMonMode ? 0 : 100;
                int notify = sigMonMode ? 0 : 1;

                PauseLiveTV();
                activerecorder->SetSignalMonitoringRate(rate,notify);
                UnpauseLiveTV();

                lockTimerOn = false;
                sigMonMode  = !sigMonMode;
            }
        }
        else if (action == "SCREENSHOT")
        {
            if (activenvp && activerbuffer && !activerbuffer->isDVD())
                ScreenShot(activenvp->GetFramesPlayed());
        }
        else if (action == "EXITSHOWNOPROMPTS")
        {
            if (nvp)
                nvp->SetBookmark();
            requestDelete = false;
            exitPlayer = true;
            wantsToQuit = true;
        }
        else if (action == "ESCAPE")
        {
            if (StateIsLiveTV(internalState) &&
                (lastSignalMsgTime.elapsed() < kSMExitTimeout))
                ClearOSD();
            else if (GetOSD() && ClearOSD())
                return;

            NormalSpeed();
            StopFFRew();

            if (StateIsLiveTV(GetState()))
            {
                if (nvp && 12 & gContext->GetNumSetting("PlaybackExitPrompt"))
                    PromptStopWatchingRecording();
                else
                {
                    exitPlayer = true;
                    wantsToQuit = true;
                }
            }
            else 
            {
                if (nvp &&
                    (5 & gContext->GetNumSetting("PlaybackExitPrompt")) &&
                    !underNetworkControl &&
                    prbuffer && !prbuffer->InDVDMenuOrStillFrame())
                {
                    PromptStopWatchingRecording();
                    break;
                }
                else if (nvp && gContext->GetNumSetting("PlaybackExitPrompt") == 2)
                    nvp->SetBookmark();
                if (nvp && gContext->GetNumSetting("AutomaticSetWatched", 0))
                    nvp->SetWatched();
                exitPlayer = true;
                wantsToQuit = true;
                requestDelete = false;
            }
            break;
        }
        else if (action == "VOLUMEDOWN")
            ChangeVolume(false);
        else if (action == "VOLUMEUP")
            ChangeVolume(true);
        else if (action == "MUTE")
            ToggleMute();
        else if (action == "STRETCHINC")
            ChangeTimeStretch(1);
        else if (action == "STRETCHDEC")
            ChangeTimeStretch(-1);
        else if (action == "TOGGLEASPECT")
            ToggleAspectOverride();
        else if (action == "TOGGLEFILL")
            ToggleAdjustFill();
        else if (action == "MENU")
            ShowOSDTreeMenu();
        else 
            handled = HandleTrackAction(action);
    }

    if (!handled)
    {
        if (doing_ff_rew)
        {
            for (unsigned int i = 0; i < actions.size() && !handled; i++)
            {
                QString action = actions[i];
                bool ok = false;
                int val = action.toInt(&ok);

                if (ok && val < (int)ff_rew_speeds.size())
                {
                    SetFFRew(val);
                    handled = true;
                }
            }

            if (!handled)
            {
                DoNVPSeek(StopFFRew());
                UpdateOSDSeekMessage(PlayMesg(), osd_general_timeout);
                handled = true;
            }
        }

        if (speed_index)
        {
            NormalSpeed();
            UpdateOSDSeekMessage(PlayMesg(), osd_general_timeout);
            handled = true;
        }
    }

    if (!handled)
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            bool ok = false;
            int val = action.toInt(&ok);

            if (ok)
            {
                AddKeyToInputQueue('0' + val);
                handled = true;
            }
        }
    }

    if (StateIsLiveTV(GetState()) || StateIsPlaying(internalState))
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "INFO")
            {
                if (HasQueuedInput())
                    DoArbSeek(ARBSEEK_SET);
                else
                    ToggleOSD(true);
            }
            else if (action == "TOGGLESLEEP")
                ToggleSleepTimer();
            else
                handled = false;
        }
    }

    uint aindx = (activenvp == nvp) ? 0 : 1;
    if (StateIsLiveTV(GetState()))
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "TOGGLERECORD")
                ToggleRecord();
            else if (action == "TOGGLEFAV")
                ToggleChannelFavorite();
            else if (action == "SELECT")
            {
                if (!CommitQueuedInput())
                    handled = false;
            }
            else if (action == "TOGGLECHANCONTROLS")
                DoTogglePictureAttribute(kAdjustingPicture_Channel);
            else if (action == "TOGGLERECCONTROLS")
                DoTogglePictureAttribute(kAdjustingPicture_Recording);
            else if (action == "TOGGLEBROWSE" && pseudoLiveTVState[aindx])
                ShowOSDTreeMenu();
            else
                handled = false;
        }

        if (redisplayBrowseInfo)
            BrowseStart();
    }

    if ((StateIsLiveTV(GetState()) || StateIsPlaying(internalState)) &&
        (activerbuffer && !activerbuffer->InDVDMenuOrStillFrame()))
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;
    
            if (action == "SELECT")
            {
                if (!was_doing_ff_rew)
                {
                    if (prbuffer->isDVD())
                        prbuffer->DVD()->JumpToTitle(false);

                    if (gContext->GetNumSetting("AltClearSavedPosition", 1)
                        && nvp->GetBookmark())
                        nvp->ClearBookmark(); 
                    else
                        nvp->SetBookmark(); 
                }
                else
                    handled = false;
            }
            else
                handled = false;
        }
    }
    
    if (StateIsLiveTV(GetState()) && !pseudoLiveTVState[aindx])
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "NEXTFAV")
                ChangeChannel(CHANNEL_DIRECTION_FAVORITE);
            else if (action == "NEXTSOURCE")
                SwitchSource(kNextSource);
            else if (action == "PREVSOURCE")
                SwitchSource(kPreviousSource);
            else if (action == "NEXTINPUT")
                ToggleInputs();
            else if (action == "NEXTCARD")
                SwitchCards();
            else if (action == "GUIDE")
                EditSchedule(kScheduleProgramGuide);
            else if (action == "TOGGLEPIPMODE")
                TogglePIPView();
            else if (action == "TOGGLEPIPWINDOW")
                ToggleActiveWindow();
            else if (action == "SWAPPIP")
                SwapPIPSoon();
            else if (action == "TOGGLEBROWSE")
                BrowseStart();
            else if (action == "PREVCHAN")
                PreviousChannel();
            else if (action == "CHANNELUP")
            {
                if (persistentbrowsemode)
                    BrowseDispInfo(BROWSE_UP);
                else
                    ChangeChannel(CHANNEL_DIRECTION_UP);
            }
            else if (action == "CHANNELDOWN")
            {
                if (persistentbrowsemode)
                    BrowseDispInfo(BROWSE_DOWN);
                else
                    ChangeChannel(CHANNEL_DIRECTION_DOWN);
            }
            else if (action == "TOGGLEEDIT")
                StartChannelEditMode();
            else
                handled = false;
        }

        if (redisplayBrowseInfo)
            BrowseStart();
    }
    else if (StateIsPlaying(internalState))
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "DELETE" && !activerbuffer->isDVD())
            {
                NormalSpeed();
                StopFFRew();
                nvp->SetBookmark(); 
                PromptDeleteRecording(tr("Delete this recording?"));
            }
            else if (action == "JUMPTODVDROOTMENU")
                activenvp->GoToDVDMenu("menu");
            else if (action == "GUIDE")
                EditSchedule(kScheduleProgramGuide);
            else if (action == "FINDER")
                EditSchedule(kScheduleProgramFinder);
            else if (action == "TOGGLEEDIT" && !activerbuffer->isDVD())
                StartProgramEditMode();
            else if (action == "TOGGLEBROWSE")
                ShowOSDTreeMenu();
            else if (action == "CHANNELUP")
            {
                if (activerbuffer->isDVD()) 
                    DVDJumpBack();
                else
                    DoSeek(-jumptime * 60, tr("Jump Back"));
            }    
            else if (action == "CHANNELDOWN")
            {
                if (activerbuffer->isDVD())
                    DVDJumpForward();
                else
                    DoSeek(jumptime * 60, tr("Jump Ahead"));
            }
            else
                handled = false;
        }
    }
}

void TV::ProcessNetworkControlCommand(const QString &command)
{
#ifdef DEBUG_ACTIONS
    VERBOSE(VB_IMPORTANT, LOC + "ProcessNetworkControlCommand(" +
            QString("%1)").arg(command));
#endif

    QStringList tokens = QStringList::split(" ", command);
    if (tokens.size() < 2)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Not enough tokens"
                "in network control command" + "\n\t\t\t" +
                QString("'%1'").arg(command));
        return;
    }

    if (!dialogname.isEmpty())
    {
        VERBOSE(VB_IMPORTANT, LOC_WARN + "Ignoring network "
                "control command\n\t\t\t" +
                QString("because dialog '%1'").arg(dialogname) +
                "is waiting for a response");
        return;
    }

    if (tokens[1] != "QUERY")
        ClearOSD();

    if (tokens.size() == 3 && tokens[1] == "CHANID")
    {
        queuedChanID = tokens[2].toUInt();
        queuedChanNum = QString::null;
        CommitQueuedInput();
    }
    else if (tokens.size() == 3 && tokens[1] == "CHANNEL")
    {
        uint aindx = (activenvp == nvp) ? 0 : 1;
        if (StateIsLiveTV(GetState()) && !pseudoLiveTVState[aindx])
        {
            if (tokens[2] == "UP")
                ChangeChannel(CHANNEL_DIRECTION_UP);
            else if (tokens[2] == "DOWN")
                ChangeChannel(CHANNEL_DIRECTION_DOWN);
            else if (tokens[2].contains(QRegExp("^[-\\.\\d_#]+$")))
                ChangeChannel(0, tokens[2]);
        }
    }
    else if (tokens.size() == 3 && tokens[1] == "SPEED")
    {
        if (tokens[2] == "0x")
        {
            NormalSpeed();
            StopFFRew();

            if (!paused)
                DoPause();
        }
        else if (tokens[2].contains(QRegExp("^\\-*\\d+x$")))
        {
            QString speed = tokens[2].left(tokens[2].length()-1);
            bool ok = false;
            int tmpSpeed = speed.toInt(&ok);
            int searchSpeed = abs(tmpSpeed);
            unsigned int index;

            if (paused)
                DoPause();

            if (tmpSpeed == 1)
            {
                StopFFRew();
                normal_speed = 1.0f;
                ChangeTimeStretch(0, false);

                return;
            }

            NormalSpeed();

            for (index = 0; index < ff_rew_speeds.size(); index++)
                if (ff_rew_speeds[index] == searchSpeed)
                    break;

            if ((index < ff_rew_speeds.size()) &&
                (ff_rew_speeds[index] == searchSpeed))
            {
                if (tmpSpeed < 0)
                    doing_ff_rew = -1;
                else if (tmpSpeed > 1)
                    doing_ff_rew = 1;
                else
                    StopFFRew();

                if (doing_ff_rew)
                    SetFFRew(index);
            }
            else
            {
                VERBOSE(VB_IMPORTANT, QString("Couldn't find %1 speed in "
                    "FFRewSpeed settings array, changing to default speed "
                    "of 1x").arg(searchSpeed));

                doing_ff_rew = 0;
                SetFFRew(kInitFFRWSpeed);
            }
        }
        else if (tokens[2].contains(QRegExp("^\\d*\\.\\d+x$")))
        {
            QString tmpSpeed = tokens[2].left(tokens[2].length() - 1);

            if (paused)
                DoPause();

            StopFFRew();

            bool floatRead;
            float stretch = tmpSpeed.toFloat(&floatRead);
            if (floatRead &&
                stretch <= 2.0 &&
                stretch >= 0.48)
            {
                normal_speed = stretch;   // alter speed before display
                ChangeTimeStretch(0, false);
            }
        }
        else if (tokens[2].contains(QRegExp("^\\d+\\/\\d+x$")))
        {
            if (activerbuffer && activerbuffer->InDVDMenuOrStillFrame())
                return;

            if (paused)
                DoPause();

            if (tokens[2] == "16x")
                ChangeSpeed(5 - speed_index);
            else if (tokens[2] == "8x")
                ChangeSpeed(4 - speed_index);
            else if (tokens[2] == "4x")
                ChangeSpeed(3 - speed_index);
            else if (tokens[2] == "3x")
                ChangeSpeed(2 - speed_index);
            else if (tokens[2] == "2x")
                ChangeSpeed(1 - speed_index);
            else if (tokens[2] == "1x")
                ChangeSpeed(0 - speed_index);
            else if (tokens[2] == "1/2x")
                ChangeSpeed(-1 - speed_index);
            else if (tokens[2] == "1/3x")
                ChangeSpeed(-2 - speed_index);
            else if (tokens[2] == "1/4x")
                ChangeSpeed(-3 - speed_index);
            else if (tokens[2] == "1/8x")
                ChangeSpeed(-4 - speed_index);
            else if (tokens[2] == "1/16x")
                ChangeSpeed(-5 - speed_index);
        }
        else
            VERBOSE(VB_IMPORTANT,
                QString("Found an unknown speed of %1").arg(tokens[2]));
    }
    else if (tokens.size() == 2 && tokens[1] == "STOP")
    {
        if (nvp)
            nvp->SetBookmark();
        if (nvp && gContext->GetNumSetting("AutomaticSetWatched", 0))
            nvp->SetWatched();
        exitPlayer = true;
        wantsToQuit = true;
    }
    else if (tokens.size() >= 3 && tokens[1] == "SEEK" && activenvp)
    {
        if (activerbuffer && activerbuffer->InDVDMenuOrStillFrame())
            return;

        if (tokens[2] == "BEGINNING")
            DoSeek(-activenvp->GetFramesPlayed(), tr("Jump to Beginning"));
        else if (tokens[2] == "FORWARD")
            DoSeek(fftime, tr("Skip Ahead"));
        else if (tokens[2] == "BACKWARD")
            DoSeek(-rewtime, tr("Skip Back"));
        else if ((tokens[2] == "POSITION") && (tokens.size() == 4) &&
                 (tokens[3].contains(QRegExp("^\\d+$"))))
            DoSeek(tokens[3].toInt() -
                   (activenvp->GetFramesPlayed() / frameRate), tr("Jump To"));
    }
    else if (tokens.size() >= 3 && tokens[1] == "QUERY" && activenvp)
    {
        if (tokens[2] == "POSITION")
        {
            QString speedStr;

            switch (speed_index)
            {
                case  4: speedStr = "16x"; break;
                case  3: speedStr = "8x";  break;
                case  2: speedStr = "3x";  break;
                case  1: speedStr = "2x";  break;
                case  0: speedStr = "1x";  break;
                case -1: speedStr = "1/3x"; break;
                case -2: speedStr = "1/8x"; break;
                case -3: speedStr = "1/16x"; break;
                case -4: speedStr = "0x"; break;
                default: speedStr = "1x"; break;
            }

            if (doing_ff_rew == -1)
                speedStr = QString("-%1").arg(speedStr);
            else if (normal_speed != 1.0)
                speedStr = QString("%1X").arg(normal_speed);

            struct StatusPosInfo posInfo;
            nvp->calcSliderPos(posInfo, true);

            QDateTime respDate = mythCurrentDateTime();
            QString infoStr = "";

            pbinfoLock.lock();
            if (internalState == kState_WatchingLiveTV)
            {
                infoStr = "LiveTV";
                if (playbackinfo)
                    respDate = playbackinfo->startts;
            }
            else
            {

		if (activerbuffer->isDVD())
                    infoStr = "DVD";
                else if (playbackinfo->isVideo)
                    infoStr = "Video";
                else
                    infoStr = "Recorded";

                if (playbackinfo)
                    respDate = playbackinfo->recstartts;
            }

	    if ((infoStr == "Recorded") || (infoStr == "LiveTV"))
            {
                infoStr += QString(" %1 %2 %3 %4 %5 %6 %7")
		            .arg(posInfo.desc)
    		            .arg(speedStr)
                            .arg(playbackinfo != NULL ? playbackinfo->chanid : "-1")
                            .arg(respDate.toString(Qt::ISODate))
                            .arg((long)nvp->GetFramesPlayed())
			    .arg(activerbuffer->GetFilename())
			    .arg(frameRate);
	    }
	    else
            {
                QString position = posInfo.desc.section(" ",0,0);
                infoStr += QString(" %1 %2 %3 %4 %5")
                            .arg(position)
                            .arg(speedStr)
                            .arg(activerbuffer->GetFilename())
                            .arg((long)nvp->GetFramesPlayed())
                            .arg(frameRate);

            }

            pbinfoLock.unlock();

            QString message = QString("NETWORK_CONTROL ANSWER %1")
                                      .arg(infoStr);
            MythEvent me(message);
            gContext->dispatch(me);
        }
    }
}

void TV::TogglePIPView(void)
{
    if (!pipnvp)
    {
        RemoteEncoder *testrec = RemoteRequestRecorder();
        
        if (!testrec || !testrec->IsValidRecorder())
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "PiP failed to locate recorder");
            if (testrec)
                delete testrec;
            return;
        }

        testrec->Setup();

        piptvchain = new LiveTVChain();
        piptvchain->InitializeNewChain("PIP"+gContext->GetHostName());
        testrec->SpawnLiveTV(piptvchain->GetID(), true, "");
        piptvchain->ReloadAll();
        ProgramInfo *tmppginfo = piptvchain->GetProgramAt(-1);
        if (!tmppginfo)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "PiP not successfully started");
            delete testrec;
            piptvchain->DestroyChain();
            delete piptvchain;
            piptvchain = NULL;
        }
        else
        {
            QString playbackURL = tmppginfo->GetPlaybackURL();

            piptvchain->SetProgram(tmppginfo);
            piprbuffer = new RingBuffer(playbackURL, false);
            piprbuffer->SetLiveMode(piptvchain);
            delete tmppginfo;
        }

        piprecorder = testrec;

        if (StartRecorder(piprecorder, -1))
        {
            SetupPipPlayer();
            VERBOSE(VB_PLAYBACK, "PiP Waiting for NVP");
            pthread_create(&pipdecode, NULL, SpawnDecode, pipnvp);
            while (!pipnvp->IsPlaying() && pipnvp->IsDecoderThreadAlive())
            {
                piptvchain->ReloadAll();
                usleep(5000);
            }
            VERBOSE(VB_PLAYBACK, "PiP NVP Started");

            if (pipnvp->IsDecoderThreadAlive())
                nvp->SetPipPlayer(pipnvp);
            else
            {
                VERBOSE(VB_IMPORTANT, LOC_ERR + "PiP player failed to start");
                osdlock.lock();
                delete pipnvp;
                pipnvp = NULL;
                osdlock.unlock();
                TeardownPipPlayer();
            }
        }
        else
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "PiP recorder failed to start");
            TeardownPipPlayer();
        }
    }
    else
    {
        if (activenvp != nvp)
            ToggleActiveWindow();

        nvp->SetPipPlayer(NULL);
        while (!nvp->PipPlayerSet())
            usleep(50);

        piprbuffer->StopReads();
        piprbuffer->Pause();
        while (!piprbuffer->isPaused())
            usleep(50);

        pipnvp->StopPlaying();

        piprecorder->StopLiveTV();

        TeardownPipPlayer();

        SetPseudoLiveTV(1, NULL, kPseudoNormalLiveTV);
    }
}

void TV::ToggleActiveWindow(void)
{
    if (!pipnvp)
        return;

    lockTimerOn = false;
    if (activenvp == nvp)
    {
        activenvp = pipnvp;
        activerbuffer = piprbuffer;
        activerecorder = piprecorder;
    }
    else
    {
        activenvp = nvp;
        activerbuffer = prbuffer;
        activerecorder = recorder;
    }
    LiveTVChain *chain = (activenvp == nvp) ? tvchain : piptvchain;
    ProgramInfo *pginfo = chain->GetProgramAt(-1);
    if (pginfo)
    {
        SetCurrentlyPlaying(pginfo);
        delete pginfo;
    }
}

struct pip_info
{
    RingBuffer    *buffer;
    RemoteEncoder *recorder;
    LiveTVChain   *chain;
    long long      frame;
};

void TV::SwapPIP(void)
{
    if (!pipnvp || !piptvchain || !tvchain) 
        return;

    bool muted = false;

    // save the mute state so we can restore it later
    if (nvp)
    {
        AudioOutput *aud = nvp->getAudioOutput();
        if (aud)
        {
            muted = aud->GetMute();

            if (!muted)
                aud->ToggleMute();
        }
    }

    lockTimerOn = false;

    struct pip_info main, pip;
    main.buffer   = prbuffer;
    main.recorder = recorder;
    main.chain    = tvchain;
    main.frame    = nvp->GetFramesPlayed();
    pip.buffer    = piprbuffer;
    pip.recorder  = piprecorder;
    pip.chain     = piptvchain;
    pip.frame     = pipnvp->GetFramesPlayed();

    prbuffer->Pause();
    prbuffer->WaitForPause();

    piprbuffer->Pause();
    piprbuffer->WaitForPause();

    nvp->StopPlaying();
    pipnvp->StopPlaying();
    {
        QMutexLocker locker(&osdlock); // prevent UpdateOSDSignal using osd...
        pthread_join(decode, NULL);
        delete nvp;
        nvp = NULL;
        pthread_join(pipdecode, NULL);
        delete pipnvp;
        pipnvp = NULL;
    }

    activerbuffer  = prbuffer = pip.buffer;
    activerecorder = recorder = pip.recorder;
    tvchain                   = pip.chain;

    piprbuffer  = main.buffer;
    piprecorder = main.recorder;
    piptvchain  = main.chain;

    prbuffer->Seek(0, SEEK_SET);
    prbuffer->Unpause();
    StartPlayer(false);
    activenvp = nvp;
    nvp->FastForward(pip.frame/recorder->GetFrameRate());

    // if we were muted before swapping PIP we need to restore it here
    if (muted && nvp)
    {
        AudioOutput *aud = nvp->getAudioOutput();

        if (aud && !aud->GetMute())
            aud->ToggleMute();
    }

    piprbuffer->Seek(0, SEEK_SET);
    piprbuffer->Unpause();
    SetupPipPlayer();
    VERBOSE(VB_PLAYBACK, "PiP Waiting for NVP -- restart");
    pthread_create(&pipdecode, NULL, SpawnDecode, pipnvp);
    while (!pipnvp->IsPlaying() && pipnvp->IsDecoderThreadAlive())
    {
        piptvchain->ReloadAll();
        usleep(5000);
    }
    VERBOSE(VB_PLAYBACK, "PiP NVP Started -- restart");
    pipnvp->FastForward(main.frame/piprecorder->GetFrameRate());

    if (pipnvp->IsDecoderThreadAlive())
        nvp->SetPipPlayer(pipnvp);
    else
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "PiP player failed to start");
        TeardownPipPlayer();
    }

    ProgramInfo *pginfo = tvchain->GetProgramAt(-1);
    if (pginfo)
    {
        SetCurrentlyPlaying(pginfo);
        delete pginfo;
    }
}

void TV::DoPlay(void)
{
    if (!activenvp)
        return; // TODO remove me as part of next mythtv-vid merge 1/08

    float time = 0.0;

    if (doing_ff_rew)
    {
        time = StopFFRew();
        activenvp->Play(normal_speed, true);
        speed_index = 0;
    }
    else if (paused || (speed_index != 0))
    {
        activenvp->Play(normal_speed, true);
        paused = false;
        speed_index = 0;
    }


    if (activenvp != nvp)
        return;

    DoNVPSeek(time);
    UpdateOSDSeekMessage(PlayMesg(), osd_general_timeout);

    gContext->DisableScreensaver();
}

QString TV::PlayMesg()
{
    QString mesg = QString(tr("Play"));
    if (normal_speed != 1.0)
    {
        mesg += " %1X";
        mesg = mesg.arg(normal_speed);
    }

    if (0)
    {
        FrameScanType scan = activenvp->GetScanType();
        if (is_progressive(scan) || is_interlaced(scan))
            mesg += " (" + toString(scan, true) + ")";
    }

    return mesg;
}

void TV::DoPause(bool showOSD)
{
    if (!activenvp)
        return; // TODO remove me as part of next mythtv-vid merge 1/08

    if (activerbuffer && 
        activerbuffer->InDVDMenuOrStillFrame())
    {
        return;
    }

    speed_index = 0;
    float time = 0.0;

    if (paused)
    {
        activenvp->Play(normal_speed, true);
    }
    else 
    {
        if (doing_ff_rew)
        {
            time = StopFFRew();
            activenvp->Play(normal_speed, true);
            usleep(1000);
        }
        
        activenvp->Pause();
    }

    paused = !paused;

    if (activenvp != nvp)
        return;

    if (paused)
    {
        activerbuffer->WaitForPause();
        DoNVPSeek(time);
        if (showOSD)
            UpdateOSDSeekMessage(tr("Paused"), -1);
        gContext->RestoreScreensaver();
    }
    else
    {
        DoNVPSeek(time);
        if (showOSD)
            UpdateOSDSeekMessage(PlayMesg(), osd_general_timeout);
        gContext->DisableScreensaver();
    }
}

bool TV::DoNVPSeek(float time)
{
    if (time > -0.001f && time < +0.001f)
        return false;

    bool muted = false;

    AudioOutput *aud = nvp->getAudioOutput(); 
    if (aud && !aud->GetMute())
    {
        aud->ToggleMute();
        muted = true;
    }

    bool res = false;

    if (LONG_LONG_MIN != audiosyncBaseline)
        activenvp->SaveAudioTimecodeOffset(activenvp->GetAudioTimecodeOffset()
            - audiosyncBaseline);

    if (time > 0.0)
        res = activenvp->FastForward(time);
    else if (time < 0.0)
        res = activenvp->Rewind(-time);

    if (muted) 
        SetMuteTimer(kMuteTimeout);

    return res;
}

void TV::DoSeek(float time, const QString &mesg)
{
    if (!keyRepeat)
        return;

    NormalSpeed();
    time += StopFFRew();
    DoNVPSeek(time);
    UpdateOSDSeekMessage(mesg, osd_general_timeout);

    if (activenvp->GetLimitKeyRepeat())
    {
        keyRepeat = false;
        keyrepeatTimer->start(300, true);
    }
}

void TV::DoArbSeek(ArbSeekWhence whence)
{
    bool ok = false;
    int seek = GetQueuedInputAsInt(&ok);
    ClearInputQueues(true);
    if (!ok)
        return;

    float time = ((seek / 100) * 3600) + ((seek % 100) * 60);

    if (whence == ARBSEEK_FORWARD)
        DoSeek(time, tr("Jump Ahead"));
    else if (whence == ARBSEEK_REWIND)
        DoSeek(-time, tr("Jump Back"));
    else
    {
        if (whence == ARBSEEK_END)
            time = (activenvp->CalcMaxFFTime(LONG_MAX, false) / frameRate) - time;
        else
            time = time - (activenvp->GetFramesPlayed() - 1) / frameRate;
        DoSeek(time, tr("Jump To"));
    }
}

void TV::NormalSpeed(void)
{
    if (!speed_index)
        return;

    speed_index = 0;
    activenvp->Play(normal_speed, true);
}

void TV::ChangeSpeed(int direction)
{
    int old_speed = speed_index;

    if (paused)
        speed_index = -4;

    speed_index += direction;

    float time = StopFFRew();
    float speed;
    QString mesg;

    switch (speed_index)
    {
        case  4: speed = 16.0;     mesg = QString(tr("Speed 16X"));    break;
        case  3: speed = 8.0;      mesg = QString(tr("Speed 8X"));    break;
        case  2: speed = 3.0;      mesg = QString(tr("Speed 3X"));    break;
        case  1: speed = 2.0;      mesg = QString(tr("Speed 2X"));    break;
        case  0: speed = 1.0;      mesg = PlayMesg();                 break;
        case -1: speed = 1.0 / 3;  mesg = QString(tr("Speed 1/3X"));  break;
        case -2: speed = 1.0 / 8;  mesg = QString(tr("Speed 1/8X"));  break;
        case -3: speed = 1.0 / 16; mesg = QString(tr("Speed 1/16X")); break;
        case -4: DoPause(); return; break;
        default: speed_index = old_speed; return; break;
    }

    if (!activenvp->Play((speed_index==0)?normal_speed:speed, speed_index==0))
    {
        speed_index = old_speed;
        return;
    }

    paused = false;
    DoNVPSeek(time);
    UpdateOSDSeekMessage(mesg, osd_general_timeout);
}

float TV::StopFFRew(void)
{
    float time = 0.0;

    if (!doing_ff_rew)
        return time;

    if (doing_ff_rew > 0)
        time = -ff_rew_speeds[ff_rew_index] * ff_rew_repos;
    else
        time = ff_rew_speeds[ff_rew_index] * ff_rew_repos;

    doing_ff_rew = 0;
    ff_rew_index = kInitFFRWSpeed;

    activenvp->Play(normal_speed, true);

    return time;
}

void TV::ChangeFFRew(int direction)
{
    if (doing_ff_rew == direction)
    {
        while (++ff_rew_index < (int)ff_rew_speeds.size())
            if (ff_rew_speeds[ff_rew_index])
                break;
        if (ff_rew_index >= (int)ff_rew_speeds.size())
            ff_rew_index = kInitFFRWSpeed;
        SetFFRew(ff_rew_index);
    }
    else if (!ff_rew_reverse && doing_ff_rew == -direction)
    {
        while (--ff_rew_index >= kInitFFRWSpeed)
            if (ff_rew_speeds[ff_rew_index])
                break;
        if (ff_rew_index >= kInitFFRWSpeed)
            SetFFRew(ff_rew_index);
        else
        {
            float time = StopFFRew();
            DoNVPSeek(time);
            UpdateOSDSeekMessage(PlayMesg(), osd_general_timeout);
        }
    }
    else
    {
        NormalSpeed();
        paused = false;
        doing_ff_rew = direction;
        SetFFRew(kInitFFRWSpeed);
    }
}

void TV::SetFFRew(int index)
{
    if (!doing_ff_rew)
        return;

    if (!ff_rew_speeds[index])
        return;

    ff_rew_index = index;
    int speed;

    QString mesg;
    if (doing_ff_rew > 0)
    {
        mesg = tr("Forward %1X").arg(ff_rew_speeds[ff_rew_index]);
        speed = ff_rew_speeds[ff_rew_index];
    }
    else
    {
        mesg = tr("Rewind %1X").arg(ff_rew_speeds[ff_rew_index]);
        speed = -ff_rew_speeds[ff_rew_index];
    }

    activenvp->Play((float)speed, (speed == 1) && (doing_ff_rew > 0));
    UpdateOSDSeekMessage(mesg, -1);
}

void TV::DoQueueTranscode(QString profile)
{
    QMutexLocker lock(&pbinfoLock);

    if (internalState == kState_WatchingPreRecorded)
    {
        bool stop = false;
        if (queuedTranscode)
            stop = true;
        else if (JobQueue::IsJobQueuedOrRunning(JOB_TRANSCODE,
                                                playbackinfo->chanid,
                                                playbackinfo->recstartts))
        {
            stop = true;
        }

        if (stop)
        {
            JobQueue::ChangeJobCmds(JOB_TRANSCODE,
                                    playbackinfo->chanid,
                                    playbackinfo->recstartts, JOB_STOP);
            queuedTranscode = false;
            if (activenvp == nvp && GetOSD())
                GetOSD()->SetSettingsText(tr("Stopping Transcode"), 3);
        }
        else
        {
            playbackinfo->ApplyTranscoderProfileChange(profile);
            QString jobHost = "";

            if (gContext->GetNumSetting("JobsRunOnRecordHost", 0))
                jobHost = playbackinfo->hostname;

            if (JobQueue::QueueJob(JOB_TRANSCODE,
                               playbackinfo->chanid, playbackinfo->recstartts,
                               jobHost, "", "", JOB_USE_CUTLIST))
            {
                queuedTranscode = true;
                if (activenvp == nvp && GetOSD())
                    GetOSD()->SetSettingsText(tr("Transcoding"), 3);
            } else {
                if (activenvp == nvp && GetOSD())
                    GetOSD()->SetSettingsText(tr("Try Again"), 3);
            }
        }
    }
}

void TV::DoSkipCommercials(int direction)
{
    NormalSpeed();
    StopFFRew();
    
    if (StateIsLiveTV(GetState()))
        return;
        
    bool muted = false;

    AudioOutput *aud = nvp->getAudioOutput();
    if (aud && !aud->GetMute())
    {
        aud->ToggleMute();
        muted = true;
    }

    bool slidertype = false;

    if (activenvp == nvp && GetOSD())
    {
        struct StatusPosInfo posInfo;
        nvp->calcSliderPos(posInfo);
        posInfo.desc = tr("Searching...");
        GetOSD()->ShowStatus(posInfo, slidertype, tr("Skip"), 6);
        update_osd_pos = true;
    }

    if (activenvp)
        activenvp->SkipCommercials(direction);

    if (muted) 
        SetMuteTimer(kMuteTimeout);
}

void TV::SwitchSource(uint source_direction)
{
    QMap<uint,InputInfo> sources;
    vector<uint> cardids = RemoteRequestFreeRecorderList();
    uint         cardid  = activerecorder->GetRecorderNumber();
    cardids.push_back(cardid);
    stable_sort(cardids.begin(), cardids.end());

    vector<uint> excluded_cardids;
    excluded_cardids.push_back(cardid);

    InfoMap info;
    activerecorder->GetChannelInfo(info);
    uint sourceid = info["sourceid"].toUInt();

    vector<uint>::const_iterator it = cardids.begin();
    for (; it != cardids.end(); ++it)
    {
        vector<InputInfo> inputs = RemoteRequestFreeInputList(
            *it, excluded_cardids);

        if (inputs.empty())
            continue;

        for (uint i = 0; i < inputs.size(); i++)
        {
            // prefer the current card's input in sources list
            if ((sources.find(inputs[i].sourceid) == sources.end()) ||
                ((cardid == inputs[i].cardid) && 
                 (cardid != sources[inputs[i].sourceid].cardid)))
            {
                sources[inputs[i].sourceid] = inputs[i];
            }
        }
    }

    // Source switching
    QMap<uint,InputInfo>::const_iterator beg = sources.find(sourceid);
    QMap<uint,InputInfo>::const_iterator sit = beg;

    if (sit == sources.end())
        return;

    if (kNextSource == source_direction)
    {
        sit++;
        if (sit == sources.end())
            sit = sources.begin();
    }

    if (kPreviousSource == source_direction)
    {
        if (sit != sources.begin())
            sit--;
        else
        {
            QMap<uint,InputInfo>::const_iterator tmp = sources.begin();
            while (tmp != sources.end())
            {
                sit = tmp;
                tmp++;
            }
        }
    }

    if (sit == beg)
        return;

    switchToInputId = (*sit).inputid;
}

void TV::SwitchInputs(uint inputid)
{
    VERBOSE(VB_PLAYBACK, LOC + QString("SwitchInputd(%1)").arg(inputid));

    if ((uint)activerecorder->GetRecorderNumber() == 
        CardUtil::GetCardID(inputid))
    {
        ToggleInputs(inputid);
    }
    else
    {
        SwitchCards(0, QString::null, inputid);
    }
}

void TV::SwitchCards(uint chanid, QString channum, uint inputid)
{
    VERBOSE(VB_PLAYBACK, LOC +
            QString("SwitchCards(%1,'%2',%3)")
            .arg(chanid).arg(channum).arg(inputid));

    RemoteEncoder *testrec = NULL;

    if (!StateIsLiveTV(GetState()) || (activenvp != nvp) || pipnvp)
        return;

    // If we are switching to a channel not on the current recorder
    // we need to find the next free recorder with that channel.
    QStringList reclist;
    if (!channum.isEmpty())
        reclist = GetValidRecorderList(chanid, channum);
    else if (inputid)
    {
        uint cardid = CardUtil::GetCardID(inputid);
        if (cardid)
            reclist.push_back(QString::number(cardid));
    }

    if (!reclist.empty())
        testrec = RemoteRequestFreeRecorderFromList(reclist);

    // If we are just switching recorders find first available recorder.
    if (!testrec)
        testrec = RemoteRequestNextFreeRecorder(recorder->GetRecorderNumber());

    if (testrec && testrec->IsValidRecorder())
    {
        // pause the decoder first, so we're not reading to close to the end.
        prbuffer->IgnoreLiveEOF(true); 
        prbuffer->StopReads(); 
        nvp->PauseDecoder(); 

        // shutdown stuff
        prbuffer->Pause();
        prbuffer->WaitForPause();
        nvp->StopPlaying();
        recorder->StopLiveTV();
        {
            QMutexLocker locker(&osdlock); // prevent UpdateOSDSignal using osd
            pthread_join(decode, NULL);
            delete nvp;
            activenvp = nvp = NULL;
        }

        delete recorder;
        activerecorder = recorder = NULL;

        delete prbuffer;
        activerbuffer = prbuffer = NULL;

        if (playbackinfo)
        {
            delete playbackinfo;
            playbackinfo = NULL;
        }

        // now restart stuff
        lastSignalUIInfo.clear();
        lockTimerOn = false;

        activerecorder = recorder = testrec;
        recorder->Setup();
        recorder->SpawnLiveTV(tvchain->GetID(), false, channum);
        tvchain->ReloadAll();
        playbackinfo = tvchain->GetProgramAt(-1);

        if (!playbackinfo)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    "LiveTV not successfully restarted");
            gContext->RestoreScreensaver();
            DeleteRecorder();

            exitPlayer = true;
        }
        else
        {
            QString playbackURL = playbackinfo->GetPlaybackURL();

            tvchain->SetProgram(playbackinfo);
            prbuffer = new RingBuffer(playbackURL, false);
            prbuffer->SetLiveMode(tvchain);
        }

        bool ok = false;
        if (playbackinfo && StartRecorder(recorder,-1))
        {
            if (StartPlayer(false))
                ok = true;
            else
                StopStuff(true, true, true);
        }

        if (!ok)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    "LiveTV not successfully started");
            gContext->RestoreScreensaver();
            DeleteRecorder();

            exitPlayer = true;
        }
        else
        {
            activenvp = nvp;
            activerbuffer = prbuffer;
            lockTimer.start();
            lockTimerOn = true;
        }
    }
    else
    {
        VERBOSE(VB_GENERAL, LOC + "No recorder to switch to...");
        delete testrec;
    }

    // If activenvp is main nvp, show input in on-screen-display
    if (nvp && activenvp == nvp)
    {
        UpdateOSDInput();
        UnpauseLiveTV();
    }
    ITVRestart(true);
}

void TV::ToggleInputs(uint inputid)
{
    // If main Nuppel Video Player is paused, unpause it
    if (activenvp == nvp && paused)
    {
        if (GetOSD())
            GetOSD()->EndStatus();
        gContext->DisableScreensaver();
        paused = false;
    }

    const QString curinputname = activerecorder->GetInput();
    QString inputname = curinputname;

    uint cardid = activerecorder->GetRecorderNumber();
    vector<uint> excluded_cardids;
    excluded_cardids.push_back(cardid);
    vector<InputInfo> inputs = RemoteRequestFreeInputList(
        cardid, excluded_cardids);

    vector<InputInfo>::const_iterator it = inputs.end();

    if (inputid)
    {
        it = find(inputs.begin(), inputs.end(), inputid);
    }
    else
    {
        it = find(inputs.begin(), inputs.end(), inputname);
        if (it != inputs.end())
            it++;
    }

    if (it == inputs.end())
        it = inputs.begin();

    if (it != inputs.end())
        inputname = (*it).name;

    if (curinputname != inputname)
    {
        // Pause the backend recorder, send command, and then unpause..
        PauseLiveTV();
        lockTimerOn = false;
        inputname = activerecorder->SetInput(inputname);
        UnpauseLiveTV();
    }

    // If activenvp is main nvp, show new input in on screen display
    if (nvp && activenvp == nvp)
        UpdateOSDInput(inputname);
}

void TV::ToggleChannelFavorite(void)
{
    activerecorder->ToggleChannelFavorite();
}

void TV::ChangeChannel(int direction)
{
    bool muted = false;

    if (nvp)
    {
        AudioOutput *aud = nvp->getAudioOutput();
        if (aud && !aud->GetMute() && activenvp == nvp)
        {
            aud->ToggleMute();
            muted = true;
        }
    }

    if (nvp && (activenvp == nvp) && paused)
    {
        if (GetOSD())
            GetOSD()->EndStatus();
        gContext->DisableScreensaver();
        paused = false;
    }

    // Save the current channel if this is the first time
    if (nvp && (activenvp == nvp) && prevChan.size() == 0)
        AddPreviousChannel();

    PauseLiveTV();

    if (activenvp)
    {
        activenvp->ResetCaptions();
        activenvp->ResetTeletext();
    }

    activerecorder->ChangeChannel(direction);
    ClearInputQueues(false);

    if (muted)
        SetMuteTimer(kMuteTimeout * 2);

    UnpauseLiveTV();
}

QString TV::GetQueuedInput(void) const
{
    QMutexLocker locker(&queuedInputLock);
    return QDeepCopy<QString>(queuedInput);
}

int TV::GetQueuedInputAsInt(bool *ok, int base) const
{
    QMutexLocker locker(&queuedInputLock);
    return queuedInput.toInt(ok, base);
}

QString TV::GetQueuedChanNum(void) const
{
    QMutexLocker locker(&queuedInputLock);

    if (queuedChanNum.isEmpty())
        return "";

    // strip initial zeros and other undesirable characters
    uint i = 0;
    for (; i < queuedChanNum.length(); i++)
    {
        if ((queuedChanNum[i] > '0') && (queuedChanNum[i] <= '9'))
            break;
    }
    queuedChanNum = queuedChanNum.right(queuedChanNum.length() - i);

    // strip whitespace at end of string
    queuedChanNum.stripWhiteSpace();

    return QDeepCopy<QString>(queuedChanNum);
}

/** \fn TV::ClearInputQueues(bool)
 *  \brief Clear channel key buffer of input keys.
 *  \param hideosd if true, hides "channel_number" OSDSet.
 */
void TV::ClearInputQueues(bool hideosd)
{
    if (hideosd && GetOSD()) 
        GetOSD()->HideSet("channel_number");

    QMutexLocker locker(&queuedInputLock);
    queuedInput   = "";
    queuedChanNum = "";
    queuedChanID  = 0;
}

void TV::AddKeyToInputQueue(char key)
{
    if (key)
    {
        QMutexLocker locker(&queuedInputLock);
        queuedInput   = queuedInput.append(key).right(kInputKeysMax);
        queuedChanNum = queuedChanNum.append(key).right(kInputKeysMax);
    }

    bool commitSmart = false;
    QString inputStr = GetQueuedInput();

    // Always use smartChannelChange when channel numbers are entered
    // in browse mode because in browse mode space/enter exit browse
    // mode and change to the currently browsed channel.
    if (StateIsLiveTV(GetState()) && !ccInputMode && !asInputMode &&
        (smartChannelChange || browsemode))
    {
        commitSmart = ProcessSmartChannel(inputStr);
    }
 
    // Handle OSD...
    inputStr = inputStr.isEmpty() ? "?" : inputStr;
    if (ccInputMode)
    {
        QString entryStr = (vbimode==VBIMode::PAL_TT) ? tr("TXT:") : tr("CC:");
        inputStr = entryStr + " " + inputStr;
    }
    else if (asInputMode)
        inputStr = tr("Seek:", "seek to location") + " " + inputStr;
    UpdateOSDTextEntry(inputStr);

    // Commit the channel if it is complete and smart changing is enabled.
    if (commitSmart)
        CommitQueuedInput();
}

static QString add_spacer(const QString &chan, const QString &spacer)
{
    if ((chan.length() >= 2) && !spacer.isEmpty())
        return chan.left(chan.length()-1) + spacer + chan.right(1);
    return chan;
}

bool TV::ProcessSmartChannel(QString &inputStr)
{
    QString chan = GetQueuedChanNum();

    if (chan.isEmpty())
        return false;

    // Check for and remove duplicate separator characters
    if ((chan.length() > 2) && (chan.right(1) == chan.right(2).left(1)))
    {
        bool ok;
        chan.right(1).toUInt(&ok);
        if (!ok)
        {
            chan = chan.left(chan.length()-1);

            QMutexLocker locker(&queuedInputLock);
            queuedChanNum = QDeepCopy<QString>(chan);
        }
    }

    // Look for channel in line-up
    QString needed_spacer;
    uint    pref_cardid;
    bool    is_not_complete;

    bool valid_prefix = activerecorder->CheckChannelPrefix(
        chan, pref_cardid, is_not_complete, needed_spacer);

#if DEBUG_CHANNEL_PREFIX
    VERBOSE(VB_IMPORTANT, QString("valid_pref(%1) cardid(%2) chan(%3) "
                                  "pref_cardid(%4) complete(%5) sp(%6)")
            .arg(valid_prefix).arg(0).arg(chan)
            .arg(pref_cardid).arg(is_not_complete).arg(needed_spacer));
#endif

    if (!valid_prefix)
    {
        // not a valid prefix.. reset...
        QMutexLocker locker(&queuedInputLock);
        queuedChanNum = "";
    }
    else if (!needed_spacer.isEmpty())
    {
        // need a spacer..
        QMutexLocker locker(&queuedInputLock);
        queuedChanNum = add_spacer(chan, needed_spacer);
    }

#if DEBUG_CHANNEL_PREFIX
    VERBOSE(VB_IMPORTANT, QString(" ValidPref(%1) CardId(%2) Chan(%3) "
                                  " PrefCardId(%4) Complete(%5) Sp(%6)")
            .arg(valid_prefix).arg(0).arg(GetQueuedChanNum())
            .arg(pref_cardid).arg(is_not_complete).arg(needed_spacer));
#endif

    QMutexLocker locker(&queuedInputLock);
    inputStr = QDeepCopy<QString>(queuedChanNum);

    return !is_not_complete;
}

void TV::UpdateOSDTextEntry(const QString &message)
{
    if (!GetOSD())
        return;

    InfoMap infoMap;

    infoMap["channum"]  = message;
    infoMap["callsign"] = "";

    GetOSD()->ClearAllText("channel_number");
    GetOSD()->SetText("channel_number", infoMap, 2);
}

bool TV::CommitQueuedInput(void)
{
    bool commited = false;

    VERBOSE(VB_PLAYBACK, LOC + "CommitQueuedInput() " + 
            QString("livetv(%1) qchannum(%2) qchanid(%3)")
            .arg(StateIsLiveTV(GetState()))
            .arg(GetQueuedChanNum())
            .arg(GetQueuedChanID()));

    if (ccInputMode)
    {
        commited = true;
        if (HasQueuedInput())
            HandleTrackAction("TOGGLECC");
    }
    else if (asInputMode)
    {
        commited = true;
        if (HasQueuedInput())
            DoArbSeek(ARBSEEK_FORWARD);
    }
    else if (StateIsLiveTV(GetState()) &&
             !pseudoLiveTVState[(activenvp == nvp) ? 0 : 1])
    {
        QString channum = GetQueuedChanNum();
        QString chaninput = GetQueuedInput();
        if (browsemode)
        {
            commited = true;
            BrowseChannel(channum);
            if (activenvp == nvp && GetOSD())
                GetOSD()->HideSet("channel_number");
        }
        else if (GetQueuedChanID() || !channum.isEmpty())
        {
            commited = true;
            ChangeChannel(GetQueuedChanID(), channum);
        }
    }

    ClearInputQueues(true);
    return commited;
}

void TV::ChangeChannel(uint chanid, const QString &chan)
{
    VERBOSE(VB_PLAYBACK, LOC + QString("ChangeChannel(%1, '%2') ")
            .arg(chanid).arg(chan));

    if (!chanid && chan.isEmpty())
        return;

    QString channum = chan;
    QStringList reclist;
    bool muted = false;

    if (channum.isEmpty())
    {
        MSqlQuery query(MSqlQuery::InitCon());
        query.prepare("SELECT channum FROM channel "
                      "WHERE chanid = :CHANID");
        query.bindValue(":CHANID", chanid);
        if (query.exec() && query.isActive() && query.size() > 0 && query.next())
            channum = query.value(0).toString();
        else
            channum = QString::number(chanid);
    }

    if (activerecorder)
    {
        bool getit = false;

        if (chanid)
        {
            getit = activerecorder->ShouldSwitchToAnotherCard(
                QString::number(chanid));
        }
        else
        {
            QString needed_spacer;
            uint pref_cardid, cardid = activerecorder->GetRecorderNumber();
            bool dummy;

            activerecorder->CheckChannelPrefix(chan,  pref_cardid,
                                               dummy, needed_spacer);

            channum = add_spacer(chan, needed_spacer);
            getit = (pref_cardid != cardid);
        }

        if (getit)
            reclist = GetValidRecorderList(chanid, channum);
    }

    if (reclist.size())
    {
        RemoteEncoder *testrec = NULL;
        testrec = RemoteRequestFreeRecorderFromList(reclist);
        if (!testrec || !testrec->IsValidRecorder())
        {
            ClearInputQueues(true);
            ShowNoRecorderDialog();
            if (testrec)
                delete testrec;
            return;
        }

        if (!prevChan.empty() && prevChan.back() == channum)
        {
            // need to remove it if the new channel is the same as the old.
            prevChan.pop_back();
        }

        // found the card on a different recorder.
        delete testrec;
        SwitchCards(chanid, channum);
        return;
    }

    if (!prevChan.empty() && prevChan.back() == channum)
        return;

    if (!activerecorder->CheckChannel(channum))
        return;

    if (nvp)
    {
        AudioOutput *aud = nvp->getAudioOutput();
        if (aud && !aud->GetMute() && activenvp == nvp)
        {
            aud->ToggleMute();
            muted = true;
        }
    }

    if (nvp && (activenvp == nvp) && paused && GetOSD())
    {
        GetOSD()->EndStatus();
        gContext->DisableScreensaver();
        paused = false;
    }

    // Save the current channel if this is the first time
    if (prevChan.size() == 0)
        AddPreviousChannel();

    PauseLiveTV();

    if (activenvp)
    {
        activenvp->ResetCaptions();
        activenvp->ResetTeletext();
    }

    activerecorder->SetChannel(channum);

    if (muted)
        SetMuteTimer(kMuteTimeout * 2);

    UnpauseLiveTV();
}

void TV::AddPreviousChannel(void)
{
    if (!tvchain)
        return;

    // Don't store more than thirty channels.  Remove the first item
    if (prevChan.size() > 29)
        prevChan.erase(prevChan.begin());

    // This method builds the stack of previous channels
    prevChan.push_back(tvchain->GetChannelName(-1));
}

void TV::PreviousChannel(void)
{
    // Save the channel if this is the first time, and return so we don't
    // change chan to the current chan
    if (prevChan.size() == 0)
        return;

    // Increment the prevChanKeyCnt counter so we know how far to jump
    prevChanKeyCnt++;

    // Figure out the vector the desired channel is in
    int i = (prevChan.size() - prevChanKeyCnt - 1) % prevChan.size();

    // Display channel name in the OSD for up to 1 second.
    if (activenvp == nvp && GetOSD())
    {
        GetOSD()->HideSet("program_info");

        InfoMap infoMap;

        infoMap["channum"] = prevChan[i];
        infoMap["callsign"] = "";
        GetOSD()->ClearAllText("channel_number");
        GetOSD()->SetText("channel_number", infoMap, 1);
    }

    // Reset the timer
    prevChanTimer->stop();
    prevChanTimer->start(750);
}

void TV::SetPreviousChannel()
{
    if (!tvchain)
        return;

    // Stop the timer
    prevChanTimer->stop();

    // Figure out the vector the desired channel is in
    int i = (prevChan.size() - prevChanKeyCnt - 1) % prevChan.size();

    // Reset the prevChanKeyCnt counter
    prevChanKeyCnt = 0;

    // Only change channel if prevChan[vector] != current channel
    QString chan_name = tvchain->GetChannelName(-1);

    if (chan_name != prevChan[i])
    {
        QMutexLocker locker(&queuedInputLock);
        queuedInput   = QDeepCopy<QString>(prevChan[i]);
        queuedChanNum = QDeepCopy<QString>(prevChan[i]);
        queuedChanID  = 0;
    }

    //Turn off the OSD Channel Num so the channel changes right away
    if (activenvp == nvp && GetOSD())
        GetOSD()->HideSet("channel_number");
}

bool TV::ClearOSD(void)
{
    bool res = false;

    if (HasQueuedInput() || HasQueuedChannel())
    {
        ClearInputQueues(true);
        res = true;
    }

    if (GetOSD() && GetOSD()->HideAll())
        res = true;

    while (res && GetOSD() && GetOSD()->HideAll())
        usleep(1000);

    return res;
}

/** \fn TV::ToggleOSD(bool includeStatus)
 *  \brief Cycle through the available Info OSDs. 
 */
void TV::ToggleOSD(bool includeStatusOSD)
{
    OSD *osd = GetOSD();
    bool showStatus = false;
    if (paused || !osd)
        return;

    // DVD toggles between status and nothing
    if (activerbuffer && activerbuffer->isDVD() &&
        playbackinfo->description.isEmpty() && playbackinfo->title.isEmpty())
    {
        if (osd->IsSetDisplaying("status"))
            osd->HideAll();
        else
            showStatus = true;
    }
    else if (osd->IsSetDisplaying("status"))
    {
        if (osd->HasSet("program_info_small"))
            UpdateOSDProgInfo("program_info_small");
        else
            UpdateOSDProgInfo("program_info");
    }
    // If small is displaying, show long if we have it, else hide info
    else if (osd->IsSetDisplaying("program_info_small")) 
    {
        if (osd->HasSet("program_info"))
            UpdateOSDProgInfo("program_info");
        else
            osd->HideAll();
    }
    // If long is displaying, hide info
    else if (osd->IsSetDisplaying("program_info"))
    {
        osd->HideAll();
    }
    // If no program_info displaying, show status if we want it
    else if (includeStatusOSD)
    {
        showStatus = true;
    }
    // No status desired? Nothing is up, Display small if we have, else display long
    else
    {   
        if (osd->HasSet("program_info_small"))
            UpdateOSDProgInfo("program_info_small");
        else
            UpdateOSDProgInfo("program_info");
    }

    if (showStatus)
    {
        osd->HideAll();
        if (nvp)
        {
            struct StatusPosInfo posInfo;
            nvp->calcSliderPos(posInfo);
            osd->ShowStatus(posInfo, false, tr("Position"),
                              osd_prog_info_timeout);
            update_osd_pos = true;
        }
        else
            update_osd_pos = false;
    }
    else
        update_osd_pos = false;
}

/** \fn TV::UpdateOSDProgInfo(const char *whichInfo)
 *  \brief Update and display the passed OSD set with programinfo
 */
void TV::UpdateOSDProgInfo(const char *whichInfo)
{
    InfoMap infoMap;
    OSD *osd = GetOSD();
    if (!osd)
        return;

    pbinfoLock.lock();
    if (playbackinfo)
        playbackinfo->ToMap(infoMap);
    pbinfoLock.unlock();

    // Clear previous osd and add new info
    osd->ClearAllText(whichInfo);
    osd->HideAll();
    osd->SetText(whichInfo, infoMap, osd_prog_info_timeout);
}

void TV::UpdateOSDSeekMessage(const QString &mesg, int disptime)
{
    if (activenvp != nvp)
        return;

    struct StatusPosInfo posInfo;
    nvp->calcSliderPos(posInfo);
    bool slidertype = StateIsLiveTV(GetState());
    int osdtype = (doSmartForward) ? kOSDFunctionalType_SmartForward :
                                     kOSDFunctionalType_Default;
    if (GetOSD())
        GetOSD()->ShowStatus(posInfo, slidertype, mesg, disptime, osdtype);
    update_osd_pos = true;
}

void TV::UpdateOSDInput(QString inputname)
{
    if (!activerecorder || !tvchain)
        return;

    int cardid = activerecorder->GetRecorderNumber();

    if (inputname.isEmpty())
        inputname = tvchain->GetInputName(-1);

    QString displayName = CardUtil::GetDisplayName(cardid, inputname);
    // If a display name doesn't exist use cardid and inputname
    if (displayName.isEmpty())
        displayName = QString("%1: %2").arg(cardid).arg(inputname);

    if (GetOSD())
        GetOSD()->SetSettingsText(displayName, 3);
}

/** \fn TV::UpdateOSDSignal(const QStringList&)
 *  \brief Updates Signal portion of OSD...
 */
void TV::UpdateOSDSignal(const QStringList& strlist)
{
    QMutexLocker locker(&osdlock);

    if (!GetOSD() || browsemode || !queuedChanNum.isEmpty())
    {
        if (&lastSignalMsg != &strlist)
            lastSignalMsg = QDeepCopy<QStringList>(strlist);
        return;
    }

    SignalMonitorList slist = SignalMonitorValue::Parse(strlist);

    InfoMap infoMap = lastSignalUIInfo;
    if (lastSignalUIInfoTime.elapsed() > 5000 || infoMap["callsign"].isEmpty())
    {
        //lastSignalUIInfo["name"]  = "signalmonitor";
        //lastSignalUIInfo["title"] = "Signal Monitor";
        lastSignalUIInfo.clear();
        pbinfoLock.lock();
        if (playbackinfo)
            playbackinfo->ToMap(lastSignalUIInfo);
        pbinfoLock.unlock();
        infoMap = lastSignalUIInfo;
        lastSignalUIInfoTime.start();
    }

    int i = 0;
    SignalMonitorList::const_iterator it;
    for (it = slist.begin(); it != slist.end(); ++it)
        if ("error" == it->GetShortName())
            infoMap[QString("error%1").arg(i++)] = it->GetName();
    i = 0;
    for (it = slist.begin(); it != slist.end(); ++it)
        if ("message" == it->GetShortName())
            infoMap[QString("message%1").arg(i++)] = it->GetName();

    uint  sig  = 0;
    float snr  = 0.0f;
    uint  ber  = 0xffffffff;
    int   pos  = -1;
    QString pat(""), pmt(""), mgt(""), vct(""), nit(""), sdt(""), crypt("");
    QString err = QString::null, msg = QString::null;
    for (it = slist.begin(); it != slist.end(); ++it)
    {
        if ("error" == it->GetShortName())
        {
            err = it->GetName();
            continue;
        }

        if ("message" == it->GetShortName())
        {
            msg = it->GetName();
            VERBOSE(VB_IMPORTANT, "msg: "<<msg);
            continue;
        }

        infoMap[it->GetShortName()] = QString::number(it->GetValue());
        if ("signal" == it->GetShortName())
            sig = it->GetNormalizedValue(0, 100);
        else if ("snr" == it->GetShortName())
            snr = it->GetValue();
        else if ("ber" == it->GetShortName())
            ber = it->GetValue();
        else if ("pos" == it->GetShortName())
            pos = it->GetValue();
        else if ("seen_pat" == it->GetShortName())
            pat = it->IsGood() ? "a" : "_";
        else if ("matching_pat" == it->GetShortName())
            pat = it->IsGood() ? "A" : pat;
        else if ("seen_pmt" == it->GetShortName())
            pmt = it->IsGood() ? "m" : "_";
        else if ("matching_pmt" == it->GetShortName())
            pmt = it->IsGood() ? "M" : pmt;
        else if ("seen_mgt" == it->GetShortName())
            mgt = it->IsGood() ? "g" : "_";
        else if ("matching_mgt" == it->GetShortName())
            mgt = it->IsGood() ? "G" : mgt;
        else if ("seen_vct" == it->GetShortName())
            vct = it->IsGood() ? "v" : "_";
        else if ("matching_vct" == it->GetShortName())
            vct = it->IsGood() ? "V" : vct;
        else if ("seen_nit" == it->GetShortName())
            nit = it->IsGood() ? "n" : "_";
        else if ("matching_nit" == it->GetShortName())
            nit = it->IsGood() ? "N" : nit;
        else if ("seen_sdt" == it->GetShortName())
            sdt = it->IsGood() ? "s" : "_";
        else if ("matching_sdt" == it->GetShortName())
            sdt = it->IsGood() ? "S" : sdt;
        else if ("seen_crypt" == it->GetShortName())
            crypt = it->IsGood() ? "c" : "_";
        else if ("matching_crypt" == it->GetShortName())
            crypt = it->IsGood() ? "C" : crypt;
    }
    if (sig)
        infoMap["signal"] = QString::number(sig); // use normalized value

    bool    allGood = SignalMonitorValue::AllGood(slist);
    QString slock   = ("1" == infoMap["slock"]) ? "L" : "l";
    QString lockMsg = (slock=="L") ? tr("Partial Lock") : tr("No Lock");
    QString sigMsg  = allGood ? tr("Lock") : lockMsg;

    QString sigDesc = tr("Signal %1\%").arg(sig,2);
    if (snr > 0.0f)
        sigDesc += " | " + tr("S/N %1dB").arg(log10f(snr), 3, 'f', 1);
    if (ber != 0xffffffff)
        sigDesc += " | " + tr("BE %1", "Bit Errors").arg(ber, 2);
    if ((pos >= 0) && (pos < 100))
        sigDesc += " | " + tr("Rotor %1\%").arg(pos,2);

    sigDesc = sigDesc + QString(" | (%1%2%3%4%5%6%7%8) %9")
        .arg(slock).arg(pat).arg(pmt).arg(mgt).arg(vct)
        .arg(nit).arg(sdt).arg(crypt).arg(sigMsg);

    if (!err.isEmpty())
        sigDesc = err;
    else if (!msg.isEmpty())
        sigDesc = msg;

    //GetOSD()->ClearAllText("signal_info");
    //GetOSD()->SetText("signal_info", infoMap, -1);

    GetOSD()->ClearAllText("channel_number");
    GetOSD()->SetText("channel_number", infoMap, osd_prog_info_timeout);

    infoMap["description"] = sigDesc;
    GetOSD()->ClearAllText("program_info");
    GetOSD()->SetText("program_info", infoMap, osd_prog_info_timeout);

    lastSignalMsg.clear();
    lastSignalMsgTime.start();

    // Turn off lock timer if we have an "All Good" or good PMT
    if (allGood || (pmt == "M"))
    {
        lockTimerOn = false;
        lastLockSeenTime = QDateTime::currentDateTime();
    }
}

void TV::UpdateOSDTimeoutMessage(void)
{
    QString dlg_name("channel_timed_out");
    bool timed_out = false;
    if (activerecorder)
    {
        QString input = activerecorder->GetInput();
        uint timeout  = activerecorder->GetSignalLockTimeout(input);
        timed_out = lockTimerOn && ((uint)lockTimer.elapsed() > timeout);
    }
    OSD *osd = GetOSD();

    if (!osd)
    {
        if (timed_out)
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "You have no OSD, "
                    "but tuning has already taken too long.");
        }
        return;
    }

    if (!timed_out)
    {
        if (osd->DialogShowing(dlg_name))
            osd->TurnDialogOff(dlg_name);
        return;
    }

    if (osd->DialogShowing(dlg_name))
        return;

    // create dialog...
    static QString chan_up   = GET_KEY("TV Playback", "CHANNELUP");
    static QString chan_down = GET_KEY("TV Playback", "CHANNELDOWN");
    static QString next_src  = GET_KEY("TV Playback", "NEXTSOURCE");
    static QString tog_cards = GET_KEY("TV Playback", "NEXTINPUT");

    QString message = tr(
        "You should have gotten a channel lock by now. "
        "You can continue to wait for a signal, or you "
        "can change the channels with %1 or %2, change "
        "video source (%3), inputs (%4), etc.")
        .arg(chan_up).arg(chan_down).arg(next_src).arg(tog_cards);

    QStringList options;
    options += tr("OK");

    dialogname = dlg_name;
    osd->NewDialogBox(dialogname, message, options, 0);
}

void TV::UpdateLCD(void)
{
    // Make sure the LCD information gets updated shortly
    lastLcdUpdate = lastLcdUpdate.addSecs(-120);
}

void TV::ShowLCDChannelInfo(void)
{
    class LCD * lcd = LCD::Get();
    if (lcd == NULL || playbackinfo == NULL)
        return;

    QString title, subtitle, callsign;

    pbinfoLock.lock();
    title = playbackinfo->title;
    subtitle = playbackinfo->subtitle;
    callsign = playbackinfo->chansign;
    pbinfoLock.unlock();

    if ((callsign != lcdCallsign) || (title != lcdTitle) || 
        (subtitle != lcdSubtitle))
    {
        lcd->switchToChannel(callsign, title, subtitle);
        lcdCallsign = callsign;
        lcdTitle = title;
        lcdSubtitle = subtitle;
    }
}

static void format_time(int seconds, QString &tMin, QString &tHrsMin)
{
    int minutes     = seconds / 60;
    int hours       = minutes / 60;
    int min         = minutes % 60;

    tMin = QString("%1 %2").arg(minutes).arg(TV::tr("minutes"));
    tHrsMin.sprintf("%d:%02d", hours, min);
}


void TV::ShowLCDDVDInfo(void)
{
    class LCD * lcd = LCD::Get();

    if (lcd == NULL || activerbuffer == NULL || !activerbuffer->isDVD())
        return;

    DVDRingBufferPriv *dvd = activerbuffer->DVD(); 
    QString dvdName, dvdSerial;
    QString mainStatus, subStatus; 

    if (!dvd->GetNameAndSerialNum(dvdName, dvdSerial))
    {
        dvdName = tr("DVD");
    }
    if (dvd->IsInMenu()) 
        mainStatus = tr("Menu");
    else if (dvd->InStillFrame())
        mainStatus = tr("Still Frame");
    else
    {
        QString timeMins, timeHrsMin;
        int playingTitle, playingPart, totalParts; 

        dvd->GetPartAndTitle(playingPart, playingTitle);
        totalParts = dvd->NumPartsInTitle();
        format_time(dvd->GetTotalTimeOfTitle(), timeMins, timeHrsMin); 

        mainStatus = tr("Title: %1 (%2)").arg(playingTitle).arg(timeHrsMin);
        subStatus = tr("Chapter: %1/%2").arg(playingPart).arg(totalParts);
    }
    if ((dvdName != lcdCallsign) || (mainStatus != lcdTitle) || 
                                    (subStatus != lcdSubtitle))
    {
        lcd->switchToChannel(dvdName, mainStatus, subStatus);
        lcdCallsign = dvdName;
        lcdTitle = mainStatus;
        lcdSubtitle = subStatus;
    }
}


/** \fn TV::GetNextProgram(RemoteEncoder*,int,InfoMap&)
 *  \brief Fetches information on the desired program from the backend.
 *  \param enc RemoteEncoder to query, if null query the activerecorder.
 *  \param direction BrowseDirection to get information on.
 *  \param infoMap InfoMap to fill in with returned data
 */
void TV::GetNextProgram(RemoteEncoder *enc, int direction,
                        InfoMap &infoMap)
{ 
    QString title, subtitle, desc, category, endtime, callsign, iconpath;
    QDateTime begts, endts;

    QString starttime = infoMap["dbstarttime"];
    QString chanid    = infoMap["chanid"];
    QString channum   = infoMap["channum"];
    QString seriesid  = infoMap["seriesid"];
    QString programid = infoMap["programid"];

    if (!enc)
        enc = activerecorder;

    enc->GetNextProgram(direction,
                        title,     subtitle,  desc,      category,
                        starttime, endtime,   callsign,  iconpath,
                        channum,   chanid,    seriesid,  programid);

    if (!starttime.isEmpty())
        begts = QDateTime::fromString(starttime, Qt::ISODate);
    else
        begts = QDateTime::fromString(infoMap["dbstarttime"], Qt::ISODate);

    infoMap["starttime"] = begts.toString(db_time_format);
    infoMap["startdate"] = begts.toString(db_short_date_format);

    infoMap["endtime"] = infoMap["enddate"] = "";
    if (!endtime.isEmpty())
    {
        endts = QDateTime::fromString(endtime, Qt::ISODate);
        infoMap["endtime"] = endts.toString(db_time_format);
        infoMap["enddate"] = endts.toString(db_short_date_format);
    }

    infoMap["lenmins"] = QString("0 %1").arg(TV::tr("minutes"));
    infoMap["lentime"] = "0:00";
    if (begts.isValid() && endts.isValid())
    {
        QString lenM, lenHM;
        format_time(begts.secsTo(endts), lenM, lenHM);
        infoMap["lenmins"] = lenM;
        infoMap["lentime"] = lenHM;
    }

    infoMap["dbstarttime"] = starttime;
    infoMap["dbendtime"]   = endtime;
    infoMap["title"]       = title;
    infoMap["subtitle"]    = subtitle;
    infoMap["description"] = desc;
    infoMap["category"]    = category;
    infoMap["callsign"]    = callsign;
    infoMap["channum"]     = channum;
    infoMap["chanid"]      = chanid;
    infoMap["iconpath"]    = iconpath;
    infoMap["seriesid"]    = seriesid;
    infoMap["programid"]   = programid;
}

bool TV::IsTunable(uint chanid, bool use_cache)
{
    VERBOSE(VB_PLAYBACK, QString("IsTunable(%1)").arg(chanid));

    if (!chanid)
        return false;

    uint mplexid = ChannelUtil::GetMplexID(chanid);
    mplexid = (32767 == mplexid) ? 0 : mplexid;

    vector<uint> excluded_cards;
    if (activerecorder)
        excluded_cards.push_back(activerecorder->GetRecorderNumber());

    uint sourceid = ChannelUtil::GetSourceIDForChannel(chanid);
    vector<uint> connected   = RemoteRequestFreeRecorderList();
    vector<uint> interesting = CardUtil::GetCardIDs(sourceid);

    // filter disconnected cards
    vector<uint> cardids = excluded_cards;
    for (uint i = 0; i < connected.size(); i++)
    {
        for (uint j = 0; j < interesting.size(); j++)
        {
            if (connected[i] == interesting[j])
            {
                cardids.push_back(interesting[j]);
                break;
            }
        }
    }

#if 0
    cout << "cardids[" << sourceid << "]: ";
    for (uint i = 0; i < cardids.size(); i++)
        cout << cardids[i] << ", ";
    cout << endl;
#endif

    for (uint i = 0; i < cardids.size(); i++)
    {
        vector<InputInfo> inputs;

        bool used_cache = false;
        if (use_cache)
        {
            QMutexLocker locker(&is_tunable_cache_lock);
            if (is_tunable_cache_inputs.contains(cardids[i]))
            {
                inputs = is_tunable_cache_inputs[cardids[i]];
                used_cache = true;
            }
        }

        if (!used_cache)
        {
            inputs = RemoteRequestFreeInputList(cardids[i], excluded_cards);
            QMutexLocker locker(&is_tunable_cache_lock);
            is_tunable_cache_inputs[cardids[i]] = inputs;
        }

#if 0
        cout << "inputs[" << cardids[i] << "]: ";
        for (uint j = 0; j < inputs.size(); j++)
            cout << inputs[j].inputid << ", ";
        cout << endl;
#endif

        for (uint j = 0; j < inputs.size(); j++)
        {
            if (inputs[j].sourceid != sourceid)
                continue;

            if (inputs[j].mplexid &&
                inputs[j].mplexid != mplexid)
                continue;

            VERBOSE(VB_PLAYBACK, QString("IsTunable(%1) -> true\n")
                    .arg(chanid));

            return true;
        }
    }

    VERBOSE(VB_PLAYBACK, QString("IsTunable(%1) -> false\n").arg(chanid));

    return false;
}

void TV::ClearTunableCache(void)
{
    QMutexLocker locker(&is_tunable_cache_lock);
    is_tunable_cache_inputs.clear();
}

void TV::EmbedOutput(WId wid, int x, int y, int w, int h)
{
    embedWinID = wid;
    embedBounds = QRect(x,y,w,h);

    if (nvp)
        nvp->EmbedInWidget(wid, x, y, w, h);
}

void TV::StopEmbeddingOutput(void)
{
    if (nvp)
        nvp->StopEmbedding();
    embedWinID = 0;
}

bool TV::IsEmbedding(void)
{
    if (nvp)
        return nvp->IsEmbedding();
    return false;
}

void TV::DrawUnusedRects(bool sync)
{
    if (nvp)
        nvp->DrawUnusedRects(sync);
}

void *TV::ViewScheduledMenuHandler(void *param)
{
    TV *obj = (TV *)param;
    obj->doEditSchedule(kViewSchedule);
    return NULL;
}

void *TV::RecordedShowMenuHandler(void *param)
{
    TV *obj = (TV *)param;
    obj->doEditSchedule(kPlaybackBox);
    return NULL;
}

/**
 * \brief Used by EditSchedule(). Unpauses embedded tv based on whether theme 
    exists and/or if knob to continued playback is enabled
    \returns true if player should be embedded
 */
bool TV::VideoThemeCheck(QString str, bool stayPaused)
{   
    if (StateIsLiveTV(GetState()))
        return true;

    bool ret = true;
    bool allowembed = (nvp && nvp->getVideoOutput() && 
                    nvp->getVideoOutput()->AllowPreviewEPG());

    long long margin = (long long)(nvp->GetFrameRate() *
                        nvp->GetAudioStretchFactor());
    margin = margin * 5;
    QDomElement xmldata;
    XMLParse *theme = new XMLParse();
    if (!allowembed ||
        !theme->LoadTheme(xmldata, str) ||
        !gContext->GetNumSetting("ContinueEmbeddedTVPlay", 0) ||
        nvp->IsNearEnd(margin) ||
        paused)
    {
        if (!stayPaused)
            DoPause(false);
        ret = false;
    }

    if (theme)
        delete theme;

    return ret;
}

void TV::doEditSchedule(int editType)
{
    if (!playbackinfo)
    {
        VERBOSE(VB_IMPORTANT,
                LOC_ERR + "doEditSchedule(): no active playbackinfo.");
        return;
    }

    if (!nvp)
        return;

    // Resize window to the MythTV GUI size
    if (nvp && nvp->getVideoOutput()) 
        nvp->getVideoOutput()->ResizeForGui(); 
    MythMainWindow *mwnd = gContext->GetMainWindow();
    bool using_gui_size_for_tv = gContext->GetNumSetting("GuiSizeForTV", 0);
    if (!using_gui_size_for_tv)
    {
        mwnd->setGeometry(saved_gui_bounds.left(), saved_gui_bounds.top(),
                          saved_gui_bounds.width(), saved_gui_bounds.height());
        mwnd->setFixedSize(saved_gui_bounds.size());
    }

    // Collect channel info
    pbinfoLock.lock();
    uint    chanid  = playbackinfo->chanid.toUInt();
    QString channum = playbackinfo->chanstr;
    pbinfoLock.unlock();

    DBChanList changeChannel;
    ProgramInfo *nextProgram = NULL;

    bool stayPaused = paused;
    TV *player = NULL;
    bool showvideo = false;

    switch (editType)
    {
        default:
        case kScheduleProgramGuide:
        {
            bool allowsecondary = true;
            if (nvp && nvp->getVideoOutput())
                allowsecondary = nvp->getVideoOutput()->AllowPreviewEPG();
            if (VideoThemeCheck("programguide-video", stayPaused))
                player = this;
            if (StateIsLiveTV(GetState()))
            {
                changeChannel = GuideGrid::Run(chanid, channum, false, 
                                            player, allowsecondary);
            }
            else
                GuideGrid::Run(chanid, channum, false, player);
            break;
        }
        case kScheduleProgramFinder:
            if (!paused)
                DoPause(false);
            RunProgramFind(false, false);
            break;
        case kScheduledRecording:
        {
            if (!paused)
                DoPause(false);
            QMutexLocker locker(&pbinfoLock);
            ScheduledRecording *record = new ScheduledRecording();
            record->loadByProgram(playbackinfo);
            record->exec();
            record->deleteLater();
            break;
        }
        case kViewSchedule:
        {
            showvideo = VideoThemeCheck("conflict-video", stayPaused);
            RunViewScheduledPtr((void *)this, showvideo);
            break;
        }
        case kPlaybackBox:
        {
            showvideo = VideoThemeCheck("playback-video", stayPaused);
            nextProgram = RunPlaybackBoxPtr((void *)this, showvideo);
        }
    }

    if (IsEmbedding())
        StopEmbeddingOutput();

    if (StateIsPlaying(GetState()) && !stayPaused && paused)
        DoPause();

    if (nextProgram)
    {
        setLastProgram(nextProgram);
        jumpToProgram = true;
        exitPlayer = true;
        delete nextProgram;
    }
    // Resize the window back to the MythTV Player size
    if (!using_gui_size_for_tv)
    {
        mwnd->setGeometry(player_bounds.left(), player_bounds.top(),
                          player_bounds.width(), player_bounds.height());
        mwnd->setFixedSize(player_bounds.size());
    }
    if (nvp && nvp->getVideoOutput()) 
        nvp->getVideoOutput()->ResizeForVideo(); 

    // If user selected a new channel in the EPG, change to that channel
    if (changeChannel.size())
        ChangeChannel(changeChannel);

    if (nvp && jumpToProgram)
        nvp->DiscardVideoFrames(true);

    menurunning = false;
}

/**
 * \brief create new thread to invoke function pointers used for
 * embedding the tv player in other containers. Example: playbackbox 
 */
void TV::EmbedWithNewThread(int editType)
{
    if (menurunning != true)
    {
        menurunning = true;
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        switch (editType)
        {
            case kViewSchedule:
                pthread_create(&tid, &attr, TV::ViewScheduledMenuHandler, this);
                break;
            case kPlaybackBox:
                pthread_create(&tid, &attr, TV::RecordedShowMenuHandler, this);
                break;
        }

        pthread_attr_destroy(&attr);

        return;
    }
}

void TV::EditSchedule(int editType)
{
    // post the request to the main UI thread
    // it will be caught in eventFilter and processed as CustomEvent
    // this will create the program guide window (widget)
    // on the main thread and avoid a deadlock on Win32
    QString message = QString("START_EPG %1").arg(editType);
    MythEvent* me = new MythEvent(message);
    qApp->postEvent(myWindow, me);
}

void TV::ChangeVolume(bool up)
{
    AudioOutput *aud = nvp->getAudioOutput();
    if (!aud)
        return;

    if (up)
        aud->AdjustCurrentVolume(2);
    else 
        aud->AdjustCurrentVolume(-2);

    int curvol = aud->GetCurrentVolume();
    QString text = QString(tr("Volume %1 %")).arg(curvol);

    if (GetOSD() && !browsemode)
    {
        GetOSD()->ShowStatus(curvol * 10, true, tr("Adjust Volume"), text, 5, 
                        kOSDFunctionalType_PictureAdjust);
        update_osd_pos = false;
    }
}

void TV::ToggleTimeStretch(void)
{
    if (normal_speed == 1.0f)
        normal_speed = prev_speed;
    else
    {
        prev_speed = normal_speed;
        normal_speed = 1.0f;
    }
    ChangeTimeStretch(0, false);
}

void TV::ChangeTimeStretch(int dir, bool allowEdit)
{
    const float kTimeStretchMin = 0.5;
    const float kTimeStretchMax = 2.0;
    float new_normal_speed = normal_speed + 0.05*dir;
    stretchAdjustment = allowEdit;

    if (new_normal_speed > kTimeStretchMax && normal_speed < kTimeStretchMax)
        new_normal_speed = kTimeStretchMax;
    else if (new_normal_speed < kTimeStretchMin &&
             normal_speed > kTimeStretchMin)
        new_normal_speed = kTimeStretchMin;

    if (new_normal_speed > kTimeStretchMax ||
        new_normal_speed < kTimeStretchMin)
        return;

    normal_speed = new_normal_speed;

    if (!paused)
        activenvp->Play(normal_speed, true);

    QString text = QString(tr("Time Stretch %1X")).arg(normal_speed);

    if (GetOSD() && !browsemode)
    {
        if (!allowEdit)
            UpdateOSDSeekMessage(PlayMesg(), osd_general_timeout);
        else
        {
            int val = (int)(normal_speed*(1000/kTimeStretchMax));
            GetOSD()->ShowStatus(val, false, tr("Adjust Time Stretch"), text, 
                                 10, kOSDFunctionalType_TimeStretchAdjust);
            update_osd_pos = false;
        }
    }
}

// dir in 10ms jumps
void TV::ChangeAudioSync(int dir, bool allowEdit)
{
    long long newval;

    if (!audiosyncAdjustment && LONG_LONG_MIN == audiosyncBaseline)
        audiosyncBaseline = activenvp->GetAudioTimecodeOffset();

    audiosyncAdjustment = allowEdit;

    if (dir == 1000000)
    {
        newval = activenvp->ResyncAudioTimecodeOffset() - 
                 audiosyncBaseline;
        audiosyncBaseline = activenvp->GetAudioTimecodeOffset();
    }
    else if (dir == -1000000)
    {
        newval = activenvp->ResetAudioTimecodeOffset() - 
                 audiosyncBaseline;
        audiosyncBaseline = activenvp->GetAudioTimecodeOffset();
    }
    else
        newval = activenvp->AdjustAudioTimecodeOffset(dir*10) - 
                 audiosyncBaseline;

    if (GetOSD() && !browsemode)
    {
        QString text = QString(" %1 ms").arg(newval);
        int val = (int)newval;
        if (dir == 1000000 || dir == -1000000)
        {
            text = tr("Audio Resync") + text;
            val = 0;
        }
        else
            text = tr("Audio Sync") + text;

        GetOSD()->ShowStatus((val/2)+500, false, tr("Adjust Audio Sync"), text,
                             10, kOSDFunctionalType_AudioSyncAdjust);
        update_osd_pos = false;
    }
}

void TV::ToggleMute(void)
{
    kMuteState mute_status;

    AudioOutput *aud = nvp->getAudioOutput();
    if (!aud)
        return;

    if (!MuteIndividualChannels)
    {
        aud->ToggleMute();
        bool muted = aud->GetMute();
        if (muted) 
            mute_status = MUTE_BOTH;
        else
            mute_status = MUTE_OFF;
    }
    else mute_status = aud->IterateMutedChannels();

    QString text;

    switch (mute_status)
    {
       case MUTE_OFF: text = tr("Mute Off"); break;
       case MUTE_BOTH:  text = tr("Mute On"); break;
       case MUTE_LEFT: text = tr("Left Channel Muted"); break;
       case MUTE_RIGHT: text = tr("Right Channel Muted"); break;
    }
 
    if (GetOSD() && !browsemode)
        GetOSD()->SetSettingsText(text, 5);
}

void TV::ToggleSleepTimer(void)
{
    QString text;

    // increment sleep index, cycle through
    if (++sleep_index == sleep_times.size()) 
        sleep_index = 0;

    // turn sleep timer off
    if (sleep_times[sleep_index].seconds == 0)
        sleepTimer->stop();
    else
    {
        if (sleepTimer->isActive())
            // sleep timer is active, adjust interval
            sleepTimer->changeInterval(sleep_times[sleep_index].seconds *
                                       1000);
        else
            // sleep timer is not active, start it
            sleepTimer->start(sleep_times[sleep_index].seconds * 1000, 
                              TRUE);
    }

    text = tr("Sleep ") + " " + sleep_times[sleep_index].dispString;

    // display OSD
    if (GetOSD() && !browsemode)
        GetOSD()->SetSettingsText(text, 3);
}

void TV::SleepEndTimer(void)
{
    exitPlayer = true;
    wantsToQuit = true;
}

/*!
 *  \brief After idleTimer has expired, display a dialogue warning the user
 *         that we will exit LiveTV unless they take action.
 *         We change idleTimer, to 45 seconds and when it expires for a second
 *         time we quit the player.
 *         If the user so decides, they may hit ok and we reset the timer
 *         back to the default expiry period.
 */
void TV::IdleDialog(void)
{
    if (!StateIsLiveTV(GetState()))
       return;

    // Display dialogue for X seconds before exiting livetv
    int timeuntil = 45;

    if (GetOSD()->DialogShowing("idletimeout"))
    {
        VERBOSE(VB_GENERAL, "Idle timeout reached, leaving LiveTV");
        exitPlayer = true;
        wantsToQuit = true;
        return;
    }

    QString message = QObject::tr("Mythtv has been idle for %1 minutes and "
        "will exit in %2 seconds. Are you still watching?")
        .arg(gContext->GetNumSetting("LiveTVIdleTimeout", 0))
        .arg("%d");

    while (!GetOSD())
    {
        qApp->unlock();
        qApp->processEvents();
        usleep(1000);
        qApp->lock();
    }

    QStringList options;
    options += tr("Yes");

    dialogname = "idletimeout";
    GetOSD()->NewDialogBox(dialogname, message, options, timeuntil, 0);
    idleTimer->changeInterval(timeuntil * 1000);
}

void TV::ToggleAspectOverride(AspectOverrideMode aspectMode)
{
    nvp->ToggleAspectOverride(aspectMode);
    QString text = toString(nvp->GetAspectOverride());

    if (GetOSD() && !browsemode && !GetOSD()->IsRunningTreeMenu())
        GetOSD()->SetSettingsText(text, 3);
}

void TV::ToggleAdjustFill(AdjustFillMode adjustfillMode)
{
    nvp->ToggleAdjustFill(adjustfillMode);
    QString text = toString(nvp->GetAdjustFill());

    if (GetOSD() && !browsemode && !GetOSD()->IsRunningTreeMenu())
        GetOSD()->SetSettingsText(text, 3);
}

void TV::ChangeChannel(const DBChanList &options)
{
    for (uint i = 0; i < options.size(); i++)
    {
        uint    chanid  = options[i].chanid;
        QString channum = options[i].channum;

        if (chanid && !channum.isEmpty() && IsTunable(chanid))
        {
            // hide the channel number, activated by certain signal monitors
            if (GetOSD())
                GetOSD()->HideSet("channel_number");
            
            QMutexLocker locker(&queuedInputLock);
            queuedInput   = QDeepCopy<QString>(channum);
            queuedChanNum = QDeepCopy<QString>(channum);
            queuedChanID  = chanid;
            break;
        }
    }
}

void TV::KeyRepeatOK(void)
{
    keyRepeat = true;
}

void TV::SetMuteTimer(int timeout)
{
    // message to set the timer will be posted to the main UI thread
    // where it will be caught in eventFilter and processed as CustomEvent
    // this will properly set the mute timer
    // otherwise it never fires on Win32
    QString message = QString("UNMUTE %1").arg(timeout);
    MythEvent* me = new MythEvent(message);
    qApp->postEvent(myWindow, me);
}

/** \fn TV::UnMute(void)
 *  \brief If the player exists and sound is muted, this unmutes the sound.
 */
void TV::UnMute(void)
{
    if (!nvp)
        return;

    AudioOutput *aud = nvp->getAudioOutput();
    if (aud && aud->GetMute())
        aud->ToggleMute();
}

void TV::customEvent(QCustomEvent *e)
{
    if ((MythEvent::Type)(e->type()) == MythEvent::MythEventMessage)
    {
        MythEvent *me = (MythEvent *)e;
        QString message = me->Message();

        if (recorder && message.left(14) == "DONE_RECORDING")
        {
            if (GetState() == kState_WatchingRecording)
            {
                message = message.simplifyWhiteSpace();
                QStringList tokens = QStringList::split(" ", message);
                int cardnum = tokens[1].toInt();
                int filelen = tokens[2].toInt();

                if (recorder && cardnum == recorder->GetRecorderNumber())
                {
                    nvp->SetWatchingRecording(false);
                    nvp->SetLength(filelen);
                    ChangeState(kState_WatchingPreRecorded);
                }
            }
            else if (StateIsLiveTV(GetState()))
            {
                message = message.simplifyWhiteSpace();
                QStringList tokens = QStringList::split(" ", message);
                int cardnum = tokens[1].toInt();
                int filelen = tokens[2].toInt();

                if (recorder && cardnum == recorder->GetRecorderNumber() &&
                    tvchain && tvchain->HasNext())
                {
                    nvp->SetWatchingRecording(false);
                    nvp->SetLength(filelen);
                }
            }
        }
        else if (StateIsLiveTV(GetState()) &&
                 message.left(14) == "ASK_RECORDING ")
        {
            message = message.simplifyWhiteSpace();
            QStringList tokens = QStringList::split(" ", message);
            int cardnum = tokens[1].toInt();
            int timeuntil = tokens[2].toInt();
            int hasrec    = tokens[3].toInt();
            int haslater  = tokens[4].toInt();
            VERBOSE(VB_GENERAL, LOC + message << " hasrec: "<<hasrec
                    << " haslater: " << haslater);

            if (recorder && cardnum == recorder->GetRecorderNumber())
            {
                menurunning = false;
                AskAllowRecording(
                    me->ExtraDataList(), timeuntil, hasrec, haslater);
            }
            else if (piprecorder &&
                     cardnum == piprecorder->GetRecorderNumber())
            {
                VERBOSE(VB_GENERAL, LOC + "Disabling PiP for recording");
                TogglePIPView();
            }
        }
        else if (recorder && message.left(11) == "QUIT_LIVETV")
        {
            message = message.simplifyWhiteSpace();
            QStringList tokens = QStringList::split(" ", message);
            int cardnum = tokens[1].toInt();

            if (cardnum == recorder->GetRecorderNumber())
            {
                menurunning = false;
                wantsToQuit = false;
                exitPlayer = true;
            }
            else if (piprecorder &&
                     cardnum == piprecorder->GetRecorderNumber())
            {
                VERBOSE(VB_GENERAL, LOC + "Disabling PiP for QUIT_LIVETV");
                TogglePIPView();
            }
        }
        else if (recorder && message.left(12) == "LIVETV_WATCH")
        {
            message = message.simplifyWhiteSpace();
            QStringList tokens = QStringList::split(" ", message);
            int cardnum = tokens[1].toInt();
            int watch   = tokens[2].toInt();

            uint s = (cardnum == recorder->GetRecorderNumber()) ? 0 : 1;

            if ((recorder    && cardnum == recorder->GetRecorderNumber()) ||
                (piprecorder && cardnum == piprecorder->GetRecorderNumber()))
            {
                if (watch)
                {
                    ProgramInfo pi;
                    QStringList list = me->ExtraDataList();
                    if (pi.FromStringList(list, 0))
                        SetPseudoLiveTV(s, &pi, kPseudoChangeChannel);

                    if (!s && pipnvp)
                        TogglePIPView();
                }
                else
                    SetPseudoLiveTV(s, NULL, kPseudoNormalLiveTV);
            }
        }
        else if (tvchain && message.left(12) == "LIVETV_CHAIN")
        {
            message = message.simplifyWhiteSpace();
            QStringList tokens = QStringList::split(" ", message);
            if (tokens[1] == "UPDATE")
            {
                tvchainUpdateLock.lock();
                tvchainUpdate += QDeepCopy<QString>(tokens[2]);
                tvchainUpdateLock.unlock();
            }
        }
        else if (nvp && message.left(12) == "EXIT_TO_MENU")
        {
            int exitprompt = gContext->GetNumSetting("PlaybackExitPrompt");
            if (exitprompt == 1 || exitprompt == 2)
                nvp->SetBookmark();
            if (nvp && gContext->GetNumSetting("AutomaticSetWatched", 0))
                    nvp->SetWatched();
            wantsToQuit = true;
            exitPlayer = true;
        }
        else if (message.left(6) == "SIGNAL")
        {
            int cardnum = (QStringList::split(" ", message))[1].toInt();
            QStringList signalList = me->ExtraDataList();
            bool tc = activerecorder &&
                (activerecorder->GetRecorderNumber() == cardnum);
            if (tc && signalList.size())
            {
                UpdateOSDSignal(signalList);
            }
        }
        else if (recorder && message.left(7) == "SKIP_TO")
        {
            int cardnum = (QStringList::split(" ", message))[1].toInt();
            QStringList keyframe = me->ExtraDataList();
            VERBOSE(VB_IMPORTANT, LOC + "Got SKIP_TO message. Keyframe: "
                    <<stringToLongLong(keyframe[0]));
            bool tc = recorder && (recorder->GetRecorderNumber() == cardnum);
            (void)tc;
        }
        else if (message.left(15) == "NETWORK_CONTROL")
        {
            QStringList tokens = QStringList::split(" ", message);
            if ((tokens[1] != "ANSWER") && (tokens[1] != "RESPONSE"))
            {
                ncLock.lock();
                networkControlCommands.push_back(QDeepCopy<QString>(message));
                ncLock.unlock();
            }
        }
        else if (message.left(6) == "UNMUTE")
        {
            message = message.simplifyWhiteSpace();
            QStringList tokens = QStringList::split(" ", message);
            int timeout = tokens[1].toInt();
            muteTimer->start(timeout, true);
        }
        else if (message.left(9) == "START_EPG")
        {
            message = message.simplifyWhiteSpace();
            QStringList tokens = QStringList::split(" ", message);
            int editType = tokens[1].toInt();
            doEditSchedule(editType);
        }

        pbinfoLock.lock();
        if (playbackinfo && message.left(14) == "COMMFLAG_START")
        {
            message = message.simplifyWhiteSpace();
            QStringList tokens = QStringList::split(" ", message);
            QString evchanid = tokens[1];
            QDateTime evstartts = QDateTime::fromString(tokens[2], Qt::ISODate);

            if ((playbackinfo->chanid == evchanid) &&
                (playbackinfo->recstartts == evstartts))
            {
                QString msg = "COMMFLAG_REQUEST ";
                msg += tokens[1] + " " + tokens[2];
                pbinfoLock.unlock();
                RemoteSendMessage(msg);
                pbinfoLock.lock();
            }
        }
        else if (nvp && playbackinfo && message.left(15) == "COMMFLAG_UPDATE")
        {
            message = message.simplifyWhiteSpace();
            QStringList tokens = QStringList::split(" ", message);
            QString evchanid = tokens[1];
            QDateTime evstartts = QDateTime::fromString(tokens[2], Qt::ISODate);

            if ((playbackinfo->chanid == evchanid) &&
                (playbackinfo->recstartts == evstartts))
            {
                QMap<long long, int> newMap;
                QStringList mark;
                QStringList marks = QStringList::split(",", tokens[3]);
                for (unsigned int i = 0; i < marks.size(); i++)
                {
                    mark = QStringList::split(":", marks[i]);
                    newMap[mark[0].toInt()] = mark[1].toInt();
                }

                nvp->SetCommBreakMap(newMap);
            }
        }
        pbinfoLock.unlock();
    }
}

/** \fn TV::BrowseStart()
 *  \brief Begins channel browsing.
 */
void TV::BrowseStart(void)
{
    if (paused || !GetOSD())
        return;

    OSDSet *oset = GetOSD()->GetSet("browse_info");
    if (!oset)
        return;

    ClearOSD();

    pbinfoLock.lock();
    if (playbackinfo)
    {
        browsemode = true;
        browsechannum = playbackinfo->chanstr;
        browsechanid = playbackinfo->chanid;
        browsestarttime = playbackinfo->startts.toString();

        BrowseDispInfo(BROWSE_SAME);

        browseTimer->start(kBrowseTimeout, true);
    }
    pbinfoLock.unlock();
}

/** \fn TV::BrowseEnd(bool)
 *  \brief Ends channel browsing. Changing the channel if change is true.
 *  \param change iff true we call ChangeChannel()
 */
void TV::BrowseEnd(bool change)
{
    if (!browsemode || !GetOSD())
        return;

    browseTimer->stop();

    GetOSD()->HideSet("browse_info");

    if (change)
    {
        ChangeChannel(0, browsechannum);
    }

    browsemode = false;
}

/** \fn TV::BrowseDispInfo(int)
 *  \brief Fetches browse info from backend and sends it to the OSD.
 *  \param direction BrowseDirection to get information on.
 */
void TV::BrowseDispInfo(int direction)
{
    if (!browsemode)
        BrowseStart();

    InfoMap infoMap;
    QDateTime curtime  = QDateTime::currentDateTime();
    QDateTime maxtime  = curtime.addSecs(60 * 60 * 4);
    QDateTime lasttime = QDateTime::fromString(browsestarttime, Qt::ISODate);

    if (paused || !GetOSD())
        return;

    browseTimer->changeInterval(kBrowseTimeout);

    if (lasttime < curtime)
        browsestarttime = curtime.toString(Qt::ISODate);

    if ((lasttime > maxtime) && (direction == BROWSE_RIGHT))
        return;

    infoMap["dbstarttime"] = browsestarttime;
    infoMap["channum"]     = browsechannum;
    infoMap["chanid"]      = browsechanid;
    
    GetNextProgram(activerecorder, direction, infoMap);
    
    browsechannum = infoMap["channum"];
    browsechanid  = infoMap["chanid"];

    if (((direction == BROWSE_LEFT) || (direction == BROWSE_RIGHT)) &&
        !infoMap["dbstarttime"].isEmpty())
    {
        browsestarttime = infoMap["dbstarttime"];
    }

    QDateTime startts = QDateTime::fromString(browsestarttime, Qt::ISODate);
    ProgramInfo *program_info = 
        ProgramInfo::GetProgramAtDateTime(browsechanid, startts);
    
    if (program_info)
        program_info->ToMap(infoMap);

    GetOSD()->ClearAllText("browse_info");
    GetOSD()->SetText("browse_info", infoMap, -1);

    delete program_info;
}

void TV::SetPseudoLiveTV(uint i, const ProgramInfo *pi, PseudoState new_state)
{
    ProgramInfo *old_rec = pseudoLiveTVRec[i];
    ProgramInfo *new_rec = NULL;

    if (pi)
    {
        new_rec = new ProgramInfo(*pi);
        QString msg = QString("Wants to record: %1 %2 %3 %4")
            .arg(new_rec->title).arg(new_rec->chanstr)
            .arg(new_rec->recstartts.toString())
            .arg(new_rec->recendts.toString());
        VERBOSE(VB_PLAYBACK, LOC + msg);
    }

    pseudoLiveTVRec[i]   = new_rec;
    pseudoLiveTVState[i] = new_state;

    if (old_rec)
    {
        QString msg = QString("Done to recording: %1 %2 %3 %4")
            .arg(old_rec->title).arg(old_rec->chanstr)
            .arg(old_rec->recstartts.toString())
            .arg(old_rec->recendts.toString());
        VERBOSE(VB_PLAYBACK, LOC + msg);        
        delete old_rec;
    }
}

void TV::ToggleRecord(void)
{
    if (browsemode)
    {
        InfoMap infoMap;
        QDateTime startts = QDateTime::fromString(
            browsestarttime, Qt::ISODate);

        ProgramInfo *program_info = 
            ProgramInfo::GetProgramAtDateTime(browsechanid, startts);
        program_info->ToggleRecord();
        program_info->ToMap(infoMap);

        if (GetOSD())
        {
            GetOSD()->ClearAllText("browse_info");
            GetOSD()->SetText("browse_info", infoMap, -1);

            if (activenvp == nvp)
                GetOSD()->SetSettingsText(tr("Record"), 3);
        }
        delete program_info;
        return;
    }

    QMutexLocker lock(&pbinfoLock);

    if (!playbackinfo)
    {
        VERBOSE(VB_GENERAL, LOC + "Unknown recording during live tv.");
        return;
    }

    QString cmdmsg("");
    uint s = (activenvp == nvp) ? 0 : 1;
    if (playbackinfo->GetAutoExpireFromRecorded() == kLiveTVAutoExpire)
    {
        int autoexpiredef = gContext->GetNumSetting("AutoExpireDefault", 0);
        playbackinfo->SetAutoExpire(autoexpiredef);
        playbackinfo->ApplyRecordRecGroupChange("Default");
        cmdmsg = tr("Record");
        SetPseudoLiveTV(s, playbackinfo, kPseudoRecording);
        activerecorder->SetLiveRecording(true);
        VERBOSE(VB_RECORD, LOC + "Toggling Record on");
    }
    else
    {
        playbackinfo->SetAutoExpire(kLiveTVAutoExpire);
        playbackinfo->ApplyRecordRecGroupChange("LiveTV");
        cmdmsg = tr("Cancel Record");
        SetPseudoLiveTV(s, playbackinfo, kPseudoNormalLiveTV);
        activerecorder->SetLiveRecording(false);
        VERBOSE(VB_RECORD, LOC + "Toggling Record off");
    }

    QString msg = cmdmsg + " \"" + playbackinfo->title + "\"";
    if (activenvp == nvp && GetOSD())
        GetOSD()->SetSettingsText(msg, 3);
}

void TV::BrowseChannel(const QString &chan)
{
    if (!activerecorder->CheckChannel(chan))
        return;

    browsechannum = chan;
    browsechanid = QString::null;
    BrowseDispInfo(BROWSE_SAME);
}

void TV::HandleOSDClosed(int osdType)
{
    switch (osdType)
    {
        case kOSDFunctionalType_PictureAdjust:
            adjustingPicture = kAdjustingPicture_None;
            adjustingPictureAttribute = kPictureAttribute_None;
            break;
        case kOSDFunctionalType_SmartForward:
            doSmartForward = false;
            break;
        case kOSDFunctionalType_TimeStretchAdjust:
            stretchAdjustment = false;
            break;
        case kOSDFunctionalType_AudioSyncAdjust:
            audiosyncAdjustment = false;
            break;
        case kOSDFunctionalType_Default:
            break;
    }
}

static PictureAttribute next(
    PictureAdjustType type, NuppelVideoPlayer *nvp, PictureAttribute attr)
{
    uint sup = kPictureAttributeSupported_None;
    if ((kAdjustingPicture_Playback == type) && nvp && nvp->getVideoOutput())
    {
        sup = nvp->getVideoOutput()->GetSupportedPictureAttributes();
        if (nvp->getAudioOutput())
            sup |= kPictureAttributeSupported_Volume;
    }
    else if (kAdjustingPicture_Channel == type)
    {
        sup = (kPictureAttributeSupported_Brightness |
               kPictureAttributeSupported_Contrast |
               kPictureAttributeSupported_Colour |
               kPictureAttributeSupported_Hue);
    }
    else if (kAdjustingPicture_Recording == type)
    {
        sup = (kPictureAttributeSupported_Brightness |
               kPictureAttributeSupported_Contrast |
               kPictureAttributeSupported_Colour |
               kPictureAttributeSupported_Hue);
    }

    return next((PictureAttributeSupported)sup, attr);
}

void TV::DoTogglePictureAttribute(PictureAdjustType type)
{
    PictureAttribute attr = next(type, nvp, adjustingPictureAttribute);
    if (kPictureAttribute_None == attr)
        return;

    adjustingPicture          = type;
    adjustingPictureAttribute = attr;

    QString title = toTitleString(type);

    if (!GetOSD())
        return;

    GetOSD()->GetSet("status");

    int value = 99;
    if (nvp && (kAdjustingPicture_Playback == type))
    {
        if (kPictureAttribute_Volume != adjustingPictureAttribute)
        {
            value = nvp->getVideoOutput()->GetPictureAttribute(attr);
        }
        else if (nvp->getAudioOutput())
        {
            value = nvp->getAudioOutput()->GetCurrentVolume();
            title = tr("Adjust Volume");
        }
    }
    else if (activerecorder && (kAdjustingPicture_Playback != type))
    {
        value = activerecorder->GetPictureAttribute(attr);
    }

    QString text = toString(attr) + " " + toTypeString(type) +
        QString(" %1 %").arg(value);

    GetOSD()->ShowStatus(value * 10, true, title, text, 5,
                         kOSDFunctionalType_PictureAdjust);

    update_osd_pos = false;
}
   
void TV::DoChangePictureAttribute(
    PictureAdjustType type, PictureAttribute attr, bool up)
{
    if (!GetOSD())
        return;

    int value = 99;

    if (nvp && (kAdjustingPicture_Playback == type))
    {
        if (kPictureAttribute_Volume == attr)
        {
            ChangeVolume(up);
            return;
        }
        value = nvp->getVideoOutput()->ChangePictureAttribute(attr, up);
    }
    else if (activerecorder && (kAdjustingPicture_Playback != type))
    {
        value = activerecorder->ChangePictureAttribute(type, attr, up);
    }

    QString text = toString(attr) + " " + toTypeString(type) +
        QString(" %1 %").arg(value);

    GetOSD()->ShowStatus(value * 10, true, toTitleString(type), text, 5,
                         kOSDFunctionalType_PictureAdjust);

    update_osd_pos = false;
}

OSD *TV::GetOSD(void)
{
    if (nvp)
        return nvp->GetOSD();
    return NULL;
}

void TV::TreeMenuEntered(OSDListTreeType *tree, OSDGenericTree *item)
{
    // show help text somewhere, perhaps?
    (void)tree;
    (void)item;
}

/** \fn TV::StartProgramEditMode(void)
 *  \brief Starts Program Cut Map Editing mode
 */
void TV::StartProgramEditMode(void)
{
    pbinfoLock.lock();
    bool isEditing = playbackinfo->IsEditing();
    pbinfoLock.unlock();

    if (isEditing && GetOSD())
    {
        nvp->Pause();

        dialogname = "alreadybeingedited";

        QString message = tr("This program is currently being edited");

        QStringList options;
        options += tr("Continue Editing");
        options += tr("Do not edit");

        GetOSD()->NewDialogBox(dialogname, message, options, 0);
        return;
    }

    editmode = nvp->EnableEdit();
}

static void insert_map(InfoMap &infoMap, const InfoMap &newMap)
{
    InfoMap::const_iterator it = newMap.begin();
    for (; it != newMap.end(); ++it)
        infoMap.insert(it.key(), *it);
}

class load_dd_map
{
  public:
    load_dd_map(TV *t, uint s) : tv(t), sourceid(s) {}
    TV   *tv;
    uint  sourceid;
};

void *TV::load_dd_map_thunk(void *param)
{
    load_dd_map *x = (load_dd_map*) param;
    x->tv->RunLoadDDMap(x->sourceid);
    delete x;
    return NULL;
}

void *TV::load_dd_map_post_thunk(void *param)
{
    uint *sourceid = (uint*) param;
    SourceUtil::UpdateChannelsFromListings(*sourceid);
    delete sourceid;
    return NULL;
}

/** \fn TV::StartChannelEditMode(void)
 *  \brief Starts channel editing mode.
 */
void TV::StartChannelEditMode(void)
{
    if (ddMapLoaderRunning)
    {
        pthread_join(ddMapLoader, NULL);
        ddMapLoaderRunning = false;
    }

    if (!activerecorder || !GetOSD())
        return;

    QMutexLocker locker(&chanEditMapLock);

    // Get the info available from the backend
    chanEditMap.clear();
    activerecorder->GetChannelInfo(chanEditMap);
    chanEditMap["dialog_label"]   = tr("Channel Editor");
    chanEditMap["callsign_label"] = tr("Callsign");
    chanEditMap["channum_label"]  = tr("Channel #");
    chanEditMap["channame_label"] = tr("Channel Name");
    chanEditMap["XMLTV_label"]    = tr("XMLTV ID");
    chanEditMap["probe_all"]      = tr("[P]robe");
    chanEditMap["ok"]             = tr("[O]k");

    // Assuming the data is valid, try to load DataDirect listings.
    uint sourceid = chanEditMap["sourceid"].toUInt();
    if (sourceid && (sourceid != ddMapSourceId))
    {
        ddMapLoaderRunning = true;
        pthread_create(&ddMapLoader, NULL, load_dd_map_thunk,
                       new load_dd_map(this, sourceid));
        return;
    }

    // Update with XDS and DataDirect Info
    ChannelEditAutoFill(chanEditMap);

    // Set proper initial values for channel editor, and make it visible..
    GetOSD()->SetText(dialogname = "channel_editor", chanEditMap, -1);
}

/** \fn TV::ChannelEditKey(const QKeyEvent *e)
 *  \brief Processes channel editing key.
 */
void TV::ChannelEditKey(const QKeyEvent *e)
{
    QMutexLocker locker(&chanEditMapLock);

    bool     focus_change   = false;
    QString  button_pressed = "";
    OSDSet  *osdset         = NULL;

    if (dialogname != "channel_editor")
        return;

    if (GetOSD())
        osdset = GetOSD()->GetSet("channel_editor");

    if (!osdset || !osdset->HandleKey(e, &focus_change, &button_pressed))
        return;

    if (button_pressed == "probe_all")
    {
        InfoMap infoMap;
        osdset->GetText(infoMap);
        ChannelEditAutoFill(infoMap);
        insert_map(chanEditMap, infoMap);
        osdset->SetText(chanEditMap);
    }
    else if (button_pressed == "ok")
    {
        InfoMap infoMap;
        osdset->GetText(infoMap);
        insert_map(chanEditMap, infoMap);
        activerecorder->SetChannelInfo(chanEditMap);
    }

    if (!osdset->Displaying())
    {
        VERBOSE(VB_IMPORTANT, "hiding channel_editor");
        GetOSD()->HideSet("channel_editor");
        dialogname = "";
    }
}

/** \fn TV::ChannelEditAutoFill(InfoMap&) const
 *  \brief Automatically fills in as much information as possible.
 */
void TV::ChannelEditAutoFill(InfoMap &infoMap) const
{
    QMap<QString,bool> dummy;
    ChannelEditAutoFill(infoMap, dummy);
}

/** \fn TV::ChannelEditAutoFill(InfoMap&,const QMap<QString,bool>&) const
 *  \brief Automatically fills in as much information as possible.
 */
void TV::ChannelEditAutoFill(InfoMap &infoMap,
                             const QMap<QString,bool> &changed) const
{
    const QString keys[4] = { "XMLTV", "callsign", "channame", "channum", };

    // fill in uninitialized and unchanged fields from XDS
    ChannelEditXDSFill(infoMap);

    // if no data direct info we're done..
    if (!ddMapSourceId)
        return;

    if (changed.size())
    {
        ChannelEditDDFill(infoMap, changed, false);
    }
    else
    {
        QMutexLocker locker(&chanEditMapLock);
        QMap<QString,bool> chg;
        // check if anything changed
        for (uint i = 0; i < 4; i++)
            chg[keys[i]] = infoMap[keys[i]] != chanEditMap[keys[i]];

        // clean up case and extra spaces
        infoMap["callsign"] = infoMap["callsign"].upper().stripWhiteSpace();
        infoMap["channum"]  = infoMap["channum"].stripWhiteSpace();
        infoMap["channame"] = infoMap["channame"].stripWhiteSpace();
        infoMap["XMLTV"]    = infoMap["XMLTV"].stripWhiteSpace();

        // make sure changes weren't just chaff
        for (uint i = 0; i < 4; i++)
            chg[keys[i]] &= infoMap[keys[i]] != chanEditMap[keys[i]];

        ChannelEditDDFill(infoMap, chg, true);
    }
}

void TV::ChannelEditXDSFill(InfoMap &infoMap) const
{
    QMap<QString,bool> modifiable;
    if (!(modifiable["callsign"] = infoMap["callsign"].isEmpty()))
    {
        QString unsetsign = QObject::tr("UNKNOWN%1", "Synthesized callsign");
        uint    unsetcmpl = unsetsign.length() - 2;
        unsetsign = unsetsign.left(unsetcmpl);
        if (infoMap["callsign"].left(unsetcmpl) == unsetcmpl)
            modifiable["callsign"] = true;
    }
    modifiable["channame"] = infoMap["channame"].isEmpty();

    const QString xds_keys[2] = { "callsign", "channame", };
    for (uint i = 0; i < 2; i++)
    {
        if (!modifiable[xds_keys[i]])
            continue;

        QString tmp = activenvp->GetXDS(xds_keys[i]).upper();
        if (tmp.isEmpty())
            continue;

        if ((xds_keys[i] == "callsign") &&
            ((tmp.length() > 5) || (tmp.find(" ") >= 0)))
        {
            continue;
        }

        infoMap[xds_keys[i]] = tmp;
    }
}

void TV::ChannelEditDDFill(InfoMap &infoMap,
                           const QMap<QString,bool> &changed,
                           bool check_unchanged) const
{
    if (!ddMapSourceId)
        return;

    QMutexLocker locker(&chanEditMapLock);
    const QString keys[4] = { "XMLTV", "callsign", "channame", "channum", };

    // First check changed keys for availability in our listings source.
    // Then, if check_unchanged is set, check unchanged fields.
    QString key = "", dd_xmltv = "";
    uint endj = (check_unchanged) ? 2 : 1;
    for (uint j = 0; (j < endj) && dd_xmltv.isEmpty(); j++)
    {
        for (uint i = 0; (i < 4) && dd_xmltv.isEmpty(); i++)
        {
            key = keys[i];
            if (((j == 1) ^ changed[key]) && !infoMap[key].isEmpty())
                dd_xmltv = GetDataDirect(key, infoMap[key], "XMLTV");
        }
    }

    // If we found the channel in the listings, fill in all the data we have
    if (!dd_xmltv.isEmpty())
    {
        infoMap[keys[0]] = dd_xmltv;
        for (uint i = 1; i < 4; i++)
        {
            QString tmp = GetDataDirect(key, infoMap[key], keys[i]);
            if (!tmp.isEmpty())
                infoMap[keys[i]] = tmp;
        }
        return;
    }

    // If we failed to find an exact match, try partial matches.
    // But only fill the current field since this data is dodgy.
    key = "callsign";
    if (!infoMap[key].isEmpty())
    {
        dd_xmltv = GetDataDirect(key, infoMap[key], "XMLTV", true);
        VERBOSE(VB_IMPORTANT, QString("xmltv: %1 for key %2")
                .arg(dd_xmltv).arg(key));
        if (!dd_xmltv.isEmpty())
            infoMap[key] = GetDataDirect("XMLTV", dd_xmltv, key);
    }

    key = "channame";
    if (!infoMap[key].isEmpty())
    {
        dd_xmltv = GetDataDirect(key, infoMap[key], "XMLTV", true);
        VERBOSE(VB_IMPORTANT, QString("xmltv: %1 for key %2")
                .arg(dd_xmltv).arg(key));
        if (!dd_xmltv.isEmpty())
            infoMap[key] = GetDataDirect("XMLTV", dd_xmltv, key);
    }
}

QString TV::GetDataDirect(QString key, QString value, QString field,
                          bool allow_partial_match) const
{
    QMutexLocker locker(&chanEditMapLock);

    uint sourceid = chanEditMap["sourceid"].toUInt();
    if (!sourceid)
        return QString::null;

    if (sourceid != ddMapSourceId)
        return QString::null;

    DDKeyMap::const_iterator it_key = ddMap.find(key);
    if (it_key == ddMap.end())
        return QString::null;

    DDValueMap::const_iterator it_val = (*it_key).find(value);
    if (it_val != (*it_key).end())
    {
        InfoMap::const_iterator it_field = (*it_val).find(field);
        if (it_field != (*it_val).end())
            return QDeepCopy<QString>(*it_field);
    }

    if (!allow_partial_match || value.isEmpty())
        return QString::null;

    // Check for partial matches.. prefer early match, then short string
    DDValueMap::const_iterator best_match = (*it_key).end();
    int best_match_idx = INT_MAX, best_match_len = INT_MAX;
    for (it_val = (*it_key).begin(); it_val != (*it_key).end(); ++it_val)
    {
        int match_idx = it_val.key().find(value);
        if (match_idx < 0)
            continue;

        int match_len = it_val.key().length();
        if ((match_idx < best_match_idx) && (match_len < best_match_len))
        {
            best_match     = it_val;
            best_match_idx = match_idx;
            best_match_len = match_len;
        }
    }

    if (best_match != (*it_key).end())
    {
        InfoMap::const_iterator it_field = (*best_match).find(field);
        if (it_field != (*it_val).end())
            return QDeepCopy<QString>(*it_field);
    }

    return QString::null;
}

void TV::RunLoadDDMap(uint sourceid)
{
    QMutexLocker locker(&chanEditMapLock);
    const QString keys[4] = { "XMLTV", "callsign", "channame", "channum", };

    // Startup channel editor gui early, with "Loading..." text
    if (GetOSD())
    {
        InfoMap tmp;
        insert_map(tmp, chanEditMap);
        for (uint i = 0; i < 4; i++)
            tmp[keys[i]] = "Loading...";
        GetOSD()->SetText(dialogname = "channel_editor", tmp, -1);
    }

    // Load DataDirect info
    LoadDDMap(sourceid);

    if (dialogname != "channel_editor")
        return;

    // Update with XDS and DataDirect Info
    ChannelEditAutoFill(chanEditMap);

    // Set proper initial values for channel editor, and make it visible..
    if (GetOSD())
        GetOSD()->SetText("channel_editor", chanEditMap, -1);
}

bool TV::LoadDDMap(uint sourceid)
{
    QMutexLocker locker(&chanEditMapLock);
    const QString keys[4] = { "XMLTV", "callsign", "channame", "channum", };

    ddMap.clear();
    ddMapSourceId = 0;

    QString grabber, userid, passwd, lineupid;
    bool ok = SourceUtil::GetListingsLoginData(sourceid, grabber, userid,
                                               passwd, lineupid);
    if (!ok || (grabber != "datadirect"))
    {
        VERBOSE(VB_PLAYBACK, LOC + QString("LoadDDMap() g(%1)").arg(grabber));
        return false;
    }

    DataDirectProcessor ddp(DD_ZAP2IT, userid, passwd);
    ddp.GrabFullLineup(lineupid, true, false, 36*60*60);
    const DDLineupChannels channels = ddp.GetDDLineup(lineupid);

    InfoMap tmp;
    DDLineupChannels::const_iterator it;
    for (it = channels.begin(); it != channels.end(); ++it)
    {
        DDStation station = ddp.GetDDStation((*it).stationid);
        tmp["XMLTV"]    = (*it).stationid;
        tmp["callsign"] = station.callsign;
        tmp["channame"] = station.stationname;
        tmp["channum"]  = (*it).channel;
        if (!(*it).channelMinor.isEmpty())
        {
            tmp["channum"] += SourceUtil::GetChannelSeparator(sourceid);
            tmp["channum"] += (*it).channelMinor;
        }

#if 0
        VERBOSE(VB_GENERAL, QString("Adding channel: %1 -- %2 -- %3 -- %4")
                .arg(tmp["channum"],4).arg(tmp["callsign"],7)
                .arg(tmp["XMLTV"]).arg(tmp["channame"]));
#endif

        for (uint j = 0; j < 4; j++)
            for (uint i = 0; i < 4; i++)
                ddMap[keys[j]][tmp[keys[j]]][keys[i]] = tmp[keys[i]];
    }

    if (!ddMap.empty())
        ddMapSourceId = sourceid;

    return !ddMap.empty();
}

void TV::TreeMenuSelected(OSDListTreeType *tree, OSDGenericTree *item)
{
    if (!tree || !item)
        return;

    bool hidetree = true;
    bool handled  = true;

    QString action = item->getAction();

    if (HandleTrackAction(action))
        ;
    else if (action == "TOGGLEMANUALZOOM")
        SetManualZoom(true);
    else if (action == "TOGGLESTRETCH")
        ToggleTimeStretch();
    else if (action.left(13) == "ADJUSTSTRETCH")
    {
        bool floatRead;
        float stretch = action.right(action.length() - 13).toFloat(&floatRead);
        if (floatRead &&
            stretch <= 2.0 &&
            stretch >= 0.48)
        {
            normal_speed = stretch;   // alter speed before display
        }

        StopFFRew();

        if (paused)
            DoPause();

        ChangeTimeStretch(0, !floatRead);   // just display
    }
    else if (action.left(11) == "SELECTSCAN_")
        activenvp->SetScanType((FrameScanType) action.right(1).toInt());
    else if (action.left(15) == "TOGGLEAUDIOSYNC")
        ChangeAudioSync(0);
    else if (action.left(11) == "TOGGLESLEEP")
    {
        ToggleSleepTimer(action.left(13));
    }
    else if (action.left(17) == "TOGGLEPICCONTROLS")
    {
        adjustingPictureAttribute = (PictureAttribute)
            (action.right(1).toInt() - 1);
        DoTogglePictureAttribute(kAdjustingPicture_Playback);
    }
    else if (action.left(12) == "TOGGLEASPECT")
    {
        ToggleAspectOverride((AspectOverrideMode) action.right(1).toInt());
    }
    else if (action.left(10) == "TOGGLEFILL")
    {
        ToggleAdjustFill((AdjustFillMode) action.right(1).toInt());
    }
    else if (action == "GUIDE")
        EditSchedule(kScheduleProgramGuide);
    else if (action == "FINDER")
        EditSchedule(kScheduleProgramFinder);
    else if (action == "SCHEDULE")
        EditSchedule(kScheduledRecording);
    else if ((action == "JUMPPREV") ||
             ((action == "PREVCHAN") && (!StateIsLiveTV(GetState()))))
    {
        if (PromptRecGroupPassword())
        {
            nvp->SetBookmark();
            exitPlayer = true;
            jumpToProgram = true;
        }
    }
    else if (action == "VIEWSCHEDULED")
        EmbedWithNewThread(kViewSchedule);
    else if (action == "JUMPREC")
    {
        if (gContext->GetNumSetting("JumpToProgramOSD", 1)
            && StateIsPlaying(internalState))
        {
            DisplayJumpMenuSoon();
        }
         else if (RunPlaybackBoxPtr)
            EmbedWithNewThread(kPlaybackBox);
    }
    else if (StateIsLiveTV(GetState()))
    {
        if (action == "TOGGLEPIPMODE")
            TogglePIPView();
        else if (action == "TOGGLEPIPWINDOW")
            ToggleActiveWindow();
        else if (action == "SWAPPIP")
            SwapPIPSoon();
        else if (action == "TOGGLEBROWSE")
            BrowseStart();
        else if (action == "PREVCHAN")
            PreviousChannel();
        else if (action.left(14) == "SWITCHTOINPUT_")
            switchToInputId = action.mid(14).toUInt();
        else
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    "Unknown menu action selected: " + action);
            hidetree = false;
        }
    }
    else if (StateIsPlaying(internalState))
    {
        if (action == "JUMPTODVDROOTMENU")
            activenvp->GoToDVDMenu("menu");
        else if (action == "JUMPTODVDCHAPTERMENU")
            activenvp->GoToDVDMenu("chapter");
        else if (action == "TOGGLEEDIT")
            StartProgramEditMode();
        else if (action == "TOGGLEAUTOEXPIRE")
            ToggleAutoExpire();
        else if (action.left(14) == "TOGGLECOMMSKIP")
            SetAutoCommercialSkip((enum commSkipMode)(action.right(1).toInt()));
        else if (action == "QUEUETRANSCODE")
            DoQueueTranscode("Default");
        else if (action == "QUEUETRANSCODE_AUTO")
            DoQueueTranscode("Autodetect");
        else if (action == "QUEUETRANSCODE_HIGH")
            DoQueueTranscode("High Quality");
        else if (action == "QUEUETRANSCODE_MEDIUM")
            DoQueueTranscode("Medium Quality");
        else if (action == "QUEUETRANSCODE_LOW")
            DoQueueTranscode("Low Quality");
        else if (action.left(8) == "JUMPPROG")
        {
            SetJumpToProgram(action.section(" ",1,-2),
                             action.section(" ",-1,-1).toInt());
            nvp->SetBookmark();
            exitPlayer = true;
            jumpToProgram = true;
        }
        else
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR +
                    "Unknown menu action selected: " + action);
            hidetree = false;
        }
    }

    if (!handled)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Unknown menu action selected: " + action);
        hidetree = false;
    }

    if (hidetree)
    {
        tree->SetVisible(false);
        tree->disconnect();
    }
}

void TV::ShowOSDTreeMenu(void)
{
    BuildOSDTreeMenu();

    if (GetOSD())
    {
        ClearOSD();

        OSDListTreeType *tree = GetOSD()->ShowTreeMenu("menu", treeMenu);
        if (tree)
        {
            connect(tree, SIGNAL(itemSelected(OSDListTreeType *,OSDGenericTree *)), 
                    this, SLOT(TreeMenuSelected(OSDListTreeType *, OSDGenericTree *)));

            connect(tree, SIGNAL(itemEntered(OSDListTreeType *, OSDGenericTree *)),
                    this, SLOT(TreeMenuEntered(OSDListTreeType *, OSDGenericTree *)));
        }
    }
}

void TV::BuildOSDTreeMenu(void)
{
    if (treeMenu)
        delete treeMenu;

    treeMenu = new OSDGenericTree(NULL, "treeMenu");
    OSDGenericTree *item, *subitem;

    if (StateIsLiveTV(GetState()))
        FillMenuLiveTV(treeMenu);
    else if (StateIsPlaying(GetState()))
        FillMenuPlaying(treeMenu);

    FillMenuTracks(treeMenu, kTrackTypeAudio);
    FillMenuTracks(treeMenu, kTrackTypeSubtitle);
    FillMenuTracks(treeMenu, kTrackTypeCC708);

    if (VBIMode::NTSC_CC == vbimode)
        FillMenuTracks(treeMenu, kTrackTypeCC608);
    else if (VBIMode::PAL_TT == vbimode)
    {
        item = new OSDGenericTree(treeMenu, tr("Toggle Teletext Captions"),
                                  "TOGGLETTC");
        item = new OSDGenericTree(treeMenu, tr("Toggle Teletext Menu"),
                                  "TOGGLETTM");
        FillMenuTracks(treeMenu, kTrackTypeTeletextCaptions);
    }

    AspectOverrideMode aspectoverride = nvp->GetAspectOverride();
    item = new OSDGenericTree(treeMenu, tr("Change Aspect Ratio"));
    for (int j = kAspect_Off; j < kAspect_END; j++)
    {
        // swap 14:9 and 16:9
        int i = ((kAspect_14_9 == j) ? kAspect_16_9 :
                 ((kAspect_16_9 == j) ? kAspect_14_9 : j));

 	bool sel = (i != kAspect_Off) ? (aspectoverride == i) :
            (aspectoverride <= kAspect_Off) || (aspectoverride >= kAspect_END);
        subitem = new OSDGenericTree(item, toString((AspectOverrideMode) i),
                                     QString("TOGGLEASPECT%1").arg(i),
                                     (sel) ? 1 : 0, NULL, "ASPECTGROUP");
    }

    AdjustFillMode adjustfill = nvp->GetAdjustFill();
    item = new OSDGenericTree(treeMenu, tr("Adjust Fill"));
    for (int i = kAdjustFill_Off; i < kAdjustFill_END; i++)
    {
 	bool sel = (i != kAdjustFill_Off) ? (adjustfill == i) :
            (adjustfill <= kAdjustFill_Off) || (adjustfill >= kAdjustFill_END);
        subitem = new OSDGenericTree(item, toString((AdjustFillMode) i),
                                     QString("TOGGLEFILL%1").arg(i),
                                     (sel) ? 1 : 0, NULL, "ADJUSTFILLGROUP");
    }

    uint sup = kPictureAttributeSupported_None;
    if (nvp && nvp->getVideoOutput())
        sup = nvp->getVideoOutput()->GetSupportedPictureAttributes();
    item = NULL;
    for (int i = kPictureAttribute_MIN; i < kPictureAttribute_MAX; i++)
    {
        if (toMask((PictureAttribute)i) & sup)
        {
            if (!item)
                item = new OSDGenericTree(treeMenu, tr("Adjust Picture"));
            subitem = new OSDGenericTree(
                item, toString((PictureAttribute) i),
                QString("TOGGLEPICCONTROLS%1").arg(i));
        }
    }

    item = new OSDGenericTree(treeMenu, tr("Manual Zoom Mode"), 
                             "TOGGLEMANUALZOOM");

    item = new OSDGenericTree(treeMenu, tr("Adjust Audio Sync"), "TOGGLEAUDIOSYNC");

    int speedX100 = (int)(round(normal_speed * 100));

    item = new OSDGenericTree(treeMenu, tr("Adjust Time Stretch"), "ADJUSTSTRETCH");
    subitem = new OSDGenericTree(item, tr("Toggle"), "TOGGLESTRETCH");
    subitem = new OSDGenericTree(item, tr("Adjust"), "ADJUSTSTRETCH");
    subitem = new OSDGenericTree(item, tr("0.5X"), "ADJUSTSTRETCH0.5",
                                 (speedX100 == 50) ? 1 : 0, NULL,
                                 "STRETCHGROUP");
    subitem = new OSDGenericTree(item, tr("0.9X"), "ADJUSTSTRETCH0.9",
                                 (speedX100 == 90) ? 1 : 0, NULL,
                                 "STRETCHGROUP");
    subitem = new OSDGenericTree(item, tr("1.0X"), "ADJUSTSTRETCH1.0",
                                 (speedX100 == 100) ? 1 : 0, NULL,
                                 "STRETCHGROUP");
    subitem = new OSDGenericTree(item, tr("1.1X"), "ADJUSTSTRETCH1.1",
                                 (speedX100 == 110) ? 1 : 0, NULL,
                                 "STRETCHGROUP");
    subitem = new OSDGenericTree(item, tr("1.2X"), "ADJUSTSTRETCH1.2",
                                 (speedX100 == 120) ? 1 : 0, NULL,
                                 "STRETCHGROUP");
    subitem = new OSDGenericTree(item, tr("1.3X"), "ADJUSTSTRETCH1.3",
                                 (speedX100 == 130) ? 1 : 0, NULL,
                                 "STRETCHGROUP");
    subitem = new OSDGenericTree(item, tr("1.4X"), "ADJUSTSTRETCH1.4",
                                 (speedX100 == 140) ? 1 : 0, NULL,
                                 "STRETCHGROUP");
    subitem = new OSDGenericTree(item, tr("1.5X"), "ADJUSTSTRETCH1.5",
                                 (speedX100 == 150) ? 1 : 0, NULL,
                                 "STRETCHGROUP");

    // add scan mode override settings to menu
    FrameScanType scan_type = kScan_Ignore;
    bool scan_type_locked = false;
    QString cur_mode = "";
    if (activenvp)
    {
        scan_type = activenvp->GetScanType();
        scan_type_locked = activenvp->IsScanTypeLocked();        
        if (!scan_type_locked)
        {
            if (kScan_Interlaced == scan_type)
                cur_mode = tr("(I)", "Interlaced (Normal)");
            else if (kScan_Intr2ndField == scan_type)
                cur_mode = tr("(i)", "Interlaced (Reversed)");
            else if (kScan_Progressive == scan_type)
                cur_mode = tr("(P)", "Progressive");
            cur_mode = " " + cur_mode;
            scan_type = kScan_Detect;
        }
    }

    item = new OSDGenericTree(
        treeMenu, tr("Video Scan"), "SCANMODE");
    subitem = new OSDGenericTree(
        item, tr("Detect") + cur_mode, "SELECTSCAN_0",
        (scan_type == kScan_Detect) ? 1 : 0, NULL, "SCANGROUP");
    subitem = new OSDGenericTree(
        item, tr("Progressive"), "SELECTSCAN_3",
        (scan_type == kScan_Progressive) ? 1 : 0, NULL, "SCANGROUP");
    subitem = new OSDGenericTree(
        item, tr("Interlaced (Normal)"), "SELECTSCAN_1",
        (scan_type == kScan_Interlaced) ? 1 : 0, NULL, "SCANGROUP");
    subitem = new OSDGenericTree(
        item, tr("Interlaced (Reversed)"), "SELECTSCAN_2",
        (scan_type == kScan_Intr2ndField) ? 1 : 0, NULL, "SCANGROUP");
    
    // add sleep items to menu

    item = new OSDGenericTree(treeMenu, tr("Sleep"), "TOGGLESLEEPON");
    if (sleepTimer->isActive())
        subitem = new OSDGenericTree(item, tr("Sleep Off"), "TOGGLESLEEPON");
    subitem = new OSDGenericTree(item, "30 " + tr("minutes"), "TOGGLESLEEP30");
    subitem = new OSDGenericTree(item, "60 " + tr("minutes"), "TOGGLESLEEP60");
    subitem = new OSDGenericTree(item, "90 " + tr("minutes"), "TOGGLESLEEP90");
    subitem = new OSDGenericTree(item, "120 " + tr("minutes"), "TOGGLESLEEP120");
}

void TV::FillMenuLiveTV(OSDGenericTree *treeMenu)
{
    OSDGenericTree *item, *subitem;

    bool freeRecorders = (pipnvp != NULL);
    if (!freeRecorders)
        freeRecorders = RemoteGetFreeRecorderCount();

    item = new OSDGenericTree(treeMenu, tr("Program Guide"), "GUIDE");

    if (!gContext->GetNumSetting("JumpToProgramOSD", 1))
    {
        item = new OSDGenericTree(treeMenu, tr("Jump to Program"));
        subitem = new OSDGenericTree(item, tr("Recorded Program"), "JUMPREC");
        if (lastProgram != NULL)
            subitem = new OSDGenericTree(item, lastProgram->title, "JUMPPREV");
    }

    if (freeRecorders)
    {
        // Picture-in-Picture
        item = new OSDGenericTree(treeMenu, tr("Picture-in-Picture"));
        subitem = new OSDGenericTree(item, tr("Enable/Disable"),
                                     "TOGGLEPIPMODE");
        subitem = new OSDGenericTree(item, tr("Swap PiP/Main"), "SWAPPIP");
        subitem = new OSDGenericTree(item, tr("Change Active Window"),
                                     "TOGGLEPIPWINDOW");

        // Input switching
        item = NULL;

        QMap<uint,InputInfo> sources;
        vector<uint> cardids = RemoteRequestFreeRecorderList();
        uint         cardid  = activerecorder->GetRecorderNumber();
        cardids.push_back(cardid);
        stable_sort(cardids.begin(), cardids.end());

        vector<uint> excluded_cardids;
        excluded_cardids.push_back(cardid);

        InfoMap info;
        activerecorder->GetChannelInfo(info);
        uint sourceid = info["sourceid"].toUInt();

        vector<uint>::const_iterator it = cardids.begin();
        for (; it != cardids.end(); ++it)
        {
            vector<InputInfo> inputs = RemoteRequestFreeInputList(
                *it, excluded_cardids);

            if (inputs.empty())
                continue;

            if (!item)
                item = new OSDGenericTree(treeMenu, tr("Switch Input"));

            for (uint i = 0; i < inputs.size(); i++)
            {
                // prefer the current card's input in sources list
                if ((sources.find(inputs[i].sourceid) == sources.end()) ||
                    ((cardid == inputs[i].cardid) && 
                     (cardid != sources[inputs[i].sourceid].cardid)))
                {
                    sources[inputs[i].sourceid] = inputs[i];
                }

                // don't add current input to list
                if ((inputs[i].cardid   == cardid) &&
                    (inputs[i].sourceid == sourceid))
                {
                    continue;
                }

                QString name = CardUtil::GetDisplayName(inputs[i].inputid);
                if (name.isEmpty())
                {
                    name = tr("C", "Card") + ":" + QString::number(*it) + " " +
                        tr("I", "Input") + ":" + inputs[i].name;
                }

                subitem = new OSDGenericTree(
                    item, name,
                    QString("SWITCHTOINPUT_%1").arg(inputs[i].inputid));
            }
        }

        // Source switching

        // delete current source from list
        sources.erase(sourceid);

        // create menu if we have any sources left
        QMap<uint,InputInfo>::const_iterator sit = sources.begin();
        if (sit != sources.end())
            item = new OSDGenericTree(treeMenu, tr("Switch Source"));
        for (; sit != sources.end(); ++sit)
        {
            subitem = new OSDGenericTree(
                item, SourceUtil::GetSourceName((*sit).sourceid),
                QString("SWITCHTOINPUT_%1").arg((*sit).inputid));
        }
    }

    if (!persistentbrowsemode)
    {
        item = new OSDGenericTree(
            treeMenu, tr("Enable Browse Mode"), "TOGGLEBROWSE");
    }

    item = new OSDGenericTree(treeMenu, tr("Previous Channel"),
                              "PREVCHAN");
}

void TV::FillMenuPlaying(OSDGenericTree *treeMenu)
{
    OSDGenericTree *item, *subitem;

    if (activerbuffer && activerbuffer->isDVD())
    {
        item = new OSDGenericTree(
            treeMenu, tr("DVD Root Menu"),    "JUMPTODVDROOTMENU");
        item = new OSDGenericTree(
            treeMenu, tr("DVD Chapter Menu"), "JUMPTODVDCHAPTERMENU");

        return;
    }

    item = new OSDGenericTree(treeMenu, tr("Edit Recording"), "TOGGLEEDIT");

    item = new OSDGenericTree(treeMenu, tr("Jump to Program"));

    subitem = new OSDGenericTree(item, tr("Recorded Program"), "JUMPREC");
    if (lastProgram != NULL)
        subitem = new OSDGenericTree(item, lastProgram->title, "JUMPPREV");

    pbinfoLock.lock();

    if (JobQueue::IsJobQueuedOrRunning(
            JOB_TRANSCODE, playbackinfo->chanid, playbackinfo->startts))
    {
        item = new OSDGenericTree(treeMenu, tr("Stop Transcoding"),
                                  "QUEUETRANSCODE");
    }
    else
    {
        item = new OSDGenericTree(treeMenu, tr("Begin Transcoding"));
        subitem = new OSDGenericTree(item, tr("Default"),
                                     "QUEUETRANSCODE");
        subitem = new OSDGenericTree(item, tr("Autodetect"),
                                     "QUEUETRANSCODE_AUTO");
        subitem = new OSDGenericTree(item, tr("High Quality"),
                                     "QUEUETRANSCODE_HIGH");
        subitem = new OSDGenericTree(item, tr("Medium Quality"),
                                     "QUEUETRANSCODE_MEDIUM");
        subitem = new OSDGenericTree(item, tr("Low Quality"),
                                     "QUEUETRANSCODE_LOW");
    }

    item = new OSDGenericTree(treeMenu, tr("Commercial Auto-Skip"));
    subitem = new OSDGenericTree(item, tr("Auto-Skip OFF"),
                                 "TOGGLECOMMSKIP0",
                                 (autoCommercialSkip == 0) ? 1 : 0, NULL,
                                 "COMMSKIPGROUP");
    subitem = new OSDGenericTree(item, tr("Auto-Skip Notify"),
                                 "TOGGLECOMMSKIP2",
                                 (autoCommercialSkip == 2) ? 1 : 0, NULL,
                                 "COMMSKIPGROUP");
    subitem = new OSDGenericTree(item, tr("Auto-Skip ON"),
                                 "TOGGLECOMMSKIP1",
                                 (autoCommercialSkip == 1) ? 1 : 0, NULL,
                                 "COMMSKIPGROUP");

    if (playbackinfo->GetAutoExpireFromRecorded())
    {
        item = new OSDGenericTree(treeMenu, tr("Turn Auto-Expire OFF"),
                                  "TOGGLEAUTOEXPIRE");
    }
    else
    {
        item = new OSDGenericTree(treeMenu, tr("Turn Auto-Expire ON"),
                                  "TOGGLEAUTOEXPIRE");
    }

    pbinfoLock.unlock();

    item = new OSDGenericTree(treeMenu, tr("Schedule Recordings"));
    subitem = new OSDGenericTree(item, tr("Program Guide"), "GUIDE");
    subitem = new OSDGenericTree(item, tr("Upcoming Recordings"), 
                                "VIEWSCHEDULED");
    subitem = new OSDGenericTree(item, tr("Program Finder"), "FINDER");
    subitem = new OSDGenericTree(item, tr("Edit Recording Schedule"),
                                 "SCHEDULE");
}

bool TV::FillMenuTracks(OSDGenericTree *treeMenu, uint type)
{
    QString mainMsg = QString::null;
    QString selStr  = QString::null;
    QString grpStr  = QString::null;
    QString typeStr = QString::null;
    bool    sel     = true;

    if (kTrackTypeAudio == type)
    {
        mainMsg = tr("Select Audio Track");
        typeStr = "AUDIO";
        selStr  = "SELECTAUDIO_";
        grpStr  = "AUDIOGROUP";
    }
    else if (kTrackTypeSubtitle == type)
    {
        mainMsg = tr("Select Subtitle");
        typeStr = "SUBTITLE";
        selStr  = "SELECTSUBTITLE_";
        grpStr  = "SUBTITLEGROUP";
        sel     = activenvp->GetCaptionMode() & kDisplayAVSubtitle;
    }
    else if (kTrackTypeCC608 == type)
    {
        mainMsg = tr("Select VBI CC");
        typeStr = "CC608";
        selStr  = "SELECTCC608_";
        grpStr  = "CC608GROUP";
        sel     = activenvp->GetCaptionMode() & kDisplayCC608;
    }
    else if (kTrackTypeCC708 == type)
    {
        mainMsg = tr("Select ATSC CC");
        typeStr = "CC708";
        selStr  = "SELECTCC708_";
        grpStr  = "CC608GROUP";
        sel     = activenvp->GetCaptionMode() & kDisplayCC708;
    }
    else if (kTrackTypeTeletextCaptions == type)
    {
        mainMsg = tr("Select DVB CC");
        typeStr = "TTC";
        selStr  = "SELECTTTC_";
        grpStr  = "TTCGROUP";
        sel     = activenvp->GetCaptionMode() & kTrackTypeTeletextCaptions;
    }
    else
    {
        return false;
    }
    
    const QStringList tracks = activenvp->GetTracks(type);
    if (tracks.empty())
        return false;

    if ((kTrackTypeAudio == type) && tracks.size() <= 1)
        return false;

    OSDGenericTree *item = new OSDGenericTree(
        treeMenu, mainMsg, "DUMMY" + QString::number(type));

    if (kTrackTypeAudio != type)
        new OSDGenericTree(item, tr("Toggle On/Off"), "TOGGLE"+typeStr);

    uint curtrack = (uint) activenvp->GetTrack(type);
    for (uint i = 0; i < tracks.size(); i++)
    {
        new OSDGenericTree(
            item, tracks[i], selStr + QString::number(i),
            (sel && (i == curtrack)) ? 1 : 0, NULL, grpStr);
    }
    return true;
}

void TV::ChangeTrack(uint type, int dir)
{
    if (!activenvp)
        return;

    int new_track = activenvp->ChangeTrack(type, dir) + 1;
    if (new_track && GetOSD())
    {
        QString msg = track_type_to_string(type) + " " +
            QString::number(new_track);
        GetOSD()->SetSettingsText(msg, 3);
    }
}

void TV::SetTrack(uint type, int track)
{
    if (!activenvp)
        return;

    int new_track = activenvp->SetTrack(type, track - 1) + 1;
    if (new_track && GetOSD())
    {
        QString msg = track_type_to_string(type) + " " +
            QString::number(new_track);
        GetOSD()->SetSettingsText(msg, 3);
    }
}

void TV::ToggleAutoExpire(void)
{
    QString desc = "";

    pbinfoLock.lock();

    if (playbackinfo->GetAutoExpireFromRecorded())
    {
        playbackinfo->SetAutoExpire(0);
        desc = tr("Auto-Expire OFF");
    }
    else
    {
        playbackinfo->SetAutoExpire(1);
        desc = tr("Auto-Expire ON");
    }

    pbinfoLock.unlock();

    if (GetOSD() && activenvp == nvp && desc != "" )
    {
        struct StatusPosInfo posInfo;
        nvp->calcSliderPos(posInfo);
        GetOSD()->ShowStatus(posInfo, false, desc, 1);
        update_osd_pos = false;
    }
}

void TV::SetAutoCommercialSkip(enum commSkipMode skipMode)
{
    QString desc = "";

    autoCommercialSkip = skipMode;

    if (autoCommercialSkip == CommSkipOff)
        desc = tr("Auto-Skip OFF");
    else if (autoCommercialSkip == CommSkipOn)
        desc = tr("Auto-Skip ON");
    else if (autoCommercialSkip == CommSkipNotify)
        desc = tr("Auto-Skip Notify");

    nvp->SetAutoCommercialSkip((int)autoCommercialSkip);

    if (GetOSD() && activenvp == nvp && desc != "" )
    {
        struct StatusPosInfo posInfo;
        nvp->calcSliderPos(posInfo);
        GetOSD()->ShowStatus(posInfo, false, desc, 1);
        update_osd_pos = false;
    }
}

void TV::SetManualZoom(bool zoomON)
{
    QString desc = "";

    zoomMode = zoomON;
    if (zoomON)
    {
        ClearOSD();
        desc = tr("Zoom Mode ON");
    }
    else
        desc = tr("Zoom Mode OFF");

    if (GetOSD() && activenvp == nvp && desc != "" )
    {
        struct StatusPosInfo posInfo;
        nvp->calcSliderPos(posInfo);
        GetOSD()->ShowStatus(posInfo, false, desc, 1);
        update_osd_pos = false;
    }
}

void TV::SetJumpToProgram(QString progKey, int progIndex)
{
    QMap<QString,ProgramList>::Iterator Iprog;
    Iprog = progLists.find(progKey);
    ProgramList plist = Iprog.data();
    ProgramInfo *p = plist.at(progIndex);
    VERBOSE(VB_IMPORTANT, QString("Switching to program: %1: %2").arg(p->title).arg(p->subtitle));
    setLastProgram(p);
}

void TV::ToggleSleepTimer(const QString time)
{
    const int minute = 60*1000; /* milliseconds in a minute */
    int mins = 0;

    if (!sleepTimer)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "No sleep timer?");
        return;
    }

    if (time == "TOGGLESLEEPON")
    {
        if (sleepTimer->isActive())
            sleepTimer->stop();
        else
        {
            mins = 60;
            sleepTimer->start(mins *minute);
        }
    }
    else
    {
        if (time.length() > 11)
        {
            bool intRead = false;
            mins = time.right(time.length() - 11).toInt(&intRead);

            if (intRead)
            {
                // catch 120 -> 240 mins
                if (mins < 30)
                {
                    mins *= 10;
                }
            }
            else
            {
                mins = 0;
                VERBOSE(VB_IMPORTANT, LOC_ERR + "Invalid time "<<time);
            }
        }
        else
        {
            VERBOSE(VB_IMPORTANT, LOC_ERR + "Invalid time string "<<time);
        }

        if (sleepTimer->isActive())
        {
            sleepTimer->stop();
        }

        if (mins)
            sleepTimer->start(mins * minute);
    }
               
    // display OSD
    if (GetOSD() && !browsemode)
    {
        QString out;

        if (mins != 0)
            out = tr("Sleep") + " " + QString::number(mins);
        else
            out = tr("Sleep") + " " + sleep_times[0].dispString;

        GetOSD()->SetSettingsText(out, 3);
    }
}

/** \fn TV::GetValidRecorderList(uint)
 *  \brief Returns list of the recorders that have chanid in their sources.
 *  \param chanid  Channel ID of channel we are querying recorders for.
 *  \return List of cardid's for recorders with channel.
 */
QStringList TV::GetValidRecorderList(uint chanid)
{
    QStringList reclist;

    // Query the database to determine which source is being used currently.
    // set the EPG so that it only displays the channels of the current source
    MSqlQuery query(MSqlQuery::InitCon());
    // We want to get the current source id for this recorder
    query.prepare(
        "SELECT cardinput.cardid "
        "FROM channel "
        "LEFT JOIN cardinput ON channel.sourceid = cardinput.sourceid "
        "WHERE channel.chanid = :CHANID");
    query.bindValue(":CHANID", chanid);

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("GetValidRecorderList ChanID", query);
        return reclist;
    }

    while (query.next())
        reclist << query.value(0).toString();

    return reclist;
}

/** \fn TV::GetValidRecorderList(const QString&)
 *  \brief Returns list of the recorders that have channum in their sources.
 *  \param channum  Channel "number" we are querying recorders for.
 *  \return List of cardid's for recorders with channel.
 */
QStringList TV::GetValidRecorderList(const QString &channum)
{
    QStringList reclist;

    // Query the database to determine which source is being used currently.
    // set the EPG so that it only displays the channels of the current source
    MSqlQuery query(MSqlQuery::InitCon());
    // We want to get the current source id for this recorder
    query.prepare(
        "SELECT cardinput.cardid "
        "FROM channel "
        "LEFT JOIN cardinput ON channel.sourceid = cardinput.sourceid "
        "WHERE channel.channum = :CHANNUM");
    query.bindValue(":CHANNUM", channum);

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("GetValidRecorderList ChanNum", query);
        return reclist;
    }

    while (query.next())
        reclist << query.value(0).toString();

    return reclist;
}

/** \fn TV::GetValidRecorderList(uint, const QString&)
 *  \brief Returns list of the recorders that have chanid or channum
 *         in their sources.
 *  \param chanid   Channel ID of channel we are querying recorders for.
 *  \param channum  Channel "number" we are querying recorders for.
 *  \return List of cardid's for recorders with channel.
 */
QStringList TV::GetValidRecorderList(uint chanid, const QString &channum)
{
    if (chanid)
        return GetValidRecorderList(chanid);
    else if (!channum.isEmpty())
        return GetValidRecorderList(channum);
    return QStringList();
}

void TV::ShowNoRecorderDialog(void)
{
    QString errorText = tr("MythTV is already using all available "
                           "inputs for the channel you selected. "
                           "If you want to watch an in-progress recording, "
                           "select one from the playback menu.  If you "
                           "want to watch live TV, cancel one of the "
                           "in-progress recordings from the delete "
                           "menu.");
    if (embedWinID)
    {
        VERBOSE(VB_IMPORTANT, errorText);
    }
    else if (GetOSD())
    {
        dialogname = "infobox";
        QStringList options("OK");
        GetOSD()->NewDialogBox(dialogname, errorText, options, 0);
    }
    else
    {
        MythPopupBox::showOkPopup(
            gContext->GetMainWindow(), QObject::tr("Channel Change Error"),
            errorText);
    }
}

/** \fn TV::PauseLiveTV(void)
 *  \brief Used in ChangeChannel(), ChangeChannel(),
 *         and ToggleInputs() to temporarily stop video output.
 */
void TV::PauseLiveTV(void)
{
    VERBOSE(VB_PLAYBACK, LOC + "PauseLiveTV()");
    lockTimerOn = false;

    if (activenvp && activerbuffer)
    {
        activerbuffer->IgnoreLiveEOF(true);
        activerbuffer->StopReads();
        activenvp->PauseDecoder();
        activerbuffer->StartReads();
    }

    // XXX: Get rid of this?
    activerecorder->PauseRecorder();

    osdlock.lock();
    lastSignalMsg.clear();
    lastSignalUIInfo.clear();
    osdlock.unlock();

    lockTimerOn = false;

    QString input = activerecorder->GetInput();
    uint timeout  = activerecorder->GetSignalLockTimeout(input);
    if (timeout < 0xffffffff)
    {
        lockTimer.start();
        lockTimerOn = true;
    }
}

/** \fn TV::UnpauseLiveTV(void)
 *  \brief Used in ChangeChannel(), ChangeChannel(),
 *         and ToggleInputs() to restart video output.
 */
void TV::UnpauseLiveTV(void)
{
    VERBOSE(VB_PLAYBACK, LOC + "UnpauseLiveTV()");

    LiveTVChain *chain = (activenvp == nvp) ? tvchain : piptvchain;

    if (activenvp && chain)
    {
        chain->ReloadAll();
        ProgramInfo *pginfo = chain->GetProgramAt(-1);
        if (pginfo)
        {
            SetCurrentlyPlaying(pginfo);
            delete pginfo;
        }

        chain->JumpTo(-1, 1);

        activenvp->Play(normal_speed, true, false);
        activerbuffer->IgnoreLiveEOF(false);
    }

    ITVRestart(true);

    if (!nvp || (nvp && activenvp == nvp))
    {
        UpdateOSDProgInfo("program_info");
        UpdateLCD();
        AddPreviousChannel();
    }
}

void TV::SetCurrentlyPlaying(ProgramInfo *pginfo)
{
    pbinfoLock.lock();

    if (playbackinfo)
        delete playbackinfo;
    playbackinfo = NULL;

    if (pginfo)
        playbackinfo = new ProgramInfo(*pginfo);

    pbinfoLock.unlock();
}

/* \fn TV::ITVRestart(bool isLive)
 * \brief Restart the MHEG/MHP engine.
 */
void TV::ITVRestart(bool isLive)
{
    uint chanid = 0;
    uint cardid = 0;

    if (activenvp != nvp || paused || !GetOSD())
        return;

    pbinfoLock.lock();
    if (playbackinfo)
        chanid = playbackinfo->chanid.toUInt();

    if (activerecorder)
        cardid = activerecorder->GetRecorderNumber();

    pbinfoLock.unlock();

    nvp->ITVRestart(chanid, cardid, isLive);
}

/* \fn TV::ScreenShot(long long frameNumber)
 * \brief Creates an image of a particular frame from the current
 *        playbackinfo recording.
 */
bool TV::ScreenShot(long long frameNumber)
{
    pbinfoLock.lock();
    if (!playbackinfo)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "ScreenShot called with NULL playbackinfo");

        pbinfoLock.unlock();
        return false;
    }

    QString outFile =
        QString("%1/.mythtv/%2_%3_%4.png")
        .arg(QDir::homeDirPath()).arg(playbackinfo->chanid)
        .arg(playbackinfo->recstartts.toString("yyyyMMddhhmmss"))
        .arg((long)frameNumber);

    PreviewGenerator *previewgen = new PreviewGenerator(playbackinfo, false);
    pbinfoLock.unlock();

    previewgen->SetPreviewTimeAsFrameNumber(frameNumber);
    previewgen->SetOutputSize(QSize(-1,-1));
    previewgen->SetOutputFilename(outFile);
    bool ok = previewgen->Run();
    previewgen->deleteLater();

    QString msg = tr("Screen Shot") + " " + ((ok) ? tr("OK") : tr("Error"));

    if (nvp && (activenvp == nvp) && GetOSD())
        GetOSD()->SetSettingsText(msg, 3);

    return ok;
}

/* \fn TV::LoadExternalSubtitles(NuppelVideoPlayer*, const QString&)
 * \brief Loads any external subtitles.
 *
 *  This tries to find an external subtitle file in the same directory
 *  in which the video file is. It then tries to parse each found
 *  candidate file until one is parsed succesfully.
 */
bool TV::LoadExternalSubtitles(NuppelVideoPlayer *nvp,
                               const QString &videoFile)
{
    if (videoFile.isEmpty())
        return false;

    QString fileName = videoFile;
    QString dirName  = ".";

    int dirPos = videoFile.findRev(QChar('/'));
    if (dirPos > 0) 
    {
        fileName = videoFile.mid(dirPos + 1);
        dirName = videoFile.left(dirPos);
    }

    QString baseName = fileName;
    int suffixPos = fileName.findRev(QChar('.'));
    if (suffixPos > 0) 
        baseName = fileName.left(suffixPos);

    // The dir listing does not work if the filename has the following chars,
    // so we convert them to the wildcard '?'
    baseName = baseName.replace("[", "?").replace("]", "?");
    baseName = baseName.replace("(", "?").replace(")", "?");

    // Some Qt versions do not accept paths in the search string of
    // entryList() so we have to set the dir first
    QDir dir;
    dir.setPath(dirName);

    // Try to find files with the same base name, but ending with
    // '.srt', '.sub', or '.txt'
    QStringList candidates = dir.entryList(
        baseName + "*.srt; " + baseName + "*.sub; " + baseName + "*.txt;");

    bool found = false;
    QString candidate = "";
    QStringList::const_iterator it = candidates.begin();
    for (; (it != candidates.end()) && !found; ++it)
    {
        candidate = dirName + "/" + *it;
        if (nvp->LoadExternalSubtitles(candidate))
            found = true;
    }

    if (found)
    {
        VERBOSE(VB_PLAYBACK, LOC +
                QString("Loaded text subtitles from '%1'.").arg(candidate));
    }

    return found;
}

/* \fn TV::DVDJumpForward(void)
 * \brief jump to the previous dvd title or chapter
 */
void TV::DVDJumpBack(void)
{
    if (!activerbuffer || !activenvp || !activerbuffer->isDVD())
        return;
               
    if (activerbuffer->InDVDMenuOrStillFrame())
        UpdateOSDSeekMessage(tr("Skip Back Not Allowed"),
                                osd_general_timeout);
    else if (!activerbuffer->DVD()->StartOfTitle())
    {
        activenvp->ChangeDVDTrack(0);
        UpdateOSDSeekMessage(tr("Previous Chapter"),
                                osd_general_timeout);
    }
    else
    {
        uint titleLength = activerbuffer->DVD()->GetTotalTimeOfTitle();
        uint chapterLength = activerbuffer->DVD()->GetChapterLength();
        if ((titleLength == chapterLength) && chapterLength > 300)
        {
            DoSeek(-jumptime * 60, tr("Jump Back"));
        }
        else
        {
            activenvp->GoToDVDProgram(0);
            UpdateOSDSeekMessage(tr("Previous Title"),
                                    osd_general_timeout);
        }
    }
}

/* \fn TV::DVDJumpForward(void)
 * \brief jump to the next dvd title or chapter
 */
void TV::DVDJumpForward(void)
{
    if (!activerbuffer->isDVD())
        return;
    if (activerbuffer->DVD()->InStillFrame())
    {
        activerbuffer->DVD()->SkipStillFrame();
        UpdateOSDSeekMessage(tr("Skip Still Frame"),
                                osd_general_timeout);
    }
    else if (!activerbuffer->DVD()->EndOfTitle())
    {
        activenvp->ChangeDVDTrack(1);
        UpdateOSDSeekMessage(tr("Next Chapter"),
                                osd_general_timeout);
    }
    else if (!activerbuffer->DVD()->NumMenuButtons())
    {
        uint titleLength = activerbuffer->DVD()->GetTotalTimeOfTitle();
        uint chapterLength = activerbuffer->DVD()->GetChapterLength();
        uint currentTime = activerbuffer->DVD()->GetCurrentTime();
        if ((titleLength == chapterLength) &&
            (currentTime < (chapterLength - (jumptime * 60))) &&
            chapterLength > 300)
        {
            DoSeek(jumptime * 60, tr("Jump Ahead"));
        }
        else
        {
            activenvp->GoToDVDProgram(1);
            UpdateOSDSeekMessage(tr("Next Title"),
                                osd_general_timeout);
        }
    }
}

/* \fn TV::BookmarkAllowed(void)
 * \brief Returns true if bookmarks are allowed for the current player.
 */
bool TV::BookmarkAllowed(void)
{
    // Allow bookmark of "Record current LiveTV program"
    if (StateIsLiveTV(GetState()) && playbackinfo &&
        (playbackinfo->GetAutoExpireFromRecorded() == kLiveTVAutoExpire))
        return false;

    if (StateIsLiveTV(GetState()) && ! playbackinfo)
        return false;

    if ((prbuffer->isDVD() && (!gContext->GetNumSetting("EnableDVDBookmark", 0)
        || prbuffer->DVD()->GetTotalTimeOfTitle() < 120)))
        return false;

    return true;
}

/* \fn TV::DeleteAllowed(void)
 * \brief Returns true if the delete menu option should be offered.
 */
bool TV::DeleteAllowed(void)
{
    if (prbuffer->isDVD() || StateIsLiveTV(GetState()) || 
        (playbackinfo && playbackinfo->isVideo))
        return false;
    return true;
}

void
TV::PromptStopWatchingRecording(void)
{
    ClearOSD();

    if (!paused)
        DoPause(false);
    
    dialogboxTimer.restart();
    QString message;
    QString videotype;
    QStringList options;

    if (StateIsLiveTV(GetState()))
        videotype = "Live TV";
    else if (prbuffer->isDVD())
        videotype = "this DVD";
    else if (playbackinfo->isVideo)
        videotype = "this Video";
    else
        videotype = "this recording";

    message = tr("You are exiting %1").arg(videotype);
    
    if (playbackinfo && BookmarkAllowed())
    {
        options += tr("Save this position and go to the menu");
        options += tr("Do not save, just exit to the menu");
    }
    else
        options += tr("Exit %1").arg(videotype);

    if (playbackinfo && DeleteAllowed())
        options += tr("Delete this recording");

    options += tr("Keep watching");
    dialogname = "exitplayoptions";

    if (GetOSD())
        GetOSD()->NewDialogBox(dialogname, message, options, 0);
}

void TV::PromptDeleteRecording(QString title)
{
    if (playbackinfo->isVideo ||
        doing_ff_rew ||
        StateIsLiveTV(internalState) ||
        exitPlayer)
    {
        return;
    }

    ClearOSD();

    if (!paused)
        DoPause(false);

    dialogboxTimer.restart();
    
    if (dialogname == "")
    {
        QMap<QString, QString> infoMap;
        playbackinfo->ToMap(infoMap);
        QString message = QString("%1\n%2\n%3")
                                  .arg(title).arg(infoMap["title"])
                                  .arg(infoMap["timedate"]);
        QStringList options;
        if (title == "End Of Recording")
        {
            options += "Delete it, but allow it to re-record";
            options += "Delete it";
            options += "Save it so I can watch it again";
        }
        else
        {
            options += "Yes, and allow re-record";
            options += "Yes, delete it";
            options += "No, keep it, I changed my mind";
        }
        
        dialogname = "askdeleterecording";
        
        if (GetOSD())
            GetOSD()->NewDialogBox(dialogname, message, options, 0, 2);
    }
}

bool TV::IsVideoExitDialog(void)
{
    if (dialogname == "")
        return false;
    
    return (dialogname == "askdeleterecording" ||
            dialogname == "exitplayoptions");
}

void TV::setLastProgram(ProgramInfo *rcinfo)
{
    if (lastProgram)
        delete lastProgram;

    if (rcinfo)
        lastProgram = new ProgramInfo(*rcinfo);
    else
        lastProgram = NULL;
}

bool TV::IsSameProgram(ProgramInfo *rcinfo)
{
    if (rcinfo && playbackinfo)
        return playbackinfo->IsSameProgram(*rcinfo);

    return false;
}

bool TV::PromptRecGroupPassword(void)
{
    if (!lastProgram)
        return false;
  
    bool stayPaused = paused;
    if (!paused)
        DoPause(false);
    QString recGroupPassword;
    lastProgram->UpdateRecGroup();
    recGroupPassword = ProgramInfo::GetRecGroupPassword(lastProgram->recgroup);
    if (recGroupPassword != "")
    {
        qApp->lock();
        bool ok = false;
        QString text = tr("'%1' Group Password:")
            .arg(lastProgram->recgroup);
        MythPasswordDialog *pwd = new MythPasswordDialog(text, &ok,
                                                recGroupPassword,
                                                gContext->GetMainWindow());
        pwd->exec();
        pwd->deleteLater();
        pwd = NULL;

        qApp->unlock();
        if (!ok)
        {
            if (GetOSD())
            {
                QString msg = tr("Password Failed");
                GetOSD()->SetSettingsText(msg, 3);
            }
            if (paused && !stayPaused)
                DoPause(false);
            return false;
        }
    }
    if (paused && !stayPaused)
        DoPause(false);
    return true;
}

void TV::DoDisplayJumpMenu(void)
{
    if (treeMenu)
        delete treeMenu;

     treeMenu = new OSDGenericTree(NULL, "treeMenu"); 
     OSDGenericTree *item, *subitem; 
   
     // Build jumpMenu of recorded program titles 
        ProgramInfo *p; 
    progLists.clear(); 
    vector<ProgramInfo *> *infoList; 
    infoList = RemoteGetRecordedList(false); 
   
    //bool LiveTVInAllPrograms = gContext->GetNumSetting("LiveTVInAllPrograms",0); 
    if (infoList) 
    { 
        pbinfoLock.lock(); 
        vector<ProgramInfo *>::iterator i = infoList->begin(); 
        for ( ; i != infoList->end(); i++) 
        { 
            p = *i; 
            //if (p->recgroup != "LiveTV" || LiveTVInAllPrograms) 
            if (p->recgroup == playbackinfo->recgroup) 
                progLists[p->title].prepend(p); 
        } 
        pbinfoLock.unlock(); 
  
        QMap<QString,ProgramList>::Iterator Iprog; 
        for (Iprog = progLists.begin(); Iprog != progLists.end(); Iprog++) 
        { 
            ProgramList plist = Iprog.data(); 
            int progIndex = plist.count(); 
            if (progIndex == 1) 
            {  
                item = new OSDGenericTree(treeMenu, tr(Iprog.key()), 
                    QString("JUMPPROG %1 0").arg(Iprog.key())); 
            }  
            else  
            { 
                item = new OSDGenericTree(treeMenu, tr(Iprog.key())); 
                for (int i = 0; i < progIndex; i++) 
                { 
                    p = plist.at(i); 
                    if (p->subtitle != "") 
                        subitem = new OSDGenericTree(item, tr(p->subtitle),  
                            QString("JUMPPROG %1 %2").arg(Iprog.key()).arg(i)); 
                    else  
                        subitem = new OSDGenericTree(item, tr(p->title),  
                            QString("JUMPPROG %1 %2").arg(Iprog.key()).arg(i)); 
                } 
            } 
        } 
    }  
       
    if (GetOSD()) 
    { 
        ClearOSD(); 
    
        OSDListTreeType *tree = GetOSD()->ShowTreeMenu("menu", treeMenu); 
        if (tree) 
        { 
            connect(tree, SIGNAL(itemSelected(OSDListTreeType *,OSDGenericTree *)),  
                this, SLOT(TreeMenuSelected(OSDListTreeType *, OSDGenericTree *))); 

            connect(tree, SIGNAL(itemEntered(OSDListTreeType *, OSDGenericTree *)), 
                this, SLOT(TreeMenuEntered(OSDListTreeType *, OSDGenericTree *))); 
        } 
    }  
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
