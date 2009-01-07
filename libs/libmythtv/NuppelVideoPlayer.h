#ifndef NUPPELVIDEOPLAYER
#define NUPPELVIDEOPLAYER

#include <qstring.h>
#include <qmutex.h>
#include <qwaitcondition.h>
#include <qptrqueue.h>
#include <qdatetime.h>
#include <sys/time.h>

#include "RingBuffer.h"
#include "osd.h"
#include "jitterometer.h"
#include "recordingprofile.h"
#include "videooutbase.h"
#include "teletextdecoder.h"
#include "textsubtitleparser.h"
#include "tv_play.h"
#include "yuv2rgb.h"
#include "cc608decoder.h"
#include "cc708decoder.h"
#include "cc708window.h"

#include "mythexp.h"

extern "C" {
#include "filter.h"
}
using namespace std;

#define MAXTBUFFER 60

#ifndef LONG_LONG_MIN
#define LONG_LONG_MIN LLONG_MIN
#endif

class VideoOutput;
class OSDSet;
class RemoteEncoder;
class MythSqlDatabase;
class ProgramInfo;
class DecoderBase;
class AudioOutput;
class FilterManager;
class FilterChain;
class VideoSync;
class LiveTVChain;
class TV;
struct AVSubtitle;
class InteractiveTV;
class NSAutoreleasePool;

struct TextContainer
{
    int timecode;
    int len;
    unsigned char *buffer;
    char type;
};

typedef  void (*StatusCallback)(int, void*);

/// Timecode types
enum TCTypes
{
    TC_VIDEO = 0,
    TC_AUDIO,
    TC_SUB,
    TC_CC
};
#define TCTYPESMAX 4

/// Track types
enum
{
    kTrackTypeAudio = 0,
    kTrackTypeSubtitle,
    kTrackTypeCC608,
    kTrackTypeCC708,
    kTrackTypeTeletextCaptions,
    kTrackTypeCount,

    kTrackTypeTeletextMenu,
};
QString track_type_to_string(uint type);
int type_string_to_track_type(const QString &str);

// Caption Display modes
enum
{
    kDisplayNone                = 0x00,
    kDisplayNUVTeletextCaptions = 0x01,
    kDisplayTeletextCaptions    = 0x02,
    kDisplayAVSubtitle          = 0x04,
    kDisplayCC608               = 0x08,
    kDisplayCC708               = 0x10,
    kDisplayNUVCaptions         = kDisplayNUVTeletextCaptions | kDisplayCC608,
    kDisplayTextSubtitle        = 0x20,
    kDisplayAllCaptions         = 0x3f,
    kDisplayTeletextMenu        = 0x40,
};

class MPUBLIC NuppelVideoPlayer : public CC608Reader, public CC708Reader
{
 public:
    NuppelVideoPlayer(QString inUseID = "Unknown",
                      const ProgramInfo *info = NULL);
   ~NuppelVideoPlayer();

    // Initialization
    bool InitVideo(void);
    int  OpenFile(bool skipDsp = false, uint retries = 4,
                  bool allow_libmpeg2 = true);
    void OpenDummy(void);

    // Windowing stuff
    void EmbedInWidget(WId wid, int x, int y, int w, int h);
    void StopEmbedding(void);
    void ExposeEvent(void);
    bool IsEmbedding(void);
    void DrawUnusedRects(bool sync);

    // Audio Sets
    void SetNoAudio(void)                     { no_audio_out = true; }
    void SetAudioStretchFactor(float factor)  { audio_stretchfactor = factor; }
    void SetAudioOutput(AudioOutput *ao)      { audioOutput = ao; }
    void SetAudioInfo(const QString &main, const QString &passthru, uint rate);
    void SetAudioParams(int bits, int channels, int samplerate, bool passthru);
    void SetEffDsp(int dsprate);
    void SetAudioCodec(void *ac);

