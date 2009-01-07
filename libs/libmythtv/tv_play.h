#ifndef TVPLAY_H
#define TVPLAY_H

#include <qstring.h>
#include <qmap.h>
#include <qdatetime.h>
#include <pthread.h>
#include <qvaluevector.h>
#include <qvaluelist.h>
#include <qptrlist.h>
#include <qmutex.h>
#include <qstringlist.h>
#include <qregexp.h>
#include <qwaitcondition.h>

#include "mythdeque.h"
#include "tv.h"
#include "util.h"
#include "programinfo.h"
#include "channelutil.h"
#include "videoouttypes.h"
#include "inputinfo.h"

#include <qobject.h>

#include <vector>
using namespace std;

class QDateTime;
class OSD;
class RemoteEncoder;
class NuppelVideoPlayer;
class RingBuffer;
class ProgramInfo;
class MythDialog;
class UDPNotify;
class OSDListTreeType;
class OSDGenericTree;
class LiveTVChain;

typedef QValueVector<QString>    str_vec_t;
typedef QMap<QString,QString>    InfoMap;
typedef QMap<QString,InfoMap>    DDValueMap;
typedef QMap<QString,DDValueMap> DDKeyMap;
typedef ProgramInfo * (*RUNPLAYBACKBOX)(void *, bool);
typedef void (*RUNVIEWSCHEDULED) (void *, bool);

class VBIMode
{
  public:
    typedef enum 
    {
        None    = 0,
        PAL_TT  = 1,
        NTSC_CC = 2,
    } vbimode_t;

    static uint Parse(QString vbiformat)
    {
        QString fmt = vbiformat.lower().left(4);
        vbimode_t mode;
        mode = (fmt == "pal ") ? PAL_TT : ((fmt == "ntsc") ? NTSC_CC : None);
        return (uint) mode;
    }
};

typedef enum
{
    kPseudoNormalLiveTV  = 0,
    kPseudoChangeChannel = 1,
    kPseudoRecording     = 2,
} PseudoState;

enum scheduleEditTypes {
    kScheduleProgramGuide = 0,
    kScheduleProgramFinder,
    kScheduledRecording,
    kViewSchedule,
    kPlaybackBox
};

typedef enum
{
    kAskAllowCancel,
    kAskAllowOneRec,
    kAskAllowMultiRec,
} AskAllowType;

class AskProgramInfo
{
  public:
    AskProgramInfo() : info(NULL) {}
    AskProgramInfo(const QDateTime &e, bool r, bool l, ProgramInfo *i) :
        expiry(e), has_rec(r), has_later(l),
        is_in_same_input_group(false), is_conflicting(false),
        info(i) {}

    QDateTime    expiry;
    bool         has_rec;
    bool         has_later;
    bool         is_in_same_input_group;
    bool         is_conflicting;
    ProgramInfo *info;
};

class MPUBLIC TV : public QObject
{
    Q_OBJECT
  public:
    /// \brief Helper class for Sleep Timer code.
    class SleepTimerInfo
    {
      public:
        SleepTimerInfo(QString str, unsigned long secs)
            : dispString(str), seconds(secs) { ; }
        QString   dispString;
        unsigned long seconds;
    };

    TV(void);
   ~TV();

    bool Init(bool createWindow = true);

    // User input processing commands
    void ProcessKeypress(QKeyEvent *e);
    void ProcessNetworkControlCommand(const QString &command);
    void customEvent(QCustomEvent *e);
    bool HandleTrackAction(const QString &action);

    // LiveTV commands
    int  LiveTV(bool showDialogs = true, bool startInGuide = false);
    /// This command is used to exit the player in order to record using
    /// the recording being used by LiveTV.
    void StopLiveTV(void) { exitPlayer = true; }
    void AddPreviousChannel(void);
    void PreviousChannel(void);

    // Embedding commands for the guidegrid to use in LiveTV
    void EmbedOutput(WId wid, int x, int y, int w, int h);
    void StopEmbeddingOutput(void);
    bool IsEmbedding(void);
    bool IsTunable(uint chanid, bool use_cache = false);
    void ClearTunableCache(void);
    void ChangeChannel(const DBChanList &options);

    void DrawUnusedRects(bool sync);
   
    // Recording commands
    int  PlayFromRecorder(int recordernum);
    int  Playback(ProgramInfo *rcinfo);

    // Various commands
    void setLastProgram(ProgramInfo *rcinfo); 
    ProgramInfo *getLastProgram(void) { return lastProgram; }
    ProgramInfo *getCurrentProgram(void) { return playbackinfo; }
    void setInPlayList(bool setting) { inPlaylist = setting; }
    void setUnderNetworkControl(bool setting) { underNetworkControl = setting; }
    bool IsSameProgram(ProgramInfo *p);

    void ShowNoRecorderDialog(void);
    void FinishRecording(void);
    void AskAllowRecording(const QStringList&, int, bool, bool);
    void PromptStopWatchingRecording(void);
    void PromptDeleteRecording(QString title);
    bool PromptRecGroupPassword(void);
    bool BookmarkAllowed(void);
    bool DeleteAllowed(void);
    // Boolean queries

    /// Returns true if we are playing back a non-LiveTV recording.
    bool IsPlaying(void)         const { return StateIsPlaying(GetState()); }
    /// Returns true if we are watching a recording not currently in progress.
    bool IsRecording(void)       const { return StateIsRecording(GetState()); }
    /// Returns true if the EPG is currently on screen.
    bool IsMenuRunning(void)     const { return menurunning; }
    /// Returns true if we are currently in the process of switching recorders.
    bool IsSwitchingCards(void)  const { return switchToRec; }
    /// Returns true if the TV event thread is running. Should always be true
    /// between the end of the constructor and the beginning of the destructor.
    bool IsRunning(void)         const { return runMainLoop; }
    /// Returns true if the user told MythTV to stop plaback. If this
    /// is false when we exit the player, we display an error screen.
    bool WantsToQuit(void)       const { return wantsToQuit; }
    /// Returns true if the user told MythTV to delete the recording
    /// we were most recently playing.
    bool getRequestDelete(void)  const { return requestDelete; }
    /// Returns true if the user told Mythtv to allow re-recording of the show
    bool getAllowRerecord(void) const { return allowRerecord;  }
    /// This is set to true if the player reaches the end of the
    /// recording without the user explicitly exiting the player.
    bool getEndOfRecording(void) const { return endOfRecording; }
    /// This is set if the user asked MythTV to jump to the previous
    /// recording in the playlist.
    bool getJumpToProgram(void)  const { return jumpToProgram; }
    /// This is set if the player encountered some irrecoverable error.
    bool IsErrored(void)         const { return errored; }
    /// true if dialog is either videoplayexit, playexit or askdelete dialog
    bool IsVideoExitDialog(void);
    /// true if NVP is near the end
    bool IsNearEnd(void) const { return isnearend; }

    // Other queries
    int GetLastRecorderNum(void) const;
    TVState GetState(void) const;

    // Non-const queries
    OSD *GetOSD(void);

    void SetCurrentlyPlaying(ProgramInfo *pginfo);

    void GetNextProgram(RemoteEncoder *enc, int direction,
                        InfoMap &infoMap);

    // static functions
    static void InitKeys(void);
    static bool StartTV(ProgramInfo *tvrec = NULL, bool startInGuide = false,
                        bool inPlaylist = false, bool initByNetworkCommand = false);
    static void SetFuncPtr(const char *, void *);

    void SetIgnoreKeys(bool ignore) { ignoreKeys = ignore; }

    // Used by EPG
    void ChangeVolume(bool up);
    void ToggleMute(void);

  public slots:
    void HandleOSDClosed(int osdType);

  protected slots:
    void SetPreviousChannel(void);
    void UnMute(void);
    void KeyRepeatOK(void);
    void BrowseEndTimer(void) { BrowseEnd(false); }
    void SleepEndTimer(void);
    void IdleDialog(void);
    void TreeMenuEntered(OSDListTreeType *tree, OSDGenericTree *item);
    void TreeMenuSelected(OSDListTreeType *tree, OSDGenericTree *item);

  protected:
    void doEditSchedule(int editType = kScheduleProgramGuide);
    static void *RecordedShowMenuHandler(void *param);
    static void *ViewScheduledMenuHandler(void *param);

    void RunTV(void);
    static void *EventThread(void *param);
    void SetMuteTimer(int timeout);

    bool eventFilter(QObject *o, QEvent *e);
    static QStringList lastProgramStringList;
    static RUNPLAYBACKBOX RunPlaybackBoxPtr;
    static RUNVIEWSCHEDULED RunViewScheduledPtr;