    // Sets
    void SetParentWidget(QWidget *widget)     { parentWidget = widget; }
    void SetAsPIP(void)                       { SetNoAudio(); SetNullVideo(); }
    void SetNullVideo(void)                   { using_null_videoout = true; }
    void SetFileName(QString lfilename)       { filename = lfilename; }
    void SetExactSeeks(bool exact)            { exactseeks = exact; }
    void SetAutoCommercialSkip(int autoskip);
    void SetCommBreakMap(QMap<long long, int> &newMap);
    void SetRingBuffer(RingBuffer *rbuf)      { ringBuffer = rbuf; }
    void SetLiveTVChain(LiveTVChain *tvchain) { livetvchain = tvchain; }
    void SetLength(int len)                   { totalLength = len; }
    void SetVideoFilters(const QString &override)
        { videoFiltersOverride = QDeepCopy<QString>(override); }
    void SetFramesPlayed(long long played)    { framesPlayed = played; }
    void SetEof(void)                         { eof = true; }
    void SetPipPlayer(NuppelVideoPlayer *pip)
        { setpipplayer = pip; needsetpipplayer = true; }
    void SetRecorder(RemoteEncoder *recorder);
    void SetParentPlayer(TV *tv)             { m_tv = tv; }

    void SetTranscoding(bool value);
    void SetWatchingRecording(bool mode);
    void SetWatched(bool forceWatched = false);
    void SetBookmark(void);
    void SetKeyframeDistance(int keyframedistance);
    void SetVideoParams(int w, int h, double fps, int keydist,
                        float a = 1.33333, FrameScanType scan = kScan_Ignore, 
                        bool video_codec_changed = false);
    void SetFileLength(int total, int frames);
    void Zoom(ZoomDirection direction);
    void ClearBookmark(void);
    void SetForcedAspectRatio(int mpeg2_aspect_value, int letterbox_permission);

    void NextScanType(void)
        { SetScanType((FrameScanType)(((int)m_scan + 1) & 0x3)); }
    void SetScanType(FrameScanType);
    FrameScanType GetScanType(void) const { return m_scan; }
    bool IsScanTypeLocked(void) const { return m_scan_locked; }

    void SetOSDFontName(const QString osdfonts[22], const QString &prefix);
    void SetOSDThemeName(const QString themename);
    void SetVideoResize(const QRect &videoRect);

    // Toggle Sets
    void ToggleAspectOverride(AspectOverrideMode aspectMode = kAspect_Toggle);
    void ToggleAdjustFill(AdjustFillMode adjustfillMode = kAdjustFill_Toggle);

    // Gets
    QSize   GetVideoBufferSize(void) const    { return video_dim; }
    QSize   GetVideoSize(void) const          { return video_disp_dim; }
    float   GetVideoAspect(void) const        { return video_aspect; }
    float   GetFrameRate(void) const          { return video_frame_rate; }

    int     GetSecondsBehind(void) const;
    AspectOverrideMode GetAspectOverride(void) const;
    AdjustFillMode     GetAdjustFill(void) const;
    int     GetFFRewSkip(void) const          { return ffrew_skip; }
    float   GetAudioStretchFactor(void) const { return audio_stretchfactor; }
    float   GetNextPlaySpeed(void) const      { return next_play_speed; }
    int     GetLength(void) const             { return totalLength; }
    long long GetTotalFrameCount(void) const  { return totalFrames; }
    long long GetFramesPlayed(void) const     { return framesPlayed; }
    long long GetBookmark(void) const;
    QString   GetEncodingType(void) const;
    QString   GetXDS(const QString &key) const;
    bool      GetAudioBufferStatus(uint &fill, uint &total) const;

    // Bool Gets
    bool    GetRawAudioState(void) const;
    bool    GetLimitKeyRepeat(void) const     { return limitKeyRepeat; }
    bool    GetEof(void) const                { return eof; }
    bool    PipPlayerSet(void) const          { return !needsetpipplayer; }
    bool    IsErrored(void) const             { return errored; }
    bool    IsPlaying(void) const             { return playing; }
    bool    AtNormalSpeed(void) const         { return next_normal_speed; }
    bool    IsDecoderThreadAlive(void) const  { return decoder_thread_alive; }
    bool    IsReallyNearEnd(void) const;
    bool    IsNearEnd(long long framesRemaining = -1) const;
    bool    PlayingSlowForPrebuffer(void) const { return m_playing_slower; }
    bool    HasAudioIn(void) const            { return !no_audio_in; }
    bool    HasAudioOut(void) const           { return !no_audio_out; }

    // Complicated gets
    long long CalcMaxFFTime(long long ff, bool setjump = true) const;
    long long CalcRWTime(long long rw) const;
    void      calcSliderPos(struct StatusPosInfo &posInfo,
                            bool paddedFields = false);

    /// Non-const gets
    OSD         *GetOSD(void)                 { return osd; }
    VideoOutput *getVideoOutput(void)         { return videoOutput; }
    AudioOutput *getAudioOutput(void)         { return audioOutput; }
    char        *GetScreenGrabAtFrame(long long frameNum, bool absolute,
                                      int &buflen, int &vw, int &vh, float &ar);
    char        *GetScreenGrab(int secondsin, int &buflen,
                               int &vw, int &vh, float &ar);
    LiveTVChain *GetTVChain(void)             { return livetvchain; }
    InteractiveTV *GetInteractiveTV(void);

    // Start/Reset/Stop playing
    void StartPlaying(void);
    void ResetPlaying(void);
    void StopPlaying(void) { killplayer = true; decoder_thread_alive = false; }

    // Pause stuff
    void PauseDecoder(void);
    void Pause(bool waitvideo = true);
    bool Play(float speed = 1.0, bool normal = true,
              bool unpauseaudio = true);
    bool IsPaused(bool *is_pause_still_possible = NULL) const;

    // Seek stuff
    bool FastForward(float seconds);
    bool Rewind(float seconds);
    bool RebuildSeekTable(bool showPercentage = true, StatusCallback cb = NULL,
                          void* cbData = NULL);
    void JumpToFrame(long long frame);

    // Commercial stuff
    void SkipCommercials(int direction);
    int FlagCommercials(bool showPercentage, bool fullSpeed,
                        bool inJobQueue);

    // Transcode stuff
    void InitForTranscode(bool copyaudio, bool copyvideo);
    bool TranscodeGetNextFrame(QMap<long long, int>::Iterator &dm_iter,
                               int *did_ff, bool *is_key, bool honorCutList);
    void TranscodeWriteText(
        void (*func)(void *, unsigned char *, int, int, int), void *ptr);
    bool WriteStoredData(
        RingBuffer *outRingBuffer, bool writevideo, long timecodeOffset);
    long UpdateStoredFrameNum(long curFrameNum);
    void SetCutList(QMap<long long, int> newCutList);

    // Edit mode stuff
    bool EnableEdit(void);
    bool DoKeypress(QKeyEvent *e);
    bool GetEditMode(void) const { return editmode; }

    // Decoder stuff..
    VideoFrame *GetNextVideoFrame(bool allow_unsafe = true);
    VideoFrame *GetRawVideoFrame(long long frameNumber = -1);
    VideoFrame *GetCurrentFrame(int &w, int &h);
    void ReleaseNextVideoFrame(VideoFrame *buffer, long long timecode);
    void ReleaseNextVideoFrame(void)
        { videoOutput->ReleaseFrame(GetNextVideoFrame(false)); }
    void ReleaseCurrentFrame(VideoFrame *frame);
    void DiscardVideoFrame(VideoFrame *buffer);
    void DiscardVideoFrames(bool next_frame_keyframe);
    void DrawSlice(VideoFrame *frame, int x, int y, int w, int h);

    // Preview Image stuff
    const QImage &GetARGBFrame(QSize &size);
    const unsigned char *GetScaledFrame(QSize &size);
    void ShutdownYUVResize(void);
    void SaveScreenshot(void);

    // Reinit
    void    ReinitOSD(void);
    void    ReinitVideo(void);
    QString ReinitAudio(void);

    // Add data
    void AddAudioData(char *buffer, int len, long long timecode);
    void AddAudioData(short int *lbuffer, short int *rbuffer, int samples,
                      long long timecode);
    void AddTextData(unsigned char *buffer, int len,
                     long long timecode, char type);
    void AddAVSubtitle(const AVSubtitle& subtitle);

    // Closed caption and teletext stuff
    uint GetCaptionMode(void) const { return textDisplayMode; }
    void ResetCaptions(uint mode_override = 0);
    void DisableCaptions(uint mode, bool osd_msg=true);
    void EnableCaptions(uint mode, bool osd_msg=true);
    bool ToggleCaptions(void);
    bool ToggleCaptions(uint mode);
    void SetCaptionsEnabled(bool, bool osd_msg=true);
    bool LoadExternalSubtitles(const QString &subtitleFileName);

    // Teletext Menu and non-NUV teletext decoder
    void EnableTeletext(void);
    void DisableTeletext(void);
    void ResetTeletext(void);
    bool HandleTeletextAction(const QString &action);

    // Teletext NUV Captions
    void SetTeletextPage(uint page);