  private:
    bool RequestNextRecorder(bool showDialogs);
    void DeleteRecorder();

    bool StartRecorder(RemoteEncoder *rec, int maxWait=-1);
    bool StartPlayer(bool isWatchingRecording, int maxWait=-1);
    void StartOSD(void);
    void StopStuff(bool stopRingbuffers, bool stopPlayers, bool stopRecorders);
    
    void ToggleChannelFavorite(void);
    void ChangeChannel(int direction);
    void ChangeChannel(uint chanid, const QString &channum);
    void PauseLiveTV(void);
    void UnpauseLiveTV(void);

    void ToggleAspectOverride(AspectOverrideMode aspectMode = kAspect_Toggle);
    void ToggleAdjustFill(AdjustFillMode adjustfillMode = kAdjustFill_Toggle);

    bool FillMenuTracks(OSDGenericTree*, uint type);
    void ChangeTrack(uint type, int dir);
    void SetTrack(uint type, int trackNo);

    // key queue commands
    void AddKeyToInputQueue(char key);
    void ClearInputQueues(bool hideosd = false); 
    bool CommitQueuedInput(void);
    bool ProcessSmartChannel(QString&);

    // query key queues
    bool HasQueuedInput(void) const
        { return !GetQueuedInput().isEmpty(); }
    bool HasQueuedChannel(void) const
        { return queuedChanID || !GetQueuedChanNum().isEmpty(); }

    // get queued up input
    QString GetQueuedInput(void)   const;
    int     GetQueuedInputAsInt(bool *ok = NULL, int base = 10) const;
    QString GetQueuedChanNum(void) const;
    uint    GetQueuedChanID(void)  const { return queuedChanID; }

    void SwitchSource(uint source_direction);
    void SwitchInputs(uint inputid);
    void ToggleInputs(uint inputid = 0); 
    void SwitchCards(uint chanid = 0, QString channum = "", uint inputid = 0);

    void ToggleSleepTimer(void);
    void ToggleSleepTimer(const QString);

    void DoPlay(void);
    void DoPause(bool showOSD = true);
    void DoSeek(float time, const QString &mesg);
    bool DoNVPSeek(float time);
    enum ArbSeekWhence {
        ARBSEEK_SET = 0,
        ARBSEEK_REWIND,
        ARBSEEK_FORWARD,
        ARBSEEK_END
    };
    void DoArbSeek(ArbSeekWhence whence);
    void NormalSpeed(void);
    void ChangeSpeed(int direction);
    void ToggleTimeStretch(void);
    void ChangeTimeStretch(int dir, bool allowEdit = true);
    void ChangeAudioSync(int dir, bool allowEdit = true);
    float StopFFRew(void);
    void ChangeFFRew(int direction);
    void SetFFRew(int index);
    void DoSkipCommercials(int direction);
    void StartProgramEditMode(void);

    // Channel editing support
    void StartChannelEditMode(void);
    void ChannelEditKey(const QKeyEvent*);
    void ChannelEditAutoFill(InfoMap&) const;
    void ChannelEditAutoFill(InfoMap&, const QMap<QString,bool>&) const;
    void ChannelEditXDSFill(InfoMap&) const;
    void ChannelEditDDFill(InfoMap&, const QMap<QString,bool>&, bool) const;
    QString GetDataDirect(QString key,   QString value,
                          QString field, bool    allow_partial = false) const;
    bool LoadDDMap(uint sourceid);
    void RunLoadDDMap(uint sourceid);
    static void *load_dd_map_thunk(void*);
    static void *load_dd_map_post_thunk(void*);

    void DoQueueTranscode(QString profile);  

    enum commSkipMode {
        CommSkipOff = 0,
        CommSkipOn = 1,
        CommSkipNotify = 2,
        CommSkipModes = 3,      /* placeholder */
    };
    void SetAutoCommercialSkip(enum commSkipMode skipMode = CommSkipOff);
    void SetManualZoom(bool zoomON = false);

    void DoDisplayJumpMenu(void);
    void SetJumpToProgram(QString progKey = "", int progIndex = 0);
 