    // ATSC/NTSC EIA-608 captions and PAL Teletext NUV captions
    void FlushTxtBuffers(void) { rtxt = wtxt; }

    // ATSC EIA-708 Captions
    CC708Window &GetCCWin(uint service_num, uint window_id)
        { return CC708services[service_num].windows[window_id]; }
    CC708Window &GetCCWin(uint svc_num)
        { return GetCCWin(svc_num, CC708services[svc_num].current_window); }

    void SetCurrentWindow(uint service_num, int window_id);
    void DefineWindow(uint service_num,     int window_id,
                      int priority,         int visible,
                      int anchor_point,     int relative_pos,
                      int anchor_vertical,  int anchor_horizontal,
                      int row_count,        int column_count,
                      int row_lock,         int column_lock,
                      int pen_style,        int window_style);
    void DeleteWindows( uint service_num,   int window_map);
    void DisplayWindows(uint service_num,   int window_map);
    void HideWindows(   uint service_num,   int window_map);
    void ClearWindows(  uint service_num,   int window_map);
    void ToggleWindows( uint service_num,   int window_map);
    void SetWindowAttributes(uint service_num,
                             int fill_color,     int fill_opacity,
                             int border_color,   int border_type,
                             int scroll_dir,     int print_dir,
                             int effect_dir,
                             int display_effect, int effect_speed,
                             int justify,        int word_wrap);
    void SetPenAttributes(uint service_num, int pen_size,
                          int offset,       int text_tag,  int font_tag,
                          int edge_type,    int underline, int italics);
    void SetPenColor(uint service_num,
                     int fg_color, int fg_opacity,
                     int bg_color, int bg_opacity,
                     int edge_color);
    void SetPenLocation(uint service_num, int row, int column);

    void Delay(uint service_num, int tenths_of_seconds);
    void DelayCancel(uint service_num);
    void Reset(uint service_num);
    void TextWrite(uint service_num, short* unicode_string, short len);

    // Audio/Subtitle/EIA-608/EIA-708 stream selection
    QStringList GetTracks(uint type) const;
    int SetTrack(uint type, int trackNo);
    int GetTrack(uint type) const;
    int ChangeTrack(uint type, int dir);
    void ChangeCaptionTrack(int dir);
    void TracksChanged(uint trackType);

    // MHEG/MHI stream selection
    bool ITVHandleAction(const QString &action);
    void ITVRestart(uint chanid, uint cardid, bool isLiveTV);
    bool SetAudioByComponentTag(int tag);
    bool SetVideoByComponentTag(int tag);

    // Time Code adjustment stuff
    long long AdjustAudioTimecodeOffset(long long v)
        { tc_wrap[TC_AUDIO] += v;  return tc_wrap[TC_AUDIO]; }
    long long ResetAudioTimecodeOffset(void)
        { tc_wrap[TC_AUDIO] = 0LL; return tc_wrap[TC_AUDIO]; }
    long long ResyncAudioTimecodeOffset(void)
        { tc_wrap[TC_AUDIO] = LONG_LONG_MIN; return 0L; }
    long long GetAudioTimecodeOffset(void) const 
        { return tc_wrap[TC_AUDIO]; }
    void SaveAudioTimecodeOffset(long long v)
        { savedAudioTimecodeOffset = v; }

    // LiveTV public stuff
    void CheckTVChain();
    void FileChangedCallback();

    // DVD public stuff
    void ChangeDVDTrack(bool ffw);
    void ActivateDVDButton(void);
    void GoToDVDMenu(QString str);
    void GoToDVDProgram(bool direction);
    void HideDVDButton(bool hide) 
    { 
        hidedvdbutton = hide;
    }

  protected:
    void DisplayPauseFrame(void);
    void DisplayNormalFrame(void);
    void OutputVideoLoop(void);
    void IvtvVideoLoop(void);

    static void *kickoffOutputVideoLoop(void *player);

  private:
    // Private initialization stuff
    void InitFilters(void);
    FrameScanType detectInterlace(FrameScanType newScan, FrameScanType scan,
                                  float fps, int video_height);
    void AutoDeint(VideoFrame*);

    // Private Sets
    void SetPlaybackInfo(ProgramInfo *pginfo);
    void SetPrebuffering(bool prebuffer);

    // Private Gets
    int  GetStatusbarPos(void) const;
    bool IsInDelete(long long testframe) const;
    bool IsIVTVDecoder(void) const;

    // Private pausing stuff
    void PauseVideo(bool wait = true);
    void UnpauseVideo(bool wait = false);
    void SetVideoActuallyPaused(bool val);
    bool IsVideoActuallyPaused(void) const;

    // Private decoder stuff
    void  SetDecoder(DecoderBase *dec);
    /// Returns the stream decoder currently in use.
    DecoderBase *GetDecoder(void) { return decoder; }
    /// Returns the stream decoder currently in use.
    const DecoderBase *GetDecoder(void) const { return decoder; }
    bool DecodeFrame(struct rtframeheader *frameheader,
                     unsigned char *strm, unsigned char *outbuf);

    bool PrebufferEnoughFrames(void);
    void CheckPrebuffering(void);
    bool GetFrameNormal(int onlyvideo);
    bool GetFrameFFREW(void);
    bool GetFrame(int onlyvideo, bool unsafe = false);

    // These actually execute commands requested by public members
    void DoPause(void);
    void DoPlay(void);
    bool DoFastForward(void);
    bool DoRewind(void);
    void DoChangeDVDTrack(void);

    // Private seeking stuff
    void ClearAfterSeek(bool clearvideobuffers = true);
    bool FrameIsInMap(long long frameNumber, QMap<long long, int> &breakMap);
    void JumpToNetFrame(long long net) { JumpToFrame(framesPlayed + net); }

    // Private commercial skipping
    void SkipCommercialsByBlanks(void);
    bool DoSkipCommercials(int direction);
    void AutoCommercialSkip(void);
    void MergeShortCommercials(void);

    // Private edit stuff
    void SaveCutList(void);
    void LoadCutList(void);
    void LoadCommBreakList(void);
    void DisableEdit(void);

    void AddMark(long long frames, int type);
    void DeleteMark(long long frames);
    void ReverseMark(long long frames);

    void SetDeleteIter(void);
    void SetBlankIter(void);
    void SetCommBreakIter(void);

    void HandleArbSeek(bool right);
    void HandleSelect(bool allowSelectNear = false);
    void HandleResponse(void);

    void UpdateTimeDisplay(void);
    void UpdateSeekAmount(bool up);
    void UpdateEditSlider(void);

    // Private A/V Sync Stuff
    float WarpFactor(void);
    void  WrapTimecode(long long &timecode, TCTypes tc_type);
    void  InitAVSync(void);
    void  AVSync(void);
    void  ShutdownAVSync(void);
    void  FallbackDeint(void);

    // Private closed caption and teletext stuff
    int   tbuffer_numvalid(void); // number of valid slots in the text buffer
    int   tbuffer_numfree(void); // number of free slots in the text buffer
    void  ShowText(void);
    void  ResetCC(void);
    void  UpdateCC(unsigned char *inpos);

    // Private subtitle stuff
    void  DisplayAVSubtitles(void);
    void  DisplayTextSubtitles(void);
    void  ClearSubtitles(void);
    void  ExpireSubtitles(void);

    // Private LiveTV stuff
    void  SwitchToProgram(void);
    void  JumpToProgram(void);

    // Private DVD stuff
    void DisplayDVDButton(void);
    long long GetDVDBookmark(void) const;
    void SetDVDBookmark(long long frames);

  private:
    DecoderBase   *decoder;
    QMutex         decoder_change_lock;
    VideoOutput   *videoOutput;
    RemoteEncoder *nvr_enc;
    ProgramInfo   *m_playbackinfo;

    // Window stuff
    QWidget *parentWidget;
    WId embedid;
    int embx, emby, embw, embh;
    FrameScanType embed_saved_scan_type;
    bool          embed_saved_scan_lock;

    // State
    QWaitCondition decoderThreadPaused;
    QWaitCondition videoThreadPaused;
    QWaitCondition videoThreadUnpaused;
    mutable QMutex vidExitLock;
    mutable QMutex pauseUnpauseLock;
    mutable QMutex internalPauseLock;
    bool     eof;             ///< At end of file/ringbuffer
    bool     m_double_framerate;///< Output fps is double Video (input) rate
    bool     m_double_process;///< Output filter must processed at double rate
    bool     m_can_double;    ///< VideoOutput capable of doubling frame rate
    bool     m_deint_possible;
    bool     paused;
    bool     pausevideo;
    bool     actuallypaused;
    bool     video_actually_paused;
    bool     playing;
    bool     decoder_thread_alive;
    bool     killplayer;
    bool     killvideo;
    bool     livetv;
    bool     watchingrecording;
    bool     editmode;
    bool     resetvideo;
    bool     using_null_videoout;
    bool     no_audio_in;
    bool     no_audio_out;
    bool     transcoding;
    bool     hasFullPositionMap;
    mutable bool     limitKeyRepeat;
    bool     errored;