    bool ClearOSD(void);
    void ToggleOSD(bool includeStatusOSD); 
    void UpdateOSDProgInfo(const char *whichInfo);
    void UpdateOSDSeekMessage(const QString &mesg, int disptime);
    void UpdateOSDInput(QString inputname = QString::null);
    void UpdateOSDTextEntry(const QString &message);
    void UpdateOSDSignal(const QStringList& strlist);
    void UpdateOSDTimeoutMessage(void);
    void UpdateOSDAskAllowDialog(void);
    void HandleOSDAskAllowResponse(void);

    void EditSchedule(int editType = kScheduleProgramGuide);
    void EmbedWithNewThread(int editType);

    void SetupPlayer(bool isWatchingRecording);
    void TeardownPlayer(void);
    void SetupPipPlayer(void);
    void TeardownPipPlayer(void);
    
    void HandleStateChange(void);
    bool InStateChange(void) const;
    void ChangeState(TVState nextState);
    void ForceNextStateNone(void);

    void TogglePIPView(void);
    void ToggleActiveWindow(void);
    void SwapPIP(void);
    void SwapPIPSoon(void) { needToSwapPIP = true; }
    
    void DisplayJumpMenuSoon(void) { needToJumpMenu = true; }

    void ToggleAutoExpire(void);

    void BrowseStart(void);
    void BrowseEnd(bool change);
    void BrowseDispInfo(int direction);
    void ToggleRecord(void);
    void BrowseChannel(const QString &channum);

    void DoTogglePictureAttribute(PictureAdjustType type);
    void DoChangePictureAttribute(
        PictureAdjustType type, PictureAttribute attr, bool up);

    void BuildOSDTreeMenu(void);
    void ShowOSDTreeMenu(void);
    void FillMenuLiveTV(OSDGenericTree *treeMenu);
    void FillMenuPlaying(OSDGenericTree *treeMenu);

    void UpdateLCD(void);
    void ShowLCDChannelInfo(void);
    void ShowLCDDVDInfo(void);

    QString PlayMesg(void);

    void GetPlayGroupSettings(const QString &group);

    void SetPseudoLiveTV(uint, const ProgramInfo*, PseudoState);

    void ITVRestart(bool isLive);

    bool ScreenShot(long long frameNumber);

    bool VideoThemeCheck(QString str, bool stayPaused = false);

    //dvd functions
    void DVDJumpBack(void);
    void DVDJumpForward(void);       

    static bool LoadExternalSubtitles(NuppelVideoPlayer *nvp,
                                      const QString &videoFile);

    static QStringList GetValidRecorderList(uint chanid);
    static QStringList GetValidRecorderList(const QString &channum);
    static QStringList GetValidRecorderList(uint, const QString&);

    static bool StateIsRecording(TVState state);
    static bool StateIsPlaying(TVState state);
    static bool StateIsLiveTV(TVState state);
    static TVState RemovePlaying(TVState state);
    static TVState RemoveRecording(TVState state);

  private:
    // Configuration variables from database
    QString baseFilters;
    QString db_channel_format;
    QString db_time_format;
    QString db_short_date_format;
    int     fftime;
    int     rewtime;
    int     jumptime;
    bool    smartChannelChange;
    bool    MuteIndividualChannels;
    bool    arrowAccel;
    int     osd_general_timeout;
    int     osd_prog_info_timeout;

    enum commSkipMode autoCommercialSkip;
    bool    tryUnflaggedSkip;

    bool    smartForward;
    int     stickykeys;
    float   ff_rew_repos;
    bool    ff_rew_reverse;
    bool    jumped_back; ///< Used by PromptDeleteRecording 
    vector<int> ff_rew_speeds;

    uint    vbimode;

    // State variables
    MythDeque<TVState> nextStates;
    mutable QMutex     stateLock;
    TVState            internalState;

    uint switchToInputId;
    bool menurunning;
    bool runMainLoop;
    bool wantsToQuit;
    bool exitPlayer;
    bool paused;
    bool errored;
    bool stretchAdjustment; ///< True if time stretch is turned on
    bool audiosyncAdjustment; ///< True if audiosync is turned on
    long long audiosyncBaseline;
    bool editmode;          ///< Are we in video editing mode
    bool zoomMode;
    bool sigMonMode;     ///< Are we in signal monitoring mode?
    bool update_osd_pos; ///< Redisplay osd?
    bool endOfRecording; ///< !nvp->IsPlaying() && StateIsPlaying(internalState)
    bool requestDelete;  ///< User wants last video deleted
    bool allowRerecord;  ///< User wants to rerecord the last video if deleted
    bool doSmartForward;
    bool queuedTranscode;
    bool getRecorderPlaybackInfo; ///< Main loop should get recorderPlaybackInfo
    /// Picture attribute type to modify.
    PictureAdjustType adjustingPicture;
    /// Picture attribute to modify (on arrow left or right)
    PictureAttribute  adjustingPictureAttribute;