    // Bookmark stuff
    long long bookmarkseek;
    bool      previewFromBookmark;

    // Seek
    /// If fftime>0, number of frames to seek forward.
    /// If fftime<0, number of frames to seek backward.
    long long fftime;
    /// 1..9 == keyframe..10 minutes. 0 == cut point
    int       seekamountpos;
    /// Seekable frame increment when not using exact seeks.
    /// Usually equal to keyframedist.
    int      seekamount;
    /// Iff true we ignore seek amount and try to seek to an
    /// exact frame ignoring key frame restrictions.
    bool     exactseeks;

    // Playback misc.
    /// How often we have tried to wait for a video output buffer and failed
    int       videobuf_retries;
    long long framesPlayed;
    long long totalFrames;
    long long totalLength;
    long long rewindtime;
    QString m_recusage;

    // -- end state stuff -- 


    // Input Video Attributes
    QSize    video_disp_dim;  ///< Video (input) width & height
    QSize    video_dim;       ///< Video (input) buffer width & height
    double   video_frame_rate;///< Video (input) Frame Rate (often inaccurate)
    float    video_aspect;    ///< Video (input) Apect Ratio
    float    forced_video_aspect; 
    /// Video (input) Scan Type (interlaced, progressive, detect, ignore...)
    FrameScanType m_scan;
    /// Set when the user selects a scan type, overriding the detected one
    bool     m_scan_locked;
    /// Used for tracking of scan type for auto-detection of interlacing
    int      m_scan_tracker;
    /// Video (input) Number of frames between key frames (often inaccurate)
    int keyframedist;

    // RingBuffer stuff
    QString    filename;        ///< Filename if we create our own ringbuf
    bool       weMadeBuffer;    ///< Iff true, we can delete ringBuffer
    RingBuffer *ringBuffer;     ///< Pointer to the RingBuffer we read from
    
    // Prebuffering (RingBuffer) control
    QWaitCondition prebuffering_wait;///< QWaitContition used by prebuffering
    QMutex     prebuffering_lock;///< Mutex used to control access to prebuf
    bool       prebuffering;    ///< Iff true, don't play until done prebuf
    int        prebuffer_tries; ///< Number of times prebuf wait attempted

    // General Caption/Teletext/Subtitle support
    bool     db_prefer708;
    uint     textDisplayMode;
    uint     prevTextDisplayMode;

    // Support for analog captions and teletext
    // (i.e. Vertical Blanking Interval (VBI) encoded data.)
    uint     vbimode;         ///< VBI decoder to use
    int      ttPageNum;       ///< VBI page to display when in PAL vbimode
    int      ccmode;          ///< VBI text to display when in NTSC vbimode

    int      wtxt;            ///< Write position for VBI text
    int      rtxt;            ///< Read position for VBI text
    QMutex   text_buflock;    ///< Lock for rtxt and wtxt VBI text positions
    int      text_size;       ///< Maximum size of a text buffer
    struct TextContainer txtbuffers[MAXTBUFFER+1]; ///< VBI text buffers

    QString  ccline;
    int      cccol;
    int      ccrow;

    // Support for captions, teletext, etc. decoded by libav
    QMutex    subtitleLock;
    /// This allows us to enable captions/subtitles later if the streams
    /// are not immediately available when the video starts playing.
    bool      textDesired;
    bool      osdHasSubtitles;
    long long osdSubtitlesExpireAt;

    /// Subtitles loaded from the video stream by libavcodec.
    /// This should contain only undisplayed subtitles, old
    /// ones are deleted after displayed.
    MythDeque<AVSubtitle> nonDisplayedAVSubtitles;

    /// Subtitles loaded from an external subtitle file.
    /// This contains all subtitles in textual format. No
    /// subtitles are deleted after displaying (so they can
    /// be displayed again after seeking). The list is ordered
    /// by the subtitle display start time.
    TextSubtitles textSubtitles;