    // Ask Allow state
    AskAllowType                 askAllowType;
    QMap<QString,AskProgramInfo> askAllowPrograms;
    QMutex                       askAllowLock;

    bool ignoreKeys;
    bool needToSwapPIP;
    bool needToJumpMenu;
    QMap<QString,ProgramList> progLists;

    mutable QMutex chanEditMapLock; ///< Lock for chanEditMap and ddMap
    InfoMap   chanEditMap;          ///< Channel Editing initial map
    DDKeyMap  ddMap;                ///< DataDirect channel map
    uint      ddMapSourceId;        ///< DataDirect channel map sourceid
    bool      ddMapLoaderRunning;   ///< Is DataDirect loader thread running
    pthread_t ddMapLoader;          ///< DataDirect map loader thread

    /// Vector or sleep timer sleep times in seconds,
    /// with the appropriate UI message.
    vector<SleepTimerInfo> sleep_times;
    uint    sleep_index; ///< Index into sleep_times.
    QTimer *sleepTimer;  ///< Timer for turning off playback.
    QTimer *idleTimer; ///< Timer for turning off playback after idle period.

    /// Queue of unprocessed key presses.
    QPtrList<QKeyEvent> keyList;
    /// Since keys are processed outside Qt event loop, we need a lock.
    QMutex  keyListLock;
    bool    keyRepeat;      ///< Are repeats logical on last key?
    QTimer *keyrepeatTimer; ///< Timeout timer for repeat key filtering

    int   doing_ff_rew;  ///< If true we are doing a rewind not a fast forward
    int   ff_rew_index;  ///< Index into ff_rew_speeds for FF and Rewind speeds
    int   speed_index;   ///< Caches value of ff_rew_speeds[ff_rew_index]

    /** \brief Time stretch speed, 1.0f for normal playback.
     *
     *  Begins at 1.0f meaning normal playback, but can be increased
     *  or decreased to speedup or slowdown playback.
     *  Ignored when doing Fast Forward or Rewind.
     */
    float normal_speed; 
    float prev_speed;

    float frameRate;     ///< Estimated framerate from recorder

    // CC/Teletex input state variables
    /// Are we in CC/Teletext page/stream selection mode?
    bool  ccInputMode;
    /// When does ccInputMode expire
    QTime ccInputModeExpires;

    // Arbitrary Seek input state variables
    /// Are we in Arbitrary seek input mode?
    bool  asInputMode;
    /// When does asInputMode expire
    QTime asInputModeExpires;

    // Channel changing state variables
    /// Input key presses queued up so far...
    QString         queuedInput;
    /// Input key presses queued up so far to form a valid ChanNum
    mutable QString queuedChanNum;
    /// Queued ChanID (from EPG channel selector)
    uint            queuedChanID;
    /// Lock used so that input QStrings can be used across threads, and so
    /// that queuedChanNumExpr can be used safely in Qt 3.2 and earlier.
    mutable QMutex  queuedInputLock;

    QTimer *muteTimer;      ///< For temp. audio muting during channel changes

    // Channel changing timeout notification variables
    QTime   lockTimer;
    bool    lockTimerOn;
    QDateTime lastLockSeenTime;

    // Previous channel functionality state variables
    str_vec_t prevChan;       ///< Previous channels
    uint      prevChanKeyCnt; ///< Number of repeated channel button presses
    QTimer   *prevChanTimer;  ///< Special (slower) repeat key filtering

    // Channel browsing state variables
    bool browsemode;
    bool persistentbrowsemode;
    QTimer *browseTimer;
    QString browsechannum;
    QString browsechanid;
    QString browsestarttime;

    // Program Info for currently playing video
    // (or next video if InChangeState() is true)
    ProgramInfo *recorderPlaybackInfo; ///< info requested from recorder
    ProgramInfo *playbackinfo;  ///< info sent in via Playback()
    QMutex       pbinfoLock;
    int          playbackLen;   ///< initial playbackinfo->CalculateLength()
    ProgramInfo *lastProgram;   ///< last program played with this player
    bool         jumpToProgram;
    
    bool         inPlaylist; ///< show is part of a playlist
    bool         underNetworkControl; ///< initial show started via by the network control interface
    bool         isnearend;

    // Recording to play next, after LiveTV
    ProgramInfo *pseudoLiveTVRec[2];
    PseudoState  pseudoLiveTVState[2];

    // Video Players
    NuppelVideoPlayer *nvp;
    NuppelVideoPlayer *pipnvp;
    NuppelVideoPlayer *activenvp;  ///< Player to which LiveTV events are sent

    // Remote Encoders
    /// Main recorder
    RemoteEncoder *recorder;
    /// Picture-in-Picture recorder
    RemoteEncoder *piprecorder;
    /// Recorder to which LiveTV events are being sent
    RemoteEncoder *activerecorder;
    /// Main recorder to use after a successful SwitchCards() call.
    RemoteEncoder *switchToRec;
    /// Storage for keeping the last recorder number around
    int lastrecordernum;

    // LiveTVChain
    LiveTVChain *tvchain;
    LiveTVChain *piptvchain;
    QStringList tvchainUpdate;
    QMutex tvchainUpdateLock;

    // RingBuffers
    RingBuffer *prbuffer;
    RingBuffer *piprbuffer;
    RingBuffer *activerbuffer; ///< Ringbuffer to which LiveTV events are sent

    // OSD info
    QString         dialogname; ///< Name of current OSD dialog
    OSDGenericTree *treeMenu;   ///< OSD menu, 'm' using default keybindings
    MythTimer  dialogboxTimer;  ///< How long a dialog box is on the screen

    /// UDPNotify instance which shows messages sent
    /// to the "UDPNotifyPort" in an OSD dialog.
    UDPNotify      *udpnotify;
    QStringList     lastSignalMsg;
    MythTimer       lastSignalMsgTime;
    InfoMap         lastSignalUIInfo;
    MythTimer       lastSignalUIInfoTime;
    QMutex          osdlock;

    // LCD Info
    QDateTime lastLcdUpdate;
    QString   lcdTitle;
    QString   lcdSubtitle;
    QString   lcdCallsign;

    // Window info (GUI is optional, transcoding, preview img, etc)
    MythDialog *myWindow;   ///< Our MythDialog window, if it exists
    WId   embedWinID;       ///< Window ID when embedded in another widget
    QRect embedBounds;      ///< Bounds when embedded in another widget
    ///< player bounds, for after doEditSchedule() returns to normal playing.
    QRect player_bounds;
    ///< Prior GUI window bounds, for doEditSchedule() and player exit().
    QRect saved_gui_bounds;

    // IsTunable() cache, used by embedded program guide
    mutable QMutex                 is_tunable_cache_lock;
    QMap< uint,vector<InputInfo> > is_tunable_cache_inputs;

    // Various threads
    /// Event processing thread, runs RunTV().
    pthread_t event;
    /// Video decoder thread, runs nvp's NuppelVideoPlayer::StartPlaying().
    pthread_t decode;
    /// Picture-in-Picture video decoder thread,
    /// runs pipnvp's NuppelVideoPlayer::StartPlaying().
    pthread_t pipdecode;

    /// Condition to signal that the Event thread is up and running
    QWaitCondition mainLoopCond;
    QMutex mainLoopCondLock;

    // Constants
    static const int kInitFFRWSpeed; ///< 1x, default to normal speed
    static const int kMuteTimeout;   ///< Channel changing mute timeout in msec
    static const int kLCDTimeout;    ///< Timeout for updating LCD info in seconds
    static const int kBrowseTimeout; ///< Timeout for browse mode exit in msec
    /// Timeout after last Signal Monitor message for ignoring OSD when exiting.
    static const int kSMExitTimeout;
    static const int kInputKeysMax;  ///< When to start discarding early keys
    static const int kInputModeTimeout; ///< Timeout for entry modes in msec

    static const uint kNextSource;
    static const uint kPreviousSource;

    // Network Control stuff
    QValueList<QString> networkControlCommands;
    QMutex ncLock;
};

#endif