    CC708Service CC708services[64];
    QString    osdfontname;
    QString    osdccfontname;
    QString    osd708fontnames[20];
    QString    osdprefix;
    QString    osdtheme;

    // Support for MHEG/MHI
    bool       itvVisible;
    InteractiveTV *interactiveTV;
    bool       itvEnabled;
    QMutex     itvLock;

    // OSD stuff
    OSD      *osd;
    OSDSet   *timedisplay;
    QString   dialogname;
    int       dialogtype;

    // Audio stuff
    AudioOutput *audioOutput;
    QString  audio_main_device;
    QString  audio_passthru_device;
    int      audio_channels;
    int      audio_bits;
    int      audio_samplerate;
    float    audio_stretchfactor;
    void    *audio_codec;
    bool     audio_passthru;

    // Picture-in-Picture
    NuppelVideoPlayer *pipplayer;
    NuppelVideoPlayer *setpipplayer;
    bool needsetpipplayer;

    // Preview window support
    unsigned char      *argb_buf;
    QSize               argb_size;
    QImage              argb_scaled_img;
    yuv2rgb_fun         yuv2argb_conv;
    bool                yuv_need_copy;
    QSize               yuv_desired_size;
    ImgReSampleContext *yuv_scaler;
    unsigned char      *yuv_frame_scaled;
    QSize               yuv_scaler_in_size;
    QSize               yuv_scaler_out_size;
    QMutex              yuv_lock;
    QWaitCondition      yuv_wait;

    // Filters
    QMutex   videofiltersLock;
    QString  videoFiltersForProgram;
    QString  videoFiltersOverride;
    int      postfilt_width;  ///< Post-Filter (output) width
    int      postfilt_height; ///< Post-Filter (output) height
    FilterChain   *videoFilters;
    FilterManager *FiltMan;

    // Commercial filtering
    QMutex     commBreakMapLock;
    int        skipcommercials;
    int        autocommercialskip;
    int        commrewindamount;
    int        commnotifyamount;
    int        lastCommSkipDirection;
    time_t     lastCommSkipTime;
    long long  lastCommSkipStart;
    time_t     lastSkipTime;

    long long  deleteframe;
    bool       hasdeletetable;
    bool       hasblanktable;
    bool       hascommbreaktable;
    QMap<long long, int> deleteMap;
    QMap<long long, int> blankMap;
    QMap<long long, int> commBreakMap;
    QMap<long long, int>::Iterator deleteIter;
    QMap<long long, int>::Iterator blankIter;
    QMap<long long, int>::Iterator commBreakIter;
    QDateTime  lastIgnoredManualSkip;
    bool       forcePositionMapSync;

    // Playback (output) speed control
    /// Lock for next_play_speed and next_normal_speed
    QMutex     decoder_lock;
    float      next_play_speed;
    bool       next_normal_speed;

    float      play_speed;    
    bool       normal_speed;  
    int        frame_interval;///< always adjusted for play_speed

    int        ffrew_skip;    

    // Audio and video synchronization stuff
    VideoSync *videosync;
    int        delay;
    int        vsynctol;
    int        avsync_delay;
    int        avsync_adjustment;
    int        avsync_avg;
    int        avsync_oldavg;
    int        refreshrate;
    bool       lastsync;
    bool       m_playing_slower;
    bool       decode_extra_audio;
    float      m_stored_audio_stretchfactor;
    bool       audio_paused;

    // Audio warping stuff
    bool       usevideotimebase;
    float      warpfactor;
    float      warpfactor_avg;
    short int *warplbuff;
    short int *warprbuff;
    int        warpbuffsize;
 
    // Time Code stuff
    int        prevtc;        ///< 32 bit timecode if last VideoFrame shown
    int        tc_avcheck_framecounter;
    long long  tc_wrap[TCTYPESMAX];
    long long  tc_lastval[TCTYPESMAX];
    long long  tc_diff_estimate;
    long long  savedAudioTimecodeOffset;

    // LiveTV
    LiveTVChain *livetvchain;
    TV *m_tv;
    bool isDummy;

    // DVD
    /// \brief true if dvd button is hidden
    bool hidedvdbutton;
    int need_change_dvd_track;
    bool dvd_stillframe_showing;

    // Debugging variables
    Jitterometer *output_jmeter;
};

#endif

/* vim: set expandtab tabstop=4 shiftwidth=4: */
